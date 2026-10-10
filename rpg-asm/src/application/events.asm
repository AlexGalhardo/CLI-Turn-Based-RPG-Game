; application/events.asm — the event buffer and the tables that describe events and commands.
;
; Role: `event_new` appends a record to `g_events`; `event_schema` gives, for every event type, its JSON name and
; the name and kind of each field, in the order of docs/cross-language-parity.md §3. The printers (canonical
; replay output, English text) are driven by these tables, so an event is described in exactly one place.
;
; Register use: event_new preserves everything except rax (so the EMIT macro can be used anywhere). The kind_*
; routines are leaves; kind_lookup uses the standard frame because it calls strcmp.
%include "common.inc"

global g_events, g_event_count, event_new, events_clear
global event_schema, command_schema, kind_tables, error_names
global kind_id, kind_name, kind_lookup

section .bss align=16
g_events:		resb Event_size * MAX_EVENTS
g_event_count:	resq 1

section .rodata align=8

; Field names. One label per distinct JSON key.
%macro FIELD_NAME 1
	%defstr FIELD_NAME_TEXT %1
	f_%1: db FIELD_NAME_TEXT, 0
%endmacro
FIELD_NAME seed
FIELD_NAME vocation
FIELD_NAME difficulty
FIELD_NAME round
FIELD_NAME tier
FIELD_NAME cycle
FIELD_NAME monsterId
FIELD_NAME isBoss
FIELD_NAME enemyClass
FIELD_NAME hp
FIELD_NAME mp
FIELD_NAME damage
FIELD_NAME crit
FIELD_NAME element
FIELD_NAME spellId
FIELD_NAME mana
FIELD_NAME amount
FIELD_NAME potionId
FIELD_NAME resource
FIELD_NAME attackId
FIELD_NAME charged
FIELD_NAME reflected
FIELD_NAME target
FIELD_NAME status
FIELD_NAME turns
FIELD_NAME perTurn
FIELD_NAME total
FIELD_NAME level
FIELD_NAME maxHp
FIELD_NAME maxMp
FIELD_NAME magicLevel
FIELD_NAME uid
FIELD_NAME itemId
FIELD_NAME rarity
FIELD_NAME gold
FIELD_NAME slot
FIELD_NAME score
FIELD_NAME won
FIELD_NAME quantity
FIELD_NAME code

; SCHEMA "type_name", field, KIND, field, KIND, ... — one fixed-size EventSchema row.
%macro SCHEMA 1-*
	%push schema
	[section .rodata.str]
	%$name: db %1, 0
	__SECT__
	%$row:
	dq %$name, (%0 - 1) / 2
	%rep (%0 - 1) / 2
		%rotate 1
		dq f_%1
		%rotate 1
		dq %1
	%endrep
	times EventSchema_size - ($ - %$row) db 0
	%pop
%endmacro

