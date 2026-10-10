; presentation/text_ui.asm — the playable game: a line-based text UI (stands in for presentation/controller.py and
; the TUI of the other ports; see docs/asm.md for what is out of scope).
;
; Role: a loop that shows the state, lists numbered options, reads one line from stdin, turns the choice into an
; engine command and prints the resulting events as English sentences. There is no cursor control, no colour, no
; animation and no save file: the screen is plain text, so the game also plays through a pipe.
;
; Every screen follows the same pattern: fill `choices` with what each number means (a potion row, an item
; uid...), print the numbered list, `read_number`, then `step` with the command. All the rules stay in the
; engine: an option that is not possible simply comes back as an `error` event sentence.
;
; Register use: standard frames; PRINTF and TEXT (below) destroy all caller-saved registers, so the loops keep
; their counters in rbx and r12-r15.
%include "common.inc"

extern g_run, g_player, g_monster, g_stats, g_sheet, g_events, g_event_count
extern engine_new_run, engine_step, round_info
extern build_sheet, item_score, item_value, required_level, equipment_score
extern xp_for_level, mana_for_magic_level, spell_level_for_uses, spell_cost, stock_price
extern kind_name, print_event_text, t

global text_ui_run

INPUT_MAX	equ 128
MAX_CHOICES	equ 64		; the longest list is the equipment screen: the bag plus one option per slot

; TEXT "i18n.key" — rax = the English text of that key (infrastructure/i18n.asm).
%macro TEXT 1
	%push text
	[section .rodata.str]
	%$key: db %1, 0
	__SECT__
	lea		rdi, [%$key]
	call	t
	%pop
%endmacro

; MENU_LINE number, "i18n.key" — one numbered option whose label is an en.json text.
%macro MENU_LINE 2
	TEXT	%2
	PRINTF	` %ld) %s\n`, %1, rax
%endmacro

section .rodata align=8
STRING s_default_name, "Hero"
STRING s_elite_tag, " [ELITE]"
STRING s_boss_tag, " [BOSS]"
STRING s_no_tag, ""
STRING s_separator, ", "
STRING s_opening, " ("
STRING s_affix, "%s+%ld %s"
STRING s_key_lost, "gameover.title"
STRING s_key_won, "gameover.title_won"

section .bss align=16
input:		resb INPUT_MAX				; the line typed by the player
name:		resb NAME_MAX
choices:	resq MAX_CHOICES			; what each numbered option of the current list stands for
quit:		resq 1						; set when the player leaves the game

section .text

; ── input ─────────────────────────────────────────────────────────────────────

; read_line() → rax = `input`, holding the next line without its line ending. At end of input the game says
; goodbye and the process exits (so a piped session ends cleanly).
read_line:
	ENTER_FRAME
	mov		rdi, [stdout]
	call	fflush							; the prompt has no newline: push it out before blocking on stdin
	lea		rdi, [input]
	mov		esi, INPUT_MAX
	mov		rdx, [stdin]
	call	fgets
	test	rax, rax
	jnz		.trim
	TEXT	"app.bye"
	PRINTF	`\n%s\n`, rax
	xor		edi, edi
	call	exit
.trim:
	lea		rax, [input]
.byte:
	mov		cl, [rax]
	test	cl, cl
	jz		.done
	cmp		cl, 10
	je		.cut
	cmp		cl, 13
	je		.cut
	inc		rax
	jmp		.byte
.cut:
	mov		byte [rax], 0
.done:
	lea		rax, [input]
	LEAVE_FRAME

; read_number() → rax = the number typed (>= 0), or -1 when the line is not a number.
; Local: [rsp] = where strtol stopped reading.
read_number:
	ENTER_FRAME 16
	PRINTF	"> "
	call	read_line
	lea		rdi, [input]
	mov		rsi, rsp
	mov		edx, 10
	call	strtol
	lea		rcx, [input]
	cmp		[rsp], rcx
	je		.invalid						; no digit was read
	cmp		rax, 0
	jge		.done
.invalid:
	mov		rax, -1
.done:
	LEAVE_FRAME

; ── output helpers ────────────────────────────────────────────────────────────

; show_events() — the events of the last command, one sentence per line.
show_events:
	ENTER_FRAME
	lea		rbx, [g_events]
	mov		r12, [g_event_count]
