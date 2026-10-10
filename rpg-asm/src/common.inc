; common.inc — included first by every source file: conventions, macros and the shared declarations.
;
; CALLING CONVENTION (System V AMD64, the one libc uses, kept for our own routines too):
;   - integer arguments, in order: rdi, rsi, rdx, rcx, r8, r9; result in rax (a second result, when a routine
;     documents one, in rdx);
;   - callee-saved (a routine must return them unchanged): rbx, rbp, r12, r13, r14, r15;
;   - everything else (rax, rcx, rdx, rsi, rdi, r8-r11, flags) may be destroyed by ANY call, unless the header of
;     the callee says otherwise. Values that must survive a call live in rbx/r12-r15 or in the stack frame;
;   - rsp must be a multiple of 16 at every `call` (libc uses SSE instructions that fault otherwise). ENTER_FRAME
;     below guarantees it;
;   - variadic libc functions (printf, snprintf, sscanf) read the number of vector arguments from al: `xor eax, eax`
;     before the call.
;
; ADDRESSING: `default rel` makes `[label]` RIP-relative. `[label + reg*8]` cannot be RIP-relative, so NASM emits
; a 32-bit absolute address for it: that is why the program is linked as a non-PIE executable (see the Makefile).
;
; All game values are 64-bit integers; percentages floor through unsigned `div` (docs/game-design.md: integer
; math only, `pct(v, p) = floor(v * p / 100)`).
%ifndef COMMON_INC
%define COMMON_INC

default rel
bits 64

%include "version.inc"
%include "domain/enums.inc"
%include "gamedata.inc"
%include "domain/definitions.inc"
%include "domain/entities.inc"
%include "application/events.inc"
%include "application/commands.inc"

; Tells the linker this object needs no executable stack (silences a warning; the data is read by `ld`, not us).
section .note.GNU-stack noalloc noexec nowrite progbits

; ── routine frame ─────────────────────────────────────────────────────────────
; Every routine that calls another one uses the same frame, so the stack is easy to picture:
;
;   [rbp + 8]   return address          (rsp ≡ 8 mod 16 right after the `call` that got us here)
;   [rbp]       caller's rbp
;   [rbp - 8]   rbx  ┐
;   [rbp - 16]  r12  │ the five callee-saved registers: the body may use all of them freely
;   [rbp - 24]  r13  │
;   [rbp - 32]  r14  │
;   [rbp - 40]  r15  ┘
;   [rsp ...]   8 bytes of padding + the locals asked for (a multiple of 16), addressed as [rsp + n]
;
; Six pushes after the return address leave rsp ≡ 8 mod 16; the extra 8 bytes realign it to 16.
%macro ENTER_FRAME 0-1 0
	push	rbp
	mov		rbp, rsp
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	sub		rsp, 8 + %1
%endmacro

%macro LEAVE_FRAME 0
	lea		rsp, [rbp - 40]
	pop		r15
	pop		r14
	pop		r13
	pop		r12
	pop		rbx
	pop		rbp
	ret
%endmacro

; ROW dst, table, index, RowSize — address of row `index` of a definition table: dst = table + index * RowSize.
; `dst` and `index` may be the same register.
%macro ROW 4
	imul	%1, %3, %4
	add		%1, %2
%endmacro

; pct in place: reg = floor(reg * percent / 100). Destroys rax, rcx, rdx (reg may be rax).
%macro PCT 2
	mov		rax, %1
	imul	rax, %2
	xor		edx, edx
	mov		ecx, 100
	div		rcx
	mov		%1, rax
%endmacro

; ASSERT_TABLE start, rows, RowSize — fails the build unless exactly `rows` rows were written since `start`.
; (`times` rejects a negative count, and one of the two counts is negative when the size is wrong.)
%macro ASSERT_TABLE 3
	times (($ - %1) - %2 * %3) db 0
	times (%2 * %3 - ($ - %1)) db 0
%endmacro

; PRINTF "format", arg1, ... (up to 5) — printf with the format written in place (stored in .rodata). Write the
; format between backquotes to use C escapes such as \n.
; The arguments are loaded left to right into rsi, rdx, rcx, r8, r9: never pass one of those registers after it
; was overwritten by an earlier argument (values usually come from callee-saved registers or memory).
; Destroys every caller-saved register, like any call.
%macro PRINTF 1-6
	%push printf
	[section .rodata.str]
	%$format: db %1, 0
	__SECT__
	lea		rdi, [%$format]
	%if %0 > 1
		mov		rsi, %2
	%endif
	%if %0 > 2
		mov		rdx, %3
	%endif
	%if %0 > 3
		mov		rcx, %4
	%endif
	%if %0 > 4
		mov		r8, %5
	%endif
	%if %0 > 5
		mov		r9, %6
	%endif
	xor		eax, eax
	call	printf
	%pop
%endmacro

; STRING label, `text` [, more pieces] — a NUL-terminated string constant in .rodata, usable from any section.
%macro STRING 2+
	[section .rodata.str]
	%1: db %2, 0
	__SECT__
%endmacro

; ── libc ──────────────────────────────────────────────────────────────────────
extern printf, fprintf, snprintf, puts, putchar, fputs, fflush, fopen, fclose, fgets, exit, abort
extern strcmp, strncmp, strlen, strtol, strtoul, strchr, strtok, strncpy, memset, memcpy
extern malloc, free, qsort, time
extern stdin, stdout, stderr

%endif
