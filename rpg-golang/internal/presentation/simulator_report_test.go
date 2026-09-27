package presentation_test

import (
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
)

func TestRenderReport(t *testing.T) {
	t.Parallel()

	report := presentation.RenderReport([]application.SimulationSummary{{
		Vocation: "mage", Difficulty: "hard", Runs: 1, MinRound: 3, P10Round: 3, MedianRound: 3, P90Round: 3, MaxRound: 3,
		MeanLevel: 2, TopKillers: []application.KillerCount{{CreatureID: "rat", Count: 1}},
	}}, testData(t))

	if !strings.Contains(report, "median") || !strings.Contains(report, "Rat (1)") || !strings.Contains(report, "----") {
		t.Fatalf("report = %s", report)
	}
}
