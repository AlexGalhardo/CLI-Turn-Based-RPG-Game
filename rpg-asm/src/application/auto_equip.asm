; application/auto_equip.asm — auto-equip with auto-sell (application/auto_equip.py; docs/game-design.md §8.1).
;
; Role: for each slot, in a fixed order, wear the best usable bag item when it beats the equipped one, and sell
; the item it replaces. Consumes no randomness.
;
; Register use: both routines use the standard frame with 16 bytes of locals.
%include "common.inc"

extern g_run, g_player, g_event_count
extern can_use, item_score, item_value, required_level
extern item_copy, bag_remove, clamp_resources, event_new

global auto_equip, best_bag_item

section .rodata align=8
; EQUIPMENT_SLOT_ORDER of the reference: the order in which the slots are filled.
equipment_slot_order:
	dq SLOT_WEAPON, SLOT_SHIELD, SLOT_HELMET, SLOT_ARMOR, SLOT_LEGS, SLOT_BOOTS, SLOT_RING, SLOT_AMULET
ASSERT_TABLE equipment_slot_order, SLOT_COUNT, 8

section .bss align=16
replaced_item:	resb ItemInstance_size	; copy of the item taken out of the slot

section .text

; best_bag_item(rdi = SLOT_*) → rax = bag index of the highest-score item the player can wear in that slot now
; (usable by the vocation, level requirement met), or -1; rdx = its score. Ties go to the lowest uid.
; Frame: rbx = current bag item, r12 = slot, r13 = Vocation row, r14 = best index, r15 = current index.
; Locals: [rsp] = best score, [rsp + 8] = uid of the best item.
best_bag_item:
	ENTER_FRAME 16
	mov		r12, rdi
	mov		r13, [g_player + Player.vocation]
	ROW		r13, vocations, r13, Vocation_size
	mov		r14, -1
	xor		r15d, r15d
	lea		rbx, [g_player + Player.bag]
.item:
	cmp		r15, [g_player + Player.bag_count]
	jae		.done
	mov		rdi, [rbx + ItemInstance.item]
	ROW		rdi, items, rdi, Item_size
	cmp		[rdi + Item.slot], r12
	jne		.next
	mov		rsi, r13
	call	can_use
	test	eax, eax
	jz		.next
	mov		rdi, rbx
	call	required_level
	cmp		rax, [g_player + Player.level]
	jg		.next
	mov		rdi, rbx
	call	item_score
	cmp		r14, -1
	je		.take
	cmp		rax, [rsp]
	jg		.take
	jl		.next
	mov		rcx, [rbx + ItemInstance.uid]
	cmp		rcx, [rsp + 8]
	jge		.next
.take:
	mov		r14, r15
	mov		[rsp], rax
	mov		rcx, [rbx + ItemInstance.uid]
	mov		[rsp + 8], rcx
.next:
	add		rbx, ItemInstance_size
	inc		r15
	jmp		.item
.done:
	mov		rax, r14
	mov		rdx, [rsp]
	LEAVE_FRAME

; auto_equip() — emits item_auto_equipped (and item_auto_sold for the replaced item) for every slot improved.
; Frame: rbx = equipment slot, r12 = position in the slot order, r13 = slot, r14 = bag index of the best item,
; r15 = its score. Local: [rsp] = number of events before the routine started.
auto_equip:
	ENTER_FRAME 16
	mov		rax, [g_event_count]
	mov		[rsp], rax
	xor		r12d, r12d
.slot:
	cmp		r12, SLOT_COUNT
	jae		.finish
	mov		r13, [equipment_slot_order + r12 * 8]
	mov		rdi, r13
	call	best_bag_item
	cmp		rax, 0
	jl		.next
	mov		r14, rax
	mov		r15, rdx
	imul	rbx, r13, ItemInstance_size
	lea		rax, [g_player + Player.equipment]
	add		rbx, rax
	cmp		qword [rbx + ItemInstance.uid], 0
	je		.equip							; empty slot: anything is better
	mov		rdi, rbx
	call	item_score
	cmp		r15, rax
	jle		.next							; must BEAT the equipped item
.equip:
	lea		rdi, [replaced_item]
	mov		rsi, rbx
	call	item_copy
	imul	rsi, r14, ItemInstance_size
	lea		rax, [g_player + Player.bag]
	add		rsi, rax
	mov		rdi, rbx
	call	item_copy
	mov		rdi, r14
	call	bag_remove
	mov		rsi, [rbx + ItemInstance.uid]
	mov		rdx, [rbx + ItemInstance.item]
	EMIT	EV_ITEM_AUTO_EQUIPPED, rsi, rdx, r13, r15
	cmp		qword [replaced_item + ItemInstance.uid], 0
	je		.next
	lea		rdi, [replaced_item]
	call	item_value
	add		[g_player + Player.gold], rax
	mov		rsi, [replaced_item + ItemInstance.uid]
	mov		rdx, [replaced_item + ItemInstance.item]
	mov		rcx, rax
	EMIT	EV_ITEM_AUTO_SOLD, rsi, rdx, rcx
.next:
	inc		r12
	jmp		.slot
.finish:
	mov		rax, [g_event_count]
	cmp		rax, [rsp]
	je		.done
	call	clamp_resources					; the new equipment may lower the maximum HP/MP
.done:
	LEAVE_FRAME
