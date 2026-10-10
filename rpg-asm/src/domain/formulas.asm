; domain/formulas.asm — pure integer formulas of docs/game-design.md (domain/formulas.py in the reference).
;
; Role: no randomness, no state. Every division is an unsigned 64-bit `div` (or a signed `idiv` on values that are
; never negative), which truncates exactly like the `//` of the reference on non-negative numbers.
;
; Register use: all leaves, no stack frame. Each routine lists what it destroys; rdi and rsi are never preserved.
%include "common.inc"

global pct, xp_for_level, mana_for_magic_level, spell_level_for_uses, armor_mitigation, round_info

section .text

; pct(rdi = value, rsi = percent) → rax = floor(value * percent / 100). Both operands are non-negative.
; Destroys rcx, rdx.
pct:
	mov		rax, rdi
	imul	rax, rsi
	xor		edx, edx
	mov		ecx, 100
	div		rcx
	ret

; xp_for_level(rdi = level) → rax = total experience needed to reach `level` (Tibia formula):
; floor(50 * (L^3 - 6 L^2 + 17 L - 12) / 3), and 0 for level <= 1.
; Destroys rcx, rdx.
xp_for_level:
	xor		eax, eax
	cmp		rdi, 1
	jle		.done
	mov		rax, rdi
	imul	rax, rdi				; L^2
	mov		rcx, rax
	imul	rax, rdi				; L^3
	imul	rcx, rcx, 6
	sub		rax, rcx				; L^3 - 6 L^2
	imul	rcx, rdi, 17
	add		rax, rcx
	sub		rax, 12
	imul	rax, rax, 50
	xor		edx, edx
	mov		ecx, 3
	div		rcx						; the polynomial is positive from level 2 on
.done:
	ret

; mana_for_magic_level(rdi = magic level) → rax = total mana spent needed to leave that magic level:
; cost(1) = base, cost(n + 1) = pct(cost(n), growthPct); the result is cost(1) + ... + cost(level).
; Destroys rcx, rdx, rsi, r8.
mana_for_magic_level:
	mov		esi, BAL_MAGIC_LEVEL_BASE	; rsi = cost of the current step
	mov		r8, rsi						; r8 = running total
	mov		ecx, 100
.step:
	dec		rdi
	jle		.done
	mov		rax, rsi
	imul	rax, rax, BAL_MAGIC_LEVEL_GROWTH_PCT
	xor		edx, edx
	div		rcx
	mov		rsi, rax
	add		r8, rax
	jmp		.step
.done:
	mov		rax, r8
	ret

; spell_level_for_uses(rdi = uses) → rax = pointer to the SpellLevel row reached with that many uses: the last
; row whose `uses` threshold is met (the first row is the default).
; Destroys rcx, rdx.
spell_level_for_uses:
	lea		rax, [spell_levels]
	mov		rdx, rax
	mov		ecx, SPELL_LEVEL_COUNT
.next:
	cmp		rdi, [rdx + SpellLevel.uses]
	cmovge	rax, rdx
	add		rdx, SpellLevel_size
	dec		ecx
	jnz		.next
	ret

; armor_mitigation(rdi = damage, rsi = armor) → rax = floor(damage * 100 / (100 + armor)).
; Destroys rdx, rsi.
armor_mitigation:
	imul	rax, rdi, 100
	add		rsi, 100
	xor		edx, edx
	div		rsi
	ret

; round_info(rdi = round, >= 1) → rax = tier index, rdx = cycle, rcx = position in the tier,
; r8 = 1 when it is the boss fight (the last position), else 0   (docs/game-design.md §3).
; Destroys rsi, r9.
round_info:
	lea		rax, [rdi - 1]			; index = round - 1
	xor		edx, edx
	mov		esi, BAL_ROUNDS_PER_TIER
	div		rsi						; rax = index / perTier, rdx = position
	mov		rcx, rdx
	xor		r8d, r8d
	cmp		rcx, BAL_ROUNDS_PER_TIER - 1
	sete	r8b
	xor		edx, edx
	mov		esi, TIER_COUNT
	div		rsi						; rax = cycle, rdx = tier
	mov		r9, rax
	mov		rax, rdx
	mov		rdx, r9
	ret
