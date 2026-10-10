; domain/character.asm — item stats and the derived character sheet (domain/character.py in the reference;
; docs/game-design.md §4 "Derived stats" and §8 "Item score").
;
; Role: `stats = vocation base + Σ equipped items (base stats × rarity statPct + affixes)`, capped where the game
; design says so. The reference returns a new CharacterSheet object each time; here `build_sheet` refills the
; static `g_sheet`, and callers read its fields right after the call.
;
; Register use: the item_* routines are leaves that destroy rax, rcx, rdx, rsi, rdi, r8-r11. build_sheet and
; equipment_score use the standard frame (common.inc).
%include "common.inc"

extern g_player

global g_sheet, item_add_stats, item_value, item_score, required_level, build_sheet, equipment_score

section .bss align=16
g_sheet:		resb Sheet_size
stat_totals:	resq STAT_COUNT		; scratch: stat sums of build_sheet
score_stats:	resq STAT_COUNT		; scratch: final stats of the item being scored

section .text

; item_add_stats(rdi = ItemInstance, rsi = pointer to STAT_COUNT qwords) — adds the item's final stats to the
; array: every base stat × the rarity's statPct (floored), plus the rolled affixes.
item_add_stats:
	mov		r8, [rdi + ItemInstance.item]
	ROW		r8, items, r8, Item_size			; r8 = Item row
	mov		r9, [rdi + ItemInstance.rarity]
	ROW		r9, rarities, r9, Rarity_size
	mov		r9, [r9 + Rarity.stat_pct]
	xor		r10d, r10d
	mov		ecx, 100
.base:
	mov		rax, [r8 + Item.stats + r10 * 8]
	imul	rax, r9
	xor		edx, edx
	div		rcx
	add		[rsi + r10 * 8], rax
	inc		r10
	cmp		r10, STAT_COUNT
	jb		.base

	mov		rcx, [rdi + ItemInstance.affix_count]
	lea		rdi, [rdi + ItemInstance.affixes]
.affix:
	test	rcx, rcx
	jz		.done
	mov		rax, [rdi + AffixRoll.stat]
	mov		rdx, [rdi + AffixRoll.value]
	add		[rsi + rax * 8], rdx
	add		rdi, AffixRoll_size
	dec		rcx
	jmp		.affix
.done:
	ret

; item_value(rdi = ItemInstance) → rax = sell price = pct(item.value, rarity.valuePct). Destroys rcx, rdx, r8.
item_value:
	mov		rax, [rdi + ItemInstance.item]
	ROW		rax, items, rax, Item_size
	mov		rax, [rax + Item.value]
	mov		r8, [rdi + ItemInstance.rarity]
	ROW		r8, rarities, r8, Rarity_size
	imul	rax, [r8 + Rarity.value_pct]
	xor		edx, edx
	mov		ecx, 100
	div		rcx
	ret

; item_score(rdi = ItemInstance) → rax = Σ final stat × balance.itemScoreWeights[stat] ("item power").
item_score:
	push	rdi
	lea		rdi, [score_stats]
	mov		ecx, STAT_COUNT
	xor		eax, eax
	rep stosq								; clear the scratch array
	pop		rdi
	lea		rsi, [score_stats]
	call	item_add_stats					; a leaf without SSE: no alignment needed
	xor		eax, eax
	xor		ecx, ecx
.sum:
	mov		rdx, [score_stats + rcx * 8]
	imul	rdx, [score_weights + rcx * 8]
	add		rax, rdx
	inc		rcx
	cmp		rcx, STAT_COUNT
	jb		.sum
	ret

; required_level(rdi = ItemInstance) → rax = 1 + instance tier × balance.itemLevelPerTier.
required_level:
	imul	rax, [rdi + ItemInstance.tier], BAL_ITEM_LEVEL_PER_TIER
	inc		rax
	ret

; equipment_score() → rax = sum of the scores of the equipped items (shown by the text UI).
equipment_score:
	ENTER_FRAME
	xor		r12d, r12d						; r12 = total
	lea		rbx, [g_player + Player.equipment]
	mov		r13d, SLOT_COUNT
.slot:
	cmp		qword [rbx + ItemInstance.uid], 0
	je		.next
	mov		rdi, rbx
	call	item_score
	add		r12, rax
.next:
	add		rbx, ItemInstance_size
	dec		r13
	jnz		.slot
	mov		rax, r12
	LEAVE_FRAME

; build_sheet() — recomputes g_sheet from the player's vocation, level and equipment.
build_sheet:
	ENTER_FRAME
	lea		rdi, [stat_totals]
	mov		ecx, STAT_COUNT
	xor		eax, eax
	rep stosq

	lea		rbx, [g_player + Player.equipment]
	mov		r12d, SLOT_COUNT
