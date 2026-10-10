; application/merchant.asm — the merchant phase: potions, bag, equipment and the rotating stock
; (application/merchant.py; docs/game-design.md §10).
;
; Role: `merchant_enter` generates the stock (the only merchant code that consumes randomness);
; `merchant_handle` runs one merchant command. A command that fails emits one `error` event and changes nothing.
;
; Register use: every routine uses the standard frame. The handlers are reached with `jmp` from merchant_handle,
; so they share its frame: r12 = first argument, r13 = second argument.
%include "common.inc"

extern g_run, g_player
extern round_info, generate_item, can_use, item_value, required_level, pct
extern take_item_uid, item_copy, bag_find, bag_remove, bag_append, potion_add, clamp_resources
extern auto_equip, event_new

global merchant_enter, merchant_handle, stock_price

MAX_POTIONS_PER_PURCHASE equ 99

section .bss align=16
moved_item:	resb ItemInstance_size		; the item being moved between bag, equipment and stock

section .text

; stock_price(rdi = ItemInstance) → rax = pct(item value, merchantMarkupPct).
stock_price:
	sub		rsp, 8
	call	item_value
	mov		rdi, rax
	mov		esi, BAL_MERCHANT_MARKUP_PCT
	call	pct
	add		rsp, 8
	ret

; merchant_enter() — generates the rotating stock for the tier of the NEXT round, then emits merchant_entered.
; Frame: rbx = Vocation row, r12 = tier, r13 = items left to generate.
merchant_enter:
	ENTER_FRAME
	mov		rbx, [g_player + Player.vocation]
	ROW		rbx, vocations, rbx, Vocation_size
	mov		rdi, [g_run + RunState.round]
	inc		rdi
	call	round_info
	mov		r12, rax
	mov		qword [g_run + RunState.stock_count], 0
	mov		r13d, BAL_MERCHANT_STOCK_SIZE
.generate:
	mov		rdi, rbx
	mov		rsi, r12
	lea		rdx, [merchant_rarity_weights]
	mov		rcx, [g_run + RunState.next_item_uid]
	imul	r8, [g_run + RunState.stock_count], ItemInstance_size
	lea		rax, [g_run + RunState.stock]
	add		r8, rax
	call	generate_item
	test	eax, eax
	jz		.next							; no candidate: nothing generated, no uid taken
	call	take_item_uid
	inc		qword [g_run + RunState.stock_count]
.next:
	dec		r13
	jnz		.generate
	mov		rsi, [g_run + RunState.round]
	EMIT	EV_MERCHANT_ENTERED, rsi
	LEAVE_FRAME

; merchant_handle(rdi = CMD_BUY_POTION / CMD_SELL_ITEM / CMD_EQUIP / CMD_UNEQUIP / CMD_BUY_STOCK_ITEM,
;                 rsi = first argument, rdx = second argument)
merchant_handle:
	ENTER_FRAME
	mov		r12, rsi
	mov		r13, rdx
	cmp		rdi, CMD_BUY_POTION
	je		buy_potion
	cmp		rdi, CMD_SELL_ITEM
	je		sell
	cmp		rdi, CMD_EQUIP
	je		equip
	cmp		rdi, CMD_UNEQUIP
	je		unequip
	jmp		buy_stock

; Every handler ends here. `fail` expects the ERR_* code in esi.
fail:
	EMIT	EV_ERROR, rsi
finish:
	LEAVE_FRAME

; buy_potion: r12 = potion row (-1 when the id is unknown), r13 = quantity.
buy_potion:
	mov		esi, ERR_UNKNOWN_POTION
	cmp		r12, POTION_COUNT				; unsigned: also rejects -1
	jae		fail
	; available = unlocked for the NEXT round (the player shops before the fight)
	ROW		rbx, potions, r12, Potion_size
	mov		rax, [g_run + RunState.round]
	inc		rax
	mov		esi, ERR_POTION_LOCKED
	cmp		[rbx + Potion.unlock_round], rax
	jg		fail
	mov		esi, ERR_INVALID_QUANTITY
	cmp		r13, 1
	jl		fail
	cmp		r13, MAX_POTIONS_PER_PURCHASE
	jg		fail
	mov		r14, [rbx + Potion.price]
	imul	r14, r13						; r14 = cost
	mov		esi, ERR_NOT_ENOUGH_GOLD
	cmp		[g_player + Player.gold], r14
	jl		fail
	sub		[g_player + Player.gold], r14
	mov		rdi, r12
	mov		rsi, r13
	call	potion_add
	EMIT	EV_POTION_BOUGHT, r12, r13, r14
	jmp		finish

; sell: r12 = uid. Only bag items can be sold (an equipped uid is invalid_item).
sell:
	mov		rdi, r12
	call	bag_find
	mov		esi, ERR_INVALID_ITEM
	cmp		rax, 0
	jl		fail
	mov		r13, rax						; r13 = bag index
	imul	rbx, r13, ItemInstance_size
	lea		rax, [g_player + Player.bag]
	add		rbx, rax						; rbx = the item in the bag
	mov		r14, [rbx + ItemInstance.item]
	mov		rdi, rbx
	call	item_value
	mov		r15, rax
	mov		rdi, r13
	call	bag_remove
	add		[g_player + Player.gold], r15
	EMIT	EV_ITEM_SOLD, r12, r14, r15
	jmp		finish

