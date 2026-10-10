; application/engine.asm — the game engine: a state machine `step(command) → events` (application/engine.py;
; docs/architecture.md). No clock, no I/O: same seed + same commands ⇒ same events.
;
; Role: `engine_new_run` creates the run and enters the merchant; `engine_step` empties the event buffer, runs one
; command according to the current phase, and feeds the events to the statistics. The caller reads the events
; from `g_events` / `g_event_count` (application/events.asm).
;
; Register use: every routine uses the standard frame (common.inc).
%include "common.inc"

extern g_run, g_player, g_monster, g_stats, g_events, g_event_count
extern rng_seed, rng_roll, rng_chance, round_info
extern events_clear, event_new, stats_reset, stats_record
extern spawn_monster, generate_item, item_value, gain_experience
extern battle_validate, battle_play_turn, merchant_enter, merchant_handle, auto_equip
extern take_item_uid, bag_append, potion_add, clamp_resources

global engine_new_run, engine_step

section .bss align=16
dropped_item:	resb ItemInstance_size	; the drop being generated
unlocked:		resq POTION_COUNT		; potion rows that can drop at this round

section .text

; engine_new_run(rdi = seed, rsi = player name, rdx = vocation row, rcx = difficulty row, r8 = autoEquip 0/1)
; The run starts in the merchant phase at round 0 with the vocation's starter weapon equipped. The events
; (run_started, merchant_entered) are left in the buffer; they are not counted by the statistics.
; Frame: rbx = Vocation row, r12 = seed, r13 = name, r14 = vocation row index, r15 = difficulty row.
engine_new_run:
	ENTER_FRAME 16
	mov		r12, rdi
	mov		r13, rsi
	mov		r14, rdx
	mov		r15, rcx
	mov		[rsp], r8

	xor		eax, eax
	lea		rdi, [g_run]
	mov		ecx, RunState_size / 8
	rep stosq
	lea		rdi, [g_player]
	mov		ecx, Player_size / 8
	rep stosq
	lea		rdi, [g_monster]
	mov		ecx, MonsterInstance_size / 8
	rep stosq
	call	stats_reset

	lea		rdi, [g_player + Player.name]
	mov		rsi, r13
	mov		edx, NAME_MAX - 1				; the buffer was zeroed, so the name stays NUL-terminated
	call	strncpy

	ROW		rbx, vocations, r14, Vocation_size
	mov		[g_player + Player.vocation], r14
	mov		rax, [rbx + Vocation.start_hp]
	mov		[g_player + Player.hp], rax
	mov		rax, [rbx + Vocation.start_mp]
	mov		[g_player + Player.mp], rax
	mov		qword [g_player + Player.gold], BAL_STARTING_GOLD
	mov		qword [g_player + Player.level], 1
	mov		qword [g_player + Player.magic_level], 1

	xor		r13d, r13d						; the name is copied: r13 = starting potion index
.potion:
	cmp		r13, STARTING_POTION_COUNT
	jae		.potions_done
	mov		rax, r13
	shl		rax, 4							; (potion row, quantity) pairs of 16 bytes
	mov		rdi, [starting_potions + rax]
	mov		rsi, [starting_potions + rax + 8]
	call	potion_add
	inc		r13
	jmp		.potion
.potions_done:

	mov		[g_run + RunState.seed], r12
	mov		[g_run + RunState.difficulty], r15
	mov		rax, [rsp]
	mov		[g_run + RunState.auto_equip], rax
	mov		qword [g_run + RunState.phase], PHASE_MERCHANT
	mov		qword [g_run + RunState.next_item_uid], 1
	mov		qword [g_run + RunState.death_cause], -1

	; starter weapon: common rarity, no affixes, uid 1
	mov		rcx, [rbx + Vocation.starter_weapon]
	ROW		rdx, items, rcx, Item_size
	imul	rsi, [rdx + Item.slot], ItemInstance_size
	lea		rax, [g_player + Player.equipment]
	add		rsi, rax
	mov		[rsi + ItemInstance.item], rcx
	mov		qword [rsi + ItemInstance.rarity], RARITY_COMMON
	mov		rax, [rdx + Item.tier]
	mov		[rsi + ItemInstance.tier], rax
	call	take_item_uid					; destroys only rax
	mov		[rsi + ItemInstance.uid], rax

	mov		rdi, r12
	call	rng_seed
	call	events_clear
	EMIT	EV_RUN_STARTED, r12, r14, r15
	call	merchant_enter
	LEAVE_FRAME

