; application/bot.asm — GreedyBot: the deterministic heuristic player of the simulator and of the end-to-end
; parity tests (application/bot.py).
;
; Role: reads the run state and returns ONE command. Its decisions are recorded in the `bot-full-run-*` golden
; files, so every threshold and every tie-breaker below is part of the contract. The reference breaks ties with
; `max(key=(value, id))`: the greater value wins and, on equal values, the id that sorts last (code-point order,
; which is what libc's strcmp compares for these ASCII ids).
;
; Register use: every routine uses the standard frame. bot_choose returns the command the way engine_step takes
; it, shifted by one register: rax = CMD_*, rsi = first argument, rdx = second argument.
%include "common.inc"

extern g_run, g_player, g_monster, g_sheet
extern build_sheet, can_use, item_score, required_level, spell_cost, monster_resist

global bot_choose

; The bot's own heuristics (constants of bot.py, not balance numbers).
HEAL_THRESHOLD_PCT			equ 45	; heal below this % of the maximum HP
MANA_POTION_THRESHOLD_PCT	equ 25	; drink a mana potion below this % of the maximum MP
MAX_POTION_STOCK			equ 20
BASE_POTION_STOCK			equ 5	; potions wanted per resource: 5 + round / 5, at most MAX_POTION_STOCK
ROUNDS_PER_EXTRA_POTION		equ 5

FILTER_OWNED	equ 0	; best_potion: potions the player owns
FILTER_UNLOCKED	equ 1	; best_potion: potions the merchant sells now

section .text

; bot_choose() → rax = CMD_*, rsi, rdx = its arguments.
bot_choose:
	mov		rax, [g_run + RunState.phase]
	cmp		rax, PHASE_BATTLE
	je		bot_battle
	cmp		rax, PHASE_VICTORY
	jne		bot_merchant
	mov		eax, CMD_END_RUN				; the simulator stops at the first victory
	ret

; ── battle ────────────────────────────────────────────────────────────────────

; bot_battle() — defend against the boss charge; heal when hurt; restore mana when dry; otherwise the attack
; spell that hurts this monster the most, or a plain attack.
bot_battle:
	ENTER_FRAME
	call	build_sheet
	cmp		qword [g_monster + MonsterInstance.is_boss], 0
	je		.heal
	mov		rax, [g_monster + MonsterInstance.boss_actions]
	xor		edx, edx
	mov		ecx, BAL_BOSS_TELEGRAPH_EVERY + 1
	div		rcx
	cmp		rdx, BAL_BOSS_TELEGRAPH_EVERY	; the next boss action is the charged attack
	jne		.heal
	mov		eax, CMD_DEFEND
	jmp		.done
.heal:
	imul	rax, [g_player + Player.hp], 100
	imul	rcx, [g_sheet + Sheet.max_hp], HEAL_THRESHOLD_PCT
	cmp		rax, rcx						; hp × 100 < maxHp × threshold (integers only)
	jge		.mana
	mov		edi, SPELLKIND_HEAL
	call	best_spell
	cmp		rax, 0
	jge		.cast
	mov		edi, RESOURCE_HP
	mov		esi, FILTER_OWNED
	call	best_potion
	cmp		rax, 0
	jge		.drink
.mana:
	imul	rax, [g_player + Player.mp], 100
	imul	rcx, [g_sheet + Sheet.max_mp], MANA_POTION_THRESHOLD_PCT
	cmp		rax, rcx
	jge		.spell
	mov		edi, RESOURCE_MP
	mov		esi, FILTER_OWNED
	call	best_potion
	cmp		rax, 0
	jge		.drink
.spell:
	mov		edi, SPELLKIND_ATTACK
	call	best_spell
	cmp		rax, 0
	jge		.cast
	mov		eax, CMD_ATTACK
	jmp		.done
.cast:
	mov		rsi, rax
	mov		eax, CMD_CAST
	jmp		.done
.drink:
	mov		rsi, rax
	mov		eax, CMD_POTION
.done:
	LEAVE_FRAME

; best_spell(rdi = SPELLKIND_*) → rax = the best affordable spell of that kind among the vocation's, or -1.
;   healing: highest `max`;  attack: highest (min + max) × monster resistance, immune elements excluded.
; Ties go to the id that sorts last.
; Frame: rbx = Vocation row, r12 = kind, r13 = best spell row, r14 = position in the vocation's spells,
; r15 = current spell row. Locals: [rsp] = best value, [rsp + 8] = current value.
best_spell:
	ENTER_FRAME 16
	mov		r12, rdi
	mov		rbx, [g_player + Player.vocation]
	ROW		rbx, vocations, rbx, Vocation_size
	mov		r13, -1
	xor		r14d, r14d
.spell:
	cmp		r14, [rbx + Vocation.spell_count]
	jae		.done
	mov		r15, [rbx + Vocation.spells + r14 * 8]
	ROW		rax, spells, r15, Spell_size
	cmp		[rax + Spell.kind], r12
	jne		.next
	mov		rdi, r15
	call	spell_cost
	cmp		rax, [g_player + Player.mp]
	jg		.next							; not affordable
	ROW		rcx, spells, r15, Spell_size
	mov		rax, [rcx + Spell.max]
	cmp		r12, SPELLKIND_HEAL
	je		.compare
	mov		rdi, [rcx + Spell.element]
	call	monster_resist					; destroys only rax
	test	rax, rax
	jle		.next							; the monster is immune to this element
	mov		rdx, [rcx + Spell.min]
	add		rdx, [rcx + Spell.max]
	imul	rax, rdx
.compare:
	mov		[rsp + 8], rax
	cmp		r13, -1
	je		.take
	cmp		rax, [rsp]
	jg		.take
	jl		.next
	ROW		rdi, spells, r15, Spell_size
	mov		rdi, [rdi + Spell.id]
	ROW		rsi, spells, r13, Spell_size
	mov		rsi, [rsi + Spell.id]
	call	strcmp
	test	eax, eax
	jle		.next
.take:
	mov		r13, r15
	mov		rax, [rsp + 8]
	mov		[rsp], rax
.next:
	inc		r14
	jmp		.spell
.done:
	mov		rax, r13
	LEAVE_FRAME

; best_potion(rdi = RESOURCE_*, rsi = FILTER_*) → rax = the potion of that resource with the highest `max` among
; the filtered ones (ties: the id that sorts last), or -1; rdx = how many of the filtered potions the player owns.
; Frame: rbx = current Potion row, r12 = resource, r13 = filter, r14 = best potion row, r15 = current potion row
; index. Local: [rsp] = owned total.
best_potion:
	ENTER_FRAME 16
	mov		r12, rdi
	mov		r13, rsi
	mov		r14, -1
	xor		r15d, r15d
	mov		qword [rsp], 0
	lea		rbx, [potions]
.potion:
	cmp		r15, POTION_COUNT
	jae		.done
	cmp		[rbx + Potion.resource], r12
	jne		.next
	cmp		r13, FILTER_OWNED
	jne		.unlocked
	cmp		qword [g_player + Player.potions + r15 * 8], 0
	jle		.next
	jmp		.candidate
.unlocked:
	mov		rax, [g_run + RunState.round]
	inc		rax								; the merchant sells what the NEXT round unlocks
	cmp		[rbx + Potion.unlock_round], rax
	jg		.next
.candidate:
	mov		rax, [g_player + Player.potions + r15 * 8]
	add		[rsp], rax
	cmp		r14, -1
	je		.take
	ROW		rcx, potions, r14, Potion_size
	mov		rax, [rbx + Potion.max]
	cmp		rax, [rcx + Potion.max]
	jg		.take
	jl		.next
	mov		rdi, [rbx + Potion.id]
	mov		rsi, [rcx + Potion.id]
	call	strcmp
	test	eax, eax
	jle		.next
.take:
	mov		r14, r15
.next:
	add		rbx, Potion_size
	inc		r15
	jmp		.potion
.done:
	mov		rax, r14
	mov		rdx, [rsp]
	LEAVE_FRAME

; ── merchant ──────────────────────────────────────────────────────────────────

; bot_merchant() — equip the first bag item (by uid) that beats the equipped one; else sell the lowest uid; else
; stock up on health then mana potions; else fight.
; Frame: rbx = current bag item, r12 = last uid visited, r13 = Vocation row, r14 = uid being looked for,
; r15 = equipment slot of the item. Local: [rsp] = score of the bag item.
bot_merchant:
	ENTER_FRAME 16
	mov		r13, [g_player + Player.vocation]
	ROW		r13, vocations, r13, Vocation_size
	xor		r12d, r12d
.scan:
	; next bag item in uid order: the smallest uid greater than the last one visited
	xor		ebx, ebx
	mov		r14, -1							; unsigned maximum
	lea		rax, [g_player + Player.bag]
	mov		rcx, [g_player + Player.bag_count]
.smallest:
	test	rcx, rcx
	jz		.smallest_done
	mov		rdx, [rax + ItemInstance.uid]
	cmp		rdx, r12
	jbe		.smallest_next
	cmp		rdx, r14
	jae		.smallest_next
	mov		r14, rdx
	mov		rbx, rax
.smallest_next:
	add		rax, ItemInstance_size
	dec		rcx
	jmp		.smallest
.smallest_done:
	test	rbx, rbx
	jz		.sell							; every bag item was considered
	mov		r12, r14
	mov		rdi, [rbx + ItemInstance.item]
	ROW		rdi, items, rdi, Item_size
	imul	r15, [rdi + Item.slot], ItemInstance_size
	lea		rax, [g_player + Player.equipment]
	add		r15, rax
	mov		rsi, r13
	call	can_use
	test	eax, eax
	jz		.scan
	mov		rdi, rbx
	call	required_level
	cmp		rax, [g_player + Player.level]
	jg		.scan
	cmp		qword [r15 + ItemInstance.uid], 0
	je		.equip							; empty slot
	mov		rdi, rbx
	call	item_score
	mov		[rsp], rax
	mov		rdi, r15
	call	item_score
	cmp		[rsp], rax
	jle		.scan
.equip:
	mov		eax, CMD_EQUIP
	mov		rsi, r12
	jmp		.done
.sell:
	; nothing to equip: sell the bag, lowest uid first
	mov		rcx, [g_player + Player.bag_count]
	test	rcx, rcx
	jz		.potions
	lea		rax, [g_player + Player.bag]
	mov		rsi, -1
.lowest:
	mov		rdx, [rax + ItemInstance.uid]
	cmp		rdx, rsi
	cmovb	rsi, rdx
	add		rax, ItemInstance_size
	dec		rcx
	jnz		.lowest
	mov		eax, CMD_SELL_ITEM
	jmp		.done
.potions:
	mov		edi, RESOURCE_HP
	call	potion_purchase
	cmp		rax, 0
	jge		.buy
	mov		edi, RESOURCE_MP
	call	potion_purchase
	cmp		rax, 0
	jge		.buy
	mov		eax, CMD_NEXT_FIGHT
	jmp		.done
.buy:
	mov		rsi, rax
	mov		eax, CMD_BUY_POTION				; rdx = quantity, as returned by potion_purchase
.done:
	LEAVE_FRAME

; potion_purchase(rdi = RESOURCE_*) → rax = potion row to buy (the best one on sale) or -1, rdx = quantity.
; The bot keeps 5 + round / 5 potions of each resource (at most 20), spends all its gold on health potions and
; at most half of what is left on mana potions.
; Frame: rbx = Potion row, r12 = resource, r13 = best potion row, r14 = quantity still wanted.
potion_purchase:
	ENTER_FRAME
	mov		r12, rdi
	mov		esi, FILTER_UNLOCKED
	call	best_potion						; rax = best on sale, rdx = owned among the ones on sale
	mov		r13, rax
	cmp		rax, 0
	jl		.none
	mov		rcx, rdx
	mov		rax, [g_run + RunState.round]
	xor		edx, edx
	mov		esi, ROUNDS_PER_EXTRA_POTION
	div		rsi
	add		rax, BASE_POTION_STOCK
	mov		esi, MAX_POTION_STOCK
	cmp		rax, rsi
	cmova	rax, rsi						; target stock
	sub		rax, rcx
	mov		r14, rax						; target - owned (may be negative)
	ROW		rbx, potions, r13, Potion_size
	mov		rax, [g_player + Player.gold]
	cmp		r12, RESOURCE_MP
	jne		.budget
	shr		rax, 1							; mana potions: half of the gold
.budget:
	xor		edx, edx
	div		qword [rbx + Potion.price]		; budget / price
	cmp		rax, r14
	cmovg	rax, r14						; quantity = min(target - owned, budget / price)
	cmp		rax, 0
	jle		.none
	mov		rdx, rax
	mov		rax, r13
	jmp		.done
.none:
	mov		rax, -1
.done:
	LEAVE_FRAME
