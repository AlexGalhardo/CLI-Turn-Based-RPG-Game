package application

import (
	"encoding/json"
	"errors"
	"fmt"
	"time"
)

// Shared file format constants (docs/persistence.md).
const (
	SchemaVersion   = 1
	Implementation  = "golang"
	timestampFormat = "2006-01-02T15:04:05Z"
	runIDFormat     = "20060102T150405Z"
)

// ErrNewerSchema means a file was written by a newer game version; it is never overwritten.
var ErrNewerSchema = errors.New("file written by a newer game version")

// FormatTimestamp formats a moment as UTC ISO 8601 with a Z suffix.
func FormatTimestamp(moment time.Time) string {
	return moment.UTC().Format(timestampFormat)
}

// ParseTimestamp parses FormatTimestamp output.
func ParseTimestamp(text string) (time.Time, error) {
	moment, err := time.Parse(timestampFormat, text)
	if err != nil {
		return time.Time{}, fmt.Errorf("parse timestamp: %w", err)
	}

	return moment, nil
}

// MakeRunID builds `<startedAt as yyyyMMddTHHmmssZ>-<seed>`.
func MakeRunID(startedAt time.Time, seed uint64) string {
	return fmt.Sprintf("%s-%d", startedAt.UTC().Format(runIDFormat), seed)
}

// CheckSchema refuses documents written with a newer schema version.
func CheckSchema(raw []byte, what string) error {
	var header struct {
		SchemaVersion int `json:"schemaVersion"`
	}
	if err := json.Unmarshal(raw, &header); err != nil {
		return fmt.Errorf("parse %s: %w", what, err)
	}

	if header.SchemaVersion > SchemaVersion {
		return fmt.Errorf("%w: %s uses schema %d (supported: %d)", ErrNewerSchema, what, header.SchemaVersion, SchemaVersion)
	}

	return nil
}

// SessionInfo tracks a run across play sessions.
type SessionInfo struct {
	RunID           string `json:"runId"`
	StartedAt       string `json:"startedAt"`
	PlayTimeSeconds int    `json:"playTimeSeconds"`
	Sessions        int    `json:"sessions"`
}

// SaveGame is the shared save.json format.
type SaveGame struct {
	SchemaVersion  int          `json:"schemaVersion"`
	GameVersion    string       `json:"gameVersion"`
	Implementation string       `json:"implementation"`
	SavedAt        string       `json:"savedAt"`
	RngState       uint32       `json:"rngState"`
	Session        *SessionInfo `json:"session"`
	Run            *RunState    `json:"run"`
}

// SaveGameFromJSON parses save.json, refusing newer schemas.
func SaveGameFromJSON(raw []byte) (*SaveGame, error) {
	if err := CheckSchema(raw, "save.json"); err != nil {
		return nil, err
	}

	var save struct {
		SaveGame

		Run json.RawMessage `json:"run"`
	}
	if err := json.Unmarshal(raw, &save); err != nil {
		return nil, fmt.Errorf("parse save.json: %w", err)
	}

	run, err := RunStateFromJSON(save.Run)
	if err != nil {
		return nil, fmt.Errorf("parse save.json: %w", err)
	}

	result := save.SaveGame
	result.Run = run

	if result.Session == nil {
		return nil, errors.New("parse save.json: missing session")
	}

	return &result, nil
}

// RunRecord is a finished run, written to history/<runId>.json.
type RunRecord struct {
	SchemaVersion   int            `json:"schemaVersion"`
	RunID           string         `json:"runId"`
	Name            string         `json:"name"`
	Vocation        string         `json:"vocation"`
	Difficulty      string         `json:"difficulty"`
	Seed            uint64         `json:"seed"`
	Implementation  string         `json:"implementation"`
	GameVersion     string         `json:"gameVersion"`
	StartedAt       string         `json:"startedAt"`
	EndedAt         string         `json:"endedAt"`
	PlayTimeSeconds int            `json:"playTimeSeconds"`
	Sessions        int            `json:"sessions"`
	Round           int            `json:"round"`
	Level           int            `json:"level"`
	MagicLevel      int            `json:"magicLevel"`
	DeathCause      string         `json:"deathCause"`
	Stats           *RunStatistics `json:"stats"`
}
