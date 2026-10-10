; presentation/event_text.asm — turns an engine event into an English sentence (presentation/event_text.py).
;
; Role: the sentence templates are the `event.<type>` and `error.<code>` texts of shared/i18n/en.json, e.g.
; "{spell} deals {damage} {element} damage.". `print_event_text` picks the template (with the same variants as
; the reference: _crit, _charged, _boss, _elite, _player/_monster) and replaces every `{placeholder}`:
;   - a field of the event with that name → its value (ids of elements, statuses... become display names);
;   - `spell`, `potion`, `monster`, `item` → the display name of the event's `spellId`, `potionId`... field;
;   - `monster` without a `monsterId` field → the name of the monster being fought.
; The event schema (application/events.asm) provides the field names and kinds, so no event is special-cased.
;
; Register use: standard frames. In print_event_text: rbx = Event, r12 = its EventSchema row, r13 = cursor in
; the template.
%include "common.inc"

extern g_monster, event_schema, kind_id, kind_name, value_string, t

global print_event_text

KEY_MAX		equ 96
PARAM_MAX	equ 32

section .rodata align=8
STRING s_error_key, "error.%s"
STRING s_event_key, "event.%s%s%s"
STRING s_underscore, "_"
STRING s_none, ""
STRING s_crit, "crit"
STRING s_charged, "charged"
STRING s_target, "target"
STRING s_boss, "boss"
STRING s_elite, "elite"
STRING s_monster, "monster"
STRING s_id_suffix, "%sId"
STRING s_text, "%s"

section .bss align=16
key:		resb KEY_MAX		; i18n key being built
param:		resb PARAM_MAX		; placeholder name being read
param_id:	resb PARAM_MAX + 2	; the same name with the "Id" suffix

section .text

; field_index(rdi = EventSchema row, rsi = field name) → rax = index of the field with that name, or -1.
field_index:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r12, rsi
	xor		r13d, r13d
.field:
	cmp		r13, [rbx + EventSchema.field_count]
	jae		.missing
	mov		rax, r13
	shl		rax, 4
	mov		rdi, [rbx + EventSchema.fields + rax]
	mov		rsi, r12
	call	strcmp
	test	eax, eax
	jz		.found
	inc		r13
	jmp		.field
.missing:
	mov		r13, -1
.found:
	mov		rax, r13
	LEAVE_FRAME

; field_flag(rdi = Event, rsi = EventSchema row, rdx = field name) → rax = value of that field, or 0 when the
; event has no such field (used for the boolean fields that select a template variant).
field_flag:
	ENTER_FRAME
	mov		rbx, rdi
	mov		rdi, rsi
	mov		rsi, rdx
	call	field_index
	mov		rcx, rax
	xor		eax, eax
	cmp		rcx, 0
	jl		.done
	mov		rax, [rbx + Event.f0 + rcx * 8]
.done:
	LEAVE_FRAME

; print_field(rdi = Event, rsi = EventSchema row, rdx = field index, rcx = 1 to print a table row by its
; display name / 0 by its id).
print_field:
	ENTER_FRAME
	mov		rax, rdx
	shl		rax, 4
	mov		r12, [rsi + EventSchema.fields + rax + 8]	; r12 = kind
	mov		r13, [rdi + Event.f0 + rdx * 8]				; r13 = value
	mov		rdi, r12
	mov		rsi, r13
	cmp		r12, KIND_VOCATION				; the first table kind
	jb		.plain
	test	rcx, rcx
	jz		.plain
	call	kind_name
	jmp		.print
.plain:
	call	value_string
.print:
	PRINTF	"%s", rax
	LEAVE_FRAME

