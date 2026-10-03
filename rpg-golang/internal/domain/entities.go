package domain

// ActiveStatus is a status currently affecting a creature.
type ActiveStatus struct {
	StatusID string `json:"statusId"`
	Turns    int    `json:"turns"`
	PerTurn  int    `json:"perTurn"`
}

// AffixRoll is one rolled affix of an item.
type AffixRoll struct {
	Stat  Stat `json:"stat"`
	Value int  `json:"value"`
}

// ItemInstance is a concrete item (base + rarity + affixes). JSON keys match the shared save format.
type ItemInstance struct {
	UID     int         `json:"uid"`
	ItemID  string      `json:"itemId"`
	Rarity  string      `json:"rarity"`
	Tier    int         `json:"tier"`
	Affixes []AffixRoll `json:"affixes"`
}

// Player is the player's mutable state. JSON keys match the shared save format (maps marshal with sorted keys).
type Player struct {
	Name         string                `json:"name"`
	VocationID   string                `json:"vocationId"`
	HP           int                   `json:"hp"`
	MP           int                   `json:"mp"`
	Gold         int                   `json:"gold"`
	Level        int                   `json:"level"`
	XP           int                   `json:"xp"`
	MagicLevel   int                   `json:"magicLevel"`
	ManaSpent    int                   `json:"manaSpent"`
	Potions      map[string]int        `json:"potions"`
	Equipment    map[Slot]ItemInstance `json:"equipment"`
	Bag          []ItemInstance        `json:"bag"`
	SpellUses    map[string]int        `json:"spellUses"`
	Statuses     []ActiveStatus        `json:"statuses"`
	StunCooldown int                   `json:"stunCooldown"`
	Defending    bool                  `json:"defending"`
}

// NewPlayer creates a level-1 player.
func NewPlayer(name, vocationID string, hp, mp, gold int) *Player {
	return &Player{
		Name:       name,
		VocationID: vocationID,
		HP:         hp,
		MP:         mp,
		Gold:       gold,
		Level:      1,
		MagicLevel: 1,
		Potions:    map[string]int{},
		Equipment:  map[Slot]ItemInstance{},
		Bag:        []ItemInstance{},
		SpellUses:  map[string]int{},
		Statuses:   []ActiveStatus{},
	}
}

// PotionCount returns how many potions of an id the player owns.
func (p *Player) PotionCount(potionID string) int {
	return p.Potions[potionID]
}

// MonsterInstance is a spawned monster with stats already scaled for the round and difficulty.
type MonsterInstance struct {
	CreatureID   string          `json:"creatureId"`
	IsBoss       bool            `json:"isBoss"`
	EnemyClass   string          `json:"enemyClass"`
	HP           int             `json:"hp"`
	MaxHP        int             `json:"maxHp"`
	XP           int             `json:"xp"`
	GoldMin      int             `json:"goldMin"`
	GoldMax      int             `json:"goldMax"`
	Attacks      []MonsterAttack `json:"attacks"`
	Statuses     []ActiveStatus  `json:"statuses"`
	StunCooldown int             `json:"stunCooldown"`
	BossActions  int             `json:"bossActions"`
}

// Attack finds an attack by id.
func (m *MonsterInstance) Attack(attackID string) MonsterAttack {
	for _, attack := range m.Attacks {
		if attack.ID == attackID {
			return attack
		}
	}

	panic("unknown attack: " + attackID)
}
