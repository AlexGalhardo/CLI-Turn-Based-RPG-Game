package domain

// ItemStats returns the stats of an item: base stats × rarity + affixes.
func ItemStats(item ItemInstance, data *GameData) map[Stat]int {
	definition := data.Item(item.ItemID)
	rarity := data.Balance.MustRarity(item.Rarity)

	stats := map[Stat]int{}
	for _, stat := range definition.Stats {
		stats[stat.Stat] += Pct(stat.Value, rarity.StatPct)
	}

	for _, affix := range item.Affixes {
		stats[affix.Stat] += affix.Value
	}

	return stats
}

// ItemValue is the sell price of an item.
func ItemValue(item ItemInstance, data *GameData) int {
	return Pct(data.Item(item.ItemID).Value, data.Balance.MustRarity(item.Rarity).ValuePct)
}

// CharacterSheet holds the derived stats of the player (docs/game-design.md §4).
type CharacterSheet struct {
	MaxHP          int
	MaxMP          int
	HPRegen        int
	MPRegen        int
	MeleeMin       int
	MeleeMax       int
	WeaponElement  Element
	Armor          int
	CritChance     int
	CritDamage     int
	SpellPower     int
	PhysicalDamage int
	Dodge          int
	Parry          int
	LifeLeech      int
	ManaLeech      int
	Protections    map[Element]int
}

// Protection returns the protection percentage for an element.
func (s CharacterSheet) Protection(element Element) int {
	return s.Protections[element]
}

// BuildSheet derives the player's stats from vocation, level and equipment.
func BuildSheet(player *Player, data *GameData) CharacterSheet {
	vocation := data.Vocation(player.VocationID)
	caps := data.Balance.Caps
	totals := map[Stat]int{}

	for _, item := range player.Equipment {
		for stat, value := range ItemStats(item, data) {
			totals[stat] += value
		}
	}

	weaponElement := Physical

	if weapon, ok := player.Equipment[SlotWeapon]; ok {
		if element := data.Item(weapon.ItemID).Element; element != "" {
			weaponElement = element
		}
	}

	levelBonus := (player.Level - 1) * vocation.MeleePerLevel
	attack := totals[StatAttack]

	protections := make(map[Element]int, len(Elements))
	for _, element := range Elements {
		protections[element] = min(totals[ProtectionByElement[element]], caps.Protection)
	}

	return CharacterSheet{
		MaxHP:          vocation.StartHP + (player.Level-1)*vocation.HPPerLevel + totals[StatMaxHp],
		MaxMP:          vocation.StartMP + (player.Level-1)*vocation.MPPerLevel + totals[StatMaxMp],
		HPRegen:        vocation.HPRegen + totals[StatHpRegen],
		MPRegen:        vocation.MPRegen + totals[StatMpRegen],
		MeleeMin:       vocation.MeleeMin + levelBonus + attack,
		MeleeMax:       vocation.MeleeMax + levelBonus + attack,
		WeaponElement:  weaponElement,
		Armor:          totals[StatArmor],
		CritChance:     min(totals[StatCritChance], caps.CritChance),
		CritDamage:     totals[StatCritDamage],
		SpellPower:     totals[StatSpellPower],
		PhysicalDamage: totals[StatPhysicalDamage],
		Dodge:          min(totals[StatDodge], caps.Dodge),
		Parry:          min(totals[StatParry], caps.Parry),
		LifeLeech:      min(totals[StatLifeLeech], caps.Leech),
		ManaLeech:      min(totals[StatManaLeech], caps.Leech),
		Protections:    protections,
	}
}
