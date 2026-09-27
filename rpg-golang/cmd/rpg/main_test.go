package main

import (
	"bytes"
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/version"
)

func TestRun(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name     string
		args     []string
		code     int
		stdout   string
		stderror string
	}{
		{"version", []string{"--version"}, 0, "rpg " + version.Version + " (golang)", ""},
		{"help", []string{"--help"}, 0, "usage: rpg", ""},
		{"bad seed", []string{"--seed", "-1"}, 2, "", "argument --seed: must be >= 0"},
		{"unknown vocation", []string{"--simulate", "1", "--vocation", "knight"}, 2, "", "invalid run config"},
		{"simulator", []string{"--simulate", "1", "--vocation", "mage", "--difficulty", "hard", "--seed", "5"}, 0, "median", ""},
		{"all combinations", []string{"--simulate", "1"}, 0, "archer    normal", ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			var stdout, stderr bytes.Buffer

			code := run(tt.args, &stdout, &stderr)
			if code != tt.code || !strings.Contains(stdout.String(), tt.stdout) || !strings.Contains(stderr.String(), tt.stderror) {
				t.Fatalf("code=%d stdout=%q stderr=%q", code, stdout.String(), stderr.String())
			}
		})
	}
}

func TestBuildServices(t *testing.T) {
	t.Parallel()

	services := BuildServices(nil, t.TempDir())
	if services.Settings == nil || services.Repositories.Saves == nil || services.Version != version.Version {
		t.Fatal("services must be wired")
	}
}