event_schema:
	SCHEMA "run_started",		seed, KIND_INT, vocation, KIND_VOCATION, difficulty, KIND_DIFFICULTY
	SCHEMA "round_started",		round, KIND_INT, tier, KIND_INT, cycle, KIND_INT, monsterId, KIND_CREATURE, \
								isBoss, KIND_BOOL, enemyClass, KIND_CLASS, hp, KIND_INT
	SCHEMA "player_attacked",	damage, KIND_INT, crit, KIND_BOOL, element, KIND_ELEMENT
	SCHEMA "spell_cast",		spellId, KIND_SPELL, damage, KIND_INT, crit, KIND_BOOL, element, KIND_ELEMENT, \
								mana, KIND_INT
	SCHEMA "spell_healed",		spellId, KIND_SPELL, amount, KIND_INT, mana, KIND_INT
	SCHEMA "potion_used",		potionId, KIND_POTION, amount, KIND_INT, resource, KIND_RESOURCE
	SCHEMA "player_defended"
	SCHEMA "leeched",			hp, KIND_INT, mp, KIND_INT
	SCHEMA "monster_attacked",	attackId, KIND_STR, damage, KIND_INT, element, KIND_ELEMENT, charged, KIND_BOOL, \
								crit, KIND_BOOL
	SCHEMA "monster_dodged"
	SCHEMA "monster_parried",	reflected, KIND_INT
	SCHEMA "monster_healed",	amount, KIND_INT
	SCHEMA "attack_dodged",		attackId, KIND_STR
	SCHEMA "attack_parried",	attackId, KIND_STR, reflected, KIND_INT
	SCHEMA "boss_telegraph",	attackId, KIND_STR, element, KIND_ELEMENT
	SCHEMA "status_applied",	target, KIND_TARGET, status, KIND_STATUS, turns, KIND_INT, perTurn, KIND_INT
	SCHEMA "status_ticked",		target, KIND_TARGET, status, KIND_STATUS, damage, KIND_INT
	SCHEMA "status_expired",	target, KIND_TARGET, status, KIND_STATUS
	SCHEMA "player_stunned"
	SCHEMA "monster_stunned"
	SCHEMA "regenerated",		hp, KIND_INT, mp, KIND_INT
	SCHEMA "monster_killed",	monsterId, KIND_CREATURE, isBoss, KIND_BOOL, enemyClass, KIND_CLASS
	SCHEMA "xp_gained",			amount, KIND_INT, total, KIND_INT
	SCHEMA "level_up",			level, KIND_INT, maxHp, KIND_INT, maxMp, KIND_INT
	SCHEMA "magic_level_up",	magicLevel, KIND_INT
	SCHEMA "spell_level_up",	spellId, KIND_SPELL, level, KIND_INT
	SCHEMA "gold_looted",		amount, KIND_INT
	SCHEMA "item_dropped",		uid, KIND_INT, itemId, KIND_ITEM, rarity, KIND_RARITY
	SCHEMA "item_auto_sold",	uid, KIND_INT, itemId, KIND_ITEM, gold, KIND_INT
	SCHEMA "item_auto_equipped", uid, KIND_INT, itemId, KIND_ITEM, slot, KIND_SLOT, score, KIND_INT
	SCHEMA "potion_dropped",	potionId, KIND_POTION
	SCHEMA "run_won",			round, KIND_INT
	SCHEMA "run_ended",			won, KIND_BOOL
	SCHEMA "merchant_entered",	round, KIND_INT
	SCHEMA "potion_bought",		potionId, KIND_POTION, quantity, KIND_INT, gold, KIND_INT
	SCHEMA "item_bought",		uid, KIND_INT, itemId, KIND_ITEM, gold, KIND_INT
	SCHEMA "item_sold",			uid, KIND_INT, itemId, KIND_ITEM, gold, KIND_INT
	SCHEMA "item_equipped",		uid, KIND_INT, itemId, KIND_ITEM, slot, KIND_SLOT
	SCHEMA "item_unequipped",	uid, KIND_INT, itemId, KIND_ITEM, slot, KIND_SLOT
	SCHEMA "player_died",		monsterId, KIND_CREATURE, round, KIND_INT
	SCHEMA "error",				code, KIND_ERROR
ASSERT_TABLE event_schema, EV_COUNT, EventSchema_size

; COMMAND "type_name", kind of argument 1, kind of argument 2 — one CommandSchema row per CMD_*.
%macro COMMAND 3
	%push command
	[section .rodata.str]
	%$name: db %1, 0
	__SECT__
	dq %$name, %2, %3
	%pop
%endmacro

command_schema:
	COMMAND "attack",			KIND_NONE, KIND_NONE
	COMMAND "cast",				KIND_SPELL, KIND_NONE
	COMMAND "potion",			KIND_POTION, KIND_NONE
	COMMAND "defend",			KIND_NONE, KIND_NONE
	COMMAND "next_fight",		KIND_NONE, KIND_NONE
	COMMAND "buy_potion",		KIND_POTION, KIND_INT
	COMMAND "sell_item",		KIND_INT, KIND_NONE
	COMMAND "equip",			KIND_INT, KIND_NONE
	COMMAND "unequip",			KIND_SLOT, KIND_NONE
	COMMAND "buy_stock_item",	KIND_INT, KIND_NONE
	COMMAND "end_run",			KIND_NONE, KIND_NONE
	COMMAND "continue_run",		KIND_NONE, KIND_NONE
ASSERT_TABLE command_schema, CMD_COUNT, CommandSchema_size

; ERROR_NAME "code" — a Named row whose display name is the code itself (the text UI translates `error.<code>`).
%macro ERROR_NAME 1
	%push error
	[section .rodata.str]
	%$name: db %1, 0
	__SECT__
	dq %$name, %$name
	%pop
%endmacro

error_names:
	ERROR_NAME "not_enough_mana"
	ERROR_NAME "not_enough_gold"
	ERROR_NAME "no_potion"
	ERROR_NAME "unknown_spell"
	ERROR_NAME "unknown_potion"
	ERROR_NAME "potion_locked"
	ERROR_NAME "invalid_phase"
	ERROR_NAME "invalid_quantity"
	ERROR_NAME "bag_full"
	ERROR_NAME "cannot_equip"
	ERROR_NAME "invalid_item"
	ERROR_NAME "level_too_low"
	ERROR_NAME "unknown_command"
