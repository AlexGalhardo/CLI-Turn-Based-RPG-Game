package application_test

// M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects, class
// drop tables, potion drops, spell level effects and the victory phase.

import (
	"maps"
	"reflect"
	"slices"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

const maxTurns = 300

func arpgFight(t *testing.T, engine *application.GameEngine, damage int, element domain.Element) *domain.MonsterInstance {
	t.Helper()

	engine.Step(application.NextFight())

	monster := engine.State.Monster
	if monster == nil {
		t.Fatal("no monster")
	}

	monster.Attacks = []domain.MonsterAttack{{ID: "test_hit", Element: element, Min: damage, Max: damage, Weight: 1}}
	monster.HP, monster.MaxHP = 10_000, 10_000

	return monster
}

// kill finishes the current fight with melee hits (the monster is left with 1 HP before each hit).
func kill(t *testing.T, engine *application.GameEngine) []application.Event {
	t.Helper()

	for range maxTurns {
		monster := engine.State.Monster
		if monster == nil {
			break
		}

		monster.HP = 1
		engine.State.Player.HP = 1_000_000

		events := engine.Step(application.Attack())
		if engine.State.Phase != domain.PhaseBattle {
			return events
		}
	}

	t.Fatal("the fight did not end")

	return nil
}

func physicalResistant100(data *domain.GameData) string {
	for _, monster := range data.Monsters {
		if monster.Resistance(domain.Physical) == 100 {
			return monster.ID
		}
	}

	return ""
}

func invalidPhase() []application.Event {
	return []application.Event{application.ErrorEvent(application.ErrInvalidPhase)}
}

// ── spawn ────────────────────────────────────────────────────────────────────

func TestEliteSpawnScalesStatsAndRewards(t *testing.T) {
	t.Parallel()

	normalData := calmData(t)
	eliteData := calmData(t)
	eliteData.Balance.EliteChancePct = 100
	difficulty := normalData.Balance.MustDifficulty("normal")
	normal, _ := application.SpawnMonster(normalData, domain.NewRng(5), 3, difficulty)
	elite, _ := application.SpawnMonster(eliteData, domain.NewRng(5), 3, difficulty)
	row := normalData.Balance.MustEnemyClass("elite")

	if normal.EnemyClass != "normal" || elite.EnemyClass != "elite" || elite.CreatureID != normal.CreatureID {
		t.Fatalf("classes %s/%s", normal.EnemyClass, elite.EnemyClass)
	}

	if elite.MaxHP != domain.Pct(normal.MaxHP, row.StatPct) || elite.Attacks[0].Max != domain.Pct(normal.Attacks[0].Max, row.StatPct) {
		t.Fatal("elite stats are scaled by statPct")
	}

	if elite.XP != domain.Pct(normal.XP, row.RewardPct) || elite.GoldMax != domain.Pct(normal.GoldMax, row.RewardPct) {
		t.Fatal("elite rewards are scaled by rewardPct")
	}
}

func TestEliteRollFollowsTheMonsterPick(t *testing.T) {
	t.Parallel()

	data := testData(t)
	difficulty := data.Balance.MustDifficulty("normal")
	rng := domain.NewRng(77)
	elites, normals := 0, 0

	for i := range 300 {
		monster, _ := application.SpawnMonster(data, rng, 1+i%9, difficulty)
		switch monster.EnemyClass {
		case "elite":
			elites++
		case "normal":
			normals++
		default:
			t.Fatalf("unexpected class %s", monster.EnemyClass)
		}
	}

	if normals == 0 || elites < 30 || elites > 90 {
		t.Fatalf("elites = %d", elites)
	}

	if boss, _ := application.SpawnMonster(data, domain.NewRng(1), 10, difficulty); boss.EnemyClass != "boss" {
		t.Fatal("bosses are of the boss class")
	}
}

func TestRoundStartedReportsTheEnemyClass(t *testing.T) {
	t.Parallel()

	data := freshData(t)
	data.Balance.EliteChancePct = 100
	engine := newEngine(t, data, "warrior", "normal", 42)

	started := engine.Step(application.NextFight())[0]
	if started.Str("enemyClass") != "elite" || started.Bool("isBoss") {
		t.Fatalf("round_started = %v", started)
	}
}

// ── monster dodge, parry, heal and crit ──────────────────────────────────────

func TestMonsterDodgeStopsMeleeAndSpells(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.Dodge = 100 })
	engine := newEngine(t, data, "warrior", "normal", 42)
	monster := arpgFight(t, engine, 1, domain.Physical)

	events := engine.Step(application.Attack())
	if events[0].Type() != "monster_dodged" || len(events[0]) != 1 || slices.Contains(eventTypes(events), "player_attacked") || monster.HP != monster.MaxHP {
		t.Fatalf("dodged melee: %v", events)
	}

	player := engine.State.Player
	player.MP = 1000

	events = engine.Step(application.Cast("brutal_strike"))
	if events[0].Type() != "monster_dodged" || player.MP >= 1000 || player.SpellUses["brutal_strike"] != 1 {
		t.Fatalf("a dodged cast still costs mana and counts: %v", events)
	}
}

