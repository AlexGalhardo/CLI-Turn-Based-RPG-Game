; application/commands.inc — player commands (application/commands.py in the reference).
;
; Role: a command is its type plus at most two integer arguments, passed to `engine_step` in rdi, rsi, rdx.
; Ids become row indexes before they reach the engine; an id that does not exist is the index -1, which the engine
; answers with `unknown_spell` / `unknown_potion` like the reference does for an unknown string.
%ifndef COMMANDS_INC
%define COMMANDS_INC

;                               arguments (rsi, rdx)
CMD_ATTACK			equ 0
CMD_CAST			equ 1	; spell row
CMD_POTION			equ 2	; potion row
CMD_DEFEND			equ 3
CMD_NEXT_FIGHT		equ 4
CMD_BUY_POTION		equ 5	; potion row, quantity
CMD_SELL_ITEM		equ 6	; item uid
CMD_EQUIP			equ 7	; item uid
CMD_UNEQUIP			equ 8	; SLOT_*
CMD_BUY_STOCK_ITEM	equ 9	; index in the merchant stock
CMD_END_RUN			equ 10
CMD_CONTINUE_RUN	equ 11
CMD_COUNT			equ 12

; One row of `command_schema` (application/events.asm): the JSON type name and the kind (KIND_*) of each argument.
struc CommandSchema
	.name		resq 1
	.arg1		resq 1
	.arg2		resq 1
endstruc

; Result of a battle turn (BattleOutcome in the reference).
OUTCOME_ONGOING		equ 0
OUTCOME_VICTORY		equ 1
OUTCOME_DEFEAT		equ 2

%endif