.event:
	test	r12, r12
	jz		.done
	PRINTF	"  "
	mov		rdi, rbx
	call	print_event_text
	add		rbx, Event_size
	dec		r12
	jmp		.event
.done:
	LEAVE_FRAME

; step(rdi = CMD_*, rsi, rdx = arguments) — runs the command and shows what happened.
step:
	sub		rsp, 8
	call	engine_step
	call	show_events
	add		rsp, 8
	ret

; print_item(rdi = ItemInstance) — "Name [rarity] slot, Lv N, score S (+3 Fire protection, ...)", no newline.
; Frame: rbx = item, r12 = Item row, r13 = slot name, then affix index, r14 = level, then current AffixRoll.
print_item:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r12, [rbx + ItemInstance.item]
	ROW		r12, items, r12, Item_size
	mov		edi, KIND_RARITY
	mov		rsi, [rbx + ItemInstance.rarity]
	call	kind_name
	PRINTF	"%s [%s] ", [r12 + Item.name], rax
	mov		edi, KIND_SLOT
	mov		rsi, [r12 + Item.slot]
	call	kind_name
	mov		r13, rax
	mov		rdi, rbx
	call	required_level
	mov		r14, rax
	mov		rdi, rbx
	call	item_score
	PRINTF	"%s, Lv %ld, score %ld", r13, r14, rax
	xor		r13d, r13d
	lea		r14, [rbx + ItemInstance.affixes]
.affix:
	cmp		r13, [rbx + ItemInstance.affix_count]
	jae		.close
	mov		edi, KIND_STAT
	mov		rsi, [r14 + AffixRoll.stat]
	call	kind_name
	lea		rsi, [s_opening]				; " (" before the first affix, ", " before the others
	lea		rcx, [s_separator]
	test	r13, r13
	cmovnz	rsi, rcx
	mov		rdx, [r14 + AffixRoll.value]
	mov		rcx, rax
	lea		rdi, [s_affix]
	xor		eax, eax
	call	printf
	add		r14, AffixRoll_size
	inc		r13
	jmp		.affix
.close:
	test	r13, r13
	jz		.done
	mov		edi, ')'
	call	putchar
.done:
	LEAVE_FRAME

; show_statuses(rdi = StatusList) — " | burning (2) | stunned (1)" for each active status, no newline.
show_statuses:
	ENTER_FRAME
	mov		rbx, rdi
	xor		r12d, r12d
	lea		r13, [rbx + StatusList.items]
.status:
	cmp		r12, [rbx + StatusList.count]
	jae		.done
	mov		edi, KIND_STATUS
	mov		rsi, [r13 + ActiveStatus.status]
	call	kind_name
	PRINTF	" | %s (%ld)", rax, [r13 + ActiveStatus.turns]
	add		r13, ActiveStatus_size
	inc		r12
	jmp		.status
.done:
	LEAVE_FRAME

; show_status() — the header printed before every menu: round, character, resources and, in battle, the monster.
show_status:
	ENTER_FRAME
	call	build_sheet
	mov		rdi, [g_run + RunState.round]
	test	rdi, rdi
	jnz		.round
	mov		edi, 1							; before the first fight: show the tier of round 1
