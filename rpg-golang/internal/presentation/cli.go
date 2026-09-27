package presentation

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"slices"
	"strconv"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/version"
)

// ErrUsage wraps command-line usage errors (exit code 2).
var ErrUsage = errors.New("usage error")

// CliOptions are the flags shared by the three implementations (docs/tui.md).
type CliOptions struct {
	Seed       *uint64
	Lang       string
	NoAnim     bool
	DataDir    string
	Simulate   int
	Vocation   string
	Difficulty string
}

// CliResult tells the caller whether to run the game or print and exit.
type CliResult struct {
	Options CliOptions
	Exit    bool
	Output  string
}

// Help mirrors the argparse help of the Python reference.
const Help = `usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]
           [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]
           [--difficulty DIFFICULTY]

Endless turn-based RPG for the terminal.

options:
  -h, --help            show this help message and exit
  --version             show program's version number and exit
  --seed SEED           deterministic run
  --lang {en,pt-BR}     override the saved language
  --no-anim             disable animations
  --data-dir DATA_DIR   saves/profile location
  --simulate N          run N headless bot games and print a report
  --vocation VOCATION   (simulator) restrict to one vocation
  --difficulty DIFFICULTY
                        (simulator) restrict to one difficulty`

// ParseCli parses command-line arguments.
func ParseCli(args []string) (CliResult, error) {
	flags := flag.NewFlagSet("rpg", flag.ContinueOnError)
	flags.SetOutput(io.Discard)

	var (
		options  CliOptions
		seed     string
		simulate string
		showHelp bool
		showVer  bool
	)

	flags.BoolVar(&showHelp, "h", false, "")
	flags.BoolVar(&showHelp, "help", false, "")
	flags.BoolVar(&showVer, "version", false, "")
	flags.StringVar(&seed, "seed", "", "")
	flags.StringVar(&options.Lang, "lang", "", "")
	flags.BoolVar(&options.NoAnim, "no-anim", false, "")
	flags.StringVar(&options.DataDir, "data-dir", "", "")
	flags.StringVar(&simulate, "simulate", "", "")
	flags.StringVar(&options.Vocation, "vocation", "", "")
	flags.StringVar(&options.Difficulty, "difficulty", "", "")

	if err := flags.Parse(args); err != nil {
		return CliResult{}, fmt.Errorf("%w: %w", ErrUsage, err)
	}

	if showHelp {
		return CliResult{Exit: true, Output: Help}, nil
	}

	if showVer {
		return CliResult{Exit: true, Output: fmt.Sprintf("rpg %s (golang)", version.Version)}, nil
	}

	if options.Lang != "" && !slices.Contains(infrastructure.SupportedLocales, options.Lang) {
		return CliResult{}, fmt.Errorf("%w: argument --lang: invalid choice: '%s' (choose from 'en', 'pt-BR')", ErrUsage, options.Lang)
	}

	if seed != "" {
		value, err := strconv.ParseUint(seed, 10, 64)
		if err != nil {
			return CliResult{}, fmt.Errorf("%w: argument --seed: must be >= 0", ErrUsage)
		}

		options.Seed = &value
	}

	if simulate != "" {
		value, err := strconv.Atoi(simulate)
		if err != nil || value <= 0 {
			return CliResult{}, fmt.Errorf("%w: argument --simulate: must be > 0", ErrUsage)
		}

		options.Simulate = value
	}

	return CliResult{Options: options}, nil
}