func TestMonsterParryReflectsPhysicalHits(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.Parry = 100 })
	engine := newEngine(t, data, "warrior", "normal", 42)
	monster := arpgFight(t, engine, 1, domain.Physical)
	player := engine.State.Player

	if events := engine.Step(application.Defend()); slices.Contains(eventTypes(events), "monster_parried") {
		t.Fatal("defending is never parried")
	}

	hp := player.HP
	events := engine.Step(application.Attack())
	parried := events[0]

	if parried.Type() != "monster_parried" || parried.Int("reflected") < 1 || slices.Contains(eventTypes(events), "leeched") || monster.HP != monster.MaxHP {
		t.Fatalf("parried melee: %v", events)
	}

	hits, regen := 0, 0

	for _, evt := range events {
		switch evt.Type() {
		case "monster_attacked":
			hits += evt.Int("damage")
		case "regenerated":
			regen += evt.Int("hp")
		}
	}

	if player.HP != hp-parried.Int("reflected")-hits+regen {
		t.Fatal("the reflected damage hits the player")
	}
}

func TestMonsterParryReflectCanKillThePlayer(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.Parry = 100 })
	engine := newEngine(t, data, "warrior", "normal", 42)
	monster := arpgFight(t, engine, 1, domain.Physical)
	engine.State.Player.HP = 1

	events := engine.Step(application.Attack())
	if !reflect.DeepEqual(eventTypes(events), []string{"monster_parried", "player_died"}) || engine.State.Phase != domain.PhaseGameOver || monster.HP != monster.MaxHP {
		t.Fatalf("events = %v", eventTypes(events))
	}
}

func TestMonsterParryIgnoresNonPhysicalSpells(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.Parry = 100 })
	engine := newEngine(t, data, "mage", "normal", 42)
	arpgFight(t, engine, 1, domain.Physical)
	engine.State.Player.MP = 1000

	types := eventTypes(engine.Step(application.Cast("flame_strike")))
	if slices.Contains(types, "monster_parried") || !slices.Contains(types, "spell_cast") {
		t.Fatalf("events = %v", types)
	}
}

func TestMonsterHealsInsteadOfAttacking(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.Heal = 100 })
	engine := newEngine(t, data, "warrior", "normal", 42)
	monster := arpgFight(t, engine, 1, domain.Physical)

	if types := eventTypes(engine.Step(application.Defend())); slices.Contains(types, "monster_healed") || !slices.Contains(types, "monster_attacked") {
		t.Fatalf("no heal at full HP: %v", types)
	}

	monster.HP = monster.MaxHP - 5

	events := engine.Step(application.Defend())
	if healed, ok := lookupEvent(events, "monster_healed"); !ok || healed.Int("amount") != 5 || slices.Contains(eventTypes(events), "monster_attacked") {
		t.Fatalf("heal capped at the missing HP: %v", events)
	}

	monster.HP = 100

	healed, _ := lookupEvent(engine.Step(application.Defend()), "monster_healed")
	if healed.Int("amount") != domain.Pct(monster.MaxHP, data.Balance.MonsterHealPct) {
		t.Fatalf("heal = %v", healed)
	}
}

