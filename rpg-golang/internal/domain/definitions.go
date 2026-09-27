package domain

import (
	"fmt"
	"slices"
	"strings"
)

// UnknownIDError is returned when a definition id is not present in the loaded data.
type UnknownIDError struct {
	ID string
}

func (e UnknownIDError) Error() string {
	return "unknown id: " + e.ID
}

// StatusOnHit is a status an attack may apply.
type StatusOnHit struct {
	Status    string `json:"id"`
	Chance    int    `json:"chance"`
	DamagePct int    `json:"damagePct"`
}

// MonsterAttack is one attack of a monster (already scaled when stored in a MonsterInstance).
type MonsterAttack struct {
	ID      string       `json:"id"`
	Element Element      `json:"element"`
	Min     int          `json:"min"`
	Max     int          `json:"max"`
	Weight  int          `json:"weight"`
	Status  *StatusOnHit `json:"status,omitempty"`
}

// MonsterDef defines a monster or a boss.
type MonsterDef struct {
	ID           string
	Name         string
	Tier         int
	Family       string
	HP           int
	XP           int
	GoldMin      int
	GoldMax      int
	Attacks      []MonsterAttack
	Resistances  map[Element]int
	IsBoss       bool
	ChargeAttack string
}

// Resistance is the damage-taken percentage for an element (default 100).
func (m *MonsterDef) Resistance(element Element) int {
	if value, ok := m.Resistances[element]; ok {
		return value
	}

	return 100
}

// Attack finds an attack by id.
func (m *MonsterDef) Attack(attackID string) (MonsterAttack, error) {
	for _, attack := range m.Attacks {
		if attack.ID == attackID {
			return attack, nil
		}
	}

	return MonsterAttack{}, UnknownIDError{ID: attackID}
}

// Level3Bonus is the extra effect of a spell at level 3.
type Level3Bonus struct {
	Status  string
	Chance  int
	Cleanse bool
}

// SpellDef defines a spell.
type SpellDef struct {
	ID            string
	Name          string
	Words         string
	Kind          string
	Element       Element
	Mana          int
	Min           int
	Max           int
	PerLevel      int
	PerMagicLevel int
	Level3Bonus   Level3Bonus
}

// VocationDef defines a vocation.
type VocationDef struct {
	ID            string
	Name          string
	StartHP       int
	StartMP       int
	HPPerLevel    int
	MPPerLevel    int
	HPRegen       int
	MPRegen       int
	MeleeMin      int
	MeleeMax      int
	MeleePerLevel int
	WeaponTypes   []string
	ShieldTypes   []string
	StarterWeapon string
	Spells        []string
}

// PotionDef defines a potion.
type PotionDef struct {
	ID          string
	Name        string
	Resource    string
	Min         int
	Max         int
	Price       int
	UnlockRound int
}

// StatusDef defines a status effect.
type StatusDef struct {
	ID      string
	Kind    string
	Element Element
	Turns   int
}

// StatValue is one stat of an item definition (kept as a slice: order never matters for sums).
type StatValue struct {
	Stat  Stat
	Value int
}

// ItemDef defines an equipment base item.
type ItemDef struct {
	ID      string
	Name    string
	Slot    Slot
	Type    string
	Tier    int
	Element Element
	Stats   []StatValue
	Value   int
}

// AffixDef defines a random affix.
type AffixDef struct {
	ID      string
	Stat    Stat
	Min     int
	Max     int
	PerTier int
	Slots   []Slot
}

// AchievementDef defines an achievement.
type AchievementDef struct {
	ID    string
	Type  string
	Value int
}

// DifficultyDef defines a difficulty.
type DifficultyDef struct {
	ID                 string
	HPPct              int
	DamagePct          int
	GoldPct            int
	XPPct              int
	NonCommonWeightPct int
}

// RarityDef defines an item rarity.
type RarityDef struct {
	ID       string
	StatPct  int
	ValuePct int
	AffixMin int
	AffixMax int
}

// SpellLevelDef defines a spell level threshold.
type SpellLevelDef struct {
	Level     int
	Uses      int
	EffectPct int
	ManaPct   int
}

// Caps are the stat caps.
type Caps struct {
	CritChance int
	Dodge      int
	Parry      int
	Leech      int
	Protection int
}

// PotionStack is a potion id with a quantity (starting kit).
type PotionStack struct {
	PotionID string
	Quantity int
}

// Balance holds the global knobs of balance.json.
type Balance struct {
	RoundsPerTier        int
	CycleStatPct         int
	CycleRewardPct       int
	PositionPct          int
	Difficulties         []DifficultyDef
	CritMultiplierPct    int
	DefendDamagePct      int
	BossTelegraphEvery   int
	BossChargeDamagePct  int
	Caps                 Caps
	MagicLevelBase       int
	MagicLevelGrowthPct  int
	SpellLevels          []SpellLevelDef
	StartingGold         int
	StartingPotions      []PotionStack
	BagCapacity          int
	DropChancePct        int
	BossDrops            int
	Rarities             []RarityDef
	RarityWeights        map[string]map[string]int
	MerchantStockSize    int
	MerchantMarkupPct    int
	SpellStatusDamagePct int
}

// Difficulty finds a difficulty by id.
func (b *Balance) Difficulty(id string) (DifficultyDef, error) {
	for _, difficulty := range b.Difficulties {
		if difficulty.ID == id {
			return difficulty, nil
		}
	}

	return DifficultyDef{}, UnknownIDError{ID: id}
}

