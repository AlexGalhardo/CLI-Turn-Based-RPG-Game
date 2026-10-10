; domain/enums.inc — the closed sets of the game (mirror of rpg-python/src/rpg/domain/enums.py).
;
; Role: gives a small integer to every element, slot, stat, phase... so the engine can index tables with them.
;
; FORMAT CONTRACT: every member line is exactly `GROUP_MEMBER equ <n> ; "<json spelling>"`.
; tools/gen_data.py reads these lines: it maps the strings found in shared/data/*.json to the numbers below and
; emits the name tables (`element_names`, `slot_names`, ...) from the quoted spellings. Add a member here and the
; generated data follows; nothing else has to be kept in sync by hand.
%ifndef ENUMS_INC
%define ENUMS_INC

; ── elements (docs/game-design.md §5) ─────────────────────────────────────────
ELEMENT_PHYSICAL	equ 0 ; "physical"
ELEMENT_FIRE		equ 1 ; "fire"
ELEMENT_ICE			equ 2 ; "ice"
ELEMENT_ENERGY		equ 3 ; "energy"
ELEMENT_EARTH		equ 4 ; "earth"
ELEMENT_HOLY		equ 5 ; "holy"
ELEMENT_DEATH		equ 6 ; "death"
ELEMENT_COUNT		equ 7

; ── equipment slots ───────────────────────────────────────────────────────────
SLOT_HELMET			equ 0 ; "helmet"
SLOT_ARMOR			equ 1 ; "armor"
SLOT_LEGS			equ 2 ; "legs"
SLOT_BOOTS			equ 3 ; "boots"
SLOT_AMULET			equ 4 ; "amulet"
SLOT_RING			equ 5 ; "ring"
SLOT_WEAPON			equ 6 ; "weapon"
SLOT_SHIELD			equ 7 ; "shield"
SLOT_COUNT			equ 8

; ── run phases ────────────────────────────────────────────────────────────────
PHASE_MERCHANT		equ 0 ; "merchant"
PHASE_BATTLE		equ 1 ; "battle"
PHASE_VICTORY		equ 2 ; "victory"
PHASE_GAME_OVER		equ 3 ; "game_over"
PHASE_COUNT			equ 4

; ── enemy classes: the index of a row of `enemy_classes` (balance.enemyClasses) ─
CLASS_NORMAL		equ 0 ; "normal"
CLASS_ELITE			equ 1 ; "elite"
CLASS_BOSS			equ 2 ; "boss"
CLASS_COUNT			equ 3

RESOURCE_HP			equ 0 ; "hp"
RESOURCE_MP			equ 1 ; "mp"
RESOURCE_COUNT		equ 2

SPELLKIND_ATTACK	equ 0 ; "attack"
SPELLKIND_HEAL		equ 1 ; "heal"
SPELLKIND_COUNT		equ 2

STATUSKIND_DOT		equ 0 ; "dot"
STATUSKIND_STUN		equ 1 ; "stun"
STATUSKIND_COUNT	equ 2

TARGET_PLAYER		equ 0 ; "player"
TARGET_MONSTER		equ 1 ; "monster"
TARGET_COUNT		equ 2

; ── stats (docs/game-design.md §4, "Derived stats") ───────────────────────────
STAT_ATTACK			equ 0 ; "attack"
STAT_ARMOR			equ 1 ; "armor"
STAT_MAX_HP			equ 2 ; "maxHp"
STAT_MAX_MP			equ 3 ; "maxMp"
STAT_HP_REGEN		equ 4 ; "hpRegen"
STAT_MP_REGEN		equ 5 ; "mpRegen"
STAT_CRIT_CHANCE	equ 6 ; "critChance"
STAT_CRIT_DAMAGE	equ 7 ; "critDamage"
STAT_SPELL_POWER	equ 8 ; "spellPower"
STAT_PHYSICAL_DAMAGE equ 9 ; "physicalDamage"
STAT_DODGE			equ 10 ; "dodge"
STAT_PARRY			equ 11 ; "parry"
STAT_LIFE_LEECH		equ 12 ; "lifeLeech"
STAT_MANA_LEECH		equ 13 ; "manaLeech"
STAT_PROT_PHYSICAL	equ 14 ; "protPhysical"
STAT_PROT_FIRE		equ 15 ; "protFire"
STAT_PROT_ICE		equ 16 ; "protIce"
STAT_PROT_ENERGY	equ 17 ; "protEnergy"
STAT_PROT_EARTH		equ 18 ; "protEarth"
STAT_PROT_HOLY		equ 19 ; "protHoly"
STAT_PROT_DEATH		equ 20 ; "protDeath"
STAT_COUNT			equ 21

; The protection stats are declared in element order, so the protection against element `e` is the stat
; `STAT_PROT_PHYSICAL + e` (PROTECTION_BY_ELEMENT in the reference). The checks below keep that trick honest.
%if STAT_PROT_FIRE != STAT_PROT_PHYSICAL + ELEMENT_FIRE || STAT_PROT_DEATH != STAT_PROT_PHYSICAL + ELEMENT_DEATH
	%error "protection stats must follow the element order"
%endif

%endif
