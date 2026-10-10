; application/simulator.inc — the result of one simulation (SimulationSummary in the reference), shared by
; application/simulator.asm (fills it) and presentation/simulator_report.asm (prints it).
%ifndef SIMULATOR_INC
%define SIMULATOR_INC

SUMMARY_KILLERS equ 3	; the report lists the three deadliest creatures

struc Summary
	.vocation		resq 1	; vocation row
	.difficulty		resq 1	; difficulty row
	.runs			resq 1
	.wins			resq 1
	.min_round		resq 1
	.p10_round		resq 1
	.median_round	resq 1
	.p90_round		resq 1
	.max_round		resq 1
	.mean_level		resq 1
	.killer_count	resq 1
	.killers		resq 2 * SUMMARY_KILLERS	; (creature row, deaths) pairs, deadliest first
endstruc

%endif
