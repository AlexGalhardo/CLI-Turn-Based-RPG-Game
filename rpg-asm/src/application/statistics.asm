; application/statistics.asm — run counters derived only from engine events (application/statistics.py;
; docs/game-design.md §11).
;
; Role: after every command the engine hands the events it produced to `stats_record`, which updates `g_stats`.
; The dispatch is a jump table indexed by the event type: `jmp [table + type*8]` is how a `match`/`switch` over
; small consecutive integers is done in assembly.
;
; Register use: stats_record is a leaf without a frame. rbx is not touched; it destroys rax, rcx, rdx, rsi, rdi,
; r8, r9.
%include "common.inc"

extern g_stats, g_events, g_event_count

global stats_record, stats_reset

section .rodata align=8

; One entry per EV_* constant; events that count nothing go to `.next`.
jump_table:
	dq stats_record.next			; run_started
	dq stats_record.next			; round_started
	dq stats_record.player_attacked
	dq stats_record.spell_cast
	dq stats_record.spell_healed
	dq stats_record.potion_used
	dq stats_record.player_defended
	dq stats_record.next			; leeched
	dq stats_record.monster_attacked
	dq stats_record.next			; monster_dodged
	dq stats_record.monster_parried
	dq stats_record.next			; monster_healed
	dq stats_record.attack_dodged
	dq stats_record.attack_parried
	dq stats_record.next			; boss_telegraph
	dq stats_record.status_applied
	dq stats_record.status_ticked
	dq stats_record.next			; status_expired
	dq stats_record.next			; player_stunned
	dq stats_record.next			; monster_stunned
	dq stats_record.next			; regenerated
	dq stats_record.monster_killed
	dq stats_record.next			; xp_gained
	dq stats_record.next			; level_up
	dq stats_record.next			; magic_level_up
	dq stats_record.next			; spell_level_up
	dq stats_record.gold_looted
	dq stats_record.item_dropped
	dq stats_record.item_sold		; item_auto_sold counts as an item sold
	dq stats_record.item_auto_equipped
	dq stats_record.potion_dropped
	dq stats_record.next			; run_won
	dq stats_record.next			; run_ended
	dq stats_record.next			; merchant_entered
	dq stats_record.potion_bought
	dq stats_record.item_bought
	dq stats_record.item_sold
	dq stats_record.next			; item_equipped
	dq stats_record.next			; item_unequipped
	dq stats_record.next			; player_died
	dq stats_record.next			; error
ASSERT_TABLE jump_table, EV_COUNT, 8

section .text

; stats_reset() — zeroes every counter (new run).
stats_reset:
	lea		rdi, [g_stats]
	mov		ecx, Stats_size / 8
	xor		eax, eax
	rep stosq
	ret

; stats_record(rdi = current round) — counts the events of the buffer.
; In the loop: rsi = current event, r8 = events left, r9 = round, rdx = g_stats.
stats_record:
	mov		r9, rdi
	lea		rsi, [g_events]
	mov		r8, [g_event_count]
	lea		rdx, [g_stats]
	jmp		.check
.next:
	add		rsi, Event_size
	dec		r8
.check:
	test	r8, r8
	jz		.done
	mov		rax, [rsi + Event.type]
	jmp		[jump_table + rax * 8]
.done:
	ret

.player_attacked:							; damage, crit, element
	inc		qword [rdx + Stats.normal_attacks]
	mov		rax, [rsi + Event.f0]
	mov		rcx, [rsi + Event.f1]
	jmp		.dealt
.spell_cast:								; spellId, damage, crit, element, mana
	mov		rax, [rsi + Event.f0]
	inc		qword [rdx + Stats.spells_cast + rax * 8]
	mov		rax, [rsi + Event.f1]
	mov		rcx, [rsi + Event.f2]
.dealt:										; rax = damage, rcx = crit flag
	add		[rdx + Stats.damage_dealt], rax
	cmp		rax, [rdx + Stats.highest_hit]
	jle		.dealt_crit
	mov		[rdx + Stats.highest_hit], rax
.dealt_crit:
	add		[rdx + Stats.crits], rcx		; the flag is 0 or 1
	jmp		.next
