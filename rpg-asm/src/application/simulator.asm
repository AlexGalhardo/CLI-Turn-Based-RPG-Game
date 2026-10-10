; application/simulator.asm — headless balance simulator: the bot plays many runs and the results are aggregated
; (application/simulator.py; docs/game-design.md §12).
;
; Role: `play_one` plays a whole run with the bot; `simulate` plays `runs` runs with consecutive seeds and fills
; a Summary (wins, round percentiles, mean level, top three killers). The report is printed by
; presentation/simulator_report.asm.
;
; Register use: standard frames. `compare_qwords` is a callback called BY libc's qsort, so it follows the System V
; convention from the callee side: arguments in rdi/rsi, result in eax, callee-saved registers untouched.
%include "common.inc"
%include "application/simulator.inc"

extern g_run, g_player
extern engine_new_run, engine_step, bot_choose

global play_one, simulate

MAX_STEPS_PER_RUN equ 200000

section .rodata align=8
s_bot:	db "Bot", 0

section .bss align=16
killer_counts:	resq CREATURE_COUNT		; deaths caused by each creature in the current simulation

section .text

; play_one(rdi = vocation row, rsi = difficulty row, rdx = seed) → eax = 0, or 1 when the run did not finish
; within MAX_STEPS_PER_RUN commands. The result is read from g_run / g_player.
play_one:
	ENTER_FRAME
	mov		rcx, rsi
	mov		rax, rdi
	mov		rdi, rdx
	lea		rsi, [s_bot]
	mov		rdx, rax
	xor		r8d, r8d						; the simulator plays without auto-equip
	call	engine_new_run
	mov		ebx, MAX_STEPS_PER_RUN
.step:
	xor		eax, eax
	cmp		qword [g_run + RunState.phase], PHASE_GAME_OVER
	je		.done
	mov		eax, 1
	dec		rbx
	js		.done
	call	bot_choose
	mov		rdi, rax
	call	engine_step
	jmp		.step
.done:
	LEAVE_FRAME

; compare_qwords(rdi = pointer to a, rsi = pointer to b) → eax < 0, 0 or > 0: ascending order for qsort.
compare_qwords:
	mov		rax, [rdi]
	cmp		rax, [rsi]
	setg	al
	setl	cl
	movzx	eax, al
	movzx	ecx, cl
	sub		eax, ecx
	ret

; percentile(rdi = sorted qwords, rsi = count, rdx = percent) → rax = sorted[min(count - 1, count × percent / 100)]
percentile:
	mov		rax, rsi
	imul	rax, rdx
	xor		edx, edx
	mov		ecx, 100
	div		rcx
	lea		rcx, [rsi - 1]
	cmp		rax, rcx
	cmova	rax, rcx
	mov		rax, [rdi + rax * 8]
	ret

; simulate(rdi = Summary to fill, rsi = vocation row, rdx = difficulty row, rcx = runs (> 0), r8 = base seed)
; → eax = 0, or 1 when a run did not finish. Run i uses the seed base + i.
; Frame: rbx = Summary, r12 = sorted rounds (malloc), r13 = run index, r14 = runs, r15 = sum of the final levels.
; Local: [rsp] = base seed.
simulate:
	ENTER_FRAME 16
	mov		rbx, rdi
	mov		[rbx + Summary.vocation], rsi
	mov		[rbx + Summary.difficulty], rdx
	mov		[rbx + Summary.runs], rcx
	mov		qword [rbx + Summary.wins], 0
	mov		r14, rcx
	mov		[rsp], r8
	lea		rdi, [killer_counts]
	mov		ecx, CREATURE_COUNT
	xor		eax, eax
	rep stosq
	lea		rdi, [r14 * 8]
	call	malloc
	mov		r12, rax
	xor		r13d, r13d
	xor		r15d, r15d
.run:
	mov		rdi, [rbx + Summary.vocation]
	mov		rsi, [rbx + Summary.difficulty]
	mov		rdx, [rsp]
	add		rdx, r13
	call	play_one
	test	eax, eax
	jnz		.failed
	mov		rax, [g_run + RunState.round]
	mov		[r12 + r13 * 8], rax
	add		r15, [g_player + Player.level]
	mov		rax, [g_run + RunState.won]
	add		[rbx + Summary.wins], rax
	mov		rax, [g_run + RunState.death_cause]
	cmp		rax, 0
	jl		.next							; a won run has no killer
	inc		qword [killer_counts + rax * 8]
.next:
	inc		r13
	cmp		r13, r14
	jb		.run

	mov		rdi, r12
	mov		rsi, r14
	mov		edx, 8
	lea		rcx, [compare_qwords]
	call	qsort
	mov		rax, [r12]
	mov		[rbx + Summary.min_round], rax
	mov		rax, [r12 + r14 * 8 - 8]
	mov		[rbx + Summary.max_round], rax
	%macro STORE_PERCENTILE 2
		mov		rdi, r12
		mov		rsi, r14
		mov		edx, %2
		call	percentile
		mov		[rbx + Summary.%1], rax
	%endmacro
	STORE_PERCENTILE p10_round, 10
	STORE_PERCENTILE median_round, 50
	STORE_PERCENTILE p90_round, 90
	mov		rax, r15
	xor		edx, edx
	div		r14
	mov		[rbx + Summary.mean_level], rax
	mov		rdi, r12
	call	free

	; top killers: up to three creatures by (most deaths, then id in code-point order)
	xor		r13d, r13d						; r13 = killers found
.killer:
	mov		r12, -1							; r12 = best creature so far
	xor		r15d, r15d						; r15 = creature row
.scan:
	mov		rax, [killer_counts + r15 * 8]
	test	rax, rax
	jz		.scan_next
	cmp		r12, -1
	je		.take
	cmp		rax, [killer_counts + r12 * 8]
	ja		.take
	jb		.scan_next
	ROW		rdi, creatures, r15, Creature_size
	mov		rdi, [rdi + Creature.id]
	ROW		rsi, creatures, r12, Creature_size
	mov		rsi, [rsi + Creature.id]
	call	strcmp
	test	eax, eax
	jge		.scan_next
.take:
	mov		r12, r15
.scan_next:
	inc		r15
	cmp		r15, CREATURE_COUNT
	jb		.scan
	cmp		r12, -1
	je		.killers_done
	mov		rax, r13
	shl		rax, 4
	mov		[rbx + Summary.killers + rax], r12
	mov		rcx, [killer_counts + r12 * 8]
	mov		[rbx + Summary.killers + rax + 8], rcx
	mov		qword [killer_counts + r12 * 8], 0	; taken: it cannot be picked again
	inc		r13
	cmp		r13, SUMMARY_KILLERS
	jb		.killer
.killers_done:
	mov		[rbx + Summary.killer_count], r13
	xor		eax, eax
	jmp		.done
.failed:
	mov		rdi, r12
	call	free
	mov		eax, 1
.done:
	LEAVE_FRAME
