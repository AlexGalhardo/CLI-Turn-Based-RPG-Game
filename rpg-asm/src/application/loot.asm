; application/loot.asm — the item factory: base item + rarity + affixes (application/loot.py; docs/game-design.md §8
; "Item generation"). Every rng call here is part of the contract, in this exact order.
;
; Role: `generate_item` fills an ItemInstance for a vocation and a round tier. Candidates are needed "sorted by
; id": the generator already sorted the row indexes (`items_by_id`, `affixes_by_id`), so filtering those lists
; in order gives the sorted candidates without comparing strings at run time.
;
; Register use: can_use is a leaf that destroys only rax, rcx, rdx. roll_rarity calls the rng leaves. generate_item
; uses the standard frame with 32 bytes of locals.
%include "common.inc"

extern rng_roll, rng_weighted

global can_use, roll_rarity, generate_item

section .bss align=16
candidates:		resq ITEM_COUNT		; item rows that fit the vocation and tier
affix_pool:		resq AFFIX_COUNT	; affix rows still allowed on the item
rarity_rows:	resq RARITY_COUNT	; rarities with a positive weight...
rarity_weights:	resq RARITY_COUNT	; ...and their weights

section .text

; can_use(rdi = Item row, rsi = Vocation row) → eax = 1 when the vocation can wear the item.
; Weapons and shields are restricted by item type (a bit mask per vocation); everything else fits everyone.
; Destroys rcx, rdx only.
can_use:
	mov		eax, 1
	mov		rcx, [rdi + Item.slot]
	cmp		rcx, SLOT_WEAPON
	je		.weapon
	cmp		rcx, SLOT_SHIELD
	jne		.done
	mov		rdx, [rsi + Vocation.shield_mask]
	jmp		.test
.weapon:
	mov		rdx, [rsi + Vocation.weapon_mask]
.test:
	mov		rcx, [rdi + Item.type]
	bt		rdx, rcx						; carry = bit `type` of the mask
	setc	al
.done:
	ret

; roll_rarity(rdi = pointer to RARITY_COUNT weights) → rax = rarity row.
; Weighted roll in the order of balance.rarities; zero weights are skipped and a single option is NOT rolled.
roll_rarity:
	xor		ecx, ecx
	xor		r8d, r8d						; r8 = number of options
.collect:
	mov		rax, [rdi + rcx * 8]
	test	rax, rax
	jle		.skip
	mov		[rarity_weights + r8 * 8], rax
	mov		[rarity_rows + r8 * 8], rcx
	inc		r8
.skip:
	inc		rcx
	cmp		rcx, RARITY_COUNT
	jb		.collect
	xor		eax, eax
	cmp		r8, 1
	jbe		.single
	lea		rdi, [rarity_weights]
	mov		rsi, r8
	call	rng_weighted
.single:
	mov		rax, [rarity_rows + rax * 8]
	ret

; generate_item(rdi = Vocation row, rsi = tier, rdx = rarity weights, rcx = uid, r8 = destination ItemInstance)
; → eax = 1 when an item was generated, 0 (consuming no randomness) when no item fits the vocation and tier.
;
; Frame: r12 = vocation, then affixes left to roll; r13 = tier; r14 = weights, then rarity row;
; r15 = destination; rbx = base item row, then the picked Affix row.
; Locals: [rsp] = slot of the base item, [rsp + 8] = bit mask of the stats already used by an affix.
generate_item:
	ENTER_FRAME 32
	mov		r12, rdi
	mov		r13, rsi
	mov		r14, rdx
	mov		r15, r8
	mov		[r15 + ItemInstance.uid], rcx

	; 1. candidates: items of tier [tier - 1, tier] (clamped at 0) the vocation can use, sorted by id
	mov		r11, r13
	dec		r11
	jns		.lowest_ok
	xor		r11d, r11d
.lowest_ok:
	xor		r9d, r9d						; r9 = position in items_by_id
	xor		r10d, r10d						; r10 = number of candidates
.candidate:
	mov		rax, [items_by_id + r9 * 8]
	ROW		rdi, items, rax, Item_size
	mov		rcx, [rdi + Item.tier]
	cmp		rcx, r11
	jl		.candidate_next
	cmp		rcx, r13
	jg		.candidate_next
	mov		rsi, r12
	call	can_use
	test	eax, eax
	jz		.candidate_next
	mov		rax, [items_by_id + r9 * 8]
	mov		[candidates + r10 * 8], rax
	inc		r10
.candidate_next:
	inc		r9
	cmp		r9, ITEM_COUNT
	jb		.candidate
	xor		eax, eax
	test	r10, r10
	jz		.return

	xor		edi, edi
	lea		rsi, [r10 - 1]
	call	rng_roll						; pick(candidates)
	mov		rbx, [candidates + rax * 8]

	; 2. rarity
	mov		rdi, r14
	call	roll_rarity
	mov		r14, rax
	mov		[r15 + ItemInstance.item], rbx
	mov		[r15 + ItemInstance.rarity], r14
	mov		[r15 + ItemInstance.tier], r13
	mov		qword [r15 + ItemInstance.affix_count], 0

	; 3. affix count: always rolled, even when min == max (the roll consumes a number)
	ROW		rax, rarities, r14, Rarity_size
	mov		rdi, [rax + Rarity.affix_min]
	mov		rsi, [rax + Rarity.affix_max]
	call	rng_roll
	mov		r12, rax

	ROW		rax, items, rbx, Item_size
	mov		rax, [rax + Item.slot]
	mov		[rsp], rax
	mov		qword [rsp + 8], 0

	; 4. each affix: pool = affixes allowed on the slot whose stat is not on the item yet, sorted by id
.affix:
	test	r12, r12
	jz		.generated
	mov		r8, [rsp]
	mov		r11, [rsp + 8]
	xor		r9d, r9d
	xor		r10d, r10d
.pool:
	mov		rax, [affixes_by_id + r9 * 8]
	ROW		rcx, affixes, rax, Affix_size
	mov		rdx, [rcx + Affix.slot_mask]
	bt		rdx, r8
	jnc		.pool_next
	mov		rdx, [rcx + Affix.stat]
	bt		r11, rdx
	jc		.pool_next
	mov		[affix_pool + r10 * 8], rax
	inc		r10
.pool_next:
	inc		r9
	cmp		r9, AFFIX_COUNT
	jb		.pool
	test	r10, r10
	jz		.generated						; nothing left to roll: the item keeps the affixes it has

	xor		edi, edi
	lea		rsi, [r10 - 1]
	call	rng_roll						; pick(pool)
	mov		rax, [affix_pool + rax * 8]
	ROW		rbx, affixes, rax, Affix_size
	mov		rdx, [rbx + Affix.stat]
	bts		qword [rsp + 8], rdx			; the stat is now used
	mov		rdi, [rbx + Affix.min]
	mov		rsi, [rbx + Affix.max]
	call	rng_roll
	mov		rcx, [rbx + Affix.per_tier]
	imul	rcx, r13
	add		rax, rcx						; value = roll(min, max) + tier × perTier

	mov		rcx, [r15 + ItemInstance.affix_count]
	inc		qword [r15 + ItemInstance.affix_count]
	imul	rcx, rcx, AffixRoll_size
	mov		rdx, [rbx + Affix.stat]
	mov		[r15 + ItemInstance.affixes + rcx + AffixRoll.stat], rdx
	mov		[r15 + ItemInstance.affixes + rcx + AffixRoll.value], rax
	dec		r12
	jmp		.affix

.generated:
	mov		eax, 1
.return:
	LEAVE_FRAME
