// Package infrastructure contains the adapters: shared data loading, i18n, ASCII art, file repositories and paths.
package infrastructure

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// ErrData is wrapped by every data loading error.
var ErrData = errors.New("invalid game data")

type rawStatusOnHit struct {
	ID        string `json:"id"`
	Chance    int    `json:"chance"`
	DamagePct int    `json:"damagePct"`
}

type rawAttack struct {
	ID      string          `json:"id"`
	Element string          `json:"element"`
	Min     int             `json:"min"`
	Max     int             `json:"max"`
	Weight  int             `json:"weight"`
	Status  *rawStatusOnHit `json:"status"`
}

type rawCreature struct {
	Schema       string                 `json:"$schema"`
	ID           string                 `json:"id"`
	Name         string                 `json:"name"`
	Tier         int                    `json:"tier"`
	Family       string                 `json:"family"`
	HP           int                    `json:"hp"`
	XP           int                    `json:"xp"`
	Gold         struct{ Min, Max int } `json:"gold"`
	Attacks      []rawAttack            `json:"attacks"`
	Resistances  map[string]int         `json:"resistances"`
	ChargeAttack string                 `json:"chargeAttack"`
}

type rawSpell struct {
	ID            string `json:"id"`
	Name          string `json:"name"`
	Words         string `json:"words"`
	Kind          string `json:"kind"`
	Element       string `json:"element"`
	Mana          int    `json:"mana"`
	Min           int    `json:"min"`
	Max           int    `json:"max"`
	PerLevel      int    `json:"perLevel"`
	PerMagicLevel int    `json:"perMagicLevel"`
	Level3Bonus   struct {
		Status  string `json:"status"`
		Chance  int    `json:"chance"`
		Cleanse bool   `json:"cleanse"`
	} `json:"level3Bonus"`
}

type rawVocation struct {
	ID            string   `json:"id"`
	Name          string   `json:"name"`
	StartHP       int      `json:"startHp"`
	StartMP       int      `json:"startMp"`
	HPPerLevel    int      `json:"hpPerLevel"`
	MPPerLevel    int      `json:"mpPerLevel"`
	HPRegen       int      `json:"hpRegen"`
	MPRegen       int      `json:"mpRegen"`
	MeleeMin      int      `json:"meleeMin"`
	MeleeMax      int      `json:"meleeMax"`
	MeleePerLevel int      `json:"meleePerLevel"`
	WeaponTypes   []string `json:"weaponTypes"`
	ShieldTypes   []string `json:"shieldTypes"`
	StarterWeapon string   `json:"starterWeapon"`
	Spells        []string `json:"spells"`
}

type rawBalance struct {
	RoundsPerTier  int `json:"roundsPerTier"`
	CycleStatPct   int `json:"cycleStatPct"`
	CycleRewardPct int `json:"cycleRewardPct"`
	PositionPct    int `json:"positionPct"`
	Difficulties   []struct {
		ID                 string `json:"id"`
		HPPct              int    `json:"hpPct"`
		DamagePct          int    `json:"damagePct"`
		GoldPct            int    `json:"goldPct"`
		XPPct              int    `json:"xpPct"`
		NonCommonWeightPct int    `json:"nonCommonWeightPct"`
	} `json:"difficulties"`
	CritMultiplierPct   int `json:"critMultiplierPct"`
	DefendDamagePct     int `json:"defendDamagePct"`
	BossTelegraphEvery  int `json:"bossTelegraphEvery"`
	BossChargeDamagePct int `json:"bossChargeDamagePct"`
	Caps                struct {
		CritChance int `json:"critChance"`
		Dodge      int `json:"dodge"`
		Parry      int `json:"parry"`
		Leech      int `json:"leech"`
		Protection int `json:"protection"`
	} `json:"caps"`
	MagicLevel struct {
		Base      int `json:"base"`
		GrowthPct int `json:"growthPct"`
	} `json:"magicLevel"`
	SpellLevels []struct {
		Level     int `json:"level"`
		Uses      int `json:"uses"`
		EffectPct int `json:"effectPct"`
		ManaPct   int `json:"manaPct"`
	} `json:"spellLevels"`
	StartingGold    int `json:"startingGold"`
	StartingPotions []struct {
		PotionID string `json:"potionId"`
		Quantity int    `json:"quantity"`
	} `json:"startingPotions"`
	BagCapacity   int `json:"bagCapacity"`
	DropChancePct int `json:"dropChancePct"`
	BossDrops     int `json:"bossDrops"`
	Rarities      []struct {
		ID       string `json:"id"`
		StatPct  int    `json:"statPct"`
		ValuePct int    `json:"valuePct"`
		AffixMin int    `json:"affixMin"`
		AffixMax int    `json:"affixMax"`
	} `json:"rarities"`
	RarityWeights        map[string]map[string]int `json:"rarityWeights"`
	MerchantStockSize    int                       `json:"merchantStockSize"`
	MerchantMarkupPct    int                       `json:"merchantMarkupPct"`
	SpellStatusDamagePct int                       `json:"spellStatusDamagePct"`
}

