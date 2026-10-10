; domain/definitions.inc — layout of the immutable game definitions (mirror of domain/definitions.py).
;
; Role: declares, with NASM `struc`, the shape of one row of every content table. The rows themselves are NOT
; written by hand: tools/gen_data.py turns shared/data/*.json into build/gamedata.asm, which fills these layouts with
; `istruc ... at Field, dq value ... iend`. So this file is the only place that knows the offsets.
;
; Conventions shared by every table:
;   - every field is a qword (8 bytes): no packing, every load is a plain 64-bit `mov`;
;   - a row always starts with `.id` (pointer to the id string used in JSON and events) followed by `.name`
;     (pointer to the English display name), so generic code can print any definition (see events.asm);
;   - references to other tables are row indexes (0-based, file order); "none" is -1;
;   - `table + index * Row_size` is the address of a row (macro ROW in common.inc).
%ifndef DEFINITIONS_INC
%define DEFINITIONS_INC

; One attack of a creature (monsters.json / bosses.json → attacks[]).
struc Attack
	.id					resq 1	; pointer to the attack id ("melee", "fire_wave", ...)
	.element			resq 1	; ELEMENT_*
	.min				resq 1	; base damage range, before the round/difficulty scaling
	.max				resq 1
	.weight				resq 1	; weight of the weighted pick
	.status				resq 1	; status row applied on hit, or -1
	.status_chance		resq 1
	.status_damage_pct	resq 1
endstruc

; A monster or a boss. `creatures` holds the monsters first (file order) and then the bosses.
struc Creature
	.id				resq 1
	.name			resq 1
	.family			resq 1	; pointer to the family name
	.tier			resq 1
	.hp				resq 1
	.xp				resq 1
	.gold_min		resq 1
	.gold_max		resq 1
	.is_boss		resq 1
	.charge_attack	resq 1	; index in .attacks of the boss charged attack, or -1
	.attack_count	resq 1
	.attacks		resq 1	; pointer to .attack_count Attack rows
	.resist			resq ELEMENT_COUNT	; damage taken % per element (100 when the JSON has no entry)
endstruc

struc Spell
	.id				resq 1
	.name			resq 1
	.words			resq 1	; incantation, shown by the text UI
	.kind			resq 1	; SPELLKIND_*
	.element		resq 1
	.mana			resq 1
	.min			resq 1
	.max			resq 1
	.per_level		resq 1
	.per_magic_level resq 1
	.l3_status		resq 1	; level3Bonus.status (status row) or -1
	.l3_chance		resq 1
	.l3_cleanse		resq 1	; level3Bonus.cleanse (0/1)
endstruc

struc Vocation
	.id				resq 1
	.name			resq 1
	.start_hp		resq 1
	.start_mp		resq 1
	.hp_per_level	resq 1
	.mp_per_level	resq 1
	.hp_regen		resq 1
	.mp_regen		resq 1
	.melee_min		resq 1
	.melee_max		resq 1
	.melee_per_level resq 1
	.weapon_mask	resq 1	; bit n set = item type n is one of weaponTypes
	.shield_mask	resq 1	; bit n set = item type n is one of shieldTypes
	.starter_weapon	resq 1	; item row
	.spell_count	resq 1
	.spells			resq MAX_VOCATION_SPELLS	; spell rows, file order
endstruc

struc Potion
	.id				resq 1
	.name			resq 1
	.resource		resq 1	; RESOURCE_*
	.min			resq 1
	.max			resq 1
	.price			resq 1
	.unlock_round	resq 1
endstruc

struc Status
	.id				resq 1
	.name			resq 1
	.kind			resq 1	; STATUSKIND_*
	.element		resq 1
	.turns			resq 1
endstruc

struc Item
	.id				resq 1
	.name			resq 1
	.slot			resq 1	; SLOT_*
	.type			resq 1	; index in the generated list of item types ("sword", "bow", ...)
	.tier			resq 1
	.element		resq 1	; weapon element, or -1
	.value			resq 1	; sell price at common rarity
	.stats			resq STAT_COUNT	; base stats, 0 when absent
endstruc

struc Affix
	.id				resq 1
	.name			resq 1
	.stat			resq 1	; STAT_*
	.min			resq 1
	.max			resq 1
	.per_tier		resq 1
	.slot_mask		resq 1	; bit n set = allowed on SLOT n
endstruc

struc Difficulty
	.id				resq 1
	.name			resq 1
	.hp_pct			resq 1
	.damage_pct		resq 1
	.gold_pct		resq 1
	.xp_pct			resq 1
endstruc

struc Rarity
	.id				resq 1
	.name			resq 1
	.stat_pct		resq 1
	.value_pct		resq 1
	.affix_min		resq 1
	.affix_max		resq 1
endstruc

; A row of balance.enemyClasses, indexed by CLASS_* (docs/game-design.md §3).
struc EnemyClass
	.id				resq 1
	.name			resq 1
	.stat_pct		resq 1
	.reward_pct		resq 1
	.dodge			resq 1
	.parry			resq 1
	.crit			resq 1
	.heal			resq 1
	.drop_chance_pct resq 1
	.drops			resq 1
	.potion_drop_pct resq 1
	.rarity_weights	resq RARITY_COUNT	; 0 when the rarity is missing from the JSON table
endstruc

struc SpellLevel
	.level			resq 1
	.uses			resq 1
	.effect_pct		resq 1
	.mana_pct		resq 1
endstruc

; Generic (id, name) pair: the generated name tables of the enums (`element_names`, ...) are arrays of these, so
; they can be printed exactly like a definition row.
struc Named
	.id				resq 1
	.name			resq 1
endstruc

%endif
