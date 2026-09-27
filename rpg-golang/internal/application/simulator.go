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
	MinRound    int
	P10Round    int
	MedianRound int
	P90Round    int
	MaxRound    int
	MeanLevel   int
	TopKillers  []KillerCount
}

// PlayOne lets the bot play a run until death.
func PlayOne(data *domain.GameData, config RunConfig, seed uint64) (RunResult, error) {
	engine, _, err := NewRun(data, config, seed)
	if err != nil {
		return RunResult{}, err
	}

	bot := NewGreedyBot(data)

	for range maxStepsPerRun {
		if engine.State.Phase == domain.PhaseGameOver {
			state := engine.State

			return RunResult{Round: state.Round, Level: state.Player.Level, DeathCause: state.DeathCauseOr("")}, nil
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

	for i := range runs {
		result, err := PlayOne(data, RunConfig{Name: "Bot", VocationID: vocation, DifficultyID: difficulty}, baseSeed+uint64(i))
		if err != nil {
			return SimulationSummary{}, err
		}

		rounds = append(rounds, result.Round)
		killers[result.DeathCause]++
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
		Vocation: vocation, Difficulty: difficulty, Runs: runs,
		MinRound: rounds[0], P10Round: percentile(rounds, 10), MedianRound: percentile(rounds, 50),
		P90Round: percentile(rounds, 90), MaxRound: rounds[len(rounds)-1], MeanLevel: levels / runs,
		TopKillers: top[:min(3, len(top))],
	}, nil
}
