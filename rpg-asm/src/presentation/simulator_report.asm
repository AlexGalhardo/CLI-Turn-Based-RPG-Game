; presentation/simulator_report.asm — `--simulate N`: runs the simulations and prints the balance report
; (run_simulator in __main__.py and presentation/simulator_report.py in the reference).
;
; Role: the report must be byte-identical to the reference's, which builds a table of strings, pads every column
; to its widest cell and strips the trailing spaces of each line. The same is done here with a static grid of
; fixed-size text cells filled by snprintf.
;
; Register use: standard frames. In the loops over the grid: r12 = row, r13 = column.
%include "common.inc"
%include "application/simulator.inc"

extern simulate, kind_id, kind_name

global simulator_main

COLUMNS		equ 12
CELL_MAX	equ 192		; bytes per cell; the widest is "top killers": three "Name (n)" entries
MAX_ROWS	equ 1 + VOCATION_COUNT * DIFFICULTY_COUNT	; the header + one row per vocation and difficulty
KILLERS_COLUMN equ 11

section .rodata align=8
%macro HEADER_CELL 1
	%push header
	[section .rodata.str]
	%$text: db %1, 0
	__SECT__
	dq %$text
	%pop
%endmacro
header:
	HEADER_CELL "vocation"
	HEADER_CELL "difficulty"
	HEADER_CELL "runs"
	HEADER_CELL "wins"
	HEADER_CELL "win %"
	HEADER_CELL "min"
	HEADER_CELL "p10"
	HEADER_CELL "median"
	HEADER_CELL "p90"
	HEADER_CELL "max"
	HEADER_CELL "avg lvl"
	HEADER_CELL "top killers"
ASSERT_TABLE header, COLUMNS, 8

STRING s_text, "%s"
STRING s_number, "%ld"
STRING s_percent, "%ld%%"
STRING s_killer_first, "%s (%ld)"
STRING s_killer_more, ", %s (%ld)"
STRING s_error_unfinished, `error: a run did not finish\n`

section .bss align=16
summaries:	resb Summary_size * (MAX_ROWS - 1)
cells:		resb CELL_MAX * COLUMNS * MAX_ROWS
widths:		resq COLUMNS

section .text

; cell(rdi = row, rsi = column) → rax = address of that cell's text. Destroys only rax.
cell:
	imul	rax, rdi, COLUMNS
	add		rax, rsi
	imul	rax, rax, CELL_MAX
	add		rax, cells
	ret

; set_cell(rdi = row, rsi = column, rdx = printf format with one conversion, rcx = its argument)
set_cell:
	sub		rsp, 8
	call	cell
	mov		rdi, rax
	mov		esi, CELL_MAX
	xor		eax, eax
	call	snprintf						; snprintf(cell, CELL_MAX, format, argument)
	add		rsp, 8
	ret

; text_width(rdi = UTF-8 text) → rax = number of characters (code points), which is what the reference pads by:
; every byte except the continuation bytes 10xxxxxx starts a character. Destroys rcx, rdi.
text_width:
	xor		eax, eax
.byte:
	movzx	ecx, byte [rdi]
	test	ecx, ecx
	jz		.done
	and		ecx, 0xC0
	cmp		ecx, 0x80
	setne	cl
	movzx	ecx, cl
	add		rax, rcx
	inc		rdi
	jmp		.byte
.done:
	ret

; repeat_char(rdi = character, rsi = times) — prints the character that many times (0 or less: nothing).
repeat_char:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r12, rsi
.next:
	cmp		r12, 0
	jle		.done
	mov		rdi, rbx
	call	putchar
	dec		r12
	jmp		.next
.done:
	LEAVE_FRAME