.round:
	call	round_info
	mov		r12, rax
	mov		edi, KIND_DIFFICULTY
	mov		rsi, [g_run + RunState.difficulty]
	call	kind_name
	PRINTF	`\n== Round %ld | Tier %ld | %s ==\n`, [g_run + RunState.round], r12, rax
	mov		edi, KIND_VOCATION
	mov		rsi, [g_player + Player.vocation]
	call	kind_name
	lea		rsi, [g_player + Player.name]
	PRINTF	"%s the %s | Lv %ld | ML %ld", rsi, rax, [g_player + Player.level], [g_player + Player.magic_level]
	PRINTF	" | HP %ld/%ld", [g_player + Player.hp], [g_sheet + Sheet.max_hp]
	PRINTF	" | MP %ld/%ld", [g_player + Player.mp], [g_sheet + Sheet.max_mp]
	PRINTF	" | Gold %ld", [g_player + Player.gold]
	lea		rdi, [g_player + Player.statuses]
	call	show_statuses
	mov		edi, 10
	call	putchar
	cmp		qword [g_run + RunState.phase], PHASE_BATTLE
	jne		.done
	mov		rbx, [g_monster + MonsterInstance.creature]
	ROW		rbx, creatures, rbx, Creature_size
	lea		r12, [s_no_tag]
	lea		rax, [s_elite_tag]
	cmp		qword [g_monster + MonsterInstance.enemy_class], CLASS_ELITE
	cmove	r12, rax
	lea		rax, [s_boss_tag]
	cmp		qword [g_monster + MonsterInstance.enemy_class], CLASS_BOSS
	cmove	r12, rax
	PRINTF	"%s%s (%s)", [rbx + Creature.name], r12, [rbx + Creature.family]
	PRINTF	" | HP %ld/%ld", [g_monster + MonsterInstance.hp], [g_monster + MonsterInstance.max_hp]
	lea		rdi, [g_monster + MonsterInstance.statuses]
	call	show_statuses
	mov		edi, 10
	call	putchar
.done:
	LEAVE_FRAME

; ── run creation ──────────────────────────────────────────────────────────────

; choose_row(rdi = KIND_* of a table, rsi = rows) → rax = the row chosen from the numbered list of display names.
; Asks again until the answer is valid.
choose_row:
	ENTER_FRAME
	mov		r12, rdi
	mov		r13, rsi
.list:
	xor		ebx, ebx
.row:
	mov		rdi, r12
	mov		rsi, rbx
	call	kind_name
	lea		rsi, [rbx + 1]
	PRINTF	` %ld) %s\n`, rsi, rax
	inc		rbx
	cmp		rbx, r13
	jb		.row
	call	read_number
	dec		rax
	cmp		rax, r13						; unsigned: also rejects 0 and "not a number"
	jae		.list
	LEAVE_FRAME

; new_run(rdi = seed) — asks for the name, vocation, difficulty and auto-equip, then creates the run.
; Frame: r12 = seed, r13 = vocation row, r14 = difficulty row.
new_run:
	ENTER_FRAME
	mov		r12, rdi
	TEXT	"app.title"
	PRINTF	`%s (x86-64 Assembly) | seed %ld\n`, rax, r12
	TEXT	"new_run.name"
	PRINTF	`\n%s\n> `, rax
	call	read_line
	lea		rsi, [input]
	lea		rax, [s_default_name]
	cmp		byte [rsi], 0
	cmove	rsi, rax						; just Enter: a default name
	lea		rdi, [name]
	mov		edx, NAME_MAX - 1
	call	strncpy
	TEXT	"new_run.vocation"
	PRINTF	`\n%s\n`, rax
	mov		edi, KIND_VOCATION
	mov		esi, VOCATION_COUNT
	call	choose_row
	mov		r13, rax
	TEXT	"new_run.difficulty"
	PRINTF	`\n%s\n`, rax
	mov		edi, KIND_DIFFICULTY
	mov		esi, DIFFICULTY_COUNT
	call	choose_row
	mov		r14, rax
	TEXT	"new_run.auto_equip"
	PRINTF	`\n%s\n 1) yes\n 2) no\n`, rax
	call	read_number
	xor		r8d, r8d
	cmp		rax, 1
	sete	r8b
	mov		rdi, r12
	lea		rsi, [name]
	mov		rdx, r13
	mov		rcx, r14
	call	engine_new_run
	mov		edi, 10
	call	putchar
	call	show_events
	LEAVE_FRAME

; ── merchant ──────────────────────────────────────────────────────────────────

; merchant_menu() — one visit to the merchant's main menu.
merchant_menu:
	ENTER_FRAME
	call	show_status
	MENU_LINE 1, "merchant.next_fight"
	MENU_LINE 2, "merchant.buy_potions"
	MENU_LINE 3, "merchant.sell_items"
	MENU_LINE 4, "merchant.equipment"
	MENU_LINE 5, "merchant.stock"
	MENU_LINE 6, "merchant.character"
	MENU_LINE 0, "menu.quit"
	call	read_number
	cmp		rax, 0
	je		.quit
	cmp		rax, 1
	je		.fight
	cmp		rax, 2
	je		.potions
	cmp		rax, 3
	je		.sell
	cmp		rax, 4
	je		.equipment
	cmp		rax, 5
	je		.stock
	cmp		rax, 6
	je		.character
	TEXT	"menu.invalid"
	PRINTF	`  %s\n`, rax
	jmp		.done
