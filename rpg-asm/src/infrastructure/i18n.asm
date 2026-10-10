; infrastructure/i18n.asm — English texts of shared/i18n/en.json (infrastructure/i18n.py in the reference).
;
; Role: the generator embeds en.json as `i18n_table`, an array of (key, text) string pointers. `t` finds a key
; with a linear scan: a few hundred strcmp calls per sentence are nothing for a text UI, and it keeps the lookup
; a dozen lines long. Only English is embedded (the other ports also ship pt-BR).
;
; Register use: standard frame. rbx = current (key, text) pair, r12 = wanted key, r13 = pairs left.
%include "common.inc"

global t

section .text

; t(rdi = key) → rax = the English text of that key, or the key itself when en.json does not have it.
t:
	ENTER_FRAME
	mov		r12, rdi
	lea		rbx, [i18n_table]
	mov		r13d, I18N_COUNT
.pair:
	mov		rdi, [rbx]
	mov		rsi, r12
	call	strcmp
	test	eax, eax
	jz		.found
	add		rbx, 16
	dec		r13
	jnz		.pair
	mov		rax, r12
	jmp		.done
.found:
	mov		rax, [rbx + 8]
.done:
	LEAVE_FRAME