; fill_row(rdi = row, rsi = Summary) — the twelve cells of one simulation.
; Frame: rbx = Summary, r12 = row, r13 = column / killer index, r14 = bytes written in the killers cell.
fill_row:
	ENTER_FRAME
	mov		r12, rdi
	mov		rbx, rsi
	mov		edi, KIND_VOCATION
	mov		rsi, [rbx + Summary.vocation]
	call	kind_id
	mov		rcx, rax
	mov		rdi, r12
	xor		esi, esi
	lea		rdx, [s_text]
	call	set_cell
	mov		edi, KIND_DIFFICULTY
	mov		rsi, [rbx + Summary.difficulty]
	call	kind_id
	mov		rcx, rax
	mov		rdi, r12
	mov		esi, 1
	lea		rdx, [s_text]
	call	set_cell
	mov		rdi, r12
	mov		esi, 2
	lea		rdx, [s_number]
	mov		rcx, [rbx + Summary.runs]
	call	set_cell
	mov		rdi, r12
	mov		esi, 3
	lea		rdx, [s_number]
	mov		rcx, [rbx + Summary.wins]
	call	set_cell
	imul	rax, [rbx + Summary.wins], 100
	xor		edx, edx
	div		qword [rbx + Summary.runs]		; win rate = floor(wins × 100 / runs)
	mov		rcx, rax
	mov		rdi, r12
	mov		esi, 4
	lea		rdx, [s_percent]
	call	set_cell
	; columns 5-10: min, p10, median, p90, max, avg lvl — consecutive fields of the Summary
	mov		r13d, 5
.number:
	mov		rdi, r12
	mov		rsi, r13
	lea		rdx, [s_number]
	mov		rcx, [rbx + Summary.min_round + r13 * 8 - 5 * 8]
	call	set_cell
	inc		r13
	cmp		r13, KILLERS_COLUMN
	jb		.number
	; top killers: "Name (n), Name (n), Name (n)", empty when nobody died
	mov		rdi, r12
	mov		esi, KILLERS_COLUMN
	call	cell
	mov		byte [rax], 0
	mov		r15, rax
	xor		r13d, r13d
	xor		r14d, r14d
.killer:
	cmp		r13, [rbx + Summary.killer_count]
	jae		.done
	mov		rax, r13
	shl		rax, 4
	mov		edi, KIND_CREATURE
	mov		rsi, [rbx + Summary.killers + rax]
	call	kind_name
	mov		rcx, rax
	mov		rax, r13
	shl		rax, 4
	mov		r8, [rbx + Summary.killers + rax + 8]
	lea		rdi, [r15 + r14]
	mov		esi, CELL_MAX
	sub		rsi, r14
	lea		rdx, [s_killer_first]
	lea		rax, [s_killer_more]
	test	r13, r13
	cmovnz	rdx, rax
	xor		eax, eax
	call	snprintf
	add		r14, rax
	inc		r13
	jmp		.killer
.done:
	LEAVE_FRAME

; print_row(rdi = row) — cells padded to the column widths and joined by two spaces, trailing spaces stripped:
; that is, every cell up to the last non-empty one, the last one unpadded.
; Frame: r12 = row, r13 = column, r14 = last non-empty column.
print_row:
	ENTER_FRAME
	mov		r12, rdi
	mov		r14, -1
	xor		r13d, r13d
.find_last:
	mov		rdi, r12
	mov		rsi, r13
	call	cell
	cmp		byte [rax], 0
	cmovne	r14, r13
	inc		r13
	cmp		r13, COLUMNS
	jb		.find_last
	xor		r13d, r13d
.column:
	cmp		r13, r14
	jg		.end_of_line
	mov		rdi, r12
	mov		rsi, r13
	call	cell
	mov		rbx, rax
	mov		rdi, rax
	mov		rsi, [stdout]
	call	fputs
	cmp		r13, r14
	je		.end_of_line
	mov		rdi, rbx
	call	text_width
	mov		rsi, [widths + r13 * 8]
	sub		rsi, rax
	add		rsi, 2							; padding + the two-space separator
	mov		edi, ' '
	call	repeat_char
	inc		r13
	jmp		.column
.end_of_line:
	mov		edi, 10
	call	putchar
	LEAVE_FRAME

