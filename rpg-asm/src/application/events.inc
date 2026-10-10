; application/events.inc — engine events (docs/cross-language-parity.md §3; application/events.py in the reference).
;
; Role: an event is a flat record: its type and up to seven 64-bit fields, in the field order of the parity
; document. The engine appends events to one static buffer (application/events.asm) that is emptied at the start
; of every command; whoever called `engine_step` then reads `g_event_count` records from `g_events`.
;
; A field holds a number, a 0/1 boolean, a row index of some table, or (attack ids only) a string pointer. The
; schema in events.asm says which, so the printers can turn rows back into ids ("monsterId=rat") and names ("Rat").
%ifndef EVENTS_INC
%define EVENTS_INC

struc Event
	.type		resq 1	; EV_*
	.f0			resq 1
	.f1			resq 1
	.f2			resq 1
	.f3			resq 1
	.f4			resq 1
	.f5			resq 1
	.f6			resq 1
endstruc
EVENT_MAX_FIELDS	equ 7
MAX_EVENTS			equ 1024	; per command; a turn emits a few dozen at most

; Event types, in the order of the parity document.          fields
EV_RUN_STARTED			equ 0	; seed, vocation, difficulty
EV_ROUND_STARTED		equ 1	; round, tier, cycle, monsterId, isBoss, enemyClass, hp
EV_PLAYER_ATTACKED		equ 2	; damage, crit, element
EV_SPELL_CAST			equ 3	; spellId, damage, crit, element, mana
EV_SPELL_HEALED			equ 4	; spellId, amount, mana
EV_POTION_USED			equ 5	; potionId, amount, resource
EV_PLAYER_DEFENDED		equ 6
EV_LEECHED				equ 7	; hp, mp
EV_MONSTER_ATTACKED		equ 8	; attackId, damage, element, charged, crit
EV_MONSTER_DODGED		equ 9
EV_MONSTER_PARRIED		equ 10	; reflected
EV_MONSTER_HEALED		equ 11	; amount
EV_ATTACK_DODGED		equ 12	; attackId
EV_ATTACK_PARRIED		equ 13	; attackId, reflected
EV_BOSS_TELEGRAPH		equ 14	; attackId, element
EV_STATUS_APPLIED		equ 15	; target, status, turns, perTurn
EV_STATUS_TICKED		equ 16	; target, status, damage
EV_STATUS_EXPIRED		equ 17	; target, status
EV_PLAYER_STUNNED		equ 18
EV_MONSTER_STUNNED		equ 19
EV_REGENERATED			equ 20	; hp, mp
EV_MONSTER_KILLED		equ 21	; monsterId, isBoss, enemyClass
EV_XP_GAINED			equ 22	; amount, total
EV_LEVEL_UP				equ 23	; level, maxHp, maxMp
EV_MAGIC_LEVEL_UP		equ 24	; magicLevel
EV_SPELL_LEVEL_UP		equ 25	; spellId, level
EV_GOLD_LOOTED			equ 26	; amount
EV_ITEM_DROPPED			equ 27	; uid, itemId, rarity
EV_ITEM_AUTO_SOLD		equ 28	; uid, itemId, gold
EV_ITEM_AUTO_EQUIPPED	equ 29	; uid, itemId, slot, score
EV_POTION_DROPPED		equ 30	; potionId
EV_RUN_WON				equ 31	; round
EV_RUN_ENDED			equ 32	; won
EV_MERCHANT_ENTERED		equ 33	; round
EV_POTION_BOUGHT		equ 34	; potionId, quantity, gold
EV_ITEM_BOUGHT			equ 35	; uid, itemId, gold
EV_ITEM_SOLD			equ 36	; uid, itemId, gold
EV_ITEM_EQUIPPED		equ 37	; uid, itemId, slot
EV_ITEM_UNEQUIPPED		equ 38	; uid, itemId, slot
EV_PLAYER_DIED			equ 39	; monsterId, round
EV_ERROR				equ 40	; code
EV_COUNT				equ 41

; Error codes (the `code` field of EV_ERROR), rows of `error_names` in events.asm.
ERR_NOT_ENOUGH_MANA		equ 0
ERR_NOT_ENOUGH_GOLD		equ 1
ERR_NO_POTION			equ 2
ERR_UNKNOWN_SPELL		equ 3
ERR_UNKNOWN_POTION		equ 4
ERR_POTION_LOCKED		equ 5
ERR_INVALID_PHASE		equ 6
ERR_INVALID_QUANTITY	equ 7
ERR_BAG_FULL			equ 8
ERR_CANNOT_EQUIP		equ 9
ERR_INVALID_ITEM		equ 10
ERR_LEVEL_TOO_LOW		equ 11
ERR_UNKNOWN_COMMAND		equ 12	; never emitted by the engine; used by the text UI
ERR_COUNT				equ 13

; Field kinds. From KIND_VOCATION on, a kind is a table of rows that start with (id, name) pointers and the field
; holds a row index; `kind_tables` in events.asm maps a kind to (table address, row size).
KIND_NONE			equ 0
KIND_INT			equ 1
KIND_BOOL			equ 2
KIND_STR			equ 3	; pointer to a NUL-terminated string
KIND_VOCATION		equ 4
KIND_DIFFICULTY		equ 5
KIND_CREATURE		equ 6
KIND_CLASS			equ 7
KIND_ELEMENT		equ 8
KIND_SPELL			equ 9
KIND_POTION			equ 10
KIND_RESOURCE		equ 11
KIND_STATUS			equ 12
KIND_TARGET			equ 13
KIND_ITEM			equ 14
KIND_RARITY			equ 15
KIND_SLOT			equ 16
KIND_ERROR			equ 17
KIND_STAT			equ 18
KIND_PHASE			equ 19
KIND_COUNT			equ 20

; One row of `event_schema`: the type name, the number of fields, then (field name, kind) for each field.
struc EventSchema
	.name		resq 1
	.field_count resq 1
	.fields		resq 2 * EVENT_MAX_FIELDS
endstruc

; EMIT type, field0, field1, ... — appends one event. The fields must be registers (not rax) or constants.
; Destroys rax only (`event_new` preserves every other register), so it can be dropped in the middle of a routine.
%macro EMIT 1-8
	mov		eax, %1
	call	event_new
	%assign EMIT_OFFSET Event.f0
	%rep %0 - 1
		%rotate 1
		mov		qword [rax + EMIT_OFFSET], %1
		%assign EMIT_OFFSET EMIT_OFFSET + 8
	%endrep
%endmacro

%endif
