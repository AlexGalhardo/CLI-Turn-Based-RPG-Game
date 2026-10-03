package infrastructure

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
)

// WriteJSONAtomic writes to a temp file then renames it, so a crash never leaves a half-written save.
func WriteJSONAtomic(path string, document any) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o750); err != nil {
		return fmt.Errorf("create %s: %w", filepath.Dir(path), err)
	}

	content, err := json.MarshalIndent(document, "", "\t")
	if err != nil {
		return fmt.Errorf("encode %s: %w", filepath.Base(path), err)
	}

	temporary := path + ".tmp"
	if err := os.WriteFile(temporary, append(content, '\n'), 0o600); err != nil {
		return fmt.Errorf("write %s: %w", filepath.Base(temporary), err)
	}

	if err := os.Rename(temporary, path); err != nil {
		return fmt.Errorf("replace %s: %w", filepath.Base(path), err)
	}

	return nil
}

func readIfExists(path string) ([]byte, bool, error) {
	content, err := os.ReadFile(path) //nolint:gosec // paths come from the player's own data directory.
	if errors.Is(err, os.ErrNotExist) {
		return nil, false, nil
	}

	if err != nil {
		return nil, false, fmt.Errorf("read %s: %w", filepath.Base(path), err)
	}

	return content, true, nil
}

// SystemClock is the real clock.
type SystemClock struct{}

// Now returns the current time.
func (SystemClock) Now() time.Time { return time.Now() }

// BattleSpeeds are the valid auto-battle paces (1x, 2x); the first one is the default.
var BattleSpeeds = []int{1, 2}

// Settings are the player's preferences (settings.json).
type Settings struct {
	Locale      string
	AutoEquip   bool
	BattleSpeed int
}

// DefaultSettings are the settings of a first launch.
func DefaultSettings() Settings {
	return Settings{BattleSpeed: BattleSpeeds[0]}
}

// SettingsRepository stores settings.json.
type SettingsRepository struct {
	path string
}

// NewSettingsRepository creates the repository under a data directory.
func NewSettingsRepository(dataDir string) *SettingsRepository {
	return &SettingsRepository{path: filepath.Join(dataDir, "settings.json")}
}

// Load reads the settings; an unknown locale is ignored and an unknown battle speed loads as 1.
func (r *SettingsRepository) Load() (Settings, error) {
	content, ok, err := readIfExists(r.path)
	if err != nil || !ok {
		return DefaultSettings(), err
	}

	if err := application.CheckSchema(content, "settings.json"); err != nil {
		return DefaultSettings(), err
	}

	content, err = MigrateSettings(content)
	if err != nil {
		return DefaultSettings(), fmt.Errorf("migrate settings.json: %w", err)
	}

	var document struct {
		Locale      string `json:"locale"`
		AutoEquip   bool   `json:"autoEquip"`
		BattleSpeed int    `json:"battleSpeed"`
	}
	if err := json.Unmarshal(content, &document); err != nil {
		return DefaultSettings(), fmt.Errorf("parse settings.json: %w", err)
	}

	settings := Settings{AutoEquip: document.AutoEquip, BattleSpeed: BattleSpeeds[0]}
	if slices.Contains(SupportedLocales, document.Locale) {
		settings.Locale = document.Locale
	}

	if slices.Contains(BattleSpeeds, document.BattleSpeed) {
		settings.BattleSpeed = document.BattleSpeed
	}

	return settings, nil
}

// Save writes the settings.
func (r *SettingsRepository) Save(settings Settings) error {
	document := map[string]any{
		"schemaVersion": application.SchemaVersion,
		"autoEquip":     settings.AutoEquip,
		"battleSpeed":   settings.BattleSpeed,
	}
	if settings.Locale != "" {
		document["locale"] = settings.Locale
	}

	return WriteJSONAtomic(r.path, document)
}

// FileSaveRepository stores save.json.
type FileSaveRepository struct {
	path string
}

