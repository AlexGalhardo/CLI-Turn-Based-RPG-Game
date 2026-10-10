; application/spawner.asm — spawns the monster of a round: tier, cycle, position, enemy class and difficulty
; scaling (application/spawner.py; docs/game-design.md §3).
;
; Role: picks the creature (the tier boss on the last position, otherwise a random monster of the tier that may
; be an elite) and writes `g_monster` with the HP, damage and rewards already scaled.
;
; Register use: scale_stat and scale_reward are leaves that destroy rax, rcx, rdx. spawn_monster uses the standard
; frame with 48 bytes of locals.
%include "common.inc"

extern g_run, g_monster, round_info, rng_roll, rng_chance

global spawn_monster

section .text

; scale_stat(rdi = value, rsi = product of three percentages, r8 = class statPct)
; → rax = max(1, pct(floor(value × product / 1 000 000), statPct))
scale_stat:
	mov		rax, rdi
	imul	rax, rsi
	xor		edx, edx
	mov		ecx, 1000000
	div		rcx								; three chained percentages floored once
	imul	rax, r8
	xor		edx, edx
	mov		ecx, 100
	div		rcx
	mov		ecx, 1
	test	rax, rax
	cmovz	rax, rcx
	ret

; scale_reward(rdi = value, rsi = product of two percentages, r8 = class rewardPct)
; → rax = pct(floor(value × product / 10 000), rewardPct)
scale_reward:
	mov		rax, rdi
	imul	rax, rsi
	xor		edx, edx
	mov		ecx, 10000
	div		rcx
	imul	rax, r8
	xor		edx, edx
	mov		ecx, 100
	div		rcx
	ret

; spawn_monster(rdi = round) → rax = tier, rdx = cycle. Writes g_monster and sets g_run.has_monster.
;
; Frame: rbx = Creature row, r12 = position in the tier, r13 = CLASS_*, r14 = EnemyClass row, r15 = attack index.
; Locals: [rsp] tier, [rsp+8] cycle, [rsp+16] HP product, [rsp+24] damage product, [rsp+32] XP product,
; [rsp+40] gold product.
spawn_monster:
	ENTER_FRAME 48
	call	round_info						; rax = tier, rdx = cycle, rcx = position, r8 = boss fight?
	mov		[rsp], rax
	mov		[rsp + 8], rdx
	mov		r12, rcx
	test	r8, r8
	jz		.regular
	mov		rbx, [tier_boss + rax * 8]
	mov		r13d, CLASS_BOSS				; bosses roll nothing
	jmp		.picked
.regular:
	mov		rbx, [tier_monster_start + rax * 8]
	mov		rsi, [tier_monster_count + rax * 8]
	dec		rsi
	xor		edi, edi
	call	rng_roll						; pick(monsters of the tier, sorted by id)
	add		rbx, rax
	mov		rbx, [tier_monsters + rbx * 8]
	mov		edi, BAL_ELITE_CHANCE_PCT
	call	rng_chance
	mov		r13, rax						; CLASS_NORMAL = 0, CLASS_ELITE = 1: the flag is the class
%if CLASS_NORMAL != 0 || CLASS_ELITE != 1
	%error "spawn_monster relies on CLASS_NORMAL = 0 and CLASS_ELITE = 1"
%endif
.picked:
	ROW		r14, enemy_classes, r13, EnemyClass_size

	lea		rdi, [g_monster]
	mov		ecx, MonsterInstance_size / 8
	xor		eax, eax
	rep stosq								; no statuses, no cooldown, bossActions = 0
	mov		[g_monster + MonsterInstance.creature], rbx
	mov		[g_monster + MonsterInstance.enemy_class], r13
	mov		qword [g_run + RunState.has_monster], 1
	ROW		rbx, creatures, rbx, Creature_size
	mov		rax, [rbx + Creature.is_boss]
	mov		[g_monster + MonsterInstance.is_boss], rax

	; Scaling percentages (docs/game-design.md §3). Bosses ignore the position bonus.
	mov		rcx, [rsp + 8]
	imul	r9, rcx, BAL_CYCLE_STAT_PCT
	add		r9, 100							; r9 = cyclePct
	imul	r10, rcx, BAL_CYCLE_REWARD_PCT
	add		r10, 100						; r10 = cycleRewardPct
	mov		r11d, 100						; r11 = positionPct
	cmp		r13, CLASS_BOSS
	je		.position_done
	imul	r11, r12, BAL_POSITION_PCT
	add		r11, 100
.position_done:
	mov		rsi, [g_run + RunState.difficulty]
	ROW		rsi, difficulties, rsi, Difficulty_size
	imul	r9, r11							; cyclePct × positionPct
	mov		rax, [rsi + Difficulty.hp_pct]
	imul	rax, r9
	mov		[rsp + 16], rax
	mov		rax, [rsi + Difficulty.damage_pct]
	imul	rax, r9
	mov		[rsp + 24], rax
	mov		rax, [rsi + Difficulty.xp_pct]
	imul	rax, r10
	mov		[rsp + 32], rax
	mov		rax, [rsi + Difficulty.gold_pct]
	imul	rax, r10
	mov		[rsp + 40], rax

	mov		rdi, [rbx + Creature.hp]
	mov		rsi, [rsp + 16]
	mov		r8, [r14 + EnemyClass.stat_pct]
	call	scale_stat
	mov		[g_monster + MonsterInstance.hp], rax
	mov		[g_monster + MonsterInstance.max_hp], rax

	mov		r8, [r14 + EnemyClass.reward_pct]
	mov		rdi, [rbx + Creature.xp]
	mov		rsi, [rsp + 32]
	call	scale_reward
	mov		[g_monster + MonsterInstance.xp], rax
	mov		rdi, [rbx + Creature.gold_min]
	mov		rsi, [rsp + 40]
	call	scale_reward
	mov		[g_monster + MonsterInstance.gold_min], rax
	mov		rdi, [rbx + Creature.gold_max]
	mov		rsi, [rsp + 40]
	call	scale_reward
	mov		[g_monster + MonsterInstance.gold_max], rax

	; every attack's damage range, scaled like the HP but with the difficulty's damagePct
	mov		r8, [r14 + EnemyClass.stat_pct]
	mov		r12, [rbx + Creature.attacks]	; r12 = current Attack row
	xor		r15d, r15d
.attack:
	cmp		r15, [rbx + Creature.attack_count]
	jae		.done
	mov		rdi, [r12 + Attack.min]
	mov		rsi, [rsp + 24]
	call	scale_stat
	mov		[g_monster + MonsterInstance.attack_min + r15 * 8], rax
	mov		rdi, [r12 + Attack.max]
	mov		rsi, [rsp + 24]
	call	scale_stat
	mov		[g_monster + MonsterInstance.attack_max + r15 * 8], rax
	add		r12, Attack_size
	inc		r15
	jmp		.attack
.done:
	mov		rax, [rsp]
	mov		rdx, [rsp + 8]
	LEAVE_FRAME