; render_report(rdi = number of summaries) — prints the header, a line of dashes and one row per summary.
; Frame: r12 = row, r13 = column, r14 = rows (header included).
render_report:
	ENTER_FRAME
	lea		r14, [rdi + 1]
	xor		r13d, r13d
.header:
	xor		edi, edi
	mov		rsi, r13
	lea		rdx, [s_text]
	mov		rcx, [header + r13 * 8]
	call	set_cell
	inc		r13
	cmp		r13, COLUMNS
	jb		.header
	mov		r12d, 1
.fill:
	cmp		r12, r14
	jae		.measure
	lea		rsi, [r12 - 1]
	imul	rsi, rsi, Summary_size
	add		rsi, summaries
	mov		rdi, r12
	call	fill_row
	inc		r12
	jmp		.fill

.measure:									; width of a column = its widest cell
	xor		r13d, r13d
.measure_column:
	mov		qword [widths + r13 * 8], 0
	xor		r12d, r12d
.measure_cell:
	mov		rdi, r12
	mov		rsi, r13
	call	cell
	mov		rdi, rax
	call	text_width
	cmp		rax, [widths + r13 * 8]
	jbe		.measure_next
	mov		[widths + r13 * 8], rax
.measure_next:
	inc		r12
	cmp		r12, r14
	jb		.measure_cell
	inc		r13
	cmp		r13, COLUMNS
	jb		.measure_column

	xor		edi, edi
	call	print_row						; header
	xor		r13d, r13d
.dashes:
	mov		edi, '-'
	mov		rsi, [widths + r13 * 8]
	call	repeat_char
	inc		r13
	cmp		r13, COLUMNS
	jae		.dashes_done
	mov		edi, ' '
	mov		esi, 2
	call	repeat_char
	jmp		.dashes
.dashes_done:
	mov		edi, 10
	call	putchar
	mov		r12d, 1
.row:
	cmp		r12, r14
	jae		.done
	mov		rdi, r12
	call	print_row
	inc		r12
	jmp		.row
.done:
	LEAVE_FRAME

; simulator_main(rdi = runs per configuration, rsi = --seed value (0 when absent), rdx = vocation row or -1 for
; all, rcx = difficulty row or -1 for all) → eax = exit code.
; The reference uses `seed or 1`: `--seed 0` and no seed both simulate with base seed 1.
; Frame: r12 = summaries filled, r13 = vocation row, r14 = difficulty row.
; Locals: [rsp] runs, [rsp+8] base seed, [rsp+16] vocation filter, [rsp+24] difficulty filter.
simulator_main:
	ENTER_FRAME 32
	mov		[rsp], rdi
	mov		eax, 1
	test	rsi, rsi
	cmovz	rsi, rax
	mov		[rsp + 8], rsi
	mov		[rsp + 16], rdx
	mov		[rsp + 24], rcx
	xor		r12d, r12d
	xor		r13d, r13d
.vocation:
	mov		rax, [rsp + 16]
	cmp		rax, 0
	jl		.vocation_ok
	cmp		rax, r13
	jne		.vocation_next
.vocation_ok:
	xor		r14d, r14d
.difficulty:
	mov		rax, [rsp + 24]
	cmp		rax, 0
	jl		.difficulty_ok
	cmp		rax, r14
	jne		.difficulty_next
.difficulty_ok:
	imul	rdi, r12, Summary_size
	add		rdi, summaries
	mov		rsi, r13
	mov		rdx, r14
	mov		rcx, [rsp]
	mov		r8, [rsp + 8]
	call	simulate
	test	eax, eax
	jnz		.unfinished
	inc		r12
.difficulty_next:
	inc		r14
	cmp		r14, DIFFICULTY_COUNT
	jb		.difficulty
.vocation_next:
	inc		r13
	cmp		r13, VOCATION_COUNT
	jb		.vocation
	mov		rdi, r12
	call	render_report
	xor		eax, eax
	jmp		.done
.unfinished:
	mov		rdi, [stderr]
	lea		rsi, [s_error_unfinished]
	xor		eax, eax
	call	fprintf
	mov		eax, 1
.done:
	LEAVE_FRAME
