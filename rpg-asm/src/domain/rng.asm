; domain/rng.asm — mulberry32, the PRNG shared by every implementation (docs/cross-language-parity.md §2).
;
; Role: the only source of randomness of the engine. The state is one unsigned 32-bit number (`g_rng_state`).
; Working on the 32-bit registers (eax, ecx, ...) gives the "mod 2^32" of the specification for free: 32-bit
; `add` and `imul` wrap, and writing a 32-bit register clears the upper half of the 64-bit one.
;
; Register use: these routines are leaves (or call only each other) and use no stack frame. Together they destroy
; only rax, rcx, rdx, rsi, r8, r9, r10 and r11; rdi is preserved by rng_next, rng_roll and rng_chance.
%include "common.inc"

global g_rng_state, rng_seed, rng_next, rng_roll, rng_chance, rng_weighted

section .bss align=16
g_rng_state:	resd 1

section .text

; rng_seed(edi = seed) — the seed is taken modulo 2^32 (only edi is read).
rng_seed:
	mov		[g_rng_state], edi
	ret

; rng_next() → eax = next unsigned 32-bit output (rax zero-extended).
; Destroys rcx, rdx, rsi.
rng_next:
	mov		eax, [g_rng_state]
	add		eax, 0x6D2B79F5			; state = (state + 0x6D2B79F5) mod 2^32
	mov		[g_rng_state], eax
	mov		ecx, eax				; ecx = t
	mov		edx, eax
	shr		edx, 15
	xor		edx, ecx				; edx = t ^ (t >>> 15)
	or		ecx, 1					; ecx = t | 1
	imul	edx, ecx				; t = imul(t ^ (t >>> 15), t | 1)
	mov		ecx, edx				; ecx = t
	mov		eax, edx
	shr		eax, 7
	xor		eax, ecx				; eax = t ^ (t >>> 7)
	mov		esi, ecx
	or		esi, 61					; esi = t | 61
	imul	eax, esi
	add		eax, ecx				; eax = t + imul(t ^ (t >>> 7), t | 61)
	xor		eax, ecx				; t = t ^ (...)
	mov		ecx, eax
	shr		ecx, 14
	xor		eax, ecx				; return t ^ (t >>> 14)
	ret

; rng_roll(rdi = min, rsi = max) → rax = min + next() % (max - min + 1). Requires min <= max.
; Destroys rcx, rdx, rsi, r8, r9.
rng_roll:
	mov		r8, rdi
	mov		r9, rsi
	sub		r9, r8
	inc		r9						; r9 = size of the range
	call	rng_next				; a leaf that uses no SSE: stack alignment does not matter here
	xor		edx, edx
	div		r9						; rdx = next % size (rax already holds the zero-extended output)
	lea		rax, [r8 + rdx]
	ret

; rng_chance(rdi = percent) → eax = 1 with `percent`% probability, else 0.
; A certain outcome (percent <= 0 or >= 100) consumes NO number, so a 0% effect never shifts the sequence.
; Destroys rcx, rdx, rsi, r8, r9.
rng_chance:
	xor		eax, eax
	cmp		rdi, 0
	jle		.done
	mov		eax, 1
	cmp		rdi, 100
	jge		.done
	push	rdi
	mov		edi, 1
	mov		esi, 100
	call	rng_roll
	pop		rdi
	cmp		rax, rdi				; roll(1, 100) <= percent
	setle	al
	movzx	eax, al
.done:
	ret

; rng_weighted(rdi = pointer to qword weights, rsi = count) → rax = index picked with those weights:
; r = roll(1, sum); the first index whose cumulative weight is >= r.
; Destroys rcx, rdx, rsi, rdi, r8-r11.
rng_weighted:
	mov		r10, rdi
	mov		r11, rsi
	xor		eax, eax
	xor		ecx, ecx
.sum:
	add		rax, [r10 + rcx * 8]
	inc		rcx
	cmp		rcx, r11
	jb		.sum
	mov		edi, 1
	mov		rsi, rax
	call	rng_roll				; rax = r
	xor		ecx, ecx
	xor		edx, edx
.scan:
	add		rdx, [r10 + rcx * 8]
	cmp		rdx, rax
	jge		.found
	inc		rcx
	jmp		.scan
.found:
	mov		rax, rcx
	ret
