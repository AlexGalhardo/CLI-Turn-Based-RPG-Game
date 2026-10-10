; application/battle.asm — one battle turn, following docs/game-design.md §6 step by step (application/battle.py).
; Every rng call here is part of the contract: the order and the conditions under which a number is consumed
; decide whether the port replays the golden files.
;
; Role: `battle_validate` rejects an invalid command without touching anything; `battle_play_turn` resolves the
; player action, the monster phase and the end of the turn, appending events, and reports who died.
;
; Register use: routines with ENTER_FRAME follow common.inc. The small leaves at the top state what they destroy
; (several promise to destroy only rax so their callers can keep values in caller-saved registers).
; The derived stats are read from `g_sheet` right after `build_sheet` (level and equipment cannot change during a
; turn, so the sheet stays valid until the turn ends).
%include "common.inc"

extern g_run, g_player, g_monster, g_sheet
extern build_sheet, pct, armor_mitigation, spell_level_for_uses
extern rng_roll, rng_chance, rng_weighted
extern status_find, status_remove, after_cast, event_new

global battle_validate, battle_play_turn, spell_cost, monster_resist

STUN_COOLDOWN_TURNS		equ 2	; turns a target cannot be stunned again after losing a turn
SPELL_BONUS_LEVEL		equ 3	; spell level that unlocks level3Bonus

; AT_LEAST_ONE reg — reg = max(1, reg) for a non-negative reg. Destroys rcx.
%macro AT_LEAST_ONE 1
	mov		ecx, 1
	test	%1, %1
	cmovz	%1, rcx
%endmacro

; HURT_PLAYER reg — player.hp = max(0, hp - reg). Destroys rcx, rdx.
%macro HURT_PLAYER 1
	mov		rdx, [g_player + Player.hp]
	xor		ecx, ecx
	sub		rdx, %1
	cmovs	rdx, rcx
	mov		[g_player + Player.hp], rdx
%endmacro

section .bss align=16
attack_weights:	resq MAX_ATTACKS		; scratch for the weighted pick of the monster attack

section .text

; ── small helpers ─────────────────────────────────────────────────────────────

; class_row() → rax = EnemyClass row of the current monster. Destroys only rax.
class_row:
	mov		rax, [g_monster + MonsterInstance.enemy_class]
	ROW		rax, enemy_classes, rax, EnemyClass_size
	ret

; monster_resist(rdi = element) → rax = damage the current monster takes from that element, in % (0 = immune).
; Destroys only rax.
monster_resist:
	mov		rax, [g_monster + MonsterInstance.creature]
	ROW		rax, creatures, rax, Creature_size
	mov		rax, [rax + Creature.resist + rdi * 8]
	ret

; hit_monster(rdi = damage) — monster.hp = max(0, hp - damage). Destroys rax, rcx.
hit_monster:
	mov		rax, [g_monster + MonsterInstance.hp]
	xor		ecx, ecx
	sub		rax, rdi
	cmovs	rax, rcx
	mov		[g_monster + MonsterInstance.hp], rax
	ret

