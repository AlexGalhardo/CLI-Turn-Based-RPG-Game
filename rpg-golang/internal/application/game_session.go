package application

import (
	"fmt"
	"time"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Clock is the time port.
type Clock interface {
	Now() time.Time
}

// SaveRepository persists the single active run.
type SaveRepository interface {
	Load() (*SaveGame, error)
	Save(save *SaveGame) error
	Delete() error
}

// HistoryRepository stores finished runs.
type HistoryRepository interface {
	Add(record *RunRecord) error
	List() ([]*RunRecord, error)
}

// ProfileRepository stores the cross-run profile.
type ProfileRepository interface {
	Load() (*Profile, error)
	Save(profile *Profile) error
}

// Repositories groups the persistence ports.
type Repositories struct {
	Saves   SaveRepository
	History HistoryRepository
	Profile ProfileRepository
}

// SessionContext holds the collaborators of a game session.
type SessionContext struct {
	Repositories Repositories
	Clock        Clock
	GameVersion  string
}

// StepResult is what a session step returns.
type StepResult struct {
	Events       []Event
	Achievements []domain.AchievementDef
}

// GameSession wraps the pure engine with time, persistence and the profile.
type GameSession struct {
	Data           *domain.GameData
	Engine         *GameEngine
	Info           *SessionInfo
	Profile        *ProfileService
	FinishedRecord *RunRecord
	context        SessionContext
	segmentStarted time.Time
	snapshot       *RunState
	snapshotRng    uint32
}

func newSession(data *domain.GameData, engine *GameEngine, info *SessionInfo, context SessionContext) (*GameSession, error) {
	profile, err := context.Repositories.Profile.Load()
	if err != nil {
		return nil, fmt.Errorf("load profile: %w", err)
	}

	return &GameSession{
		Data: data, Engine: engine, Info: info, Profile: NewProfileService(data, profile),
		context: context, segmentStarted: context.Clock.Now(),
	}, nil
}

// StartSession creates a new run and saves it at the merchant.
func StartSession(data *domain.GameData, config RunConfig, seed uint64, context SessionContext) (*GameSession, []Event, error) {
	engine, events, err := NewRun(data, config, seed)
	if err != nil {
		return nil, nil, err
	}

	now := context.Clock.Now()
	info := &SessionInfo{RunID: MakeRunID(now, seed), StartedAt: FormatTimestamp(now), Sessions: 1}

	session, err := newSession(data, engine, info, context)
	if err != nil {
		return nil, nil, err
	}

	if _, err := session.afterStep(events); err != nil {
		return nil, nil, err
	}

	return session, events, nil
}

// ResumeSession continues the saved run, or returns nil when there is none.
func ResumeSession(data *domain.GameData, context SessionContext) (*GameSession, error) {
	save, err := context.Repositories.Saves.Load()
	if err != nil || save == nil {
		return nil, err
	}

	engine := Restore(data, save.Run, save.RngState)
	save.Session.Sessions++

	session, err := newSession(data, engine, save.Session, context)
	if err != nil {
		return nil, err
	}

	session.snapshot, session.snapshotRng = save.Run.Clone(), save.RngState

	return session, nil
}

// State returns the current run state.
func (s *GameSession) State() *RunState { return s.Engine.State }

// Step applies a command and persists what the new state requires.
func (s *GameSession) Step(command Command) (StepResult, error) {
	events := s.Engine.Step(command)

	achievements, err := s.afterStep(events)
	if err != nil {
		return StepResult{}, err
	}

	return StepResult{Events: events, Achievements: achievements}, nil
}

func (s *GameSession) afterStep(events []Event) ([]domain.AchievementDef, error) {
	now := FormatTimestamp(s.context.Clock.Now())
	unlocked := s.Profile.Observe(events, s.State(), now, s.Info.RunID)

	profileChanged := len(unlocked) > 0

	for _, evt := range events {
		if evt.Type() == "monster_killed" {
			profileChanged = true
		}
	}

	switch {
	case s.State().Phase == domain.PhaseMerchant || s.State().Phase == domain.PhaseVictory:
		s.snapshot, s.snapshotRng = s.State().Clone(), s.Engine.RngState()
		if err := s.writeSave(); err != nil {
			return nil, err
		}
	case s.State().Phase == domain.PhaseGameOver && s.FinishedRecord == nil:
		if err := s.finish(); err != nil {
			return nil, err
		}

		profileChanged = true
	}

	if profileChanged {
		if err := s.context.Repositories.Profile.Save(s.Profile.Profile); err != nil {
			return nil, fmt.Errorf("save profile: %w", err)
		}
	}

	return unlocked, nil
}

// SaveAndQuit persists play time. Mid-battle quits resume from the last merchant visit.
func (s *GameSession) SaveAndQuit() error {
	if s.State().Phase == domain.PhaseGameOver {
		return nil
	}

	return s.writeSave()
}

func (s *GameSession) accumulatePlayTime() time.Time {
	now := s.context.Clock.Now()
	s.Info.PlayTimeSeconds += max(0, int(now.Sub(s.segmentStarted).Seconds()))
	s.segmentStarted = now

	return now
}

func (s *GameSession) writeSave() error {
	if s.snapshot == nil {
		return nil
	}

	now := s.accumulatePlayTime()

	err := s.context.Repositories.Saves.Save(&SaveGame{
		SchemaVersion: SchemaVersion, GameVersion: s.context.GameVersion, Implementation: Implementation,
		SavedAt: FormatTimestamp(now), RngState: s.snapshotRng, Session: s.Info, Run: s.snapshot,
	})
	if err != nil {
		return fmt.Errorf("save run: %w", err)
	}

	return nil
}

func (s *GameSession) finish() error {
	now := FormatTimestamp(s.accumulatePlayTime())
	state := s.State()
	record := &RunRecord{
		SchemaVersion: SchemaVersion, RunID: s.Info.RunID, Name: state.Config.Name, Vocation: state.Config.VocationID,
		Difficulty: state.Config.DifficultyID, Seed: state.Seed, Implementation: Implementation,
		GameVersion: s.context.GameVersion, StartedAt: s.Info.StartedAt, EndedAt: now,
		PlayTimeSeconds: s.Info.PlayTimeSeconds, Sessions: s.Info.Sessions, Round: state.Round,
		Level: state.Player.Level, MagicLevel: state.Player.MagicLevel, DeathCause: state.DeathCauseOr(""), Won: state.Won, Stats: state.Stats,
	}

	if err := s.context.Repositories.History.Add(record); err != nil {
		return fmt.Errorf("add history: %w", err)
	}

	if err := s.context.Repositories.Saves.Delete(); err != nil {
		return fmt.Errorf("delete save: %w", err)
	}

	s.Profile.RecordFinishedRun(HallOfFameEntry{
		RunID: record.RunID, Name: record.Name, Vocation: record.Vocation, Difficulty: record.Difficulty,
		Round: record.Round, Level: record.Level, EndedAt: record.EndedAt, Won: record.Won,
	})
	s.FinishedRecord = record

	return nil
}