.slot:
	cmp		qword [rbx + ItemInstance.uid], 0
	je		.next
	mov		rdi, rbx
	lea		rsi, [stat_totals]
	call	item_add_stats
.next:
	add		rbx, ItemInstance_size
	dec		r12
	jnz		.slot

	; Weapon element: the equipped weapon's, physical when there is none (bare hands or an element-less item).
	mov		eax, ELEMENT_PHYSICAL
	lea		rbx, [g_player + Player.equipment + SLOT_WEAPON * ItemInstance_size]
	cmp		qword [rbx + ItemInstance.uid], 0
	je		.element_done
	mov		rcx, [rbx + ItemInstance.item]
	ROW		rcx, items, rcx, Item_size
	mov		rcx, [rcx + Item.element]
	cmp		rcx, 0
	cmovge	rax, rcx
.element_done:
	mov		[g_sheet + Sheet.weapon_element], rax

	mov		rbx, [g_player + Player.vocation]
	ROW		rbx, vocations, rbx, Vocation_size		; rbx = Vocation row
	mov		r12, [g_player + Player.level]
	dec		r12										; r12 = level - 1

	mov		rax, [rbx + Vocation.hp_per_level]
	imul	rax, r12
	add		rax, [rbx + Vocation.start_hp]
	add		rax, [stat_totals + STAT_MAX_HP * 8]
	mov		[g_sheet + Sheet.max_hp], rax

	mov		rax, [rbx + Vocation.mp_per_level]
	imul	rax, r12
	add		rax, [rbx + Vocation.start_mp]
	add		rax, [stat_totals + STAT_MAX_MP * 8]
	mov		[g_sheet + Sheet.max_mp], rax

	mov		rax, [rbx + Vocation.hp_regen]
	add		rax, [stat_totals + STAT_HP_REGEN * 8]
	mov		[g_sheet + Sheet.hp_regen], rax
	mov		rax, [rbx + Vocation.mp_regen]
	add		rax, [stat_totals + STAT_MP_REGEN * 8]
	mov		[g_sheet + Sheet.mp_regen], rax

	; melee range = vocation range + (level - 1) × meleePerLevel + attack, on both ends
	mov		rcx, [rbx + Vocation.melee_per_level]
	imul	rcx, r12
	add		rcx, [stat_totals + STAT_ATTACK * 8]
	mov		rax, [rbx + Vocation.melee_min]
	add		rax, rcx
	mov		[g_sheet + Sheet.melee_min], rax
	mov		rax, [rbx + Vocation.melee_max]
	add		rax, rcx
	mov		[g_sheet + Sheet.melee_max], rax

	; COPY_STAT field, stat — uncapped;  CAP_STAT field, stat, cap — min(total, cap)
	%macro COPY_STAT 2
		mov		rax, [stat_totals + %2 * 8]
		mov		[g_sheet + Sheet.%1], rax
	%endmacro
	%macro CAP_STAT 3
		mov		rax, [stat_totals + %2 * 8]
		mov		ecx, %3
		cmp		rax, rcx
		cmovg	rax, rcx
		mov		[g_sheet + Sheet.%1], rax
	%endmacro
	COPY_STAT	armor, STAT_ARMOR
	CAP_STAT	crit_chance, STAT_CRIT_CHANCE, BAL_CAP_CRIT_CHANCE
	COPY_STAT	crit_damage, STAT_CRIT_DAMAGE
	COPY_STAT	spell_power, STAT_SPELL_POWER
	COPY_STAT	physical_damage, STAT_PHYSICAL_DAMAGE
	CAP_STAT	dodge, STAT_DODGE, BAL_CAP_DODGE
	CAP_STAT	parry, STAT_PARRY, BAL_CAP_PARRY
	CAP_STAT	life_leech, STAT_LIFE_LEECH, BAL_CAP_LEECH
	CAP_STAT	mana_leech, STAT_MANA_LEECH, BAL_CAP_LEECH

	; protections: the stat STAT_PROT_PHYSICAL + element, capped (see the check in enums.inc)
	xor		edx, edx
	mov		ecx, BAL_CAP_PROTECTION
.prot:
	mov		rax, [stat_totals + STAT_PROT_PHYSICAL * 8 + rdx * 8]
	cmp		rax, rcx
	cmovg	rax, rcx
	mov		[g_sheet + Sheet.prot + rdx * 8], rax
	inc		rdx
	cmp		rdx, ELEMENT_COUNT
	jb		.prot
	LEAVE_FRAME