; engine_step(rdi = CMD_*, rsi = first argument, rdx = second argument)
; A command that is not valid in the current phase emits `error: invalid_phase` and changes nothing.
; Frame: r12 = command, r13 / r14 = arguments.
engine_step:
	ENTER_FRAME
	mov		r12, rdi
	mov		r13, rsi
	mov		r14, rdx
	call	events_clear
	mov		rax, [g_run + RunState.phase]
	cmp		r12, CMD_DEFEND
	jbe		.battle							; attack, cast, potion, defend
	cmp		r12, CMD_NEXT_FIGHT
	je		.next_fight
	cmp		r12, CMD_BUY_STOCK_ITEM
	jbe		.merchant						; buy_potion, sell_item, equip, unequip, buy_stock_item
	cmp		rax, PHASE_VICTORY				; end_run, continue_run
	jne		.invalid_phase
	cmp		r12, CMD_END_RUN
	je		.end_run
	; continue_run: endless mode. Entering the merchant is the only randomness it consumes.
	mov		qword [g_run + RunState.phase], PHASE_MERCHANT
	call	merchant_enter
	jmp		.record
.end_run:
	mov		qword [g_run + RunState.phase], PHASE_GAME_OVER
	mov		qword [g_run + RunState.death_cause], -1
	mov		rsi, [g_run + RunState.won]
	EMIT	EV_RUN_ENDED, rsi
	jmp		.record

.battle:
	cmp		rax, PHASE_BATTLE
	jne		.invalid_phase
	mov		rdi, r12
	mov		rsi, r13
	call	battle_validate
	cmp		rax, 0
	jl		.turn
	mov		rsi, rax						; EMIT destroys rax
	EMIT	EV_ERROR, rsi
	jmp		.record
.turn:
	mov		rdi, r12
	mov		rsi, r13
	call	battle_play_turn
	cmp		eax, OUTCOME_VICTORY
	jne		.not_victory
	call	victory
	jmp		.record
.not_victory:
	cmp		eax, OUTCOME_DEFEAT
	jne		.record
	; defeat: death is permanent
	mov		qword [g_run + RunState.phase], PHASE_GAME_OVER
	mov		rsi, [g_monster + MonsterInstance.creature]
	mov		[g_run + RunState.death_cause], rsi
	mov		rdx, [g_run + RunState.round]
	EMIT	EV_PLAYER_DIED, rsi, rdx
	jmp		.record

.next_fight:
	cmp		rax, PHASE_MERCHANT
	jne		.invalid_phase
	; leaving the merchant: the round counter advances and the monster is spawned
	inc		qword [g_run + RunState.round]
	mov		rdi, [g_run + RunState.round]
	call	spawn_monster					; rax = tier, rdx = cycle
	mov		r8, rax
	mov		r9, rdx
	mov		qword [g_run + RunState.phase], PHASE_BATTLE
	mov		qword [g_run + RunState.turn], 1
	mov		qword [g_run + RunState.stock_count], 0
	mov		rsi, [g_run + RunState.round]
	mov		r10, [g_monster + MonsterInstance.creature]
	mov		r11, [g_monster + MonsterInstance.is_boss]
	mov		rcx, [g_monster + MonsterInstance.enemy_class]
	mov		rdi, [g_monster + MonsterInstance.hp]
	EMIT	EV_ROUND_STARTED, rsi, r8, r9, r10, r11, rcx, rdi
	jmp		.record

.merchant:
	cmp		rax, PHASE_MERCHANT
	jne		.invalid_phase
	mov		rdi, r12
	mov		rsi, r13
	mov		rdx, r14
	call	merchant_handle
	jmp		.record

.invalid_phase:
	EMIT	EV_ERROR, ERR_INVALID_PHASE
.record:
	mov		rdi, [g_run + RunState.round]
	call	stats_record
	LEAVE_FRAME

; victory() — docs/game-design.md §8, in this order: kill event and XP, gold, drops, auto-equip, clean-up, then
; the merchant (or the victory phase after the final boss).
victory:
	ENTER_FRAME
	mov		rsi, [g_monster + MonsterInstance.creature]
	mov		rdx, [g_monster + MonsterInstance.is_boss]
	mov		rcx, [g_monster + MonsterInstance.enemy_class]
	EMIT	EV_MONSTER_KILLED, rsi, rdx, rcx
	mov		rdi, [g_monster + MonsterInstance.xp]
	call	gain_experience

	mov		rdi, [g_monster + MonsterInstance.gold_min]
	mov		rsi, [g_monster + MonsterInstance.gold_max]
	call	rng_roll
	add		[g_player + Player.gold], rax
	mov		rsi, rax
	EMIT	EV_GOLD_LOOTED, rsi

	call	drops
	cmp		qword [g_run + RunState.auto_equip], 0
	je		.cleanup
	call	auto_equip