; equip: r12 = uid. Checks, in this order: invalid_item, cannot_equip, level_too_low.
equip:
	mov		rdi, r12
	call	bag_find
	mov		esi, ERR_INVALID_ITEM
	cmp		rax, 0
	jl		fail
	mov		r13, rax						; r13 = bag index
	imul	rbx, r13, ItemInstance_size
	lea		rax, [g_player + Player.bag]
	add		rbx, rax						; rbx = the item in the bag
	mov		r14, [rbx + ItemInstance.item]
	ROW		r14, items, r14, Item_size		; r14 = Item row
	mov		rdi, r14
	mov		rsi, [g_player + Player.vocation]
	ROW		rsi, vocations, rsi, Vocation_size
	call	can_use
	mov		esi, ERR_CANNOT_EQUIP
	test	eax, eax
	jz		fail
	mov		rdi, rbx
	call	required_level
	mov		esi, ERR_LEVEL_TOO_LOW
	cmp		rax, [g_player + Player.level]
	jg		fail

	; move the item out of the bag, the previous one (if any) into it, then fill the slot
	lea		rdi, [moved_item]
	mov		rsi, rbx
	call	item_copy
	mov		rdi, r13
	call	bag_remove
	mov		r15, [r14 + Item.slot]			; r15 = slot
	imul	rbx, r15, ItemInstance_size
	lea		rax, [g_player + Player.equipment]
	add		rbx, rax						; rbx = the equipment slot
	cmp		qword [rbx + ItemInstance.uid], 0
	je		.fill
	mov		rdi, rbx
	call	bag_append
	mov		rsi, [rbx + ItemInstance.uid]
	mov		rdx, [rbx + ItemInstance.item]
	EMIT	EV_ITEM_UNEQUIPPED, rsi, rdx, r15
.fill:
	mov		rdi, rbx
	lea		rsi, [moved_item]
	call	item_copy
	mov		rsi, [rbx + ItemInstance.uid]
	mov		rdx, [rbx + ItemInstance.item]
	EMIT	EV_ITEM_EQUIPPED, rsi, rdx, r15
	call	clamp_resources
	jmp		finish

; unequip: r12 = SLOT_*.
unequip:
	imul	rbx, r12, ItemInstance_size
	lea		rax, [g_player + Player.equipment]
	add		rbx, rax						; rbx = the equipment slot
	mov		esi, ERR_INVALID_ITEM
	cmp		qword [rbx + ItemInstance.uid], 0
	je		fail
	mov		esi, ERR_BAG_FULL
	cmp		qword [g_player + Player.bag_count], BAL_BAG_CAPACITY
	jge		fail
	mov		rdi, rbx
	call	bag_append
	mov		r13, [rbx + ItemInstance.uid]
	mov		r14, [rbx + ItemInstance.item]
	mov		qword [rbx + ItemInstance.uid], 0	; the slot is empty again
	call	clamp_resources
	EMIT	EV_ITEM_UNEQUIPPED, r13, r14, r12
	jmp		finish

; buy_stock: r12 = index in the stock. Checks, in this order: invalid_item, bag_full, not_enough_gold.
buy_stock:
	mov		esi, ERR_INVALID_ITEM
	cmp		r12, [g_run + RunState.stock_count]	; unsigned: also rejects negative indexes
	jae		fail
	mov		esi, ERR_BAG_FULL
	cmp		qword [g_player + Player.bag_count], BAL_BAG_CAPACITY
	jge		fail
	imul	rbx, r12, ItemInstance_size
	lea		rax, [g_run + RunState.stock]
	add		rbx, rax						; rbx = the item in the stock
	mov		rdi, rbx
	call	stock_price
	mov		r13, rax						; r13 = price
	mov		esi, ERR_NOT_ENOUGH_GOLD
	cmp		[g_player + Player.gold], r13
	jl		fail
	sub		[g_player + Player.gold], r13
	mov		r14, [rbx + ItemInstance.uid]
	mov		r15, [rbx + ItemInstance.item]
	mov		rdi, rbx
	call	bag_append
	; remove it from the stock, keeping the order of the others
	mov		rcx, [g_run + RunState.stock_count]
	dec		rcx
	mov		[g_run + RunState.stock_count], rcx
	sub		rcx, r12
	imul	rcx, rcx, ItemInstance_size / 8
	mov		rdi, rbx
	lea		rsi, [rbx + ItemInstance_size]
	rep movsq
	EMIT	EV_ITEM_BOUGHT, r14, r15, r13
	cmp		qword [g_run + RunState.auto_equip], 0
	je		finish
	call	auto_equip						; item_bought first, then the auto-equip events
	jmp		finish
