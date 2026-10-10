; domain/entities.inc — layout of the mutable run state (mirror of domain/entities.py and application/run_state.py).
;
; Role: the reference keeps the run in Python objects with lists and dicts; here everything has a fixed size known
; from the generated content (BAL_BAG_CAPACITY, POTION_COUNT, ...), so one run is a handful of static blocks:
; `g_run`, `g_player`, `g_monster` and `g_stats` (reserved in application/run_state.asm). Only one run exists at a
; time (the simulator plays its runs one after the other).
;
; Dicts keyed by an id become arrays indexed by the row of that id; lists become (count, items[capacity]).
%ifndef ENTITIES_INC
%define ENTITIES_INC

NAME_MAX equ 64	; player name buffer, NUL included

; An active status effect (burn, poison, stun...).
struc ActiveStatus
	.status		resq 1	; status row
	.turns		resq 1
	.per_turn	resq 1	; damage per turn before the target's protection/resistance (0 for stun)
endstruc

; The statuses of the player or of the monster, in the order they were applied. A status id appears at most once,
; so STATUS_COUNT entries are always enough.
struc StatusList
	.count		resq 1
	.items		resb ActiveStatus_size * STATUS_COUNT
endstruc

struc AffixRoll
	.stat		resq 1	; STAT_*
	.value		resq 1
endstruc

; A generated item: base item + rarity + rolled affixes. `.uid == 0` marks an empty equipment slot (uids start
; at 1).
struc ItemInstance
	.uid		resq 1
	.item		resq 1	; item row
	.rarity		resq 1	; rarity row
	.tier		resq 1	; round tier it was generated for (drives the level requirement)
	.affix_count resq 1
	.affixes	resb AffixRoll_size * MAX_AFFIXES
endstruc

struc Player
	.name		resb NAME_MAX
	.vocation	resq 1	; vocation row
	.hp			resq 1
	.mp			resq 1
	.gold		resq 1
	.level		resq 1
	.xp			resq 1
	.magic_level resq 1
	.mana_spent	resq 1
	.potions	resq POTION_COUNT	; quantity owned, by potion row
	.potion_known resq POTION_COUNT	; 1 once the potion was ever owned (the reference dict keeps a key at 0)
	.equipment	resb ItemInstance_size * SLOT_COUNT	; by SLOT_*
	.bag_count	resq 1
	.bag		resb ItemInstance_size * BAL_BAG_CAPACITY
	.spell_uses	resq SPELL_COUNT	; by spell row
	.statuses	resb StatusList_size
	.stun_cooldown resq 1
	.defending	resq 1
endstruc

; The spawned monster: stats already scaled for the round, the difficulty and the enemy class. The unscaled parts
; of its attacks (id, element, weight, status) are read from its Creature row.
struc MonsterInstance
	.creature	resq 1	; creature row
	.is_boss	resq 1
	.enemy_class resq 1	; CLASS_*
	.hp			resq 1
	.max_hp		resq 1
	.xp			resq 1
	.gold_min	resq 1
	.gold_max	resq 1
	.attack_min	resq MAX_ATTACKS	; scaled damage range of each attack
	.attack_max	resq MAX_ATTACKS
	.statuses	resb StatusList_size
	.stun_cooldown resq 1
	.boss_actions resq 1
endstruc

struc RunState
	.seed		resq 1
	.difficulty	resq 1	; config.difficulty (difficulty row); the vocation and the name live in the player
	.auto_equip	resq 1	; config.autoEquip
	.phase		resq 1	; PHASE_*
	.round		resq 1
	.turn		resq 1
	.has_monster resq 1	; 0 = no monster (the reference's `monster is None`)
	.stock_count resq 1
	.stock		resb ItemInstance_size * BAL_MERCHANT_STOCK_SIZE
	.next_item_uid resq 1
	.death_cause resq 1	; creature row, or -1
	.won		resq 1
endstruc

; An entry of RunStatistics.dropped_items.
struc DroppedItem
	.item		resq 1
	.rarity		resq 1
	.round		resq 1
endstruc

; The reference list has no limit; a run would need thousands of rounds to fill this. Further drops still happen,
; they are just not listed.
MAX_DROPPED_ITEMS equ 4096

; Run counters derived from the events (application/statistics.asm). The 16 scalars come first, in the order of
; RunStatistics.to_dict(), so they can be printed with one loop.
struc Stats
	.damage_dealt	resq 1
	.damage_taken	resq 1
	.healing_done	resq 1
	.highest_hit	resq 1
	.normal_attacks	resq 1
	.crits			resq 1
	.dodges			resq 1
	.parries		resq 1
	.defends		resq 1
	.gold_looted	resq 1
	.gold_spent		resq 1
	.gold_earned	resq 1
	.items_sold		resq 1
	.items_auto_equipped resq 1
	.bosses_killed	resq 1
	.elites_killed	resq 1
	.spells_cast	resq SPELL_COUNT
	.potions_used	resq POTION_COUNT
	.potions_bought	resq POTION_COUNT
	.potions_dropped resq POTION_COUNT
	.items_dropped	resq RARITY_COUNT
	.kills			resq CREATURE_COUNT
	.statuses_applied resq STATUS_COUNT
	.dropped_count	resq 1
	.dropped		resb DroppedItem_size * MAX_DROPPED_ITEMS
endstruc
STATS_SCALAR_COUNT equ 16

; The derived character sheet (domain/character.asm, CharacterSheet in the reference): vocation base + equipment.
struc Sheet
	.max_hp		resq 1
	.max_mp		resq 1
	.hp_regen	resq 1
	.mp_regen	resq 1
	.melee_min	resq 1
	.melee_max	resq 1
	.weapon_element resq 1
	.armor		resq 1
	.crit_chance resq 1
	.crit_damage resq 1
	.spell_power resq 1
	.physical_damage resq 1
	.dodge		resq 1
	.parry		resq 1
	.life_leech	resq 1
	.mana_leech	resq 1
	.prot		resq ELEMENT_COUNT	; by ELEMENT_*
endstruc

%endif