.spell_healed:								; spellId, amount, mana
	mov		rax, [rsi + Event.f0]
	inc		qword [rdx + Stats.spells_cast + rax * 8]
	mov		rax, [rsi + Event.f1]
	add		[rdx + Stats.healing_done], rax
	jmp		.next
.potion_used:								; potionId, amount, resource
	mov		rax, [rsi + Event.f0]
	inc		qword [rdx + Stats.potions_used + rax * 8]
	cmp		qword [rsi + Event.f2], RESOURCE_HP
	jne		.next
	mov		rax, [rsi + Event.f1]
	add		[rdx + Stats.healing_done], rax
	jmp		.next
.player_defended:
	inc		qword [rdx + Stats.defends]
	jmp		.next
.monster_attacked:							; attackId, damage, ...
	mov		rax, [rsi + Event.f1]
	add		[rdx + Stats.damage_taken], rax
	jmp		.next
.attack_dodged:
	inc		qword [rdx + Stats.dodges]
	jmp		.next
.attack_parried:							; attackId, reflected (damage taken by the monster)
	inc		qword [rdx + Stats.parries]
	mov		rax, [rsi + Event.f1]
	add		[rdx + Stats.damage_dealt], rax
	jmp		.next
.monster_parried:							; reflected (damage taken by the player)
	mov		rax, [rsi + Event.f0]
	add		[rdx + Stats.damage_taken], rax
	jmp		.next
.status_ticked:								; target, status, damage
	mov		rax, [rsi + Event.f2]
	cmp		qword [rsi + Event.f0], TARGET_PLAYER
	jne		.ticked_monster
	add		[rdx + Stats.damage_taken], rax
	jmp		.next
.ticked_monster:
	add		[rdx + Stats.damage_dealt], rax
	jmp		.next
.status_applied:							; target, status, ...
	cmp		qword [rsi + Event.f0], TARGET_MONSTER
	jne		.next
	mov		rax, [rsi + Event.f1]
	inc		qword [rdx + Stats.statuses_applied + rax * 8]
	jmp		.next
.monster_killed:							; monsterId, isBoss, enemyClass
	mov		rax, [rsi + Event.f0]
	inc		qword [rdx + Stats.kills + rax * 8]
	mov		rax, [rsi + Event.f1]
	add		[rdx + Stats.bosses_killed], rax
	cmp		qword [rsi + Event.f2], CLASS_ELITE
	jne		.next
	inc		qword [rdx + Stats.elites_killed]
	jmp		.next
.gold_looted:								; amount
	mov		rax, [rsi + Event.f0]
	add		[rdx + Stats.gold_looted], rax
	jmp		.next
.item_dropped:								; uid, itemId, rarity
	mov		rcx, [rsi + Event.f2]
	inc		qword [rdx + Stats.items_dropped + rcx * 8]
	mov		rax, [rdx + Stats.dropped_count]
	cmp		rax, MAX_DROPPED_ITEMS
	jae		.next
	inc		qword [rdx + Stats.dropped_count]
	imul	rax, rax, DroppedItem_size
	lea		rax, [rdx + Stats.dropped + rax]
	mov		rdi, [rsi + Event.f1]
	mov		[rax + DroppedItem.item], rdi
	mov		[rax + DroppedItem.rarity], rcx
	mov		[rax + DroppedItem.round], r9
	jmp		.next
.potion_bought:								; potionId, quantity, gold
	mov		rax, [rsi + Event.f0]
	mov		rcx, [rsi + Event.f1]
	add		[rdx + Stats.potions_bought + rax * 8], rcx
	mov		rax, [rsi + Event.f2]
	add		[rdx + Stats.gold_spent], rax
	jmp		.next
.item_bought:								; uid, itemId, gold
	mov		rax, [rsi + Event.f2]
	add		[rdx + Stats.gold_spent], rax
	jmp		.next
.potion_dropped:							; potionId
	mov		rax, [rsi + Event.f0]
	inc		qword [rdx + Stats.potions_dropped + rax * 8]
	jmp		.next
.item_auto_equipped:
	inc		qword [rdx + Stats.items_auto_equipped]
	jmp		.next
.item_sold:									; uid, itemId, gold (item_sold and item_auto_sold)
	inc		qword [rdx + Stats.items_sold]
	mov		rax, [rsi + Event.f2]
	add		[rdx + Stats.gold_earned], rax
	jmp		.next