// NewFileSaveRepository creates the repository under a data directory.
func NewFileSaveRepository(dataDir string) *FileSaveRepository {
	return &FileSaveRepository{path: filepath.Join(dataDir, "save.json")}
}

// Load returns the save or nil when there is none.
func (r *FileSaveRepository) Load() (*application.SaveGame, error) {
	content, ok, err := readIfExists(r.path)
	if err != nil || !ok {
		return nil, err
	}

	if err := application.CheckSchema(content, "save.json"); err != nil {
		return nil, err
	}

	content, err = MigrateSave(content)
	if err != nil {
		return nil, fmt.Errorf("migrate save.json: %w", err)
	}

	return application.SaveGameFromJSON(content)
}

// Save writes the save atomically.
func (r *FileSaveRepository) Save(save *application.SaveGame) error {
	return WriteJSONAtomic(r.path, save)
}

// Delete removes the save if present.
func (r *FileSaveRepository) Delete() error {
	if err := os.Remove(r.path); err != nil && !errors.Is(err, os.ErrNotExist) {
		return fmt.Errorf("delete save.json: %w", err)
	}

	return nil
}

// FileHistoryRepository stores history/<runId>.json.
type FileHistoryRepository struct {
	dir string
}

// NewFileHistoryRepository creates the repository under a data directory.
func NewFileHistoryRepository(dataDir string) *FileHistoryRepository {
	return &FileHistoryRepository{dir: filepath.Join(dataDir, "history")}
}

// Add writes a finished run.
func (r *FileHistoryRepository) Add(record *application.RunRecord) error {
	return WriteJSONAtomic(filepath.Join(r.dir, record.RunID+".json"), record)
}

// List reads every finished run, sorted by file name.
func (r *FileHistoryRepository) List() ([]*application.RunRecord, error) {
	entries, err := os.ReadDir(r.dir)
	if errors.Is(err, os.ErrNotExist) {
		return []*application.RunRecord{}, nil
	}

	if err != nil {
		return nil, fmt.Errorf("list history: %w", err)
	}

	records := []*application.RunRecord{}

	for _, entry := range entries {
		if !strings.HasSuffix(entry.Name(), ".json") {
			continue
		}

		content, err := os.ReadFile(filepath.Join(r.dir, entry.Name()))
		if err != nil {
			return nil, fmt.Errorf("read %s: %w", entry.Name(), err)
		}

		if err := application.CheckSchema(content, "history record"); err != nil {
			return nil, err
		}

		content, err = MigrateHistory(content)
		if err != nil {
			return nil, fmt.Errorf("migrate %s: %w", entry.Name(), err)
		}

		var record application.RunRecord
		if err := json.Unmarshal(content, &record); err != nil {
			return nil, fmt.Errorf("parse %s: %w", entry.Name(), err)
		}

		records = append(records, &record)
	}

	return records, nil
}

// FileProfileRepository stores profile.json.
type FileProfileRepository struct {
	path string
}

// NewFileProfileRepository creates the repository under a data directory.
func NewFileProfileRepository(dataDir string) *FileProfileRepository {
	return &FileProfileRepository{path: filepath.Join(dataDir, "profile.json")}
}

// Load returns the profile (empty when missing).
func (r *FileProfileRepository) Load() (*application.Profile, error) {
	content, ok, err := readIfExists(r.path)
	if err != nil {
		return nil, err
	}

	if !ok {
		return application.NewProfile(), nil
	}

	if err := application.CheckSchema(content, "profile.json"); err != nil {
		return nil, err
	}

	content, err = MigrateProfile(content)
	if err != nil {
		return nil, fmt.Errorf("migrate profile.json: %w", err)
	}

	profile := application.NewProfile()
	if err := json.Unmarshal(content, profile); err != nil {
		return nil, fmt.Errorf("parse profile.json: %w", err)
	}

	profile.SchemaVersion = application.SchemaVersion

	return profile, nil
}

// Save writes the profile atomically.
func (r *FileProfileRepository) Save(profile *application.Profile) error {
	return WriteJSONAtomic(r.path, profile)
}
