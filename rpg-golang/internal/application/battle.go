package application

import (
	"slices"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

const (
	stunStatus        = "stun"
	stunCooldownTurns = 2
	targetPlayer      = "player"
	targetMonster     = "monster"
)

// BattleOutcome is the result of a battle turn.
type BattleOutcome int

// Battle outcomes.
const (
	OutcomeOngoing BattleOutcome = iota
	OutcomeVictory
	OutcomeDefeat
)

// Battle resolves turns following docs/game-design.md §6. Every RNG call here is part of the contract.
type Battle struct {
	data        *domain.GameData
	rng         *domain.Rng
	state       *RunState
	progression Progression
}

// NewBattle creates a battle over the current state.
func NewBattle(data *domain.GameData, rng *domain.Rng, state *RunState) *Battle {
	return &Battle{data: data, rng: rng, state: state, progression: NewProgression(data)}
}

func (b *Battle) player() *domain.Player { return b.state.Player }

func (b *Battle) monster() *domain.MonsterInstance {
	if b.state.Monster == nil {
		panic("battle without a monster")
	}

	return b.state.Monster
}

func (b *Battle) sheet() domain.CharacterSheet { return domain.BuildSheet(b.player(), b.data) }

// Validate returns an error event for an invalid command. Validation never consumes randomness.
func (b *Battle) Validate(command Command) Event {
	switch command.Type {
	case CmdCast:
		vocation := b.data.Vocation(b.player().VocationID)
		if !slices.Contains(vocation.Spells, command.SpellID) {
			return ErrorEvent(ErrUnknownSpell)
		}

		if b.player().MP < b.spellCost(b.data.Spell(command.SpellID)) {
			return ErrorEvent(ErrNotEnoughMana)
		}
	case CmdPotion:
		if !b.data.HasPotion(command.PotionID) {
			return ErrorEvent(ErrUnknownPotion)
		}

		if b.player().PotionCount(command.PotionID) <= 0 {
			return ErrorEvent(ErrNoPotion)
		}
	}

	return nil
}

func (b *Battle) spellCost(spell *domain.SpellDef) int {
	level := domain.SpellLevelForUses(b.player().SpellUses[spell.ID], b.data.Balance.SpellLevels)

	return domain.Pct(spell.Mana, level.ManaPct)
}

// PlayTurn resolves one player command and the monster's response.
func (b *Battle) PlayTurn(command Command) ([]Event, BattleOutcome) {
	events := []Event{}
	b.playerAction(command, &events)

	if b.monster().HP <= 0 {
		return events, OutcomeVictory
	}

	for {
		if b.monsterPhase(&events) {
			return events, OutcomeVictory
		}

		if b.player().HP <= 0 {
			return events, OutcomeDefeat
		}

		if b.endOfTurn(&events) {
			return events, OutcomeDefeat
		}

		if !consumeStun(&b.player().Statuses) {
			return events, OutcomeOngoing
		}

		b.player().StunCooldown = stunCooldownTurns

		events = append(events, Event{"type": "player_stunned"})
	}
}

// ── step 1: player action ─────────────────────────────────────────────────

func (b *Battle) playerAction(command Command, events *[]Event) {
	switch command.Type {
	case CmdAttack:
		b.melee(events)
	case CmdCast:
		b.cast(b.data.Spell(command.SpellID), events)
	case CmdPotion:
		b.drink(command.PotionID, events)
	case CmdDefend:
		b.player().Defending = true

		*events = append(*events, Event{"type": "player_defended"})
	}
}

func (b *Battle) melee(events *[]Event) {
	sheet := b.sheet()
	base := domain.Pct(b.rng.Roll(sheet.MeleeMin, sheet.MeleeMax), 100+sheet.PhysicalDamage)
	damage, crit := b.rollCrit(base, sheet)
	damage = b.resisted(damage, sheet.WeaponElement)
	b.hitMonster(damage)
	*events = append(*events, NewEvent("player_attacked", map[string]any{"damage": damage, "crit": crit, "element": string(sheet.WeaponElement)}))
	b.leech(damage, sheet, events)
}

func (b *Battle) cast(spell *domain.SpellDef, events *[]Event) {
	player := b.player()
	sheet := b.sheet()
	level := domain.SpellLevelForUses(player.SpellUses[spell.ID], b.data.Balance.SpellLevels)
	cost := domain.Pct(spell.Mana, level.ManaPct)
	player.MP -= cost
	bonus := player.Level*spell.PerLevel + player.MagicLevel*spell.PerMagicLevel
	amount := domain.Pct(domain.Pct(b.rng.Roll(spell.Min+bonus, spell.Max+bonus), level.EffectPct), 100+sheet.SpellPower)

	if spell.Kind == "attack" {
		damage, crit := b.rollCrit(amount, sheet)
		damage = b.resisted(damage, spell.Element)
		b.hitMonster(damage)
		*events = append(*events, NewEvent("spell_cast", map[string]any{
			"spellId": spell.ID, "damage": damage, "crit": crit, "element": string(spell.Element), "mana": cost,
		}))
		b.leech(damage, sheet, events)

		bonusEffect := spell.Level3Bonus
		if level.Level == 3 && bonusEffect.Status != "" && b.rng.Chance(bonusEffect.Chance) {
			perTurn := max(1, domain.Pct(damage, b.data.Balance.SpellStatusDamagePct))
			b.applyStatus(targetMonster, bonusEffect.Status, perTurn, events)
		}
	} else {
		healed := min(amount, sheet.MaxHP-player.HP)
		player.HP += healed
		*events = append(*events, NewEvent("spell_healed", map[string]any{"spellId": spell.ID, "amount": healed, "mana": cost}))

		if level.Level == 3 && spell.Level3Bonus.Cleanse {
			for _, status := range slices.Clone(player.Statuses) {
				player.Statuses = removeStatus(player.Statuses, status.StatusID)
				*events = append(*events, NewEvent("status_expired", map[string]any{"target": targetPlayer, "status": status.StatusID}))
			}
		}
	}

	*events = append(*events, b.progression.AfterCast(player, spell, cost)...)
}

func (b *Battle) drink(potionID string, events *[]Event) {
	player := b.player()
	sheet := b.sheet()
	potion := b.data.Potion(potionID)
	player.Potions[potionID]--
	amount := b.rng.Roll(potion.Min, potion.Max)

	var restored int
	if potion.Resource == "hp" {
		restored = min(amount, sheet.MaxHP-player.HP)
		player.HP += restored
	} else {
		restored = min(amount, sheet.MaxMP-player.MP)
		player.MP += restored
	}

	*events = append(*events, NewEvent("potion_used", map[string]any{"potionId": potionID, "amount": restored, "resource": potion.Resource}))
}

func (b *Battle) rollCrit(damage int, sheet domain.CharacterSheet) (int, bool) {
	if b.rng.Chance(sheet.CritChance) {
		return domain.Pct(damage, b.data.Balance.CritMultiplierPct+sheet.CritDamage), true
	}

	return damage, false
}

func (b *Battle) resisted(damage int, element domain.Element) int {
	resistance := b.data.Creature(b.monster().CreatureID).Resistance(element)
	if resistance == 0 {
		return 0
	}

	return max(1, domain.Pct(damage, resistance))
}

func (b *Battle) hitMonster(damage int) {
	b.monster().HP = max(0, b.monster().HP-damage)
}

func (b *Battle) leech(damage int, sheet domain.CharacterSheet, events *[]Event) {
	player := b.player()
	hpGain := min(domain.Pct(damage, sheet.LifeLeech), sheet.MaxHP-player.HP)

	mpGain := min(domain.Pct(damage, sheet.ManaLeech), sheet.MaxMP-player.MP)
	if hpGain <= 0 && mpGain <= 0 {
		return
	}

	player.HP += max(0, hpGain)
	player.MP += max(0, mpGain)
	*events = append(*events, NewEvent("leeched", map[string]any{"hp": max(0, hpGain), "mp": max(0, mpGain)}))
}

// ── step 3: monster phase ─────────────────────────────────────────────────

// monsterPhase returns true when the monster died from its own status ticks.
func (b *Battle) monsterPhase(events *[]Event) bool {
	monster := b.monster()

	b.tick(targetMonster, &monster.Statuses, events)

	if monster.HP <= 0 {
		return true
	}

	if consumeStun(&monster.Statuses) {
		monster.StunCooldown = stunCooldownTurns

		*events = append(*events, Event{"type": "monster_stunned"})

		return false
	}

	if monster.IsBoss {
		every := b.data.Balance.BossTelegraphEvery
		position := monster.BossActions % (every + 1)
		monster.BossActions++

		chargeID := b.data.Creature(monster.CreatureID).ChargeAttack
		if chargeID != "" && position == every-1 {
			charge := monster.Attack(chargeID)
			*events = append(*events, NewEvent("boss_telegraph", map[string]any{"attackId": charge.ID, "element": string(charge.Element)}))

			return false
		}

		if chargeID != "" && position == every {
			b.resolveMonsterAttack(monster.Attack(chargeID), true, events)

			return false
		}
	}

	weights := make([]int, len(monster.Attacks))
	for i, attack := range monster.Attacks {
		weights[i] = attack.Weight
	}

	b.resolveMonsterAttack(monster.Attacks[b.rng.Weighted(weights)], false, events)

	return false
}

func (b *Battle) resolveMonsterAttack(attack domain.MonsterAttack, charged bool, events *[]Event) {
	player := b.player()
	sheet := b.sheet()

	if b.rng.Chance(sheet.Dodge) {
		*events = append(*events, NewEvent("attack_dodged", map[string]any{"attackId": attack.ID}))

		return
	}

	if attack.Element == domain.Physical && b.rng.Chance(sheet.Parry) {
		*events = append(*events, NewEvent("attack_parried", map[string]any{"attackId": attack.ID}))

		return
	}

	damage := b.rng.Roll(attack.Min, attack.Max)
	if charged {
		damage = domain.Pct(damage, b.data.Balance.BossChargeDamagePct)
	}

	if attack.Element == domain.Physical {
		damage = domain.ArmorMitigation(damage, sheet.Armor)
	}

	damage = domain.Pct(damage, 100-sheet.Protection(attack.Element))
	if player.Defending {
		damage = domain.Pct(damage, b.data.Balance.DefendDamagePct)
	}

	damage = max(1, damage)
	player.HP = max(0, player.HP-damage)
	*events = append(*events, NewEvent("monster_attacked", map[string]any{
		"attackId": attack.ID, "damage": damage, "element": string(attack.Element), "charged": charged,
	}))

	if attack.Status != nil && b.rng.Chance(attack.Status.Chance) {
		perTurn := max(1, domain.Pct(damage, attack.Status.DamagePct))
		b.applyStatus(targetPlayer, attack.Status.Status, perTurn, events)
	}
}

// ── step 5: end of turn ───────────────────────────────────────────────────

// endOfTurn returns true when the player died from status ticks.
func (b *Battle) endOfTurn(events *[]Event) bool {
	player := b.player()

	b.tick(targetPlayer, &player.Statuses, events)

	if player.HP <= 0 {
		return true
	}

	sheet := b.sheet()
	hpGain := max(0, min(sheet.HPRegen, sheet.MaxHP-player.HP))
	mpGain := max(0, min(sheet.MPRegen, sheet.MaxMP-player.MP))
	player.HP += hpGain

	player.MP += mpGain
	if hpGain > 0 || mpGain > 0 {
		*events = append(*events, NewEvent("regenerated", map[string]any{"hp": hpGain, "mp": mpGain}))
	}

	player.Defending = false
	player.StunCooldown = max(0, player.StunCooldown-1)
	b.monster().StunCooldown = max(0, b.monster().StunCooldown-1)
	b.state.Turn++

	return false
}

// ── statuses ──────────────────────────────────────────────────────────────

func (b *Battle) applyStatus(target, statusID string, perTurn int, events *[]Event) {
	definition := b.data.Status(statusID)

	var (
		statuses *[]domain.ActiveStatus
		cooldown int
	)

	if target == targetPlayer {
		statuses, cooldown = &b.player().Statuses, b.player().StunCooldown
	} else {
		statuses, cooldown = &b.monster().Statuses, b.monster().StunCooldown
		if b.data.Creature(b.monster().CreatureID).Resistance(definition.Element) == 0 {
			return
		}
	}

	if definition.Kind == stunStatus {
		if cooldown > 0 || hasStatus(*statuses, stunStatus) {
			return
		}

		*statuses = append(*statuses, domain.ActiveStatus{StatusID: stunStatus, Turns: definition.Turns, PerTurn: 0})
		*events = append(*events, NewEvent("status_applied", map[string]any{"target": target, "status": stunStatus, "turns": definition.Turns, "perTurn": 0}))

		return
	}

	index := slices.IndexFunc(*statuses, func(s domain.ActiveStatus) bool { return s.StatusID == statusID })
	if index < 0 {
		*statuses = append(*statuses, domain.ActiveStatus{StatusID: statusID, Turns: definition.Turns, PerTurn: perTurn})
		index = len(*statuses) - 1
	} else {
		(*statuses)[index].Turns = definition.Turns
		(*statuses)[index].PerTurn = max((*statuses)[index].PerTurn, perTurn)
	}

	current := (*statuses)[index]
	*events = append(*events, NewEvent("status_applied", map[string]any{
		"target": target, "status": statusID, "turns": current.Turns, "perTurn": current.PerTurn,
	}))
}

func (b *Battle) tick(target string, statuses *[]domain.ActiveStatus, events *[]Event) {
	for _, status := range slices.Clone(*statuses) {
		definition := b.data.Status(status.StatusID)
		if definition.Kind != "dot" {
			continue
		}

		damage := b.statusDamage(target, status.PerTurn, definition.Element)
		if target == targetPlayer {
			b.player().HP = max(0, b.player().HP-damage)
		} else {
			b.hitMonster(damage)
		}

		*events = append(*events, NewEvent("status_ticked", map[string]any{"target": target, "status": status.StatusID, "damage": damage}))

		index := slices.IndexFunc(*statuses, func(s domain.ActiveStatus) bool { return s.StatusID == status.StatusID })
		(*statuses)[index].Turns--

		if (*statuses)[index].Turns <= 0 {
			*statuses = slices.Delete(*statuses, index, index+1)

			*events = append(*events, NewEvent("status_expired", map[string]any{"target": target, "status": status.StatusID}))
		}
	}
}

func (b *Battle) statusDamage(target string, perTurn int, element domain.Element) int {
	if target == targetPlayer {
		return max(1, domain.Pct(perTurn, 100-b.sheet().Protection(element)))
	}

	resistance := b.data.Creature(b.monster().CreatureID).Resistance(element)
	if resistance == 0 {
		return 0
	}

	return max(1, domain.Pct(perTurn, resistance))
}

func hasStatus(statuses []domain.ActiveStatus, statusID string) bool {
	return slices.ContainsFunc(statuses, func(s domain.ActiveStatus) bool { return s.StatusID == statusID })
}

func removeStatus(statuses []domain.ActiveStatus, statusID string) []domain.ActiveStatus {
	return slices.DeleteFunc(statuses, func(s domain.ActiveStatus) bool { return s.StatusID == statusID })
}

func consumeStun(statuses *[]domain.ActiveStatus) bool {
	index := slices.IndexFunc(*statuses, func(s domain.ActiveStatus) bool { return s.StatusID == stunStatus })
	if index < 0 {
		return false
	}

	*statuses = slices.Delete(*statuses, index, index+1)

	return true
}