.quit:
	mov		qword [quit], 1
	jmp		.done
.fight:
	mov		edi, CMD_NEXT_FIGHT
	call	step
	jmp		.done
.potions:
	call	buy_potions
	jmp		.done
.sell:
	call	sell_items
	jmp		.done
.equipment:
	call	equipment
	jmp		.done
.stock:
	call	buy_stock
	jmp		.done
.character:
	call	character
.done:
	LEAVE_FRAME

; buy_potions() — lists the potions on sale (unlocked for the next round), asks which one and how many.
; Frame: rbx = Potion row, r12 = potion row index, r13 = options listed.
buy_potions:
	ENTER_FRAME
	xor		r12d, r12d
	xor		r13d, r13d
	lea		rbx, [potions]
.potion:
	mov		rax, [g_run + RunState.round]
	inc		rax
	cmp		[rbx + Potion.unlock_round], rax
	jg		.next
	mov		[choices + r13 * 8], r12
	inc		r13
	PRINTF	" %ld) %s | %ld gold", r13, [rbx + Potion.name], [rbx + Potion.price]
	PRINTF	` (you have %ld)\n`, [g_player + Player.potions + r12 * 8]
.next:
	add		rbx, Potion_size
	inc		r12
	cmp		r12, POTION_COUNT
	jb		.potion
	MENU_LINE 0, "menu.back"
	call	read_number
	dec		rax
	cmp		rax, r13
	jae		.done							; 0, an unknown number or not a number: back
	mov		r12, [choices + rax * 8]
	PRINTF	`How many? (0 to go back)\n`
	call	read_number
	cmp		rax, 0
	jle		.done
	mov		edi, CMD_BUY_POTION
	mov		rsi, r12
	mov		rdx, rax
	call	step
.done:
	LEAVE_FRAME

; list_items(rdi = first ItemInstance, rsi = count, rdx = 0 to show the sell value / 1 to show the stock price,
; rcx = 0 to store uids in `choices` / 1 to store list indexes) → rax = count. Prints one numbered line per item.
; Frame: rbx = current item, r12 = index, r13 = count, r14 = price mode, r15 = choice mode.
list_items:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r13, rsi
	mov		r14, rdx
	mov		r15, rcx
	xor		r12d, r12d
.item:
	cmp		r12, r13
	jae		.done
	mov		rax, [rbx + ItemInstance.uid]
	test	r15, r15
	cmovnz	rax, r12
	mov		[choices + r12 * 8], rax
	lea		rsi, [r12 + 1]
	PRINTF	" %ld) ", rsi
	mov		rdi, rbx
	call	print_item
	mov		rdi, rbx
	test	r14, r14
	jnz		.price
	call	item_value
	PRINTF	` | sells for %ld gold\n`, rax
	jmp		.next
.price:
	call	stock_price
	PRINTF	` | %ld gold\n`, rax
.next:
	add		rbx, ItemInstance_size
	inc		r12
	jmp		.item
.done:
	mov		rax, r13
	LEAVE_FRAME

; sell_items() — lists the bag; the chosen item is sold for its value.
sell_items:
	ENTER_FRAME
	cmp		qword [g_player + Player.bag_count], 0
	jne		.list
	TEXT	"merchant.empty_bag"
	PRINTF	`  %s\n`, rax
	jmp		.done
.list:
	lea		rdi, [g_player + Player.bag]
	mov		rsi, [g_player + Player.bag_count]
	xor		edx, edx
	xor		ecx, ecx
	call	list_items
	mov		r12, rax
	MENU_LINE 0, "menu.back"
	call	read_number
	dec		rax
	cmp		rax, r12
	jae		.done
	mov		edi, CMD_SELL_ITEM
	mov		rsi, [choices + rax * 8]
	call	step
.done:
	LEAVE_FRAME

