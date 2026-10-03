package application_test

// Auto-battle policy decisions (docs/game-design.md §13).

import (
	"reflect"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

const offenseTurn = 1

func autoBattle(t *testing.T, data *domain.GameData, vocation string, round int) *application.GameEngine {
	t.Helper()

	engine, _, err := application.NewRun(data, application.RunConfig{Name: "Auto", VocationID: vocation, DifficultyID: "normal"}, 11)
	if err != nil {
		t.Fatal(err)
	}

	engine.State.Round = round
	engine.Step(application.NextFight())

	return engine
}

func choose(t *testing.T, data *domain.GameData, engine *application.GameEngine, mode application.AutoBattleMode, turn int) application.Command {
	t.Helper()

	policy, err := application.NewAutoBattlePolicy(data, mode)
	if err != nil {
		t.Fatal(err)
	}

	engine.State.Turn = turn

	return policy.Choose(engine.State)
}

func supportTurn(t *testing.T, data *domain.GameData, mode application.AutoBattleMode) int {
	t.Helper()

	definition, err := data.Balance.AutoBattle.Mode(string(mode))
	if err != nil {
		t.Fatal(err)
	}

	return definition.SupportEvery - 1
}

func expectCommand(t *testing.T, got, want application.Command) {
	t.Helper()

	if !reflect.DeepEqual(got, want) {
		t.Fatalf("command = %+v, want %+v", got, want)
	}
}

func TestAutoBattle_OffensiveActionsPerMode(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := autoBattle(t, data, "warrior", 0)
	engine.State.Player.MP = 10_000

	expectCommand(t, choose(t, data, engine, application.AutoBattleMelee, offenseTurn+1), application.Attack())
	expectCommand(t, choose(t, data, engine, application.AutoBattleSpells, offenseTurn), application.Cast("annihilation"))
	expectCommand(t, choose(t, data, engine, application.AutoBattleBalanced, 2), application.Cast("annihilation"))

	engine.State.Player.MP = data.Spell("brutal_strike").Mana
	expectCommand(t, choose(t, data, engine, application.AutoBattleSpells, offenseTurn), application.Cast("brutal_strike"))

	engine.State.Player.MP = 0
	expectCommand(t, choose(t, data, engine, application.AutoBattleSpells, offenseTurn), application.Attack())

	if _, err := application.NewAutoBattlePolicy(data, "berserk"); err == nil {
		t.Fatal("unknown modes are refused")
	}
}

func TestAutoBattle_SupportTurnCadencePerMode(t *testing.T) {
	t.Parallel()

	data := testData(t)
	turns := []int{}

	for _, mode := range application.AutoBattleModes {
		turns = append(turns, supportTurn(t, data, mode))
	}

	if !reflect.DeepEqual(turns, []int{4, 4, 1}) {
		t.Fatalf("support turns = %v", turns)
	}
}

func TestAutoBattle_SupportTurnHealsBelowHalfHP(t *testing.T) {
	t.Parallel()

	data := testData(t)

	for _, mode := range application.AutoBattleModes {
		engine := autoBattle(t, data, "warrior", 0)
		player := engine.State.Player
		player.HP = domain.BuildSheet(player, data).MaxHP * 40 / 100
		player.MP = 10_000
		turn := supportTurn(t, data, mode)

		expectCommand(t, choose(t, data, engine, mode, turn), application.Cast("wound_cleansing"))

		player.MP = 0

		expectCommand(t, choose(t, data, engine, mode, turn), application.UsePotion("health_potion"))

		player.Potions = map[string]int{}

		expectCommand(t, choose(t, data, engine, mode, turn), application.Attack())
	}
}

func TestAutoBattle_HealWaitsForTheSupportTurnAboveTheEmergencyLine(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := autoBattle(t, data, "warrior", 0)
	player := engine.State.Player
	player.HP = domain.BuildSheet(player, data).MaxHP * 40 / 100

	expectCommand(t, choose(t, data, engine, application.AutoBattleMelee, offenseTurn), application.Attack())
}

func TestAutoBattle_EmergencyHealOnAnyTurn(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := autoBattle(t, data, "warrior", 0)
	player := engine.State.Player
	player.HP = domain.BuildSheet(player, data).MaxHP * 20 / 100
	player.MP = 10_000

	expectCommand(t, choose(t, data, engine, application.AutoBattleMelee, offenseTurn), application.Cast("wound_cleansing"))
}

func TestAutoBattle_BestPotionIsTheStrongestOwned(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := autoBattle(t, data, "warrior", 0)
	player := engine.State.Player
	player.HP, player.MP = 1, 0
	player.Potions["strong_health_potion"] = 1

	expectCommand(t, choose(t, data, engine, application.AutoBattleMelee, offenseTurn), application.UsePotion("strong_health_potion"))
}

func TestAutoBattle_SupportTurnDrinksManaWhenLow(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := autoBattle(t, data, "mage", 0)
	engine.State.Player.MP = 0
	turn := supportTurn(t, data, application.AutoBattleSpells)

	expectCommand(t, choose(t, data, engine, application.AutoBattleSpells, turn), application.UsePotion("mana_potion"))

	engine.State.Player.Potions = map[string]int{}
	expectCommand(t, choose(t, data, engine, application.AutoBattleSpells, turn), application.Attack())
}

func TestAutoBattle_SupportTurnDefendsAgainstATelegraphedCharge(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := autoBattle(t, data, "warrior", 9)

	boss := engine.State.Monster
	if !boss.IsBoss {
		t.Fatal("round 10 is a boss")
	}

	boss.BossActions = data.Balance.BossTelegraphEvery
	turn := supportTurn(t, data, application.AutoBattleMelee)

	expectCommand(t, choose(t, data, engine, application.AutoBattleMelee, turn), application.Defend())
	expectCommand(t, choose(t, data, engine, application.AutoBattleMelee, turn+1), application.Attack())

	boss.BossActions = 0

	expectCommand(t, choose(t, data, engine, application.AutoBattleMelee, turn), application.Attack())
}

func TestAutoBattle_PolicyFinishesFightsWithValidCommands(t *testing.T) {
	t.Parallel()

	data := testData(t)

	for _, mode := range application.AutoBattleModes {
		engine := autoBattle(t, data, "archer", 0)

		policy, err := application.NewAutoBattlePolicy(data, mode)
		if err != nil {
			t.Fatal(err)
		}

		for range 500 {
			if engine.State.Phase != domain.PhaseBattle {
				break
			}

			for _, evt := range engine.Step(policy.Choose(engine.State)) {
				if evt.Type() == "error" {
					t.Fatalf("%s: invalid command %v", mode, evt)
				}
			}
		}

		if engine.State.Phase == domain.PhaseBattle {
			t.Fatalf("%s: the fight did not end", mode)
		}
	}
}

func TestAutoBattle_PolicyIsDeterministic(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := autoBattle(t, data, "mage", 0)
	rngState := engine.RngState()
	run := func() []application.Command {
		commands := []application.Command{}

		for _, mode := range application.AutoBattleModes {
			for turn := range 6 {
				commands = append(commands, choose(t, data, engine, mode, turn))
			}
		}

		return commands
	}

	if !reflect.DeepEqual(run(), run()) || engine.RngState() != rngState {
		t.Fatal("the policy is deterministic and consumes no randomness")
	}
}
