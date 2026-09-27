package infrastructure

import (
	"os"
	"path/filepath"
)

// Data directory resolution (docs/persistence.md).
const (
	DataDirEnv         = "RPG_DATA_DIR"
	defaultDataDirName = ".cli-turn-based-rpg"
)

// ResolveDataDir picks the --data-dir flag, then RPG_DATA_DIR, then ~/.cli-turn-based-rpg.
func ResolveDataDir(cliValue string) string {
	if cliValue != "" {
		return cliValue
	}

	if override := os.Getenv(DataDirEnv); override != "" {
		return override
	}

	home, err := os.UserHomeDir()
	if err != nil {
		return defaultDataDirName
	}

	return filepath.Join(home, defaultDataDirName)
}