; buy_stock() — lists the merchant's rotating stock; the chosen item is bought (and auto-equipped if enabled).
buy_stock:
	ENTER_FRAME
	cmp		qword [g_run + RunState.stock_count], 0
	jne		.list
	TEXT	"merchant.empty_stock"
	PRINTF	`  %s\n`, rax
	jmp		.done
.list:
	lea		rdi, [g_run + RunState.stock]
	mov		rsi, [g_run + RunState.stock_count]
	mov		edx, 1
	mov		ecx, 1
	call	list_items
	mov		r12, rax
	MENU_LINE 0, "menu.back"
	call	read_number
	dec		rax
	cmp		rax, r12
	jae		.done
	mov		edi, CMD_BUY_STOCK_ITEM
	mov		rsi, [choices + rax * 8]
	call	step
.done:
	LEAVE_FRAME

; equipment() — shows the equipped items, then one numbered list: first the bag items (choosing one equips it),
; then the filled slots (choosing one unequips it).
; Frame: rbx = current item, r12 = slot, r13 = options listed, r14 = number of bag options.
equipment:
	ENTER_FRAME
	call	equipment_score
	PRINTF	`Equipped (total score %ld):\n`, rax
	xor		r12d, r12d
	lea		rbx, [g_player + Player.equipment]
.slot:
	mov		edi, KIND_SLOT
	mov		rsi, r12
	call	kind_name
	PRINTF	"  %s: ", rax
	cmp		qword [rbx + ItemInstance.uid], 0
	jne		.worn
	PRINTF	`-\n`
	jmp		.slot_next
.worn:
	mov		rdi, rbx
	call	print_item
	mov		edi, 10
	call	putchar
.slot_next:
	add		rbx, ItemInstance_size
	inc		r12
	cmp		r12, SLOT_COUNT
	jb		.slot

	PRINTF	`Bag %ld/%ld:\n`, [g_player + Player.bag_count], BAL_BAG_CAPACITY
	xor		r13d, r13d
	lea		rbx, [g_player + Player.bag]
.bag:
	cmp		r13, [g_player + Player.bag_count]
	jae		.bag_done
	mov		rax, [rbx + ItemInstance.uid]
	mov		[choices + r13 * 8], rax
	inc		r13
	PRINTF	" %ld) Equip ", r13
	mov		rdi, rbx
	call	print_item
	mov		edi, 10
	call	putchar
	add		rbx, ItemInstance_size
	jmp		.bag
.bag_done:
	mov		r14, r13
	xor		r12d, r12d
	lea		rbx, [g_player + Player.equipment]
.unequip:
	cmp		qword [rbx + ItemInstance.uid], 0
	je		.unequip_next
	mov		[choices + r13 * 8], r12
	inc		r13
	mov		edi, KIND_SLOT
	mov		rsi, r12
	call	kind_name
	PRINTF	` %ld) Unequip %s\n`, r13, rax
.unequip_next:
	add		rbx, ItemInstance_size
	inc		r12
	cmp		r12, SLOT_COUNT
	jb		.unequip
	MENU_LINE 0, "menu.back"
	call	read_number
	dec		rax
	cmp		rax, r13
	jae		.done
	mov		rsi, [choices + rax * 8]
	mov		edi, CMD_EQUIP
	mov		ecx, CMD_UNEQUIP
	cmp		rax, r14
	cmovae	edi, ecx						; the options after the bag items are the slots
	call	step
.done:
	LEAVE_FRAME

