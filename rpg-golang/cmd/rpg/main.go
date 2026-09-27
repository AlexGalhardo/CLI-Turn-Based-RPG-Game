// Command rpg is the Go implementation of the CLI Turn-Based RPG.
package main

import (
	"fmt"
	"io"
	"os"
	"strings"

	tea "charm.land/bubbletea/v2"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation/tui"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/version"
)

// writef and writeLine write to the terminal; a failed write to stdout/stderr has no better place to be reported.
func writef(w io.Writer, format string, args ...any) {
	_, _ = fmt.Fprintf(w, format, args...)
}

func writeLine(w io.Writer, args ...any) {
	_, _ = fmt.Fprintln(w, args...)
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}

// run is main without process exit, so it can be tested.
func run(args []string, stdout, stderr io.Writer) int {
	result, err := presentation.ParseCli(args)
	if err != nil {
		writef(stderr, "rpg: error: %s\n", strings.TrimPrefix(err.Error(), presentation.ErrUsage.Error()+": "))

		return 2
	}

	if result.Exit {
		writeLine(stdout, result.Output)

		return 0
	}

	data, err := infrastructure.LoadGameData(assets.Shared())
	if err != nil {
		writef(stderr, "rpg: %v\n", err)

		return 1
	}

	options := result.Options
	if options.Simulate > 0 {
		return runSimulator(data, options, stdout, stderr)
	}

	return runTUI(data, options, stderr)
}

func runSimulator(data *domain.GameData, options presentation.CliOptions, stdout, stderr io.Writer) int {
	vocations := []string{options.Vocation}
	if options.Vocation == "" {
		vocations = nil
		for _, vocation := range data.Vocations {
			vocations = append(vocations, vocation.ID)
		}
	}

	difficulties := []string{options.Difficulty}
	if options.Difficulty == "" {
		difficulties = nil
		for _, difficulty := range data.Balance.Difficulties {
			difficulties = append(difficulties, difficulty.ID)
		}
	}

	baseSeed := uint64(1)
	if options.Seed != nil {
		baseSeed = *options.Seed
	}

	summaries := []application.SimulationSummary{}

	for _, vocation := range vocations {
		for _, difficulty := range difficulties {
			summary, err := application.Simulate(data, vocation, difficulty, options.Simulate, baseSeed)
			if err != nil {
				writef(stderr, "error: %v\n", err)

				return 2
			}

			summaries = append(summaries, summary)
		}
	}

	writeLine(stdout, presentation.RenderReport(summaries, data))

	return 0
}

// BuildServices wires the real adapters for a data directory.
func BuildServices(data *domain.GameData, dataDir string) presentation.Services {
	return presentation.Services{
		Data:     data,
		Shared:   assets.Shared(),
		Settings: infrastructure.NewSettingsRepository(dataDir),
		Repositories: application.Repositories{
			Saves:   infrastructure.NewFileSaveRepository(dataDir),
			History: infrastructure.NewFileHistoryRepository(dataDir),
			Profile: infrastructure.NewFileProfileRepository(dataDir),
		},
		Clock:   infrastructure.SystemClock{},
		Version: version.Version,
	}
}

func runTUI(data *domain.GameData, options presentation.CliOptions, stderr io.Writer) int {
	services := BuildServices(data, infrastructure.ResolveDataDir(options.DataDir))

	controller, err := presentation.NewController(services, options.Seed, options.Lang, nil)
	if err != nil {
		writef(stderr, "rpg: %v\n", err)

		return 1
	}

	animate := !options.NoAnim && os.Getenv("RPG_NO_ANIM") == ""
	model := tui.NewModel(controller, infrastructure.NewArtLibrary(assets.Shared()), animate)

	if _, err := tea.NewProgram(model).Run(); err != nil {
		writef(stderr, "rpg: %v\n", err)

		return 1
	}

	if controller.Session != nil {
		if err := controller.Session.SaveAndQuit(); err != nil {
			writef(stderr, "rpg: %v\n", err)

			return 1
		}
	}

	return 0
}
