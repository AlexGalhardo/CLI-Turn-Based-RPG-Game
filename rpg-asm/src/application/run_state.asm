; application/run_state.asm — the storage of the current run and the small list operations on it.
;
; Role: reserves the four state blocks described in domain/entities.inc and implements what the reference gets
; from Python lists: append/remove on the bag, lookup by uid, removal from a status list.
;
; Register use: leaves without a stack frame (except clamp_resources). Each routine lists what it destroys.
%include "common.inc"

extern build_sheet, g_sheet

global g_run, g_player, g_monster, g_stats
global take_item_uid, item_copy, bag_find, bag_remove, bag_append, potion_add
global status_find, status_remove, clamp_resources

section .bss align=16
g_run:		resb RunState_size
g_player:	resb Player_size
g_monster:	resb MonsterInstance_size
g_stats:	resb Stats_size

section .text

; take_item_uid() → rax = the next item uid; the counter advances (deterministic item ids).
take_item_uid:
	mov		rax, [g_run + RunState.next_item_uid]
	inc		qword [g_run + RunState.next_item_uid]
	ret

; item_copy(rdi = destination ItemInstance, rsi = source ItemInstance). Destroys rcx, rsi, rdi.
item_copy:
	mov		ecx, ItemInstance_size / 8
	rep movsq
	ret

; bag_find(rdi = uid) → rax = index of that item in the bag, or -1. Destroys rcx, rdx.
bag_find:
	xor		eax, eax
	lea		rdx, [g_player + Player.bag]
	mov		rcx, [g_player + Player.bag_count]
.next:
	cmp		rax, rcx
	jae		.missing
	cmp		[rdx + ItemInstance.uid], rdi
	je		.done
	add		rdx, ItemInstance_size
	inc		rax
	jmp		.next
.missing:
	mov		rax, -1
.done:
	ret

; bag_remove(rdi = index) — removes one item, keeping the order of the others (list.remove in the reference).
; Destroys rax, rcx, rsi, rdi.
bag_remove:
	mov		rcx, [g_player + Player.bag_count]
	dec		rcx
	mov		[g_player + Player.bag_count], rcx
	sub		rcx, rdi						; items after the removed one
	imul	rcx, rcx, ItemInstance_size / 8	; qwords to move down
	imul	rax, rdi, ItemInstance_size
	lea		rdi, [g_player + Player.bag]
	add		rdi, rax
	lea		rsi, [rdi + ItemInstance_size]
	rep movsq								; forward copy: safe because the destination is below the source
	ret

; bag_append(rdi = ItemInstance to copy at the end of the bag). The caller checked the capacity.
; Destroys rax, rcx, rsi, rdi.
bag_append:
	mov		rsi, rdi
	mov		rax, [g_player + Player.bag_count]
	inc		qword [g_player + Player.bag_count]
	imul	rax, rax, ItemInstance_size
	lea		rdi, [g_player + Player.bag]
	add		rdi, rax
	jmp		item_copy

; potion_add(rdi = potion row, rsi = quantity) — also marks the potion as known (see Player.potion_known).
potion_add:
	lea		rax, [g_player + Player.potions]
	add		[rax + rdi * 8], rsi
	lea		rax, [g_player + Player.potion_known]
	mov		qword [rax + rdi * 8], 1
	ret

; status_find(rdi = StatusList, rsi = status row) → rax = pointer to that ActiveStatus, or 0. Destroys rcx.
status_find:
	mov		rcx, [rdi + StatusList.count]
	lea		rax, [rdi + StatusList.items]
.next:
	test	rcx, rcx
	jz		.missing
	cmp		[rax + ActiveStatus.status], rsi
	je		.done
	add		rax, ActiveStatus_size
	dec		rcx
	jmp		.next
.missing:
	xor		eax, eax
.done:
	ret

; status_remove(rdi = StatusList, rsi = index) — removes one entry, keeping the order of the others.
; Destroys rax, rcx, rsi, rdi.
status_remove:
	mov		rcx, [rdi + StatusList.count]
	dec		rcx
	mov		[rdi + StatusList.count], rcx
	sub		rcx, rsi
	imul	rcx, rcx, ActiveStatus_size / 8
	imul	rax, rsi, ActiveStatus_size
	lea		rdi, [rdi + StatusList.items + rax]
	lea		rsi, [rdi + ActiveStatus_size]
	rep movsq
	ret

; clamp_resources() — HP and MP never exceed the current maximum (after equipment changes and victories).
clamp_resources:
	sub		rsp, 8
	call	build_sheet
	add		rsp, 8
	mov		rax, [g_sheet + Sheet.max_hp]
	cmp		[g_player + Player.hp], rax
	jle		.mp
	mov		[g_player + Player.hp], rax
.mp:
	mov		rax, [g_sheet + Sheet.max_mp]
	cmp		[g_player + Player.mp], rax
	jle		.done
	mov		[g_player + Player.mp], rax
.done:
	ret
