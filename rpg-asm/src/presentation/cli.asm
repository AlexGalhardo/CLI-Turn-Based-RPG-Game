; presentation/cli.asm — `main`: command-line flags and the choice of what to run (presentation/cli.py and
; __main__.py in the reference).
;
; Role: libc's start code calls `main(argc, argv)` like any C program (edi = argc, rsi = argv) and turns the
; value returned in eax into the process exit code.
;
;   rpg-asm [--seed N]                    play (line-based text UI)
;   rpg-asm --simulate N [--seed S] [--vocation V] [--difficulty D]
;   rpg-asm --replay SCRIPT               golden-file test mode (presentation/replay.asm)
;   rpg-asm --prng SEED COUNT             print the first COUNT outputs of the PRNG (test mode)
;   rpg-asm --version | --help
;
; Register use: standard frames. In main: r12 = argc, r13 = argv, r14 = index of the current argument,
; rbx = current argument.
%include "common.inc"

extern rng_seed, rng_next, kind_lookup
extern replay_run, simulator_main, text_ui_run

global main

section .rodata align=8
s_help:
	db `Usage: rpg-asm [options]\n\n`
	db `Endless turn-based RPG for the terminal (x86-64 Assembly port, line-based text UI).\n\n`
	db `  --seed N           deterministic run\n`
	db `  --simulate N       run N headless bot games per vocation and difficulty and print a report\n`
	db `  --vocation V       (simulator) restrict to one vocation\n`
	db `  --difficulty D     (simulator) restrict to one difficulty\n`
	db `  --replay SCRIPT    replay a command script, print canonical events and state (golden tests)\n`
	db `  --prng SEED COUNT  print the first COUNT outputs of the shared PRNG\n`
	db `  --version          print the version\n`
	db `  --help             print this help\n`
	db 0
STRING s_version, "rpg ", VERSION, ` (asm)\n`
STRING s_opt_help, "--help"
STRING s_opt_h, "-h"
STRING s_opt_version, "--version"
STRING s_opt_seed, "--seed"
STRING s_opt_simulate, "--simulate"
STRING s_opt_vocation, "--vocation"
STRING s_opt_difficulty, "--difficulty"
STRING s_opt_replay, "--replay"
STRING s_opt_prng, "--prng"
STRING s_error_unknown, `rpg-asm: error: unrecognized argument: %s (try --help)\n`
STRING s_error_value, `rpg-asm: error: argument %s: expected a non-negative integer\n`
STRING s_error_missing, `rpg-asm: error: argument %s: expected a value\n`
STRING s_error_positive, `rpg-asm: error: argument --simulate: must be > 0\n`
STRING s_error_config, `error: invalid run config: '%s'\n`
STRING s_unsigned, `%u\n`

section .bss align=16
options:
.seed:			resq 1		; --seed value
.has_seed:		resq 1
.simulate:		resq 1		; --simulate value (0 = not asked)
.vocation:		resq 1		; --vocation text, or 0
.difficulty:	resq 1		; --difficulty text, or 0
.replay:		resq 1		; --replay path, or 0
.prng:			resq 1		; 1 when --prng was given
.prng_seed:		resq 1
.prng_count:	resq 1

section .text

; parse_number(rdi = text) → rax = value, edx = 0; or edx = 1 when the text is not a plain decimal number.
parse_number:
	mov		rsi, rdi
	mov		edx, 1
	cmp		byte [rsi], 0
	je		.done
.digit:
	mov		al, [rsi]
	test	al, al
	jz		.convert
	cmp		al, '0'
	jb		.done
	cmp		al, '9'
	ja		.done
	inc		rsi
	jmp		.digit
.convert:
	sub		rsp, 8
	xor		esi, esi
	mov		edx, 10
	call	strtoul
	add		rsp, 8
	xor		edx, edx
.done:
	ret

; main(edi = argc, rsi = argv) → eax = exit code.
main:
	ENTER_FRAME
	mov		r12d, edi
	mov		r13, rsi
	mov		r14d, 1

	; IF_OPTION option string, label — jump when the current argument is that option
	%macro IF_OPTION 2
		mov		rdi, rbx
		lea		rsi, [%1]
		call	strcmp
		test	eax, eax
		jz		%2
	%endmacro
	; NEXT_VALUE — rbx = the argument after the current option, r15 = the option (for error messages)
	%macro NEXT_VALUE 0
		mov		r15, rbx
		inc		r14
		cmp		r14, r12
		jae		.missing
		mov		rbx, [r13 + r14 * 8]
	%endmacro
	; NEXT_NUMBER — like NEXT_VALUE, then rax = the value parsed as a non-negative number
	%macro NEXT_NUMBER 0
		NEXT_VALUE
		mov		rdi, rbx
		call	parse_number
		test	edx, edx
		jnz		.bad_value
	%endmacro