; character() — the character sheet: progression, derived stats (docs/game-design.md §4) and the spells.
character:
	ENTER_FRAME
	call	build_sheet
	mov		rdi, [g_player + Player.level]
	inc		rdi
	call	xp_for_level
	PRINTF	`Level %ld | XP %ld / %ld\n`, [g_player + Player.level], [g_player + Player.xp], rax
	mov		rdi, [g_player + Player.magic_level]
	call	mana_for_magic_level
	mov		rcx, rax
	PRINTF	`Magic level %ld | mana spent %ld / %ld\n`, [g_player + Player.magic_level], \
			[g_player + Player.mana_spent], rcx
	mov		edi, KIND_ELEMENT
	mov		rsi, [g_sheet + Sheet.weapon_element]
	call	kind_name
	PRINTF	`Melee damage %ld-%ld (%s)\n`, [g_sheet + Sheet.melee_min], [g_sheet + Sheet.melee_max], rax
	PRINTF	`Regeneration per turn: %ld HP, %ld MP\n`, [g_sheet + Sheet.hp_regen], [g_sheet + Sheet.mp_regen]
	%macro SHEET_LINE 2						; SHEET_LINE STAT_*, Sheet field — "<stat name>: <value>"
		mov		edi, KIND_STAT
		mov		esi, %1
		call	kind_name
		PRINTF	`%s: %ld\n`, rax, [g_sheet + Sheet.%2]
	%endmacro
	SHEET_LINE STAT_ARMOR, armor
	SHEET_LINE STAT_CRIT_CHANCE, crit_chance
	SHEET_LINE STAT_CRIT_DAMAGE, crit_damage
	SHEET_LINE STAT_SPELL_POWER, spell_power
	SHEET_LINE STAT_PHYSICAL_DAMAGE, physical_damage
	SHEET_LINE STAT_DODGE, dodge
	SHEET_LINE STAT_PARRY, parry
	SHEET_LINE STAT_LIFE_LEECH, life_leech
	SHEET_LINE STAT_MANA_LEECH, mana_leech
	xor		r12d, r12d						; protections: only the ones the equipment gives
.protection:
	cmp		qword [g_sheet + Sheet.prot + r12 * 8], 0
	je		.protection_next
	mov		edi, KIND_STAT
	lea		rsi, [r12 + STAT_PROT_PHYSICAL]
	call	kind_name
	PRINTF	`%s: %ld\n`, rax, [g_sheet + Sheet.prot + r12 * 8]
.protection_next:
	inc		r12
	cmp		r12, ELEMENT_COUNT
	jb		.protection
	call	list_spells
	LEAVE_FRAME

; ── battle ────────────────────────────────────────────────────────────────────

; list_spells() → rax = number of spells. Prints the vocation's spells and stores their rows in `choices`.
; Frame: rbx = Vocation row, r12 = index, r13 = Spell row, r14 = spell row index, r15 = mana cost.
list_spells:
	ENTER_FRAME
	mov		rbx, [g_player + Player.vocation]
	ROW		rbx, vocations, rbx, Vocation_size
	xor		r12d, r12d
.spell:
	cmp		r12, [rbx + Vocation.spell_count]
	jae		.done
	mov		r14, [rbx + Vocation.spells + r12 * 8]
	mov		[choices + r12 * 8], r14
	ROW		r13, spells, r14, Spell_size
	mov		rdi, r14
	call	spell_cost
	mov		r15, rax
	lea		rsi, [r12 + 1]
	PRINTF	" %ld) %s (%s) | %ld MP", rsi, [r13 + Spell.name], [r13 + Spell.words], r15
	mov		rdi, [g_player + Player.spell_uses + r14 * 8]
	call	spell_level_for_uses
	PRINTF	` | Lv %ld (%ld uses)\n`, [rax + SpellLevel.level], [g_player + Player.spell_uses + r14 * 8]
	inc		r12
	jmp		.spell
.done:
	mov		rax, r12
	LEAVE_FRAME

; battle_menu() — one player turn.
; Frame: rbx = Potion row, r12 = spells listed / potion row index, r13 = potions listed.
battle_menu:
	ENTER_FRAME
	call	show_status
	MENU_LINE 1, "battle.attack"
	MENU_LINE 2, "battle.spells"
	MENU_LINE 3, "battle.potions"
	MENU_LINE 4, "battle.defend"
	MENU_LINE 0, "menu.quit"
	call	read_number
	cmp		rax, 0
	je		.quit
	cmp		rax, 1
	je		.attack
	cmp		rax, 2
	je		.spells
	cmp		rax, 3
	je		.potions
	cmp		rax, 4
	je		.defend
	TEXT	"menu.invalid"
	PRINTF	`  %s\n`, rax
	jmp		.done
.quit:
	mov		qword [quit], 1
	jmp		.done
.attack:
	mov		edi, CMD_ATTACK
	call	step
	jmp		.done
.defend:
	mov		edi, CMD_DEFEND
	call	step
	jmp		.done
