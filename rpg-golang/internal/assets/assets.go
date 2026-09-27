// Package assets embeds the shared game content (data, i18n, art) copied from ../shared by `go generate`.
package assets

import (
	"embed"
	"io/fs"
)

//go:generate go run ../../tools/syncshared ../../../shared shared

//go:embed all:shared
var embedded embed.FS

// Shared returns the embedded shared/ tree (paths like "data/monsters.json", "art/families/rat.txt").
func Shared() fs.FS {
	sub, err := fs.Sub(embedded, "shared")
	if err != nil {
		panic(err)
	}

	return sub
}