func TestHealingBossDoesNotAdvanceItsPattern(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "boss", func(row *domain.EnemyClassDef) { row.Heal = 100 })
	engine := newEngine(t, data, "warrior", "normal", 42)
	engine.State.Round = 9
	engine.Step(application.NextFight())

	boss := engine.State.Monster
	boss.HP = boss.MaxHP - 1
	engine.State.Player.HP = 1_000_000
	engine.Step(application.Defend())

	if boss.BossActions != 0 {
		t.Fatal("a healing boss keeps its pattern")
	}
}

func TestMonsterCritMultipliesRawDamage(t *testing.T) {
	t.Parallel()

	for _, tt := range []struct{ crit, expected int }{{0, 100}, {100, 150}} {
		data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.Crit = tt.crit })
		engine := newEngine(t, data, "warrior", "normal", 42)
		arpgFight(t, engine, 100, domain.Fire)
		engine.State.Player.HP = 1000

		hit, _ := lookupEvent(engine.Step(application.Attack()), "monster_attacked")
		if hit.Bool("crit") != (tt.crit == 100) || hit.Int("damage") != tt.expected {
			t.Fatalf("crit %d: %v", tt.crit, hit)
		}
	}
}

func TestPlayerParryReflectsDamageToTheMonster(t *testing.T) {
	t.Parallel()

	data := calmData(t)
	data.Items = append(data.Items, domain.ItemDef{
		ID: "test_parry_shield", Name: "Test Shield", Slot: domain.SlotShield, Type: "shield",
		Stats: []domain.StatValue{{Stat: domain.StatParry, Value: 100}}, Value: 10,
	})
	data.Index()
	data.Balance.Caps = domain.Caps{CritChance: data.Balance.Caps.CritChance, Dodge: 0, Parry: 100, Leech: 25, Protection: 75}
	engine := newEngine(t, data, "warrior", "normal", 42)
	engine.State.Player.Equipment[domain.SlotShield] = domain.ItemInstance{UID: 90, ItemID: "test_parry_shield", Rarity: "common", Affixes: []domain.AffixRoll{}}

	if domain.BuildSheet(engine.State.Player, data).Parry != 100 {
		t.Fatal("parry 100 expected")
	}

	monster := arpgFight(t, engine, 50, domain.Physical)

	events := engine.Step(application.Defend())
	want := application.Event{"type": "attack_parried", "attackId": "test_hit", "reflected": 10}

	if !slices.ContainsFunc(events, func(evt application.Event) bool { return reflect.DeepEqual(evt, want) }) || monster.HP != monster.MaxHP-10 || engine.State.Stats.Parries != 1 {
		t.Fatalf("events = %v", events)
	}

	monster.HP = 5

	if types := eventTypes(engine.Step(application.Defend())); !slices.Contains(types, "monster_killed") || engine.State.Phase != domain.PhaseMerchant {
		t.Fatal("a reflected parry can kill the monster")
	}
}

// ── victory, drops and potions ───────────────────────────────────────────────

func drops(events []application.Event) []application.Event {
	result := []application.Event{}

	for _, evt := range events {
		if evt.Type() == "item_dropped" {
			result = append(result, evt)
		}
	}

	return result
}

func TestNormalDropTable(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.DropChancePct = 100 })
	engine := newEngine(t, data, "warrior", "normal", 42)
	arpgFight(t, engine, 1, domain.Physical)
	events := kill(t, engine)

	dropped := drops(events)
	if len(dropped) != 1 || (dropped[0].Str("rarity") != "common" && dropped[0].Str("rarity") != "rare") || slices.Contains(eventTypes(events), "potion_dropped") {
		t.Fatalf("drops = %v", dropped)
	}

	data = withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.DropChancePct = 0 })
	engine = newEngine(t, data, "warrior", "normal", 42)
	arpgFight(t, engine, 1, domain.Physical)

	if len(drops(kill(t, engine))) != 0 {
		t.Fatal("no drop at 0%")
	}
}