; spell_cost(rdi = spell row) → rax = pct(spell.mana, manaPct of the spell's current level).
; Destroys rcx, rdx, rsi, rdi.
spell_cost:
	mov		rsi, rdi
	mov		rdi, [g_player + Player.spell_uses + rsi * 8]
	call	spell_level_for_uses
	ROW		rdi, spells, rsi, Spell_size
	mov		rdi, [rdi + Spell.mana]
	mov		rsi, [rax + SpellLevel.mana_pct]
	jmp		pct

; resisted(rdi = damage, rsi = element) → rax = damage after the monster's resistance: 0 when immune, else at
; least 1. Destroys rcx, rdx, rsi.
resisted:
	xchg	rdi, rsi
	call	monster_resist
	xchg	rdi, rsi						; rdi = damage again
	mov		rsi, rax
	xor		eax, eax
	test	rsi, rsi
	jz		.done
	call	pct
	AT_LEAST_ONE rax
.done:
	ret

; consume_stun(rdi = StatusList) → eax = 1 when the list had a stun, which is removed.
consume_stun:
	xor		esi, esi
	mov		rcx, [rdi + StatusList.count]
	lea		rax, [rdi + StatusList.items]
.next:
	cmp		rsi, rcx
	jae		.none
	cmp		qword [rax + ActiveStatus.status], STATUS_STUN
	je		.found
	add		rax, ActiveStatus_size
	inc		rsi
	jmp		.next
.found:
	call	status_remove
	mov		eax, 1
	ret
.none:
	xor		eax, eax
	ret

; death_check() → rax = OUTCOME_*. The player is checked first: a parried hit can kill the attacker.
death_check:
	mov		eax, OUTCOME_DEFEAT
	cmp		qword [g_player + Player.hp], 0
	jle		.done
	mov		eax, OUTCOME_VICTORY
	cmp		qword [g_monster + MonsterInstance.hp], 0
	jle		.done
	mov		eax, OUTCOME_ONGOING
.done:
	ret

; ── validation ────────────────────────────────────────────────────────────────

; battle_validate(rdi = CMD_*, rsi = argument) → rax = ERR_* for an invalid command, -1 for a valid one.
; Validation never consumes randomness and changes nothing.
battle_validate:
	ENTER_FRAME
	mov		r12, rsi
	cmp		rdi, CMD_CAST
	je		.cast
	cmp		rdi, CMD_POTION
	jne		.valid
	mov		eax, ERR_UNKNOWN_POTION
	cmp		r12, POTION_COUNT				; unsigned: also rejects -1 (an id that is not a potion)
	jae		.done
	mov		eax, ERR_NO_POTION
	cmp		qword [g_player + Player.potions + r12 * 8], 0
	jle		.done
	jmp		.valid
.cast:
	; the spell must be one of the vocation's
	mov		rbx, [g_player + Player.vocation]
	ROW		rbx, vocations, rbx, Vocation_size
	mov		rcx, [rbx + Vocation.spell_count]
	mov		eax, ERR_UNKNOWN_SPELL
.known:
	test	rcx, rcx
	jz		.done
	dec		rcx
	cmp		[rbx + Vocation.spells + rcx * 8], r12
	jne		.known
	mov		rdi, r12
	call	spell_cost
	mov		rcx, rax
	mov		eax, ERR_NOT_ENOUGH_MANA
	cmp		[g_player + Player.mp], rcx
	jl		.done
.valid:
	mov		rax, -1
.done:
	LEAVE_FRAME

; ── step 1: player action ─────────────────────────────────────────────────────

; monster_dodges() → eax = 1 when the monster dodged the player's attack or spell (monster_dodged emitted).
monster_dodges:
	sub		rsp, 8
	call	class_row
	mov		rdi, [rax + EnemyClass.dodge]
	call	rng_chance
	test	eax, eax
	jz		.done
	EMIT	EV_MONSTER_DODGED
	mov		eax, 1
.done:
	add		rsp, 8
	ret

; monster_parries(rdi = damage, rsi = element) → eax = 1 when the monster parried.
; Physical hits only: the monster takes nothing and the player takes max(1, pct(damage, parryReflectPct)), with
; no mitigation. It is rolled even when the damage is 0 (immune monster), so the reflect is then 1.
monster_parries:
	ENTER_FRAME
	mov		rbx, rdi
	xor		eax, eax
	cmp		rsi, ELEMENT_PHYSICAL
	jne		.done
	call	class_row
	mov		rdi, [rax + EnemyClass.parry]
	call	rng_chance
	test	eax, eax
	jz		.done
	mov		rdi, rbx
	mov		esi, BAL_PARRY_REFLECT_PCT
	call	pct
	AT_LEAST_ONE rax
	mov		rsi, rax
	HURT_PLAYER rsi
	EMIT	EV_MONSTER_PARRIED, rsi
	mov		eax, 1
.done:
	LEAVE_FRAME

; roll_crit(rdi = damage) → rax = damage (× critMultiplierPct + critDamage on a critical hit), rdx = 1 on a crit.
roll_crit:
	ENTER_FRAME
	mov		rbx, rdi
	mov		rdi, [g_sheet + Sheet.crit_chance]
	call	rng_chance
	mov		r12, rax
	mov		rax, rbx
	test	r12, r12
	jz		.done
	mov		rdi, rbx
	mov		rsi, [g_sheet + Sheet.crit_damage]
	add		rsi, BAL_CRIT_MULTIPLIER_PCT
	call	pct
.done:
	mov		rdx, r12
	LEAVE_FRAME

; leech(rdi = damage dealt) — returns lifeLeech% / manaLeech% of the damage as HP / MP, capped at the maximum.
; Frame: r12 = HP gained, r13 = MP gained.
leech:
	ENTER_FRAME
	mov		rbx, rdi
	mov		rsi, [g_sheet + Sheet.life_leech]
	call	pct
	mov		rcx, [g_sheet + Sheet.max_hp]
	sub		rcx, [g_player + Player.hp]
	cmp		rax, rcx
	cmovg	rax, rcx
	mov		r12, rax
	mov		rdi, rbx
	mov		rsi, [g_sheet + Sheet.mana_leech]
	call	pct
	mov		rcx, [g_sheet + Sheet.max_mp]
	sub		rcx, [g_player + Player.mp]
	cmp		rax, rcx
	cmovg	rax, rcx
	mov		r13, rax
	cmp		r12, 0
	jg		.apply
	cmp		r13, 0
	jle		.done							; nothing gained: no event
.apply:
	xor		eax, eax
	test	r12, r12
	cmovs	r12, rax
	test	r13, r13
	cmovs	r13, rax
	add		[g_player + Player.hp], r12
	add		[g_player + Player.mp], r13
	EMIT	EV_LEECHED, r12, r13
.done:
	LEAVE_FRAME

; melee() — the `attack` command (§6.1). Frame: rbx = damage, r12 = crit flag.
melee:
	ENTER_FRAME
	call	build_sheet
	call	monster_dodges					; 1. dodge: no damage, no further rolls
	test	eax, eax
	jnz		.done
	mov		rdi, [g_sheet + Sheet.melee_min]
	mov		rsi, [g_sheet + Sheet.melee_max]
	call	rng_roll						; 2. base roll
	mov		rdi, rax
	mov		rsi, [g_sheet + Sheet.physical_damage]
	add		rsi, 100
	call	pct								; 3. physicalDamage bonus
	mov		rdi, rax
	call	roll_crit						; 4. crit
	mov		rbx, rax
	mov		r12, rdx
	mov		rdi, rbx
	mov		rsi, [g_sheet + Sheet.weapon_element]
	call	resisted						; 5-6. resistance, minimum 1 / immune
	mov		rbx, rax
	mov		rdi, rbx
	mov		rsi, [g_sheet + Sheet.weapon_element]
	call	monster_parries					; 7. parry (physical only)
	test	eax, eax
	jnz		.done
	mov		rdi, rbx
	call	hit_monster
	mov		rsi, [g_sheet + Sheet.weapon_element]
	EMIT	EV_PLAYER_ATTACKED, rbx, r12, rsi
	mov		rdi, rbx
	call	leech							; 8. leech
.done:
	LEAVE_FRAME

; cast(rdi = spell row) — the `cast` command, attack and healing spells (§6.1). The command was validated.
; Frame: rbx = Spell row, r12 = spell row index, r13 = SpellLevel row, r14 = mana cost, r15 = damage / healing.
; Local: [rsp] = crit flag.
cast:
	ENTER_FRAME 16
	mov		r12, rdi
	ROW		rbx, spells, r12, Spell_size
	call	build_sheet
	mov		rdi, [g_player + Player.spell_uses + r12 * 8]
	call	spell_level_for_uses
	mov		r13, rax
	mov		rdi, [rbx + Spell.mana]
	mov		rsi, [r13 + SpellLevel.mana_pct]
	call	pct
	mov		r14, rax
	sub		[g_player + Player.mp], r14		; the mana is paid first
	cmp		qword [rbx + Spell.kind], SPELLKIND_ATTACK
	jne		.roll
	call	monster_dodges					; a dodged spell still costs mana and counts as a use
	test	eax, eax
	jnz		.after
.roll:
	mov		rax, [g_player + Player.level]
	imul	rax, [rbx + Spell.per_level]
	mov		rcx, [g_player + Player.magic_level]
	imul	rcx, [rbx + Spell.per_magic_level]
	add		rax, rcx						; bonus = level × perLevel + magicLevel × perMagicLevel
	mov		rdi, [rbx + Spell.min]
	add		rdi, rax
	mov		rsi, [rbx + Spell.max]
	add		rsi, rax
	call	rng_roll
	mov		rdi, rax
	mov		rsi, [r13 + SpellLevel.effect_pct]
	call	pct
	mov		rdi, rax
	mov		rsi, [g_sheet + Sheet.spell_power]
	add		rsi, 100
	call	pct
	mov		r15, rax
	cmp		qword [rbx + Spell.kind], SPELLKIND_ATTACK
	jne		.heal

	mov		rdi, r15
	call	roll_crit
	mov		r15, rax
	mov		[rsp], rdx
	mov		rdi, r15
	mov		rsi, [rbx + Spell.element]
	call	resisted
	mov		r15, rax
	mov		rdi, r15
	mov		rsi, [rbx + Spell.element]
	call	monster_parries
	test	eax, eax
	jnz		.after
	mov		rdi, r15
	call	hit_monster
	mov		rsi, [rsp]
	mov		rdx, [rbx + Spell.element]
	EMIT	EV_SPELL_CAST, r12, r15, rsi, rdx, r14
	mov		rdi, r15
	call	leech
	; level 3 bonus: a chance to apply a status whose damage per turn comes from this hit
	cmp		qword [r13 + SpellLevel.level], SPELL_BONUS_LEVEL
	jne		.after
	cmp		qword [rbx + Spell.l3_status], 0
	jl		.after
	mov		rdi, [rbx + Spell.l3_chance]
	call	rng_chance
	test	eax, eax
	jz		.after
	mov		rdi, r15
	mov		esi, BAL_SPELL_STATUS_DAMAGE_PCT
	call	pct
	AT_LEAST_ONE rax
	mov		rdx, rax
	mov		edi, TARGET_MONSTER
	mov		rsi, [rbx + Spell.l3_status]
	call	apply_status
	jmp		.after

.heal:
	mov		rcx, [g_sheet + Sheet.max_hp]
	sub		rcx, [g_player + Player.hp]
	cmp		r15, rcx
	cmovg	r15, rcx						; healing is capped at the maximum HP
	add		[g_player + Player.hp], r15
	EMIT	EV_SPELL_HEALED, r12, r15, r14
	cmp		qword [r13 + SpellLevel.level], SPELL_BONUS_LEVEL
	jne		.after
	cmp		qword [rbx + Spell.l3_cleanse], 0
	je		.after
	; cleanse: every status of the player expires, in list order
	lea		r15, [g_player + Player.statuses + StatusList.items]
	mov		r13, [g_player + Player.statuses + StatusList.count]
.cleanse:
	test	r13, r13
	jz		.cleansed
	mov		rsi, [r15 + ActiveStatus.status]
	EMIT	EV_STATUS_EXPIRED, TARGET_PLAYER, rsi
	add		r15, ActiveStatus_size
	dec		r13
	jmp		.cleanse
.cleansed:
	mov		qword [g_player + Player.statuses + StatusList.count], 0

.after:
	mov		rdi, r12
	mov		rsi, r14
	call	after_cast
	LEAVE_FRAME

; drink(rdi = potion row) — the `potion` command: roll(min, max) restored to HP or MP, capped.
; Frame: rbx = Potion row, r12 = potion row index.
drink:
	ENTER_FRAME
	mov		r12, rdi
	ROW		rbx, potions, r12, Potion_size
	call	build_sheet
	dec		qword [g_player + Player.potions + r12 * 8]
	mov		rdi, [rbx + Potion.min]
	mov		rsi, [rbx + Potion.max]
	call	rng_roll
	cmp		qword [rbx + Potion.resource], RESOURCE_HP
	jne		.mana
	mov		rcx, [g_sheet + Sheet.max_hp]
	sub		rcx, [g_player + Player.hp]
	cmp		rax, rcx
	cmovg	rax, rcx
	add		[g_player + Player.hp], rax
	jmp		.emit
.mana:
	mov		rcx, [g_sheet + Sheet.max_mp]
	sub		rcx, [g_player + Player.mp]
	cmp		rax, rcx
	cmovg	rax, rcx
	add		[g_player + Player.mp], rax
.emit:
	mov		rsi, rax
	mov		rdx, [rbx + Potion.resource]
	EMIT	EV_POTION_USED, r12, rsi, rdx
	LEAVE_FRAME

; ── step 3: monster phase ─────────────────────────────────────────────────────

; monster_phase() — status ticks, stun, heal, boss pattern, attack (§6.3).
; Frame: rbx = Creature row, r12 = charged attack index.
monster_phase:
	ENTER_FRAME
	mov		edi, TARGET_MONSTER
	call	tick							; 3.1 status ticks; a dead monster does nothing else
	cmp		qword [g_monster + MonsterInstance.hp], 0
	jle		.done
	lea		rdi, [g_monster + MonsterInstance.statuses]
	call	consume_stun					; 3.2 stunned: the turn is lost
	test	eax, eax
	jz		.heal
	mov		qword [g_monster + MonsterInstance.stun_cooldown], STUN_COOLDOWN_TURNS
	EMIT	EV_MONSTER_STUNNED
	jmp		.done
.heal:
	; 3.3 heal: only rolled below full HP; a healing monster does nothing else (a boss keeps its pattern step)
	mov		rax, [g_monster + MonsterInstance.hp]
	cmp		rax, [g_monster + MonsterInstance.max_hp]
	jge		.act
	call	class_row
	mov		rdi, [rax + EnemyClass.heal]
	call	rng_chance
	test	eax, eax
	jz		.act
	mov		rdi, [g_monster + MonsterInstance.max_hp]
	mov		esi, BAL_MONSTER_HEAL_PCT
	call	pct
	mov		rcx, [g_monster + MonsterInstance.max_hp]
	sub		rcx, [g_monster + MonsterInstance.hp]
	cmp		rax, rcx
	cmovg	rax, rcx
	add		[g_monster + MonsterInstance.hp], rax
	mov		rsi, rax
	EMIT	EV_MONSTER_HEALED, rsi
	jmp		.done
.act:
	mov		rbx, [g_monster + MonsterInstance.creature]
	ROW		rbx, creatures, rbx, Creature_size
	cmp		qword [g_monster + MonsterInstance.is_boss], 0
	je		.normal
	; 3.4 boss pattern: normal attacks, then a telegraph, then the charged attack
	mov		rax, [g_monster + MonsterInstance.boss_actions]
	xor		edx, edx
	mov		ecx, BAL_BOSS_TELEGRAPH_EVERY + 1
	div		rcx								; rdx = position in the pattern
	inc		qword [g_monster + MonsterInstance.boss_actions]
	mov		r12, [rbx + Creature.charge_attack]
	cmp		r12, 0
	jl		.normal
	cmp		rdx, BAL_BOSS_TELEGRAPH_EVERY - 1
	je		.telegraph
	cmp		rdx, BAL_BOSS_TELEGRAPH_EVERY
	jne		.normal
	mov		rdi, r12
	mov		esi, 1
	call	resolve_monster_attack			; the charged attack
	jmp		.done
.telegraph:
	imul	rax, r12, Attack_size
	add		rax, [rbx + Creature.attacks]
	mov		rsi, [rax + Attack.id]
	mov		rdx, [rax + Attack.element]
	EMIT	EV_BOSS_TELEGRAPH, rsi, rdx
	jmp		.done
.normal:
	; 3.5 weighted pick over the attacks: always rolled, even with one attack
	mov		rsi, [rbx + Creature.attack_count]
	mov		rax, [rbx + Creature.attacks]
	xor		ecx, ecx
.weights:
	mov		rdx, [rax + Attack.weight]
	mov		[attack_weights + rcx * 8], rdx
	add		rax, Attack_size
	inc		rcx
	cmp		rcx, rsi
	jb		.weights
	lea		rdi, [attack_weights]
	call	rng_weighted
	mov		rdi, rax
	xor		esi, esi
	call	resolve_monster_attack
.done:
	LEAVE_FRAME

; resolve_monster_attack(rdi = attack index, rsi = 1 for the boss charged attack) — §6.3 steps 6 to 11.
; Frame: rbx = Attack row, r12 = attack index, r13 = charged flag, r14 = damage, r15 = crit flag.
resolve_monster_attack:
	ENTER_FRAME
	mov		r12, rdi
	mov		r13, rsi
	mov		rbx, [g_monster + MonsterInstance.creature]
	ROW		rbx, creatures, rbx, Creature_size
	imul	rax, r12, Attack_size
	add		rax, [rbx + Creature.attacks]
	mov		rbx, rax
	call	build_sheet
	mov		rdi, [g_sheet + Sheet.dodge]
	call	rng_chance						; 6. player dodge: no further rolls
	test	eax, eax
	jz		.hit
	mov		rsi, [rbx + Attack.id]
	EMIT	EV_ATTACK_DODGED, rsi
	jmp		.done
.hit:
	mov		rdi, [g_monster + MonsterInstance.attack_min + r12 * 8]
	mov		rsi, [g_monster + MonsterInstance.attack_max + r12 * 8]
	call	rng_roll						; 7. raw damage (already scaled for the round)
	mov		r14, rax
	test	r13, r13
	jz		.parry
	PCT		r14, BAL_BOSS_CHARGE_DAMAGE_PCT
.parry:
	cmp		qword [rbx + Attack.element], ELEMENT_PHYSICAL
	jne		.crit
	mov		rdi, [g_sheet + Sheet.parry]
	call	rng_chance						; 8. player parry (physical only): the monster takes the reflect
	test	eax, eax
	jz		.crit
	PCT		r14, BAL_PARRY_REFLECT_PCT
	AT_LEAST_ONE r14
	mov		rdi, r14
	call	hit_monster
	mov		rsi, [rbx + Attack.id]
	EMIT	EV_ATTACK_PARRIED, rsi, r14
	jmp		.done
.crit:
	call	class_row
	mov		rdi, [rax + EnemyClass.crit]
	call	rng_chance						; 9. monster crit (the player's critDamage does not apply)
	mov		r15, rax
	test	r15, r15
	jz		.mitigate
	PCT		r14, BAL_CRIT_MULTIPLIER_PCT
.mitigate:
	cmp		qword [rbx + Attack.element], ELEMENT_PHYSICAL
	jne		.protection
	mov		rdi, r14
	mov		rsi, [g_sheet + Sheet.armor]
	call	armor_mitigation				; 10. armor (physical only)...
	mov		r14, rax
.protection:
	mov		rax, [rbx + Attack.element]
	mov		esi, 100
	sub		rsi, [g_sheet + Sheet.prot + rax * 8]
	mov		rdi, r14
	call	pct								; ...then the element protection...
	mov		r14, rax
	cmp		qword [g_player + Player.defending], 0
	je		.minimum
	PCT		r14, BAL_DEFEND_DAMAGE_PCT		; ...then defend...
.minimum:
	AT_LEAST_ONE r14						; ...and a hit always deals at least 1
	HURT_PLAYER r14
	mov		rsi, [rbx + Attack.id]
	mov		rdx, [rbx + Attack.element]
	EMIT	EV_MONSTER_ATTACKED, rsi, r14, rdx, r13, r15
	cmp		qword [rbx + Attack.status], 0
	jl		.done
	mov		rdi, [rbx + Attack.status_chance]
	call	rng_chance						; 11. status on hit
	test	eax, eax
	jz		.done
	mov		rdi, r14
	mov		rsi, [rbx + Attack.status_damage_pct]
	call	pct
	AT_LEAST_ONE rax
	mov		rdx, rax
	mov		edi, TARGET_PLAYER
	mov		rsi, [rbx + Attack.status]
	call	apply_status
.done:
	LEAVE_FRAME

; ── step 5: end of turn ───────────────────────────────────────────────────────

; end_of_turn() → eax = 1 when the player died from status ticks.
; Frame: rbx = HP regenerated, r12 = MP regenerated.
end_of_turn:
	ENTER_FRAME
	mov		edi, TARGET_PLAYER
	call	tick
	mov		eax, 1
	cmp		qword [g_player + Player.hp], 0
	jle		.done
	call	build_sheet
	mov		rbx, [g_sheet + Sheet.hp_regen]
	mov		rcx, [g_sheet + Sheet.max_hp]
	sub		rcx, [g_player + Player.hp]
	cmp		rbx, rcx
	cmovg	rbx, rcx
	xor		ecx, ecx
	test	rbx, rbx
	cmovs	rbx, rcx
	mov		r12, [g_sheet + Sheet.mp_regen]
	mov		rcx, [g_sheet + Sheet.max_mp]
	sub		rcx, [g_player + Player.mp]
	cmp		r12, rcx
	cmovg	r12, rcx
	xor		ecx, ecx
	test	r12, r12
	cmovs	r12, rcx
	add		[g_player + Player.hp], rbx
	add		[g_player + Player.mp], r12
	mov		rax, rbx
	or		rax, r12
	jz		.flags							; nothing regenerated: no event
	EMIT	EV_REGENERATED, rbx, r12
.flags:
	mov		qword [g_player + Player.defending], 0
	xor		ecx, ecx
	mov		rax, [g_player + Player.stun_cooldown]
	dec		rax
	cmovs	rax, rcx
	mov		[g_player + Player.stun_cooldown], rax
	mov		rax, [g_monster + MonsterInstance.stun_cooldown]
	dec		rax
	cmovs	rax, rcx
	mov		[g_monster + MonsterInstance.stun_cooldown], rax
	inc		qword [g_run + RunState.turn]
	xor		eax, eax
.done:
	LEAVE_FRAME

; ── statuses (§7) ─────────────────────────────────────────────────────────────

; apply_status(rdi = TARGET_*, rsi = status row, rdx = damage per turn).
; Frame: rbx = Status row, r12 = target, r13 = status row index, r14 = per turn, r15 = StatusList of the target.
apply_status:
	ENTER_FRAME
	mov		r12, rdi
	mov		r13, rsi
	mov		r14, rdx
	ROW		rbx, statuses, r13, Status_size
	cmp		r12, TARGET_PLAYER
	jne		.monster
	lea		r15, [g_player + Player.statuses]
	mov		rcx, [g_player + Player.stun_cooldown]
	jmp		.kind
.monster:
	lea		r15, [g_monster + MonsterInstance.statuses]
	mov		rcx, [g_monster + MonsterInstance.stun_cooldown]
	mov		rdi, [rbx + Status.element]
	call	monster_resist					; an immune monster is not affected (the chance was already rolled)
	test	rax, rax
	jz		.done
.kind:
	cmp		qword [rbx + Status.kind], STATUSKIND_STUN
	jne		.dot
	; stun: not while stunned or on cooldown, so nobody loses two turns in a row
	test	rcx, rcx
	jg		.done
	mov		rdi, r15
	mov		esi, STATUS_STUN
	call	status_find
	test	rax, rax
	jnz		.done
	mov		rcx, [r15 + StatusList.count]
	inc		qword [r15 + StatusList.count]
	imul	rcx, rcx, ActiveStatus_size
	lea		rax, [r15 + StatusList.items + rcx]
	mov		rdx, [rbx + Status.turns]
	mov		qword [rax + ActiveStatus.status], STATUS_STUN
	mov		[rax + ActiveStatus.turns], rdx
	mov		qword [rax + ActiveStatus.per_turn], 0
	EMIT	EV_STATUS_APPLIED, r12, STATUS_STUN, rdx, 0
	jmp		.done
.dot:
	mov		rdi, r15
	mov		rsi, r13
	call	status_find
	mov		rdx, [rbx + Status.turns]
	test	rax, rax
	jnz		.refresh
	mov		rcx, [r15 + StatusList.count]
	inc		qword [r15 + StatusList.count]
	imul	rcx, rcx, ActiveStatus_size
	lea		rax, [r15 + StatusList.items + rcx]
	mov		[rax + ActiveStatus.status], r13
	mov		[rax + ActiveStatus.turns], rdx
	mov		[rax + ActiveStatus.per_turn], r14
	jmp		.emit
.refresh:
	; re-applying refreshes the duration and keeps the higher damage per turn
	mov		[rax + ActiveStatus.turns], rdx
	cmp		r14, [rax + ActiveStatus.per_turn]
	jle		.emit
	mov		[rax + ActiveStatus.per_turn], r14
.emit:
	mov		rsi, [rax + ActiveStatus.per_turn]
	EMIT	EV_STATUS_APPLIED, r12, r13, rdx, rsi
.done:
	LEAVE_FRAME

; status_damage(rdi = TARGET_*, rsi = damage per turn, rdx = element) → rax = damage of one tick:
; reduced by the player's protection (minimum 1) or by the monster's resistance (0 when immune, else minimum 1).
status_damage:
	ENTER_FRAME
	mov		rbx, rsi
	mov		r12, rdx
	cmp		rdi, TARGET_PLAYER
	jne		.monster
	call	build_sheet
	mov		esi, 100
	sub		rsi, [g_sheet + Sheet.prot + r12 * 8]
	jmp		.reduce
.monster:
	mov		rdi, r12
	call	monster_resist
	test	rax, rax
	jz		.done
	mov		rsi, rax
.reduce:
	mov		rdi, rbx
	call	pct
	AT_LEAST_ONE rax
.done:
	LEAVE_FRAME

; tick(rdi = TARGET_*) — every damage-over-time status of the target deals its damage and loses one turn;
; statuses that reach 0 turns expire.
; Frame: rbx = current ActiveStatus, r12 = target, r13 = index in the list, r14 = damage, then the expired status,
; r15 = StatusList.
tick:
	ENTER_FRAME
	mov		r12, rdi
	lea		r15, [g_player + Player.statuses]
	lea		rax, [g_monster + MonsterInstance.statuses]
	cmp		r12, TARGET_PLAYER
	cmovne	r15, rax
	xor		r13d, r13d
.next:
	cmp		r13, [r15 + StatusList.count]
	jae		.done
	imul	rax, r13, ActiveStatus_size
	lea		rbx, [r15 + StatusList.items + rax]
	mov		rax, [rbx + ActiveStatus.status]
	ROW		rax, statuses, rax, Status_size
	cmp		qword [rax + Status.kind], STATUSKIND_DOT
	jne		.advance
	mov		rdi, r12
	mov		rsi, [rbx + ActiveStatus.per_turn]
	mov		rdx, [rax + Status.element]
	call	status_damage
	mov		r14, rax
	cmp		r12, TARGET_PLAYER
	jne		.monster
	HURT_PLAYER r14
	jmp		.emit
.monster:
	mov		rdi, r14
	call	hit_monster
.emit:
	mov		rsi, [rbx + ActiveStatus.status]
	EMIT	EV_STATUS_TICKED, r12, rsi, r14
	dec		qword [rbx + ActiveStatus.turns]
	jg		.advance
	; expired: removing it shifts the next status into this index, so the index does not advance
	mov		r14, [rbx + ActiveStatus.status]
	mov		rdi, r15
	mov		rsi, r13
	call	status_remove
	EMIT	EV_STATUS_EXPIRED, r12, r14
	jmp		.next
.advance:
	inc		r13
	jmp		.next
.done:
	LEAVE_FRAME

; ── the turn ──────────────────────────────────────────────────────────────────

; battle_play_turn(rdi = CMD_ATTACK / CMD_CAST / CMD_POTION / CMD_DEFEND, rsi = argument) → rax = OUTCOME_*.
; The command must have passed battle_validate.
battle_play_turn:
	ENTER_FRAME
	cmp		rdi, CMD_ATTACK
	je		.attack
	cmp		rdi, CMD_CAST
	je		.cast
	cmp		rdi, CMD_POTION
	je		.potion
	mov		qword [g_player + Player.defending], 1
	EMIT	EV_PLAYER_DEFENDED
	jmp		.resolve
.attack:
	call	melee
	jmp		.resolve
.cast:
	mov		rdi, rsi
	call	cast
	jmp		.resolve
.potion:
	mov		rdi, rsi
	call	drink
.resolve:
	call	death_check						; 2. player dead? monster dead?
	cmp		eax, OUTCOME_ONGOING
	jne		.done
	call	monster_phase					; 3.
	call	death_check						; 4.
	cmp		eax, OUTCOME_ONGOING
	jne		.done
	call	end_of_turn						; 5.
	test	eax, eax
	mov		eax, OUTCOME_DEFEAT
	jnz		.done
	lea		rdi, [g_player + Player.statuses]
	call	consume_stun					; 6. a stunned player loses the next turn: the monster acts again
	test	eax, eax
	mov		eax, OUTCOME_ONGOING
	jz		.done
	mov		qword [g_player + Player.stun_cooldown], STUN_COOLDOWN_TURNS
	EMIT	EV_PLAYER_STUNNED
	jmp		.resolve
.done:
	LEAVE_FRAME