.spells:
	call	list_spells
	mov		r12, rax
	MENU_LINE 0, "menu.back"
	call	read_number
	dec		rax
	cmp		rax, r12
	jae		.done
	mov		edi, CMD_CAST
	mov		rsi, [choices + rax * 8]
	call	step
	jmp		.done
.potions:									; the potions the player owns
	xor		r12d, r12d
	xor		r13d, r13d
	lea		rbx, [potions]
.potion:
	cmp		qword [g_player + Player.potions + r12 * 8], 0
	jle		.potion_next
	mov		[choices + r13 * 8], r12
	inc		r13
	PRINTF	` %ld) %s x%ld\n`, r13, [rbx + Potion.name], [g_player + Player.potions + r12 * 8]
.potion_next:
	add		rbx, Potion_size
	inc		r12
	cmp		r12, POTION_COUNT
	jb		.potion
	test	r13, r13
	jnz		.potion_choice
	TEXT	"battle.no_potions"
	PRINTF	`  %s\n`, rax
	jmp		.done
.potion_choice:
	MENU_LINE 0, "menu.back"
	call	read_number
	dec		rax
	cmp		rax, r13
	jae		.done
	mov		edi, CMD_POTION
	mov		rsi, [choices + rax * 8]
	call	step
.done:
	LEAVE_FRAME

; ── victory and game over ─────────────────────────────────────────────────────

; victory_menu() — after the final boss: end the run as a winner or continue endlessly.
victory_menu:
	ENTER_FRAME
	TEXT	"victory.title"
	PRINTF	`\n*** %s ***\n`, rax
	TEXT	"victory.choice"
	PRINTF	`%s\n`, rax
	MENU_LINE 1, "victory.end_run"
	MENU_LINE 2, "victory.continue"
	call	read_number
	mov		edi, CMD_END_RUN
	cmp		rax, 1
	je		.step
	mov		edi, CMD_CONTINUE_RUN
	cmp		rax, 2
	jne		.done							; anything else: the menu is shown again
.step:
	call	step
.done:
	LEAVE_FRAME

; game_over() — the closing summary of the run. Frame: r12 = total kills.
game_over:
	ENTER_FRAME
	lea		rdi, [s_key_lost]
	lea		rax, [s_key_won]
	cmp		qword [g_run + RunState.won], 0
	cmovne	rdi, rax
	call	t
	PRINTF	`\n*** %s ***\n`, rax
	xor		r12d, r12d						; total kills = sum of the per-creature counters
	xor		ecx, ecx
.kills:
	add		r12, [g_stats + Stats.kills + rcx * 8]
	inc		rcx
	cmp		rcx, CREATURE_COUNT
	jb		.kills
	PRINTF	"Round %ld | Level %ld | Damage dealt %ld | Monsters killed %ld", [g_run + RunState.round], \
			[g_player + Player.level], [g_stats + Stats.damage_dealt], r12
	PRINTF	` | Elites %ld | Bosses %ld\n`, [g_stats + Stats.elites_killed], [g_stats + Stats.bosses_killed]
	PRINTF	`Seed %ld\n`, [g_run + RunState.seed]
	LEAVE_FRAME

; ── main loop ─────────────────────────────────────────────────────────────────

; text_ui_run(rdi = --seed value, rsi = 1 when --seed was given) → eax = exit code.
; Without --seed the seed comes from the clock: the only place where the program reads the time, and it is
; outside the engine.
text_ui_run:
	ENTER_FRAME
	mov		rbx, rdi
	test	rsi, rsi
	jnz		.start
	xor		edi, edi
	call	time
	mov		ebx, eax						; keep it in 32 bits, like the PRNG state
.start:
	mov		rdi, rbx
	call	new_run
.loop:
	cmp		qword [quit], 0
	jne		.bye
	mov		rax, [g_run + RunState.phase]
	cmp		rax, PHASE_GAME_OVER
	je		.over
	cmp		rax, PHASE_BATTLE
	je		.battle
	cmp		rax, PHASE_VICTORY
	je		.victory
	call	merchant_menu
	jmp		.loop
.battle:
	call	battle_menu
	jmp		.loop
.victory:
	call	victory_menu
	jmp		.loop
.over:
	call	game_over
.bye:
	TEXT	"app.bye"
	PRINTF	`%s\n`, rax
	xor		eax, eax
	LEAVE_FRAME