func TestEliteDropsAGoodItemAndMaybeAPotion(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "elite", func(row *domain.EnemyClassDef) { row.PotionDropPct = 100 })
	data.Balance.EliteChancePct = 100
	engine := newEngine(t, data, "warrior", "normal", 42)
	arpgFight(t, engine, 1, domain.Physical)

	before := map[string]int{}
	maps.Copy(before, engine.State.Player.Potions)

	events := kill(t, engine)

	dropped := drops(events)
	if len(dropped) != 1 || (dropped[0].Str("rarity") != "rare" && dropped[0].Str("rarity") != "legendary") {
		t.Fatalf("drops = %v", dropped)
	}

	potion, ok := lookupEvent(events, "potion_dropped")
	potionID := potion.Str("potionId")

	if !ok || data.Potion(potionID).UnlockRound > 1 || engine.State.Player.Potions[potionID] != before[potionID]+1 {
		t.Fatalf("potion drop = %v", potion)
	}

	killed, _ := lookupEvent(events, "monster_killed")
	if engine.State.Stats.PotionsDropped[potionID] != 1 || engine.State.Stats.ElitesKilled != 1 || killed.Str("enemyClass") != "elite" {
		t.Fatal("elite statistics")
	}
}

func TestPotionDropWithoutUnlockedPotionsConsumesNothing(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.DropChancePct, row.PotionDropPct = 0, 100 })
	for i := range data.Potions {
		data.Potions[i].UnlockRound = 99
	}

	engine := newEngine(t, data, "warrior", "normal", 42)
	arpgFight(t, engine, 1, domain.Physical)

	if slices.Contains(eventTypes(kill(t, engine)), "potion_dropped") {
		t.Fatal("no unlocked potion, no drop")
	}
}

