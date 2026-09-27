package application_test

import (
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

func types(events []application.Event) []string {
	result := make([]string, len(events))
	for i, evt := range events {
		result[i] = evt.Type()
	}

	return result
}

func countType(events []application.Event, eventType string) int {
	count := 0

	for _, evt := range events {
		if evt.Type() == eventType {
			count++
		}
	}

	return count
}

func findEvent(t *testing.T, events []application.Event, eventType string) application.Event {
	t.Helper()

	for _, evt := range events {
		if evt.Type() == eventType {
			return evt
		}
	}

	t.Fatalf("no %s event in %v", eventType, types(events))

	return nil
}

func fight(t *testing.T, engine *application.GameEngine) *domain.MonsterInstance {
	t.Helper()
	engine.Step(application.NextFight())

	if engine.State.Monster == nil {
		t.Fatal("no monster after next fight")
	}

	return engine.State.Monster
}

func fixedAttack(monster *domain.MonsterInstance, damage int, element domain.Element, status *domain.StatusOnHit) {
	monster.Attacks = []domain.MonsterAttack{{ID: "test_hit", Element: element, Min: damage, Max: damage, Weight: 1, Status: status}}
	monster.HP, monster.MaxHP = 10_000, 10_000
}

func TestBattle_Validate(t *testing.T) {
	t.Parallel()

	engine := newEngine(t, testData(t), "warrior", "normal", 42)
	if got := engine.Step(application.Attack()); got[0].Str("code") != application.ErrInvalidPhase {
		t.Fatal("battle commands are invalid at the merchant")
	}

	fight(t, engine)
	rngState := engine.RngState()
	engine.State.Player.MP = 0
	engine.State.Player.Potions = map[string]int{}

	tests := []struct {
		name    string
		command application.Command
		code    string
	}{
		{"spell of another vocation", application.Cast("flame_strike"), application.ErrUnknownSpell},
		{"not enough mana", application.Cast("brutal_strike"), application.ErrNotEnoughMana},
		{"no potion", application.UsePotion("health_potion"), application.ErrNoPotion},
		{"unknown potion", application.UsePotion("elixir"), application.ErrUnknownPotion},
		{"next fight in battle", application.NextFight(), application.ErrInvalidPhase},
		{"merchant command in battle", application.BuyPotion("health_potion", 1), application.ErrInvalidPhase},
	}
	for _, tt := range tests {
		if got := engine.Step(tt.command); len(got) != 1 || got[0].Str("code") != tt.code {
			t.Fatalf("%s: got %v", tt.name, got)
		}
	}

	if engine.RngState() != rngState {
		t.Fatal("invalid commands must not consume randomness")
	}
}

func TestBattle_PlayTurn(t *testing.T) {
	t.Parallel()

	t.Run("defend halves damage", func(t *testing.T) {
		t.Parallel()

		engine := newEngine(t, testData(t), "warrior", "normal", 42)
		fixedAttack(fight(t, engine), 100, domain.Physical, nil)
		engine.State.Player.HP = 150

		if damage := findEvent(t, engine.Step(application.Defend()), "monster_attacked").Int("damage"); damage != 50 {
			t.Fatalf("defended damage = %d, want 50", damage)
		}
	})

	t.Run("big hits kill", func(t *testing.T) {
		t.Parallel()

		engine := newEngine(t, testData(t), "warrior", "normal", 42)
		monster := fight(t, engine)
		fixedAttack(monster, 1000, domain.Physical, nil)

		died := findEvent(t, engine.Step(application.Attack()), "player_died")
		if died.Str("monsterId") != monster.CreatureID || engine.State.Phase != domain.PhaseGameOver {
			t.Fatal("the player must die")
		}
	})

	t.Run("statuses apply refresh tick and expire", func(t *testing.T) {
		t.Parallel()

		engine := newEngine(t, testData(t), "warrior", "normal", 42)
		fixedAttack(fight(t, engine), 10, domain.Fire, &domain.StatusOnHit{Status: "burn", Chance: 100, DamagePct: 50})
		engine.State.Player.Statuses = append(engine.State.Player.Statuses, domain.ActiveStatus{StatusID: "bleed", Turns: 1, PerTurn: 1})
		events := engine.Step(application.Defend())

		applied := findEvent(t, events, "status_applied")
		if applied.Int("perTurn") != 2 || applied.Int("turns") != 3 {
			t.Fatalf("unexpected status: %v", applied)
		}

		if countType(events, "status_expired") != 1 || len(engine.State.Player.Statuses) != 1 {
			t.Fatalf("bleed must expire and burn stay: %v", engine.State.Player.Statuses)
		}

		engine.State.Player.Statuses[0].PerTurn = 9
		engine.Step(application.Defend())

		if engine.State.Player.Statuses[0].PerTurn != 9 {
			t.Fatal("refresh keeps the higher per-turn damage")
		}
	})

	t.Run("stuns skip turns with cooldown", func(t *testing.T) {
		t.Parallel()

		engine := newEngine(t, testData(t), "warrior", "normal", 42)
		fixedAttack(fight(t, engine), 1, domain.Physical, &domain.StatusOnHit{Status: "stun", Chance: 100})
		events := engine.Step(application.Defend())

		if countType(events, "monster_attacked") != 2 || countType(events, "player_stunned") != 1 || engine.State.Player.StunCooldown != 1 {
			t.Fatalf("stun flow wrong: %v", types(events))
		}

		if countType(engine.Step(application.Defend()), "status_applied") != 0 {
			t.Fatal("cooldown must block a second stun")
		}
	})

	t.Run("monster stun and death by tick", func(t *testing.T) {
		t.Parallel()

		engine := newEngine(t, testData(t), "warrior", "normal", 42)
		monster := fight(t, engine)
		fixedAttack(monster, 5, domain.Physical, nil)
		monster.Statuses = append(monster.Statuses, domain.ActiveStatus{StatusID: "stun", Turns: 1})

		if events := engine.Step(application.Defend()); countType(events, "monster_stunned") != 1 || countType(events, "monster_attacked") != 0 {
			t.Fatal("a stunned monster skips its attack")
		}

		monster.HP = 1
		monster.Statuses = append(monster.Statuses, domain.ActiveStatus{StatusID: "bleed", Turns: 3, PerTurn: 5})

		if countType(engine.Step(application.Defend()), "monster_killed") != 1 || engine.State.Phase != domain.PhaseMerchant {
			t.Fatal("the monster must die from its status tick")
		}
	})

	t.Run("bosses telegraph then charge", func(t *testing.T) {
		t.Parallel()

		engine := newEngine(t, testData(t), "warrior", "normal", 42)
		engine.State.Round = 9
		boss := fight(t, engine)
		boss.HP, boss.MaxHP = 1_000_000, 1_000_000

		for i := range boss.Attacks {
			boss.Attacks[i].Status = nil
		}

		engine.State.Player.HP = 1_000_000
		sequence := []string{}

		for range 4 {
			for _, evt := range engine.Step(application.Defend()) {
				if evt.Type() == "boss_telegraph" {
					sequence = append(sequence, "telegraph")
				} else if evt.Type() == "monster_attacked" {
					sequence = append(sequence, map[bool]string{true: "charged", false: "normal"}[evt.Bool("charged")])
				}
			}
		}

		if len(sequence) != 4 || sequence[2] != "telegraph" || sequence[3] != "charged" || sequence[0] != "normal" {
			t.Fatalf("boss pattern = %v", sequence)
		}
	})
}

func TestBattle_Spells(t *testing.T) {
	t.Parallel()

	t.Run("heal caps and level 3 cleanses", func(t *testing.T) {
		t.Parallel()

		data := testData(t)
		engine := newEngine(t, data, "warrior", "normal", 42)
		fixedAttack(fight(t, engine), 1, domain.Physical, nil)
		player := engine.State.Player
		player.SpellUses["wound_cleansing"] = 50
		player.Statuses = append(player.Statuses, domain.ActiveStatus{StatusID: "poison", Turns: 5, PerTurn: 1})
		player.HP = domain.BuildSheet(player, data).MaxHP - 3
		events := engine.Step(application.Cast("wound_cleansing"))

		healed := findEvent(t, events, "spell_healed")
		if healed.Int("amount") != 3 || healed.Int("mana") != 32 || countType(events, "status_expired") != 1 {
			t.Fatalf("heal wrong: %v", events)
		}
	})

	t.Run("spell and magic levels grow", func(t *testing.T) {
		t.Parallel()

		data := testData(t)
		engine := newEngine(t, data, "warrior", "normal", 42)
		fixedAttack(fight(t, engine), 1, domain.Physical, nil)
		player := engine.State.Player
		player.SpellUses["brutal_strike"] = 19
		player.MP = 1000
		player.ManaSpent = data.Balance.MagicLevelBase - 1
		events := engine.Step(application.Cast("brutal_strike"))

		if findEvent(t, events, "spell_level_up").Int("level") != 2 || countType(events, "magic_level_up") == 0 {
			t.Fatalf("levels must grow: %v", types(events))
		}
	})

	t.Run("immune monsters take no damage", func(t *testing.T) {
		t.Parallel()

		engine := newEngine(t, testData(t), "mage", "normal", 42)
		monster := fight(t, engine)
		fixedAttack(monster, 1, domain.Physical, nil)
		monster.CreatureID = "fire_elemental"
		engine.State.Player.MP = 1000

		if findEvent(t, engine.Step(application.Cast("flame_strike")), "spell_cast").Int("damage") != 0 {
			t.Fatal("immune monsters take 0 damage")
		}
	})

	for _, vocation := range []string{"warrior", "archer", "mage"} {
		t.Run("every "+vocation+" spell can be cast", func(t *testing.T) {
			t.Parallel()

			data := testData(t)
			engine := newEngine(t, data, vocation, "normal", 42)
			fixedAttack(fight(t, engine), 1, domain.Physical, nil)

			for _, spellID := range data.Vocation(vocation).Spells {
				engine.State.Player.MP, engine.State.Player.HP = 10_000, 5
				events := engine.Step(application.Cast(spellID))

				if countType(events, "spell_cast")+countType(events, "spell_healed") != 1 {
					t.Fatalf("%s was not cast: %v", spellID, types(events))
				}
			}
		})
	}
}

func TestBattle_ItemsAndPotions(t *testing.T) {
	t.Parallel()

	data := freshData(t)
	data.Items = append(data.Items, domain.ItemDef{
		ID: "test_ring", Name: "Test Ring", Slot: domain.SlotRing, Type: "ring",
		Stats: []domain.StatValue{{Stat: domain.StatCritChance, Value: 80}, {Stat: domain.StatDodge, Value: 90}, {Stat: domain.StatLifeLeech, Value: 50}}, Value: 100,
	})
	data.Index()

	engine := newEngine(t, data, "warrior", "normal", 42)
	engine.State.Player.Equipment[domain.SlotRing] = domain.ItemInstance{UID: 99, ItemID: "test_ring", Rarity: "common"}
	fixedAttack(fight(t, engine), 5, domain.Physical, nil)

	crits, dodges, leeches := 0, 0, 0

	for range 60 {
		engine.State.Player.HP = 100
		for _, evt := range engine.Step(application.Attack()) {
			switch evt.Type() {
			case "player_attacked":
				if evt.Bool("crit") {
					crits++
				}
			case "attack_dodged":
				dodges++
			case "leeched":
				leeches++
			}
		}
	}

	if crits == 0 || dodges == 0 || leeches == 0 {
		t.Fatalf("items must add crit, dodge and leech: %d %d %d", crits, dodges, leeches)
	}

	engine.State.Player.MP = 0
	before := engine.State.Player.PotionCount("mana_potion")

	if findEvent(t, engine.Step(application.UsePotion("mana_potion")), "potion_used").Str("resource") != "mp" ||
		engine.State.Player.PotionCount("mana_potion") != before-1 {
		t.Fatal("potions restore and are consumed")
	}
}