; print_param() — prints the value of the placeholder whose name is in `param`, for the event in rbx / r12
; (the caller's registers: this routine is only called by print_event_text and keeps them).
print_param:
	ENTER_FRAME
	mov		rdi, r12
	lea		rsi, [param]
	call	field_index
	cmp		rax, 0
	jl		.by_id
	mov		rdi, rbx
	mov		rsi, r12
	mov		rdx, rax
	mov		ecx, 1
	call	print_field
	jmp		.done
.by_id:										; {spell} → the spellId field, shown by name
	lea		rdi, [param_id]
	mov		esi, PARAM_MAX + 2
	lea		rdx, [s_id_suffix]
	lea		rcx, [param]
	xor		eax, eax
	call	snprintf
	mov		rdi, r12
	lea		rsi, [param_id]
	call	field_index
	cmp		rax, 0
	jl		.monster
	mov		rdi, rbx
	mov		rsi, r12
	mov		rdx, rax
	mov		ecx, 1
	call	print_field
	jmp		.done
.monster:									; {monster} in an event without monsterId: the current monster
	lea		rdi, [param]
	lea		rsi, [s_monster]
	call	strcmp
	test	eax, eax
	jnz		.unknown
	mov		edi, KIND_CREATURE
	mov		rsi, [g_monster + MonsterInstance.creature]
	call	kind_name
	PRINTF	"%s", rax
	jmp		.done
.unknown:
	lea		rax, [param]
	PRINTF	"{%s}", rax
.done:
	LEAVE_FRAME

; print_event_text(rdi = Event) — prints the sentence of one event, followed by a newline.
; Frame: rbx = Event, r12 = EventSchema row, r13 = template cursor, r14 = variant, r15 = "_" or "".
print_event_text:
	ENTER_FRAME
	mov		rbx, rdi
	mov		r12, [rbx + Event.type]
	ROW		r12, event_schema, r12, EventSchema_size

	cmp		qword [rbx + Event.type], EV_ERROR
	jne		.event
	mov		edi, KIND_ERROR
	mov		rsi, [rbx + Event.f0]
	call	kind_id
	mov		rcx, rax
	lea		rdi, [key]
	mov		esi, KEY_MAX
	lea		rdx, [s_error_key]
	xor		eax, eax
	call	snprintf						; "error.<code>"
	jmp		.template

.event:
	; the variant of the template, in the reference's order of precedence
	lea		r15, [s_underscore]
	lea		r14, [s_crit]
	mov		rdi, rbx
	mov		rsi, r12
	lea		rdx, [s_crit]
	call	field_flag
	test	rax, rax
	jnz		.build
	lea		r14, [s_charged]
	mov		rdi, rbx
	mov		rsi, r12
	lea		rdx, [s_charged]
	call	field_flag
	test	rax, rax
	jnz		.build
	cmp		qword [rbx + Event.type], EV_ROUND_STARTED
	jne		.target
	lea		r14, [s_boss]
	cmp		qword [rbx + Event.f4], 0		; isBoss
	jne		.build
	lea		r14, [s_elite]
	cmp		qword [rbx + Event.f5], CLASS_ELITE	; enemyClass
	je		.build
.target:
	mov		rdi, r12
	lea		rsi, [s_target]
	call	field_index
	cmp		rax, 0
	jl		.no_variant
	mov		edi, KIND_TARGET
	mov		rsi, [rbx + Event.f0 + rax * 8]
	call	kind_id							; "_player" / "_monster"
	mov		r14, rax
	jmp		.build
.no_variant:
	lea		r15, [s_none]
	lea		r14, [s_none]
.build:
	lea		rdi, [key]
	mov		esi, KEY_MAX
	lea		rdx, [s_event_key]
	mov		rcx, [r12 + EventSchema.name]
	mov		r8, r15
	mov		r9, r14
	xor		eax, eax
	call	snprintf						; "event.<type>[_<variant>]"

.template:
	lea		rdi, [key]
	call	t
	mov		r13, rax
.char:
	movzx	edi, byte [r13]
	test	edi, edi
	jz		.end
	cmp		edi, '{'
	je		.placeholder
	call	putchar
	inc		r13
	jmp		.char
.placeholder:
	inc		r13
	lea		rdi, [param]
	lea		rcx, [param + PARAM_MAX - 1]	; last byte that may receive a character
.name:
	mov		al, [r13]
	test	al, al
	jz		.closed							; unterminated placeholder: print what was read
	inc		r13
	cmp		al, '}'
	je		.closed
	cmp		rdi, rcx
	jae		.name							; too long: the extra characters are dropped
	mov		[rdi], al
	inc		rdi
	jmp		.name
.closed:
	mov		byte [rdi], 0
	call	print_param
	jmp		.char
.end:
	mov		edi, 10
	call	putchar
	LEAVE_FRAME