// Rarity finds a rarity by id.
func (b *Balance) Rarity(id string) (RarityDef, error) {
	for _, rarity := range b.Rarities {
		if rarity.ID == id {
			return rarity, nil
		}
	}

	return RarityDef{}, UnknownIDError{ID: id}
}

// MustDifficulty is Difficulty for ids already validated by the engine.
func (b *Balance) MustDifficulty(id string) DifficultyDef {
	difficulty, err := b.Difficulty(id)
	if err != nil {
		panic(err)
	}

	return difficulty
}

// MustRarity is Rarity for ids produced by the engine itself.
func (b *Balance) MustRarity(id string) RarityDef {
	rarity, err := b.Rarity(id)
	if err != nil {
		panic(err)
	}

	return rarity
}

// GameData is the immutable, loaded game content with lookups.
type GameData struct {
	Balance      Balance
	Vocations    []VocationDef
	Spells       []SpellDef
	Monsters     []MonsterDef
	Bosses       []MonsterDef
	Potions      []PotionDef
	Statuses     []StatusDef
	Items        []ItemDef
	Affixes      []AffixDef
	Achievements []AchievementDef
	Families     []string

	vocations      map[string]*VocationDef
	spells         map[string]*SpellDef
	creatures      map[string]*MonsterDef
	potions        map[string]*PotionDef
	statuses       map[string]*StatusDef
	items          map[string]*ItemDef
	monstersByTier map[int][]*MonsterDef
	bossesByTier   map[int]*MonsterDef
}

// ByID compares ids in code-point order, like Python's default string comparison.
func ByID(a, b string) int {
	return strings.Compare(a, b)
}

// Index builds the lookup tables. Call it after changing any definition slice.
func (g *GameData) Index() *GameData {
	g.vocations = make(map[string]*VocationDef, len(g.Vocations))
	for i := range g.Vocations {
		g.vocations[g.Vocations[i].ID] = &g.Vocations[i]
	}

	g.spells = make(map[string]*SpellDef, len(g.Spells))
	for i := range g.Spells {
		g.spells[g.Spells[i].ID] = &g.Spells[i]
	}

	g.creatures = make(map[string]*MonsterDef, len(g.Monsters)+len(g.Bosses))

	g.monstersByTier = map[int][]*MonsterDef{}
	for i := range g.Monsters {
		monster := &g.Monsters[i]
		g.creatures[monster.ID] = monster
		g.monstersByTier[monster.Tier] = append(g.monstersByTier[monster.Tier], monster)
	}

	for tier := range g.monstersByTier {
		slices.SortFunc(g.monstersByTier[tier], func(a, b *MonsterDef) int { return ByID(a.ID, b.ID) })
	}

	g.bossesByTier = map[int]*MonsterDef{}
	for i := range g.Bosses {
		boss := &g.Bosses[i]
		g.creatures[boss.ID] = boss
		g.bossesByTier[boss.Tier] = boss
	}

	g.potions = make(map[string]*PotionDef, len(g.Potions))
	for i := range g.Potions {
		g.potions[g.Potions[i].ID] = &g.Potions[i]
	}

	g.statuses = make(map[string]*StatusDef, len(g.Statuses))
	for i := range g.Statuses {
		g.statuses[g.Statuses[i].ID] = &g.Statuses[i]
	}

	g.items = make(map[string]*ItemDef, len(g.Items))
	for i := range g.Items {
		g.items[g.Items[i].ID] = &g.Items[i]
	}

	return g
}

func lookup[T any](index map[string]*T, id string) *T {
	value, ok := index[id]
	if !ok {
		panic(UnknownIDError{ID: id})
	}

	return value
}

// TierCount is the number of tiers (one boss per tier).
func (g *GameData) TierCount() int { return len(g.Bosses) }

// Vocation looks up a vocation (panics with UnknownIDError on bad ids coming from code, not players).
func (g *GameData) Vocation(id string) *VocationDef { return lookup(g.vocations, id) }

// Spell looks up a spell.
func (g *GameData) Spell(id string) *SpellDef { return lookup(g.spells, id) }

// Creature looks up a monster or a boss.
func (g *GameData) Creature(id string) *MonsterDef { return lookup(g.creatures, id) }

// Potion looks up a potion.
func (g *GameData) Potion(id string) *PotionDef { return lookup(g.potions, id) }

// Status looks up a status.
func (g *GameData) Status(id string) *StatusDef { return lookup(g.statuses, id) }

// Item looks up an item.
func (g *GameData) Item(id string) *ItemDef { return lookup(g.items, id) }

// HasVocation reports whether a vocation exists.
func (g *GameData) HasVocation(id string) bool { _, ok := g.vocations[id]; return ok }

// HasPotion reports whether a potion exists.
func (g *GameData) HasPotion(id string) bool { _, ok := g.potions[id]; return ok }

// HasItem reports whether an item exists.
func (g *GameData) HasItem(id string) bool { _, ok := g.items[id]; return ok }

// HasCreature reports whether a creature exists.
func (g *GameData) HasCreature(id string) bool { _, ok := g.creatures[id]; return ok }

// HasSpell reports whether a spell exists.
func (g *GameData) HasSpell(id string) bool { _, ok := g.spells[id]; return ok }

// MonstersInTier returns the monsters of a tier sorted by id.
func (g *GameData) MonstersInTier(tier int) []*MonsterDef { return g.monstersByTier[tier] }

// BossOfTier returns the boss of a tier.
func (g *GameData) BossOfTier(tier int) *MonsterDef {
	boss, ok := g.bossesByTier[tier]
	if !ok {
		panic(UnknownIDError{ID: fmt.Sprintf("boss of tier %d", tier)})
	}

	return boss
}
