; application/progression.asm — experience, levels, magic levels and spell levels (application/progression.py;
; docs/game-design.md §4 and §9).
;
; Register use: both routines use the standard frame.
%include "common.inc"

extern g_player, g_sheet, build_sheet, xp_for_level, mana_for_magic_level, spell_level_for_uses, event_new

global gain_experience, after_cast

section .text

; gain_experience(rdi = amount) — adds XP, then levels up while the total reaches the next level's threshold.
; Each level raises the maximum HP/MP (through the sheet) and restores the same amount.
; Frame: rbx = Vocation row.
gain_experience:
	ENTER_FRAME
	add		[g_player + Player.xp], rdi
	mov		rsi, [g_player + Player.xp]
	EMIT	EV_XP_GAINED, rdi, rsi
	mov		rbx, [g_player + Player.vocation]
	ROW		rbx, vocations, rbx, Vocation_size
.check:
	mov		rdi, [g_player + Player.level]
	inc		rdi
	call	xp_for_level
	cmp		[g_player + Player.xp], rax
	jl		.done
	inc		qword [g_player + Player.level]
	call	build_sheet
	mov		rax, [g_player + Player.hp]
	add		rax, [rbx + Vocation.hp_per_level]
	mov		rsi, [g_sheet + Sheet.max_hp]
	cmp		rax, rsi
	cmovg	rax, rsi
	mov		[g_player + Player.hp], rax
	mov		rax, [g_player + Player.mp]
	add		rax, [rbx + Vocation.mp_per_level]
	mov		rdx, [g_sheet + Sheet.max_mp]
	cmp		rax, rdx
	cmovg	rax, rdx
	mov		[g_player + Player.mp], rax
	mov		rdi, [g_player + Player.level]
	EMIT	EV_LEVEL_UP, rdi, rsi, rdx
	jmp		.check
.done:
	LEAVE_FRAME

; after_cast(rdi = spell row, rsi = mana cost paid) — runs after every successful cast, dodged and parried casts
; included: the use counts towards the spell level and the mana towards the magic level.
; Frame: r12 = spell row, r13 = cost, rbx = uses before this cast, r14 = spell level before.
after_cast:
	ENTER_FRAME
	mov		r12, rdi
	mov		r13, rsi
	mov		rbx, [g_player + Player.spell_uses + r12 * 8]
	inc		qword [g_player + Player.spell_uses + r12 * 8]
	mov		rdi, rbx
	call	spell_level_for_uses
	mov		r14, [rax + SpellLevel.level]
	lea		rdi, [rbx + 1]
	call	spell_level_for_uses
	mov		rsi, [rax + SpellLevel.level]
	cmp		rsi, r14
	je		.mana
	EMIT	EV_SPELL_LEVEL_UP, r12, rsi
.mana:
	add		[g_player + Player.mana_spent], r13
.check:
	mov		rdi, [g_player + Player.magic_level]
	call	mana_for_magic_level
	cmp		[g_player + Player.mana_spent], rax
	jl		.done
	inc		qword [g_player + Player.magic_level]
	mov		rsi, [g_player + Player.magic_level]
	EMIT	EV_MAGIC_LEVEL_UP, rsi
	jmp		.check
.done:
	LEAVE_FRAME
