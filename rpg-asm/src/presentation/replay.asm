; presentation/replay.asm — `rpg-asm --replay <script>`: the golden-file test mode (docs/asm.md).
;
; Role: the other ports read shared/golden/*.json in their test suites. This port has no JSON parser, so
; tools/golden_test.py converts each golden file into a plain-text script and the lines expected from this
; routine. The script is:
;
;     seed 7
;     name Alex
;     vocation warrior
;     difficulty normal
;     autoEquip false
;     begin                       ← creates the run; its events are printed with index 0
;     buy_potion health_potion 1  ← one command per line: the JSON `type`, then its fields in order
;     @bot                        ← optional: from here the bot plays until the run ends
;
; and the output is canonical text that carries every event field and every field of the final state:
;
;     [1] potion_bought potionId=health_potion quantity=1 gold=50     one line per event, [n] = command index
;     > next_fight                                                     a command chosen by the bot (@bot only)
;     state.round=1                                                    finalState of the golden file, flattened
;     run.player.bag.0.itemId=sword                                    finalRun (the whole run state), flattened
;
; Flattening: nested keys are joined with dots, lists get a `.#=<length>` line and numbered entries, booleans are
; true/false and a missing value is `null`. Maps keyed by id print one line per present key, in table order (the
; test tool compares the state lines as a set, the event lines in order).
;
; Register use: every routine uses the standard frame; PRINTF (common.inc) destroys all caller-saved registers.
%include "common.inc"

extern g_run, g_player, g_monster, g_stats, g_events, g_event_count, g_rng_state
extern event_schema, command_schema, kind_tables, kind_id, kind_lookup
extern engine_new_run, engine_step, bot_choose

global replay_run, value_string, print_events

LINE_MAX	equ 512
PATH_MAX	equ 160
MAX_BOT_COMMANDS equ 200000

; FIELD "key", KIND_*, address of the qword — one row of a dump table.
%macro FIELD 3
	%push field
	[section .rodata.str]
	%$key: db %1, 0
	__SECT__
	dq %$key, %2, %3
	%pop
%endmacro
FIELD_SIZE equ 24

section .rodata align=8

state_fields:								; finalState of the golden files (tools/golden.py: final_state)
	FIELD "phase",			KIND_PHASE,	g_run + RunState.phase
	FIELD "round",			KIND_INT,	g_run + RunState.round
	FIELD "turn",			KIND_INT,	g_run + RunState.turn
	FIELD "level",			KIND_INT,	g_player + Player.level
	FIELD "xp",				KIND_INT,	g_player + Player.xp
	FIELD "magicLevel",		KIND_INT,	g_player + Player.magic_level
	FIELD "hp",				KIND_INT,	g_player + Player.hp
	FIELD "mp",				KIND_INT,	g_player + Player.mp
	FIELD "gold",			KIND_INT,	g_player + Player.gold
	FIELD "rngState",		KIND_INT,	rng_state_copy
	FIELD "nextItemUid",	KIND_INT,	g_run + RunState.next_item_uid
STATE_FIELDS equ ($ - state_fields) / FIELD_SIZE

run_fields:									; RunState.to_dict()
	FIELD "seed",			KIND_INT,	g_run + RunState.seed
	FIELD "phase",			KIND_PHASE,	g_run + RunState.phase
	FIELD "round",			KIND_INT,	g_run + RunState.round
	FIELD "turn",			KIND_INT,	g_run + RunState.turn
	FIELD "nextItemUid",	KIND_INT,	g_run + RunState.next_item_uid
	FIELD "won",			KIND_BOOL,	g_run + RunState.won
RUN_FIELDS equ ($ - run_fields) / FIELD_SIZE

config_fields:								; RunConfig.to_dict() (the name is printed apart: it is a string)
	FIELD "vocation",		KIND_VOCATION,	g_player + Player.vocation
	FIELD "difficulty",		KIND_DIFFICULTY, g_run + RunState.difficulty
	FIELD "autoEquip",		KIND_BOOL,	g_run + RunState.auto_equip
CONFIG_FIELDS equ ($ - config_fields) / FIELD_SIZE

player_fields:								; Player.to_dict(), scalar part
	FIELD "vocationId",		KIND_VOCATION,	g_player + Player.vocation
	FIELD "hp",				KIND_INT,	g_player + Player.hp
	FIELD "mp",				KIND_INT,	g_player + Player.mp
	FIELD "gold",			KIND_INT,	g_player + Player.gold
	FIELD "level",			KIND_INT,	g_player + Player.level
	FIELD "xp",				KIND_INT,	g_player + Player.xp
	FIELD "magicLevel",		KIND_INT,	g_player + Player.magic_level
	FIELD "manaSpent",		KIND_INT,	g_player + Player.mana_spent
	FIELD "stunCooldown",	KIND_INT,	g_player + Player.stun_cooldown
	FIELD "defending",		KIND_BOOL,	g_player + Player.defending
PLAYER_FIELDS equ ($ - player_fields) / FIELD_SIZE

monster_fields:								; MonsterInstance.to_dict(), scalar part
	FIELD "creatureId",		KIND_CREATURE,	g_monster + MonsterInstance.creature
	FIELD "isBoss",			KIND_BOOL,	g_monster + MonsterInstance.is_boss
	FIELD "enemyClass",		KIND_CLASS,	g_monster + MonsterInstance.enemy_class
	FIELD "hp",				KIND_INT,	g_monster + MonsterInstance.hp
	FIELD "maxHp",			KIND_INT,	g_monster + MonsterInstance.max_hp
	FIELD "xp",				KIND_INT,	g_monster + MonsterInstance.xp
	FIELD "goldMin",		KIND_INT,	g_monster + MonsterInstance.gold_min
	FIELD "goldMax",		KIND_INT,	g_monster + MonsterInstance.gold_max
	FIELD "stunCooldown",	KIND_INT,	g_monster + MonsterInstance.stun_cooldown
	FIELD "bossActions",	KIND_INT,	g_monster + MonsterInstance.boss_actions
MONSTER_FIELDS equ ($ - monster_fields) / FIELD_SIZE

; The 16 scalar counters of RunStatistics.to_dict(), in the order of the Stats struc.
%macro STAT_NAME 1
	%push stat
	[section .rodata.str]
	%$name: db %1, 0
	__SECT__
	dq %$name
	%pop
%endmacro
stat_scalar_names:
	STAT_NAME "damageDealt"
	STAT_NAME "damageTaken"
	STAT_NAME "healingDone"
	STAT_NAME "highestHit"
	STAT_NAME "normalAttacks"
	STAT_NAME "crits"
	STAT_NAME "dodges"
	STAT_NAME "parries"
	STAT_NAME "defends"
	STAT_NAME "goldLooted"
	STAT_NAME "goldSpent"
	STAT_NAME "goldEarned"
	STAT_NAME "itemsSold"
	STAT_NAME "itemsAutoEquipped"
	STAT_NAME "bossesKilled"
	STAT_NAME "elitesKilled"
ASSERT_TABLE stat_scalar_names, STATS_SCALAR_COUNT, 8

; The counters keyed by id: (JSON key, kind of the id, address of the array).
stat_counters:
	FIELD "spellsCast",		KIND_SPELL,		g_stats + Stats.spells_cast
	FIELD "potionsUsed",	KIND_POTION,	g_stats + Stats.potions_used
	FIELD "potionsBought",	KIND_POTION,	g_stats + Stats.potions_bought
	FIELD "potionsDropped",	KIND_POTION,	g_stats + Stats.potions_dropped
	FIELD "itemsDropped",	KIND_RARITY,	g_stats + Stats.items_dropped
	FIELD "kills",			KIND_CREATURE,	g_stats + Stats.kills
	FIELD "statusesApplied", KIND_STATUS,	g_stats + Stats.statuses_applied
STAT_COUNTERS equ ($ - stat_counters) / FIELD_SIZE

STRING s_true, "true"
STRING s_false, "false"
STRING s_empty, ""
STRING s_read_mode, "r"
STRING s_space, " "
STRING s_key_seed, "seed"
STRING s_key_name, "name"
STRING s_key_vocation, "vocation"
STRING s_key_difficulty, "difficulty"
STRING s_key_auto_equip, "autoEquip"
STRING s_key_begin, "begin"
STRING s_key_bot, "@bot"
STRING s_number, "%ld"
STRING s_state, "state"
STRING s_state_stats, "state.stats"
STRING s_run, "run"
STRING s_run_config, "run.config"
STRING s_run_player, "run.player"
STRING s_run_player_statuses, "run.player.statuses"
STRING s_run_monster, "run.monster"
STRING s_run_monster_statuses, "run.monster.statuses"
STRING s_run_stats, "run.stats"
STRING s_equipment_path, "run.player.equipment.%s"
STRING s_bag_path, "run.player.bag.%ld"
STRING s_stock_path, "run.merchantStock.%ld"
STRING s_error_open, `rpg-asm: cannot open %s\n`
STRING s_error_script, `rpg-asm: invalid script line: %s\n`

section .bss align=16
rng_state_copy:	resq 1				; g_rng_state widened to a qword for the dump tables
number_text:	resb 32				; value_string: the decimal text of a number
line:			resb LINE_MAX		; current script line
line_copy:		resb LINE_MAX		; untouched copy for error messages (strtok cuts `line`)
path:			resb PATH_MAX		; prefix of a nested dump ("run.player.bag.3")
header:										; the run configuration read from the script
.seed:			resq 1
.vocation:		resq 1
.difficulty:	resq 1
.auto_equip:	resq 1
.name:			resb NAME_MAX
command:									; the parsed command
.type:			resq 1
.arg1:			resq 1
.arg2:			resq 1

section .text

; value_string(rdi = KIND_*, rsi = value) → rax = the canonical text of a field: a decimal number, true/false, the
; string itself, or the id of the row. The text of a number lives in a static buffer: use it before the next
; call.
value_string:
	ENTER_FRAME
	cmp		rdi, KIND_INT
	je		.number
	cmp		rdi, KIND_BOOL
	je		.boolean
	mov		rax, rsi
	cmp		rdi, KIND_STR
	je		.done
	call	kind_id
	jmp		.done
.boolean:
	lea		rax, [s_true]
	lea		rcx, [s_false]
	test	rsi, rsi
	cmovz	rax, rcx
	jmp		.done
.number:
	mov		rcx, rsi
	lea		rdi, [number_text]
	mov		esi, 32
	lea		rdx, [s_number]
	xor		eax, eax
	call	snprintf
	lea		rax, [number_text]
.done:
	LEAVE_FRAME

; print_events(rdi = command index) — prints every event of the buffer as `[index] type field=value ...`.
; Frame: rbx = current Event, r12 = its EventSchema row, r13 = field index, r14 = events left, r15 = index.
print_events:
	ENTER_FRAME
	mov		r15, rdi
	lea		rbx, [g_events]
	mov		r14, [g_event_count]
.event:
	test	r14, r14
	jz		.done
	mov		r12, [rbx + Event.type]
	ROW		r12, event_schema, r12, EventSchema_size
	PRINTF	"[%ld] %s", r15, [r12 + EventSchema.name]
	xor		r13d, r13d
.field:
	cmp		r13, [r12 + EventSchema.field_count]
	jae		.end_of_line
	mov		rax, r13
	shl		rax, 4							; (name, kind) pairs of 16 bytes
	mov		rdi, [r12 + EventSchema.fields + rax + 8]
	mov		rsi, [rbx + Event.f0 + r13 * 8]
	call	value_string
	mov		rcx, r13
	shl		rcx, 4
	PRINTF	" %s=%s", [r12 + EventSchema.fields + rcx], rax
	inc		r13
	jmp		.field
.end_of_line:
	mov		edi, 10
	call	putchar
	add		rbx, Event_size
	dec		r14
	jmp		.event
.done:
	LEAVE_FRAME

; print_command(rdi = CMD_*, rsi = first argument, rdx = second argument) — `> type arg1 arg2`, the same text a
; script line has. Frame: rbx = CommandSchema row, r12 / r13 = arguments.
print_command:
	ENTER_FRAME
	mov		r12, rsi
	mov		r13, rdx
	ROW		rbx, command_schema, rdi, CommandSchema_size
	PRINTF	"> %s", [rbx + CommandSchema.name]
	mov		rdi, [rbx + CommandSchema.arg1]
	cmp		rdi, KIND_NONE
	je		.end_of_line
	mov		rsi, r12
	call	value_string
	PRINTF	" %s", rax
	mov		rdi, [rbx + CommandSchema.arg2]
	cmp		rdi, KIND_NONE
	je		.end_of_line
	mov		rsi, r13
	call	value_string
	PRINTF	" %s", rax
.end_of_line:
	mov		edi, 10
	call	putchar
	LEAVE_FRAME

; parse_argument(rdi = KIND_* of the argument, rsi = pointer to where the value goes) → eax = 0, or 1 on error.
; Reads the next space-separated token of the line being cut by strtok. A number is parsed with strtol; an id is
; looked up in its table (an unknown spell or potion stays -1: the engine answers with an error event; an
; unknown slot is a broken script).
parse_argument:
	ENTER_FRAME
	mov		r12, rdi
	mov		rbx, rsi
	xor		eax, eax
	cmp		r12, KIND_NONE
	je		.done
	xor		edi, edi						; strtok(NULL, " "): continue with the same line
	lea		rsi, [s_space]
	call	strtok
	test	rax, rax
	jz		.error
	cmp		r12, KIND_INT
	jne		.lookup
	mov		rdi, rax
	xor		esi, esi
	mov		edx, 10
	call	strtol
	mov		[rbx], rax
	xor		eax, eax
	jmp		.done
.lookup:
	mov		rdi, r12
	mov		rsi, rax
	call	kind_lookup
	mov		[rbx], rax
	cmp		rax, 0
	jge		.ok
	cmp		r12, KIND_SLOT
	je		.error
.ok:
	xor		eax, eax
	jmp		.done
.error:
	mov		eax, 1
.done:
	LEAVE_FRAME

; parse_command() → eax = 0 when `line` holds a valid command (stored in `command`), 1 otherwise.
; Frame: rbx = CommandSchema row, r12 = CMD_* candidate, r13 = first token.
parse_command:
	ENTER_FRAME
	lea		rdi, [line]
	lea		rsi, [s_space]
	call	strtok
	test	rax, rax
	jz		.error
	mov		r13, rax
	xor		r12d, r12d
	lea		rbx, [command_schema]
.type:
	cmp		r12, CMD_COUNT
	jae		.error
	mov		rdi, [rbx + CommandSchema.name]
	mov		rsi, r13
	call	strcmp
	test	eax, eax
	jz		.found
	add		rbx, CommandSchema_size
	inc		r12
	jmp		.type
.found:
	mov		[command.type], r12
	mov		rdi, [rbx + CommandSchema.arg1]
	lea		rsi, [command.arg1]
	call	parse_argument
	test	eax, eax
	jnz		.error
	mov		rdi, [rbx + CommandSchema.arg2]
	lea		rsi, [command.arg2]
	call	parse_argument
	jmp		.done
.error:
	mov		eax, 1
.done:
	LEAVE_FRAME

; ── final state dump ──────────────────────────────────────────────────────────

; dump_fields(rdi = prefix, rsi = FIELD table, rdx = rows) — one `prefix.key=value` line per row.
dump_fields:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r12, rsi
	mov		r13, rdx
.field:
	test	r13, r13
	jz		.done
	mov		rdi, [r12 + 8]
	mov		rax, [r12 + 16]
	mov		rsi, [rax]
	call	value_string
	PRINTF	`%s.%s=%s\n`, rbx, [r12], rax
	add		r12, FIELD_SIZE
	dec		r13
	jmp		.field
.done:
	LEAVE_FRAME

; dump_item(rdi = prefix, rsi = ItemInstance) — ItemInstance.to_dict().
; Frame: rbx = prefix, r12 = item, r13 = affix index, r14 = current AffixRoll.
dump_item:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r12, rsi
	PRINTF	`%s.uid=%ld\n`, rbx, [r12 + ItemInstance.uid]
	mov		edi, KIND_ITEM
	mov		rsi, [r12 + ItemInstance.item]
	call	kind_id
	PRINTF	`%s.itemId=%s\n`, rbx, rax
	mov		edi, KIND_RARITY
	mov		rsi, [r12 + ItemInstance.rarity]
	call	kind_id
	PRINTF	`%s.rarity=%s\n`, rbx, rax
	PRINTF	`%s.tier=%ld\n`, rbx, [r12 + ItemInstance.tier]
	PRINTF	`%s.affixes.#=%ld\n`, rbx, [r12 + ItemInstance.affix_count]
	xor		r13d, r13d
	lea		r14, [r12 + ItemInstance.affixes]
.affix:
	cmp		r13, [r12 + ItemInstance.affix_count]
	jae		.done
	mov		edi, KIND_STAT
	mov		rsi, [r14 + AffixRoll.stat]
	call	kind_id
	PRINTF	`%s.affixes.%ld.stat=%s\n`, rbx, r13, rax
	PRINTF	`%s.affixes.%ld.value=%ld\n`, rbx, r13, [r14 + AffixRoll.value]
	add		r14, AffixRoll_size
	inc		r13
	jmp		.affix
.done:
	LEAVE_FRAME

; dump_statuses(rdi = prefix, rsi = StatusList) — a list of ActiveStatus.to_dict().
dump_statuses:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r12, rsi
	PRINTF	`%s.#=%ld\n`, rbx, [r12 + StatusList.count]
	xor		r13d, r13d
	lea		r14, [r12 + StatusList.items]
.status:
	cmp		r13, [r12 + StatusList.count]
	jae		.done
	mov		edi, KIND_STATUS
	mov		rsi, [r14 + ActiveStatus.status]
	call	kind_id
	PRINTF	`%s.%ld.statusId=%s\n`, rbx, r13, rax
	PRINTF	`%s.%ld.turns=%ld\n`, rbx, r13, [r14 + ActiveStatus.turns]
	PRINTF	`%s.%ld.perTurn=%ld\n`, rbx, r13, [r14 + ActiveStatus.per_turn]
	add		r14, ActiveStatus_size
	inc		r13
	jmp		.status
.done:
	LEAVE_FRAME

; dump_stats(rdi = prefix) — RunStatistics.to_dict().
; Frame: rbx = prefix, r12 = table cursor, r13 = row/entry index, r14 = id index inside a counter, r15 = rows of
; the id table of the counter.
dump_stats:
	ENTER_FRAME
	mov		rbx, rdi
	xor		r13d, r13d
.scalar:
	PRINTF	`%s.%s=%ld\n`, rbx, [stat_scalar_names + r13 * 8], [g_stats + r13 * 8]
	inc		r13
	cmp		r13, STATS_SCALAR_COUNT
	jb		.scalar

	; counters keyed by id: only the ids that were counted (a Counter has no zero entries)
	lea		r12, [stat_counters]
	mov		r13d, STAT_COUNTERS
.counter:
	mov		rax, [r12 + 8]
	imul	rax, rax, 24
	mov		r15, [kind_tables + rax + 16]
	xor		r14d, r14d
.entry:
	cmp		r14, r15
	jae		.counter_next
	mov		rax, [r12 + 16]
	cmp		qword [rax + r14 * 8], 0
	je		.entry_next
	mov		rdi, [r12 + 8]
	mov		rsi, r14
	call	kind_id
	mov		r8, [r12 + 16]
	PRINTF	`%s.%s.%s=%ld\n`, rbx, [r12], rax, [r8 + r14 * 8]
.entry_next:
	inc		r14
	jmp		.entry
.counter_next:
	add		r12, FIELD_SIZE
	dec		r13
	jnz		.counter

	PRINTF	`%s.droppedItems.#=%ld\n`, rbx, [g_stats + Stats.dropped_count]
	xor		r13d, r13d
	lea		r12, [g_stats + Stats.dropped]
.dropped:
	cmp		r13, [g_stats + Stats.dropped_count]
	jae		.done
	mov		edi, KIND_ITEM
	mov		rsi, [r12 + DroppedItem.item]
	call	kind_id
	PRINTF	`%s.droppedItems.%ld.itemId=%s\n`, rbx, r13, rax
	mov		edi, KIND_RARITY
	mov		rsi, [r12 + DroppedItem.rarity]
	call	kind_id
	PRINTF	`%s.droppedItems.%ld.rarity=%s\n`, rbx, r13, rax
	PRINTF	`%s.droppedItems.%ld.round=%ld\n`, rbx, r13, [r12 + DroppedItem.round]
	add		r12, DroppedItem_size
	inc		r13
	jmp		.dropped
.done:
	LEAVE_FRAME

; dump_state() — the two final-state sections: `state.*` (finalState) and `run.*` (finalRun).
; Frame: r12 = index, r13 = cursor, rbx = Creature row.
dump_state:
	ENTER_FRAME
	mov		eax, [g_rng_state]				; 32-bit load: the upper half of rax is cleared
	mov		[rng_state_copy], rax

	lea		rdi, [s_state]
	lea		rsi, [state_fields]
	mov		edx, STATE_FIELDS
	call	dump_fields
	lea		rdi, [s_state_stats]
	call	dump_stats

	lea		rdi, [s_run]
	lea		rsi, [run_fields]
	mov		edx, RUN_FIELDS
	call	dump_fields
	lea		rax, [g_player + Player.name]
	PRINTF	`run.config.name=%s\n`, rax
	lea		rdi, [s_run_config]
	lea		rsi, [config_fields]
	mov		edx, CONFIG_FIELDS
	call	dump_fields
	cmp		qword [g_run + RunState.death_cause], 0
	jge		.death_cause
	PRINTF	`run.deathCause=null\n`
	jmp		.player
.death_cause:
	mov		edi, KIND_CREATURE
	mov		rsi, [g_run + RunState.death_cause]
	call	kind_id
	PRINTF	`run.deathCause=%s\n`, rax

.player:
	lea		rax, [g_player + Player.name]
	PRINTF	`run.player.name=%s\n`, rax
	lea		rdi, [s_run_player]
	lea		rsi, [player_fields]
	mov		edx, PLAYER_FIELDS
	call	dump_fields

	xor		r12d, r12d						; potions: every potion ever owned, even at 0
.potion:
	cmp		qword [g_player + Player.potion_known + r12 * 8], 0
	je		.potion_next
	mov		edi, KIND_POTION
	mov		rsi, r12
	call	kind_id
	PRINTF	`run.player.potions.%s=%ld\n`, rax, [g_player + Player.potions + r12 * 8]
.potion_next:
	inc		r12
	cmp		r12, POTION_COUNT
	jb		.potion

	xor		r12d, r12d						; equipment: the filled slots
	lea		r13, [g_player + Player.equipment]
.slot:
	cmp		qword [r13 + ItemInstance.uid], 0
	je		.slot_next
	mov		edi, KIND_SLOT
	mov		rsi, r12
	call	kind_id
	lea		rdi, [path]
	mov		esi, PATH_MAX
	lea		rdx, [s_equipment_path]
	mov		rcx, rax
	xor		eax, eax
	call	snprintf
	lea		rdi, [path]
	mov		rsi, r13
	call	dump_item
.slot_next:
	add		r13, ItemInstance_size
	inc		r12
	cmp		r12, SLOT_COUNT
	jb		.slot

	PRINTF	`run.player.bag.#=%ld\n`, [g_player + Player.bag_count]
	xor		r12d, r12d
	lea		r13, [g_player + Player.bag]
.bag:
	cmp		r12, [g_player + Player.bag_count]
	jae		.bag_done
	lea		rdi, [path]
	mov		esi, PATH_MAX
	lea		rdx, [s_bag_path]
	mov		rcx, r12
	xor		eax, eax
	call	snprintf
	lea		rdi, [path]
	mov		rsi, r13
	call	dump_item
	add		r13, ItemInstance_size
	inc		r12
	jmp		.bag
.bag_done:

	xor		r12d, r12d						; spellUses: only the spells that were cast
.spell:
	cmp		qword [g_player + Player.spell_uses + r12 * 8], 0
	je		.spell_next
	mov		edi, KIND_SPELL
	mov		rsi, r12
	call	kind_id
	PRINTF	`run.player.spellUses.%s=%ld\n`, rax, [g_player + Player.spell_uses + r12 * 8]
.spell_next:
	inc		r12
	cmp		r12, SPELL_COUNT
	jb		.spell

	lea		rdi, [s_run_player_statuses]
	lea		rsi, [g_player + Player.statuses]
	call	dump_statuses

	cmp		qword [g_run + RunState.has_monster], 0
	jne		.monster
	PRINTF	`run.monster=null\n`
	jmp		.stock
.monster:
	lea		rdi, [s_run_monster]
	lea		rsi, [monster_fields]
	mov		edx, MONSTER_FIELDS
	call	dump_fields
	mov		rbx, [g_monster + MonsterInstance.creature]
	ROW		rbx, creatures, rbx, Creature_size
	PRINTF	`run.monster.attacks.#=%ld\n`, [rbx + Creature.attack_count]
	xor		r12d, r12d
	mov		r13, [rbx + Creature.attacks]	; r13 = current Attack row
.attack:
	cmp		r12, [rbx + Creature.attack_count]
	jae		.attacks_done
	PRINTF	`run.monster.attacks.%ld.id=%s\n`, r12, [r13 + Attack.id]
	mov		edi, KIND_ELEMENT
	mov		rsi, [r13 + Attack.element]
	call	kind_id
	PRINTF	`run.monster.attacks.%ld.element=%s\n`, r12, rax
	PRINTF	`run.monster.attacks.%ld.min=%ld\n`, r12, [g_monster + MonsterInstance.attack_min + r12 * 8]
	PRINTF	`run.monster.attacks.%ld.max=%ld\n`, r12, [g_monster + MonsterInstance.attack_max + r12 * 8]
	PRINTF	`run.monster.attacks.%ld.weight=%ld\n`, r12, [r13 + Attack.weight]
	cmp		qword [r13 + Attack.status], 0
	jl		.attack_next
	mov		edi, KIND_STATUS
	mov		rsi, [r13 + Attack.status]
	call	kind_id
	PRINTF	`run.monster.attacks.%ld.status.id=%s\n`, r12, rax
	PRINTF	`run.monster.attacks.%ld.status.chance=%ld\n`, r12, [r13 + Attack.status_chance]
	PRINTF	`run.monster.attacks.%ld.status.damagePct=%ld\n`, r12, [r13 + Attack.status_damage_pct]
.attack_next:
	add		r13, Attack_size
	inc		r12
	jmp		.attack
.attacks_done:
	lea		rdi, [s_run_monster_statuses]
	lea		rsi, [g_monster + MonsterInstance.statuses]
	call	dump_statuses

.stock:
	PRINTF	`run.merchantStock.#=%ld\n`, [g_run + RunState.stock_count]
	xor		r12d, r12d
	lea		r13, [g_run + RunState.stock]
.stock_item:
	cmp		r12, [g_run + RunState.stock_count]
	jae		.stock_done
	lea		rdi, [path]
	mov		esi, PATH_MAX
	lea		rdx, [s_stock_path]
	mov		rcx, r12
	xor		eax, eax
	call	snprintf
	lea		rdi, [path]
	mov		rsi, r13
	call	dump_item
	add		r13, ItemInstance_size
	inc		r12
	jmp		.stock_item
.stock_done:
	lea		rdi, [s_run_stats]
	call	dump_stats
	LEAVE_FRAME

; ── the replay itself ─────────────────────────────────────────────────────────

; read_line(rdi = FILE*) → eax = 1 when a line was read into `line` (and copied to `line_copy`), 0 at end of
; file. The line ending (LF or CRLF) is removed.
read_line:
	ENTER_FRAME
	mov		rdx, rdi
	lea		rdi, [line]
	mov		esi, LINE_MAX
	call	fgets
	test	rax, rax
	jz		.done
	lea		rdi, [line]
.trim:										; cut at the first CR or LF
	mov		al, [rdi]
	test	al, al
	jz		.copy
	cmp		al, 10
	je		.cut
	cmp		al, 13
	je		.cut
	inc		rdi
	jmp		.trim
.cut:
	mov		byte [rdi], 0
.copy:
	lea		rdi, [line_copy]
	lea		rsi, [line]
	mov		edx, LINE_MAX
	call	strncpy
	mov		eax, 1
.done:
	LEAVE_FRAME

; replay_run(rdi = script path) → eax = process exit code (0, or 2 for an unreadable or invalid script).
; Frame: rbx = FILE*, r12 = command index, r13 = value part of a header line.
replay_run:
	ENTER_FRAME
	mov		r13, rdi
	lea		rsi, [s_read_mode]
	call	fopen
	mov		rbx, rax
	test	rax, rax
	jnz		.header
	mov		rdi, [stderr]
	lea		rsi, [s_error_open]
	mov		rdx, r13
	xor		eax, eax
	call	fprintf
	mov		eax, 2
	jmp		.done

.header:									; `key value` lines until `begin`
	mov		rdi, rbx
	call	read_line
	test	eax, eax
	jz		.invalid
	lea		rdi, [line]
	lea		rsi, [s_key_begin]
	call	strcmp
	test	eax, eax
	jz		.begin
	lea		rdi, [line]
	mov		esi, ' '
	call	strchr
	lea		r13, [s_empty]					; a key without a value (e.g. an empty name)
	test	rax, rax
	jz		.key
	mov		byte [rax], 0					; split the line: key in `line`, value after the space
	lea		r13, [rax + 1]
.key:
	%macro IF_KEY 2							; IF_KEY key string, label to jump to
		lea		rdi, [line]
		lea		rsi, [%1]
		call	strcmp
		test	eax, eax
		jz		%2
	%endmacro
	IF_KEY	s_key_seed, .seed
	IF_KEY	s_key_name, .name
	IF_KEY	s_key_vocation, .vocation
	IF_KEY	s_key_difficulty, .difficulty
	IF_KEY	s_key_auto_equip, .auto_equip
	jmp		.invalid
.seed:
	mov		rdi, r13
	xor		esi, esi
	mov		edx, 10
	call	strtoul
	mov		[header.seed], rax
	jmp		.header
.name:
	lea		rdi, [header.name]
	mov		rsi, r13
	mov		edx, NAME_MAX - 1
	call	strncpy
	jmp		.header
.vocation:
	mov		edi, KIND_VOCATION
	mov		rsi, r13
	call	kind_lookup
	mov		[header.vocation], rax
	cmp		rax, 0
	jl		.invalid
	jmp		.header
.difficulty:
	mov		edi, KIND_DIFFICULTY
	mov		rsi, r13
	call	kind_lookup
	mov		[header.difficulty], rax
	cmp		rax, 0
	jl		.invalid
	jmp		.header
.auto_equip:
	mov		rdi, r13
	lea		rsi, [s_true]
	call	strcmp
	test	eax, eax
	setz	al
	movzx	eax, al
	mov		[header.auto_equip], rax
	jmp		.header

.begin:
	mov		rdi, [header.seed]
	lea		rsi, [header.name]
	mov		rdx, [header.vocation]
	mov		rcx, [header.difficulty]
	mov		r8, [header.auto_equip]
	call	engine_new_run
	xor		edi, edi
	call	print_events					; index 0 = the events of the run creation
	mov		r12d, 1

.command:
	mov		rdi, rbx
	call	read_line
	test	eax, eax
	jz		.finished
	cmp		byte [line], 0
	je		.command						; blank line
	lea		rdi, [line]
	lea		rsi, [s_key_bot]
	call	strcmp
	test	eax, eax
	jz		.bot
	call	parse_command
	test	eax, eax
	jnz		.invalid
	mov		rdi, [command.type]
	mov		rsi, [command.arg1]
	mov		rdx, [command.arg2]
	call	engine_step
	mov		rdi, r12
	call	print_events
	inc		r12
	jmp		.command

.bot:										; the bot plays until the run is over, printing what it chooses
	cmp		qword [g_run + RunState.phase], PHASE_GAME_OVER
	je		.command
	cmp		r12, MAX_BOT_COMMANDS
	ja		.invalid
	call	bot_choose
	mov		[command.type], rax
	mov		[command.arg1], rsi
	mov		[command.arg2], rdx
	mov		rdi, rax
	call	print_command
	mov		rdi, [command.type]
	mov		rsi, [command.arg1]
	mov		rdx, [command.arg2]
	call	engine_step
	mov		rdi, r12
	call	print_events
	inc		r12
	jmp		.bot

.finished:
	call	dump_state
	mov		rdi, rbx
	call	fclose
	xor		eax, eax
	jmp		.done
.invalid:
	mov		rdi, [stderr]
	lea		rsi, [s_error_script]
	lea		rdx, [line_copy]
	xor		eax, eax
	call	fprintf
	mov		rdi, rbx
	call	fclose
	mov		eax, 2
.done:
	LEAVE_FRAME