.cleanup:
	mov		qword [g_player + Player.statuses + StatusList.count], 0
	mov		qword [g_player + Player.stun_cooldown], 0
	mov		qword [g_player + Player.defending], 0
	call	clamp_resources					; a level-up or new equipment may have changed the maximum
	mov		qword [g_run + RunState.has_monster], 0
	mov		qword [g_run + RunState.turn], 0
	cmp		qword [g_run + RunState.round], BAL_FINAL_ROUND
	jne		.merchant
	; the final boss of the first cycle: the run is won; no merchant, no stock, no randomness
	mov		qword [g_run + RunState.won], 1
	mov		qword [g_run + RunState.phase], PHASE_VICTORY
	mov		rsi, [g_run + RunState.round]
	EMIT	EV_RUN_WON, rsi
	jmp		.done
.merchant:
	mov		qword [g_run + RunState.phase], PHASE_MERCHANT
	call	merchant_enter
.done:
	LEAVE_FRAME

; drops() — one rule for the three enemy classes; chance(100) and chance(0) consume nothing, so a normal monster
; rolls its drop chance and an elite or a boss does not.
; Frame: rbx = EnemyClass row, r12 = items left to drop.
drops:
	ENTER_FRAME
	mov		rbx, [g_monster + MonsterInstance.enemy_class]
	ROW		rbx, enemy_classes, rbx, EnemyClass_size
	mov		rdi, [rbx + EnemyClass.drop_chance_pct]
	call	rng_chance
	test	eax, eax
	jz		.potion
	mov		r12, [rbx + EnemyClass.drops]
.item:
	test	r12, r12
	jz		.potion
	mov		rdi, rbx
	call	drop_item
	dec		r12
	jmp		.item
.potion:
	mov		rdi, [rbx + EnemyClass.potion_drop_pct]
	call	rng_chance
	test	eax, eax
	jz		.done
	call	drop_potion
.done:
	LEAVE_FRAME

; drop_item(rdi = EnemyClass row) — generates one item for the round tier with the class rarity table. A full
; bag sells the new item on the spot.
drop_item:
	ENTER_FRAME
	mov		rbx, rdi
	mov		rdi, [g_run + RunState.round]
	call	round_info						; rax = tier
	mov		rsi, rax
	mov		rdi, [g_player + Player.vocation]
	ROW		rdi, vocations, rdi, Vocation_size
	lea		rdx, [rbx + EnemyClass.rarity_weights]
	mov		rcx, [g_run + RunState.next_item_uid]
	lea		r8, [dropped_item]
	call	generate_item
	test	eax, eax
	jz		.done							; no candidate item: no drop, no uid taken
	call	take_item_uid
	mov		r12, [dropped_item + ItemInstance.uid]
	mov		r13, [dropped_item + ItemInstance.item]
	mov		rdx, [dropped_item + ItemInstance.rarity]
	EMIT	EV_ITEM_DROPPED, r12, r13, rdx
	cmp		qword [g_player + Player.bag_count], BAL_BAG_CAPACITY
	jl		.keep
	lea		rdi, [dropped_item]
	call	item_value
	add		[g_player + Player.gold], rax
	mov		rsi, rax
	EMIT	EV_ITEM_AUTO_SOLD, r12, r13, rsi
	jmp		.done
.keep:
	lea		rdi, [dropped_item]
	call	bag_append
.done:
	LEAVE_FRAME

; drop_potion() — one potion picked among those unlocked at this round (file order). With no unlocked potion
; nothing drops and nothing is rolled.
drop_potion:
	ENTER_FRAME
	xor		ecx, ecx						; rcx = potion row
	xor		r8d, r8d						; r8 = number of unlocked potions
	lea		rdx, [potions]
	mov		rax, [g_run + RunState.round]
.collect:
	cmp		[rdx + Potion.unlock_round], rax
	jg		.locked
	mov		[unlocked + r8 * 8], rcx
	inc		r8
.locked:
	add		rdx, Potion_size
	inc		rcx
	cmp		rcx, POTION_COUNT
	jb		.collect
	test	r8, r8
	jz		.done
	xor		edi, edi
	lea		rsi, [r8 - 1]
	call	rng_roll
	mov		rbx, [unlocked + rax * 8]
	mov		rdi, rbx
	mov		esi, 1
	call	potion_add
	EMIT	EV_POTION_DROPPED, rbx
.done:
	LEAVE_FRAME