func TestBossDropsSeveralTopItems(t *testing.T) {
	t.Parallel()

	data := calmData(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	engine.State.Round = 9
	arpgFight(t, engine, 1, domain.Physical)

	dropped := drops(kill(t, engine))
	if len(dropped) != data.Balance.MustEnemyClass("boss").Drops {
		t.Fatalf("drops = %v", dropped)
	}

	for _, evt := range dropped {
		if rarity := evt.Str("rarity"); rarity != "legendary" && rarity != "mythic" {
			t.Fatalf("boss rarity %s", rarity)
		}
	}
}

func TestFullBagAutoSellsDrops(t *testing.T) {
	t.Parallel()

	data := withEnemyClass(calmData(t), "normal", func(row *domain.EnemyClassDef) { row.DropChancePct = 100 })
	engine := newEngine(t, data, "warrior", "normal", 42)
	player := engine.State.Player

	for i := range data.Balance.BagCapacity {
		player.Bag = append(player.Bag, domain.ItemInstance{UID: 500 + i, ItemID: "sword", Rarity: "common", Affixes: []domain.AffixRoll{}})
	}

	arpgFight(t, engine, 1, domain.Physical)

	if !slices.Contains(eventTypes(kill(t, engine)), "item_auto_sold") || len(player.Bag) != data.Balance.BagCapacity {
		t.Fatal("a full bag auto-sells the drop")
	}
}

func finalVictory(t *testing.T, engine *application.GameEngine) []application.Event {
	t.Helper()

	engine.State.Round = engine.Data.Balance.FinalRound - 1
	arpgFight(t, engine, 1, domain.Physical)

	return kill(t, engine)
}

func TestBeatingTheFinalBossEntersTheVictoryPhase(t *testing.T) {
	t.Parallel()

	data := calmData(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	events := finalVictory(t, engine)

	last := events[len(events)-1]
	if !reflect.DeepEqual(last, application.Event{"type": "run_won", "round": data.Balance.FinalRound}) || slices.Contains(eventTypes(events), "merchant_entered") {
		t.Fatalf("events = %v", eventTypes(events))
	}

	if engine.State.Phase != domain.PhaseVictory || !engine.State.Won {
		t.Fatal("victory phase expected")
	}

	rngState := engine.RngState()
	stock := slices.Clone(engine.State.MerchantStock)

	for _, command := range []application.Command{
		application.Attack(), application.Defend(), application.NextFight(), application.BuyPotion("health_potion", 1),
		application.Equip(1), application.SellItem(1),
	} {
		if events := engine.Step(command); !reflect.DeepEqual(events, invalidPhase()) {
			t.Fatalf("%s in victory: %v", command.Type, events)
		}
	}

	if engine.RngState() != rngState || !reflect.DeepEqual(engine.State.MerchantStock, stock) {
		t.Fatal("invalid commands consume nothing")
	}

	if events := engine.Step(application.EndRun()); !reflect.DeepEqual(events, []application.Event{{"type": "run_ended", "won": true}}) {
		t.Fatalf("end_run = %v", events)
	}

	if engine.State.Phase != domain.PhaseGameOver || engine.State.DeathCause != nil {
		t.Fatal("a won run ends without a cause of death")
	}

	if events := engine.Step(application.ContinueRun()); !reflect.DeepEqual(events, invalidPhase()) {
		t.Fatal("continue_run after the end")
	}
}

func TestContinueRunEntersTheMerchant(t *testing.T) {
	t.Parallel()

	data := calmData(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	finalVictory(t, engine)

	events := engine.Step(application.ContinueRun())
	if !reflect.DeepEqual(events, []application.Event{{"type": "merchant_entered", "round": data.Balance.FinalRound}}) {
		t.Fatalf("continue_run = %v", events)
	}

	if engine.State.Phase != domain.PhaseMerchant || len(engine.State.MerchantStock) != data.Balance.MerchantStockSize {
		t.Fatal("the merchant generates its stock")
	}

	engine.Step(application.NextFight())

	if engine.State.Round != data.Balance.FinalRound+1 || !engine.State.Won {
		t.Fatal("the run goes on and stays won")
	}
}

func TestVictoryCommandsAreRejectedOutsideTheVictoryPhase(t *testing.T) {
	t.Parallel()

	engine := newEngine(t, testData(t), "warrior", "normal", 42)

	if !reflect.DeepEqual(engine.Step(application.EndRun()), invalidPhase()) || !reflect.DeepEqual(engine.Step(application.ContinueRun()), invalidPhase()) {
		t.Fatal("victory commands outside the victory phase")
	}
}

// ── spell levels ─────────────────────────────────────────────────────────────

func TestSpellLevelEffectScalesDamage(t *testing.T) {
	t.Parallel()

	for _, tt := range []struct{ uses, effect int }{{0, 100}, {20, 150}, {50, 200}} {
		data := calmData(t)
		engine := newEngine(t, data, "warrior", "normal", 42)
		arpgFight(t, engine, 1, domain.Physical)
		engine.State.Monster.CreatureID = physicalResistant100(data)
		player := engine.State.Player
		player.SpellUses["brutal_strike"] = tt.uses
		player.MP = 1000

		rng := domain.NewRng(uint64(engine.RngState()))
		spell := data.Spell("brutal_strike")
		bonus := player.Level*spell.PerLevel + player.MagicLevel*spell.PerMagicLevel
		base := rng.Roll(spell.Min+bonus, spell.Max+bonus)

		cast, _ := lookupEvent(engine.Step(application.Cast("brutal_strike")), "spell_cast")
		if cast.Int("damage") != max(1, domain.Pct(base, tt.effect)) {
			t.Fatalf("uses %d: %v", tt.uses, cast)
		}
	}
}

func TestSpellLevelEffectScalesHealing(t *testing.T) {
	t.Parallel()

	data := calmData(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	arpgFight(t, engine, 1, domain.Physical)
	player := engine.State.Player
	player.SpellUses["wound_cleansing"] = 20
	player.MP = 1000
	player.HP = 1

	rng := domain.NewRng(uint64(engine.RngState()))
	spell := data.Spell("wound_cleansing")
	bonus := player.Level*spell.PerLevel + player.MagicLevel*spell.PerMagicLevel
	expected := domain.Pct(rng.Roll(spell.Min+bonus, spell.Max+bonus), 150)
	room := domain.BuildSheet(player, data).MaxHP - player.HP

	healed, _ := lookupEvent(engine.Step(application.Cast("wound_cleansing")), "spell_healed")
	if healed.Int("amount") != min(expected, room) {
		t.Fatalf("healed = %v", healed)
	}
}
