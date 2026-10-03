package application

import (
	"errors"
	"fmt"
	"slices"
	"strings"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

const maxStepsPerRun = 200_000

// ErrInvalidSimulation is returned for a non-positive number of runs.
var ErrInvalidSimulation = errors.New("runs must be positive")

// RunResult is the outcome of one simulated run.
type RunResult struct {
	Round      int
	Level      int
	DeathCause string
	Won        bool
}

// KillerCount counts deaths caused by a creature.
type KillerCount struct {
	CreatureID string
	Count      int
}

// SimulationSummary aggregates simulated runs.
type SimulationSummary struct {
	Vocation    string
	Difficulty  string
	Runs        int
	Wins        int
	MinRound    int
	P10Round    int
	MedianRound int
	P90Round    int
	MaxRound    int
	MeanLevel   int
	TopKillers  []KillerCount
}

// WinRatePct is floor(wins * 100 / runs).
func (s SimulationSummary) WinRatePct() int {
	return s.Wins * 100 / s.Runs
}

// PlayOne lets the bot play a run until it ends (death, or end_run after the final boss).
func PlayOne(data *domain.GameData, config RunConfig, seed uint64) (RunResult, error) {
	engine, _, err := NewRun(data, config, seed)
	if err != nil {
		return RunResult{}, err
	}

	bot := NewGreedyBot(data)

	for range maxStepsPerRun {
		if engine.State.Phase == domain.PhaseGameOver {
			state := engine.State

			return RunResult{Round: state.Round, Level: state.Player.Level, DeathCause: state.DeathCauseOr(""), Won: state.Won}, nil
		}

		engine.Step(bot.Choose(engine.State))
	}

	return RunResult{}, fmt.Errorf("run did not finish (seed %d)", seed)
}

func percentile(sorted []int, percent int) int {
	return sorted[min(len(sorted)-1, len(sorted)*percent/100)]
}

// Simulate runs `runs` games with consecutive seeds and aggregates how far the bot gets.
func Simulate(data *domain.GameData, vocation, difficulty string, runs int, baseSeed uint64) (SimulationSummary, error) {
	if runs <= 0 {
		return SimulationSummary{}, ErrInvalidSimulation
	}

	rounds := make([]int, 0, runs)
	killers := map[string]int{}
	levels := 0
	wins := 0

	for i := range runs {
		result, err := PlayOne(data, RunConfig{Name: "Bot", VocationID: vocation, DifficultyID: difficulty}, baseSeed+uint64(i))
		if err != nil {
			return SimulationSummary{}, err
		}

		rounds = append(rounds, result.Round)
		if result.DeathCause != "" {
			killers[result.DeathCause]++
		}

		if result.Won {
			wins++
		}

		levels += result.Level
	}

	slices.Sort(rounds)

	top := make([]KillerCount, 0, len(killers))
	for id, count := range killers {
		top = append(top, KillerCount{CreatureID: id, Count: count})
	}

	slices.SortFunc(top, func(a, b KillerCount) int {
		if a.Count != b.Count {
			return b.Count - a.Count
		}

		return strings.Compare(a.CreatureID, b.CreatureID)
	})

	return SimulationSummary{
		Vocation: vocation, Difficulty: difficulty, Runs: runs, Wins: wins,
		MinRound: rounds[0], P10Round: percentile(rounds, 10), MedianRound: percentile(rounds, 50),
		P90Round: percentile(rounds, 90), MaxRound: rounds[len(rounds)-1], MeanLevel: levels / runs,
		TopKillers: top[:min(3, len(top))],
	}, nil
}
