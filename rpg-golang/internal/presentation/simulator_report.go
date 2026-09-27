package presentation

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

var reportHeader = []string{"vocation", "difficulty", "runs", "min", "p10", "median", "p90", "max", "avg lvl", "top killers"}

// RenderReport renders simulator summaries as an aligned table (same output as the reference).
func RenderReport(summaries []application.SimulationSummary, data *domain.GameData) string {
	rows := [][]string{reportHeader}

	for _, s := range summaries {
		killers := make([]string, 0, len(s.TopKillers))
		for _, killer := range s.TopKillers {
			killers = append(killers, fmt.Sprintf("%s (%d)", data.Creature(killer.CreatureID).Name, killer.Count))
		}

		rows = append(rows, []string{
			s.Vocation, s.Difficulty, strconv.Itoa(s.Runs), strconv.Itoa(s.MinRound), strconv.Itoa(s.P10Round),
			strconv.Itoa(s.MedianRound), strconv.Itoa(s.P90Round), strconv.Itoa(s.MaxRound), strconv.Itoa(s.MeanLevel),
			strings.Join(killers, ", "),
		})
	}

	widths := make([]int, len(reportHeader))

	for _, row := range rows {
		for i, cell := range row {
			widths[i] = max(widths[i], len(cell))
		}
	}

	lines := make([]string, 0, len(rows)+1)

	for _, row := range rows {
		cells := make([]string, len(row))
		for i, cell := range row {
			cells[i] = cell + strings.Repeat(" ", widths[i]-len(cell))
		}

		lines = append(lines, strings.TrimRight(strings.Join(cells, "  "), " "))
	}

	separator := make([]string, len(widths))
	for i, width := range widths {
		separator[i] = strings.Repeat("-", width)
	}

	lines = append(lines[:1], append([]string{strings.Join(separator, "  ")}, lines[1:]...)...)

	return strings.Join(lines, "\n")
}