// rawItem keeps `stats` as raw JSON so the stat order of the file is preserved.
type rawItem struct {
	ID      string          `json:"id"`
	Name    string          `json:"name"`
	Slot    string          `json:"slot"`
	Type    string          `json:"type"`
	Tier    int             `json:"tier"`
	Element string          `json:"element"`
	Stats   json.RawMessage `json:"stats"`
	Value   int             `json:"value"`
}

func readDocument(shared fs.FS, name string, target any) error {
	content, err := fs.ReadFile(shared, "data/"+name+".json")
	if err != nil {
		return fmt.Errorf("%w: read %s.json: %w", ErrData, name, err)
	}

	if err := json.Unmarshal(content, target); err != nil {
		return fmt.Errorf("%w: %s.json: %w", ErrData, name, err)
	}

	return nil
}

func convertAttacks(raw []rawAttack) []domain.MonsterAttack {
	attacks := make([]domain.MonsterAttack, len(raw))
	for i, attack := range raw {
		attacks[i] = domain.MonsterAttack{
			ID: attack.ID, Element: domain.Element(attack.Element), Min: attack.Min, Max: attack.Max, Weight: attack.Weight,
		}
		if attack.Status != nil {
			attacks[i].Status = &domain.StatusOnHit{Status: attack.Status.ID, Chance: attack.Status.Chance, DamagePct: attack.Status.DamagePct}
		}
	}

	return attacks
}

func convertCreatures(raw []rawCreature, isBoss bool) []domain.MonsterDef {
	creatures := make([]domain.MonsterDef, len(raw))
	for i, creature := range raw {
		resistances := make(map[domain.Element]int, len(creature.Resistances))
		for element, value := range creature.Resistances {
			resistances[domain.Element(element)] = value
		}

		creatures[i] = domain.MonsterDef{
			ID: creature.ID, Name: creature.Name, Tier: creature.Tier, Family: creature.Family, HP: creature.HP, XP: creature.XP,
			GoldMin: creature.Gold.Min, GoldMax: creature.Gold.Max, Attacks: convertAttacks(creature.Attacks),
			Resistances: resistances, IsBoss: isBoss, ChargeAttack: creature.ChargeAttack,
		}
	}

	return creatures
}

// orderedStats decodes a JSON object into stat/value pairs in file order.
func orderedStats(raw json.RawMessage) ([]domain.StatValue, error) {
	decoder := json.NewDecoder(bytes.NewReader(raw))
	if _, err := decoder.Token(); err != nil {
		return nil, fmt.Errorf("stats: %w", err)
	}

	stats := []domain.StatValue{}

	for decoder.More() {
		key, err := decoder.Token()
		if err != nil {
			return nil, fmt.Errorf("stats key: %w", err)
		}

		var value int
		if err := decoder.Decode(&value); err != nil {
			return nil, fmt.Errorf("stats value: %w", err)
		}

		name, _ := key.(string)
		stats = append(stats, domain.StatValue{Stat: domain.Stat(name), Value: value})
	}

	return stats, nil
}

