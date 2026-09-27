package application_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// goldenDir points at the repository's shared/golden, recorded by the Python reference.
var goldenDir = filepath.Join("..", "..", "..", "shared", "golden")

type goldenScenario struct {
	Name       string                `json:"name"`
	Seed       uint64                `json:"seed"`
	Config     application.RunConfig `json:"config"`
	Commands   []json.RawMessage     `json:"commands"`
	Events     []json.RawMessage     `json:"events"`
	FinalState json.RawMessage       `json:"finalState"`
	FinalRun   json.RawMessage       `json:"finalRun"`
}

// normalise decodes JSON into generic values so documents compare structurally (key order does not matter).
func normalise(t *testing.T, value any) any {
	t.Helper()

	raw, ok := value.(json.RawMessage)
	if !ok {
		var err error

		raw, err = json.Marshal(value)
		if err != nil {
			t.Fatalf("marshal: %v", err)
		}
	}

	var decoded any
	if err := json.Unmarshal(raw, &decoded); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}

	return decoded
}

func scenarioFiles(t *testing.T) []string {
	t.Helper()

	entries, err := os.ReadDir(goldenDir)
	if err != nil {
		t.Fatalf("read golden dir: %v", err)
	}

	files := []string{}

	for _, entry := range entries {
		if strings.HasSuffix(entry.Name(), ".json") && entry.Name() != "prng.json" {
			files = append(files, entry.Name())
		}
	}

	slices.Sort(files)

	return files
}

func loadScenario(t *testing.T, file string) goldenScenario {
	t.Helper()

	content, err := os.ReadFile(filepath.Join(goldenDir, file))
	if err != nil {
		t.Fatalf("read %s: %v", file, err)
	}

	var scenario goldenScenario
	if err := json.Unmarshal(content, &scenario); err != nil {
		t.Fatalf("parse %s: %v", file, err)
	}

	return scenario
}

func TestPRNGReferenceVectors(t *testing.T) {
	t.Parallel()

	content, err := os.ReadFile(filepath.Join(goldenDir, "prng.json"))
	if err != nil {
		t.Fatalf("read prng.json: %v", err)
	}

	var document struct {
		Vectors []struct {
			Seed    uint64   `json:"seed"`
			Outputs []uint32 `json:"outputs"`
		} `json:"vectors"`
	}
	if err := json.Unmarshal(content, &document); err != nil {
		t.Fatalf("parse prng.json: %v", err)
	}

	for _, vector := range document.Vectors {
		rng := domain.NewRng(vector.Seed)
		for index, expected := range vector.Outputs {
			if got := rng.NextU32(); got != expected {
				t.Fatalf("seed %d output %d = %d, want %d", vector.Seed, index, got, expected)
			}
		}
	}
}

func TestGoldenScenarios(t *testing.T) {
	t.Parallel()

	data := testData(t)
	files := scenarioFiles(t)

	if len(files) < 11 {
		t.Fatalf("expected at least 11 golden scenarios, got %d", len(files))
	}

	for _, file := range files {
		t.Run(strings.TrimSuffix(file, ".json"), func(t *testing.T) {
			t.Parallel()

			scenario := loadScenario(t, file)

			engine, first, err := application.NewRun(data, scenario.Config, scenario.Seed)
			if err != nil {
				t.Fatalf("new run: %v", err)
			}

			if !reflect.DeepEqual(normalise(t, first), normalise(t, scenario.Events[0])) {
				t.Fatalf("run creation events differ:\n got %s", mustJSON(t, first))
			}

			for index, raw := range scenario.Commands {
				command, err := application.CommandFromJSON(raw)
				if err != nil {
					t.Fatalf("command %d: %v", index+1, err)
				}

				events := engine.Step(command)
				if !reflect.DeepEqual(normalise(t, events), normalise(t, scenario.Events[index+1])) {
					t.Fatalf("command #%d %s: events differ\n got  %s\n want %s", index+1, raw, mustJSON(t, events), scenario.Events[index+1])
				}
			}

			if !reflect.DeepEqual(normalise(t, finalState(engine)), normalise(t, scenario.FinalState)) {
				t.Fatalf("final state differs:\n got  %s\n want %s", mustJSON(t, finalState(engine)), scenario.FinalState)
			}

			// Save-format parity: the Go run state serialises exactly like the Python one, both ways.
			if !reflect.DeepEqual(normalise(t, engine.State), normalise(t, scenario.FinalRun)) {
				t.Fatalf("final run differs:\n got  %s\n want %s", mustJSON(t, engine.State), scenario.FinalRun)
			}

			restored, err := application.RunStateFromJSON(scenario.FinalRun)
			if err != nil {
				t.Fatalf("restore final run: %v", err)
			}

			if !reflect.DeepEqual(normalise(t, restored), normalise(t, scenario.FinalRun)) {
				t.Fatalf("final run does not round-trip")
			}
		})
	}
}

func TestGoldenBotParity(t *testing.T) {
	t.Parallel()

	data := testData(t)

	for _, file := range scenarioFiles(t) {
		if !strings.HasPrefix(file, "bot-full-run-") {
			continue
		}

		t.Run(strings.TrimSuffix(file, ".json"), func(t *testing.T) {
			t.Parallel()

			scenario := loadScenario(t, file)

			engine, _, err := application.NewRun(data, scenario.Config, scenario.Seed)
			if err != nil {
				t.Fatalf("new run: %v", err)
			}

			bot := application.NewGreedyBot(data)
			commands := []map[string]any{}

			for engine.State.Phase != domain.PhaseGameOver {
				command := bot.Choose(engine.State)
				commands = append(commands, command.ToMap())
				engine.Step(command)
			}

			if !reflect.DeepEqual(normalise(t, commands), normalise(t, scenario.Commands)) {
				t.Fatalf("the Go bot diverged from the recorded Python commands")
			}
		})
	}
}

func finalState(engine *application.GameEngine) map[string]any {
	state := engine.State
	player := state.Player

	return map[string]any{
		"phase":       state.Phase,
		"round":       state.Round,
		"turn":        state.Turn,
		"level":       player.Level,
		"xp":          player.XP,
		"magicLevel":  player.MagicLevel,
		"hp":          player.HP,
		"mp":          player.MP,
		"gold":        player.Gold,
		"rngState":    engine.RngState(),
		"nextItemUid": state.NextItemUID,
		"stats":       state.Stats,
	}
}

func mustJSON(t *testing.T, value any) string {
	t.Helper()

	raw, err := json.Marshal(value)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}

	return string(raw)
}