ASSERT_TABLE error_names, ERR_COUNT, Named_size

; kind → (table address, row size, row count). The first four kinds are not tables.
kind_tables:
	dq 0, 0, 0									; KIND_NONE
	dq 0, 0, 0									; KIND_INT
	dq 0, 0, 0									; KIND_BOOL
	dq 0, 0, 0									; KIND_STR
	dq vocations, Vocation_size, VOCATION_COUNT
	dq difficulties, Difficulty_size, DIFFICULTY_COUNT
	dq creatures, Creature_size, CREATURE_COUNT
	dq class_names, Named_size, CLASS_COUNT
	dq element_names, Named_size, ELEMENT_COUNT
	dq spells, Spell_size, SPELL_COUNT
	dq potions, Potion_size, POTION_COUNT
	dq resource_names, Named_size, RESOURCE_COUNT
	dq statuses, Status_size, STATUS_COUNT
	dq target_names, Named_size, TARGET_COUNT
	dq items, Item_size, ITEM_COUNT
	dq rarities, Rarity_size, RARITY_COUNT
	dq slot_names, Named_size, SLOT_COUNT
	dq error_names, Named_size, ERR_COUNT
	dq stat_names, Named_size, STAT_COUNT
	dq phase_names, Named_size, PHASE_COUNT
ASSERT_TABLE kind_tables, KIND_COUNT, 24

s_overflow: db "rpg-asm: event buffer overflow", 10, 0

section .text

; events_clear() — empties the buffer (start of every command).
events_clear:
	mov		qword [g_event_count], 0
	ret

; event_new(eax = EV_* type) → rax = pointer to the new Event, fields zeroed.
; Preserves every other register: see the EMIT macro in events.inc.
event_new:
	push	rcx
	mov		rcx, [g_event_count]
	cmp		rcx, MAX_EVENTS
	jae		.overflow
	inc		qword [g_event_count]
	imul	rcx, rcx, Event_size
	add		rcx, g_events
	mov		eax, eax						; zero-extend the type
	mov		[rcx + Event.type], rax
	xor		eax, eax
	mov		[rcx + Event.f0], rax
	mov		[rcx + Event.f1], rax
	mov		[rcx + Event.f2], rax
	mov		[rcx + Event.f3], rax
	mov		[rcx + Event.f4], rax
	mov		[rcx + Event.f5], rax
	mov		[rcx + Event.f6], rax
	mov		rax, rcx
	pop		rcx
	ret
.overflow:
	and		rsp, -16						; a bug, not a game situation: report and stop
	mov		rdi, [stderr]
	lea		rsi, [s_overflow]
	call	fputs
	call	abort

; kind_row(rdi = KIND_* of a table kind, rsi = row index) → rax = pointer to the row. Destroys rdx.
kind_row:
	imul	rdx, rdi, 24
	mov		rax, rsi
	imul	rax, [kind_tables + rdx + 8]
	add		rax, [kind_tables + rdx]
	ret

; kind_id(rdi = table kind, rsi = row index) → rax = pointer to the id string of that row ("rat").
kind_id:
	call	kind_row
	mov		rax, [rax + Named.id]
	ret

; kind_name(rdi = table kind, rsi = row index) → rax = pointer to the English display name of that row ("Rat").
kind_name:
	call	kind_row
	mov		rax, [rax + Named.name]
	ret

; kind_lookup(rdi = table kind, rsi = id string) → rax = row index of that id, or -1 when the table has no such
; id. The linear scan with strcmp is the port's only "dictionary": it runs when a command is parsed, never
; inside the rules.
kind_lookup:
	ENTER_FRAME
	imul	rax, rdi, 24
	mov		rbx, [kind_tables + rax]		; rbx = current row
	mov		r12, [kind_tables + rax + 8]	; r12 = row size
	mov		r13, [kind_tables + rax + 16]	; r13 = row count
	mov		r14, rsi						; r14 = wanted id
	xor		r15d, r15d						; r15 = index
.next:
	cmp		r15, r13
	jae		.missing
	mov		rdi, [rbx + Named.id]
	mov		rsi, r14
	call	strcmp
	test	eax, eax
	jz		.found
	add		rbx, r12
	inc		r15
	jmp		.next
.missing:
	mov		r15, -1
.found:
	mov		rax, r15
	LEAVE_FRAME