.argument:
	cmp		r14, r12
	jae		.run
	mov		rbx, [r13 + r14 * 8]
	IF_OPTION s_opt_help, .help
	IF_OPTION s_opt_h, .help
	IF_OPTION s_opt_version, .version
	IF_OPTION s_opt_seed, .seed
	IF_OPTION s_opt_simulate, .simulate
	IF_OPTION s_opt_vocation, .vocation
	IF_OPTION s_opt_difficulty, .difficulty
	IF_OPTION s_opt_replay, .replay
	IF_OPTION s_opt_prng, .prng
	mov		rdi, [stderr]
	lea		rsi, [s_error_unknown]
	mov		rdx, rbx
	jmp		.usage_error
.seed:
	NEXT_NUMBER
	mov		[options.seed], rax
	mov		qword [options.has_seed], 1
	jmp		.next
.simulate:
	NEXT_NUMBER
	mov		[options.simulate], rax
	test	rax, rax
	jnz		.next
	mov		rdi, [stderr]
	lea		rsi, [s_error_positive]
	jmp		.usage_error
.vocation:
	NEXT_VALUE
	mov		[options.vocation], rbx
	jmp		.next
.difficulty:
	NEXT_VALUE
	mov		[options.difficulty], rbx
	jmp		.next
.replay:
	NEXT_VALUE
	mov		[options.replay], rbx
	jmp		.next
.prng:
	mov		qword [options.prng], 1
	NEXT_NUMBER
	mov		[options.prng_seed], rax
	NEXT_NUMBER
	mov		[options.prng_count], rax
.next:
	inc		r14
	jmp		.argument

.missing:
	mov		rdi, [stderr]
	lea		rsi, [s_error_missing]
	mov		rdx, r15
	jmp		.usage_error
.bad_value:
	mov		rdi, [stderr]
	lea		rsi, [s_error_value]
	mov		rdx, r15
.usage_error:
	xor		eax, eax
	call	fprintf
	mov		eax, 2							; the exit code argparse uses for usage errors
	jmp		.done

.help:
	lea		rdi, [s_help]
	xor		eax, eax
	call	printf
	xor		eax, eax
	jmp		.done
.version:
	lea		rdi, [s_version]
	xor		eax, eax
	call	printf
	xor		eax, eax
	jmp		.done

.run:
	cmp		qword [options.prng], 0
	jne		.run_prng
	mov		rdi, [options.replay]
	test	rdi, rdi
	jnz		.run_replay
	cmp		qword [options.simulate], 0
	jne		.run_simulator
	mov		rdi, [options.seed]
	mov		rsi, [options.has_seed]
	call	text_ui_run
	jmp		.done
.run_replay:
	call	replay_run
	jmp		.done
.run_prng:
	mov		rdi, [options.prng_seed]
	call	rng_seed
	mov		rbx, [options.prng_count]
.prng_output:
	test	rbx, rbx
	jz		.ok
	call	rng_next
	lea		rdi, [s_unsigned]
	mov		esi, eax
	xor		eax, eax
	call	printf
	dec		rbx
	jmp		.prng_output
.run_simulator:
	; --vocation / --difficulty: an id of the game data, or -1 for "all of them"
	mov		r14, -1
	mov		rbx, [options.vocation]
	test	rbx, rbx
	jz		.sim_difficulty
	mov		edi, KIND_VOCATION
	mov		rsi, rbx
	call	kind_lookup
	mov		r14, rax
	cmp		rax, 0
	jl		.bad_config
.sim_difficulty:
	mov		r15, -1
	mov		rbx, [options.difficulty]
	test	rbx, rbx
	jz		.sim_start
	mov		edi, KIND_DIFFICULTY
	mov		rsi, rbx
	call	kind_lookup
	mov		r15, rax
	cmp		rax, 0
	jl		.bad_config
.sim_start:
	mov		rdi, [options.simulate]
	mov		rsi, [options.seed]				; 0 when --seed was not given
	mov		rdx, r14
	mov		rcx, r15
	call	simulator_main
	jmp		.done
.bad_config:
	mov		rdi, [stderr]
	lea		rsi, [s_error_config]
	mov		rdx, rbx
	xor		eax, eax
	call	fprintf
	mov		eax, 2
	jmp		.done
.ok:
	xor		eax, eax
.done:
	LEAVE_FRAME