func convertBalance(raw *rawBalance) domain.Balance {
	balance := domain.Balance{
		RoundsPerTier: raw.RoundsPerTier, CycleStatPct: raw.CycleStatPct, CycleRewardPct: raw.CycleRewardPct,
		PositionPct: raw.PositionPct, CritMultiplierPct: raw.CritMultiplierPct, DefendDamagePct: raw.DefendDamagePct,
		BossTelegraphEvery: raw.BossTelegraphEvery, BossChargeDamagePct: raw.BossChargeDamagePct,
		Caps: domain.Caps{
			CritChance: raw.Caps.CritChance, Dodge: raw.Caps.Dodge, Parry: raw.Caps.Parry, Leech: raw.Caps.Leech,
			Protection: raw.Caps.Protection,
		},
		MagicLevelBase: raw.MagicLevel.Base, MagicLevelGrowthPct: raw.MagicLevel.GrowthPct, StartingGold: raw.StartingGold,
		BagCapacity: raw.BagCapacity, DropChancePct: raw.DropChancePct, BossDrops: raw.BossDrops,
		RarityWeights: raw.RarityWeights, MerchantStockSize: raw.MerchantStockSize,
		MerchantMarkupPct: raw.MerchantMarkupPct, SpellStatusDamagePct: raw.SpellStatusDamagePct,
	}
	for _, d := range raw.Difficulties {
		balance.Difficulties = append(balance.Difficulties, domain.DifficultyDef{
			ID: d.ID, HPPct: d.HPPct, DamagePct: d.DamagePct, GoldPct: d.GoldPct, XPPct: d.XPPct, NonCommonWeightPct: d.NonCommonWeightPct,
		})
	}

	for _, s := range raw.SpellLevels {
		balance.SpellLevels = append(balance.SpellLevels, domain.SpellLevelDef{Level: s.Level, Uses: s.Uses, EffectPct: s.EffectPct, ManaPct: s.ManaPct})
	}

	for _, p := range raw.StartingPotions {
		balance.StartingPotions = append(balance.StartingPotions, domain.PotionStack{PotionID: p.PotionID, Quantity: p.Quantity})
	}

	for _, r := range raw.Rarities {
		balance.Rarities = append(balance.Rarities, domain.RarityDef{ID: r.ID, StatPct: r.StatPct, ValuePct: r.ValuePct, AffixMin: r.AffixMin, AffixMax: r.AffixMax})
	}

	return balance
}

// LoadGameData parses shared/data (untrusted JSON, validated by schemas in CI) into domain definitions.
func LoadGameData(shared fs.FS) (*domain.GameData, error) {
	var (
		balance      rawBalance
		vocations    struct{ Vocations []rawVocation }
		spells       struct{ Spells []rawSpell }
		monsters     struct{ Monsters []rawCreature }
		bosses       struct{ Bosses []rawCreature }
		items        struct{ Items []rawItem }
		families     struct{ Families []string }
		potions      struct{ Potions []domain.PotionDef }
		statuses     struct{ Statuses []domain.StatusDef }
		affixes      struct{ Affixes []domain.AffixDef }
		achievements struct{ Achievements []domain.AchievementDef }
	)

	documents := []struct {
		name   string
		target any
	}{
		{"balance", &balance},
		{"vocations", &vocations},
		{"spells", &spells},
		{"monsters", &monsters},
		{"bosses", &bosses},
		{"items", &items},
		{"families", &families},
		{"potions", &potions},
		{"statuses", &statuses},
		{"affixes", &affixes},
		{"achievements", &achievements},
	}
	for _, document := range documents {
		if err := readDocument(shared, document.name, document.target); err != nil {
			return nil, err
		}
	}

	data := &domain.GameData{
		Balance:      convertBalance(&balance),
		Monsters:     convertCreatures(monsters.Monsters, false),
		Bosses:       convertCreatures(bosses.Bosses, true),
		Potions:      potions.Potions,
		Statuses:     statuses.Statuses,
		Affixes:      affixes.Affixes,
		Achievements: achievements.Achievements,
		Families:     families.Families,
	}

	for _, v := range vocations.Vocations {
		data.Vocations = append(data.Vocations, domain.VocationDef(v))
	}

	for _, s := range spells.Spells {
		data.Spells = append(data.Spells, domain.SpellDef{
			ID: s.ID, Name: s.Name, Words: s.Words, Kind: s.Kind, Element: domain.Element(s.Element), Mana: s.Mana,
			Min: s.Min, Max: s.Max, PerLevel: s.PerLevel, PerMagicLevel: s.PerMagicLevel,
			Level3Bonus: domain.Level3Bonus{Status: s.Level3Bonus.Status, Chance: s.Level3Bonus.Chance, Cleanse: s.Level3Bonus.Cleanse},
		})
	}

	for _, item := range items.Items {
		stats, err := orderedStats(item.Stats)
		if err != nil {
			return nil, fmt.Errorf("%w: item %s: %w", ErrData, item.ID, err)
		}

		data.Items = append(data.Items, domain.ItemDef{
			ID: item.ID, Name: item.Name, Slot: domain.Slot(item.Slot), Type: item.Type, Tier: item.Tier,
			Element: domain.Element(item.Element), Stats: stats, Value: item.Value,
		})
	}

	if len(data.Vocations) == 0 || len(data.Bosses) == 0 || len(data.Balance.SpellLevels) == 0 {
		return nil, fmt.Errorf("%w: vocations, bosses and spell levels are required", ErrData)
	}

	return data.Index(), nil
}
