// Command syncshared copies the repository's shared/ folder into internal/assets/shared so it can be embedded
// (go:embed cannot reach parent directories). Run it through `go generate ./...`.
package main

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: syncshared <source shared dir> <destination dir>")
		os.Exit(2)
	}

	if err := sync(os.Args[1], os.Args[2]); err != nil {
		fmt.Fprintln(os.Stderr, "syncshared:", err)
		os.Exit(1)
	}
}

// sync mirrors data/, i18n/ and art/ (golden files and schemas are only needed by tests and CI).
func sync(source, destination string) error {
	if err := os.RemoveAll(destination); err != nil {
		return err
	}

	for _, folder := range []string{"data", "i18n", "art"} {
		root := filepath.Join(source, folder)

		err := filepath.WalkDir(root, func(path string, entry fs.DirEntry, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}

			relative, err := filepath.Rel(source, path)
			if err != nil {
				return err
			}

			target := filepath.Join(destination, relative)
			if entry.IsDir() {
				return os.MkdirAll(target, 0o750)
			}

			if !strings.HasSuffix(path, ".json") && !strings.HasSuffix(path, ".txt") {
				return nil
			}

			content, err := os.ReadFile(path)
			if err != nil {
				return err
			}

			return os.WriteFile(target, content, 0o600)
		})
		if err != nil {
			return err
		}
	}

	return nil
}
