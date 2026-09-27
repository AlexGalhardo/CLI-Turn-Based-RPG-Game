package presentation

import (
	"strings"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Layout constants from docs/tui.md.
const (
	BarWidth   = 25
	MinColumns = 100
	MinRows    = 30
	listKeys   = "123456789abcdefghijklmnopqrstuvwxyz"
)

// ElementColors are the terminal colours of each element (hex so every renderer matches).
var ElementColors = map[domain.Element]string{
	domain.Physical: "#ffffff",
	domain.Fire:     "#ff5f5f",
	domain.Ice:      "#5fd7ff",
	domain.Energy:   "#d75fff",
	domain.Earth:    "#5fd75f",
	domain.Holy:     "#ffd75f",
	domain.Death:    "#8a8a8a",
}

// RarityColors are the colours of item rarities.
var RarityColors = map[string]string{
	"common":    "#ffffff",
	"rare":      "#1e90ff",
	"epic":      "#af87ff",
	"legendary": "#ffaf00",
}

// Bar renders `█` filled / `░` empty cells. A living creature always shows at least one filled cell.
func Bar(current, maximum, width int) string {
	if maximum <= 0 {
		return strings.Repeat("░", width)
	}

	filled := width * max(0, min(current, maximum)) / maximum
	if current > 0 {
		filled = max(1, filled)
	}

	return strings.Repeat("█", filled) + strings.Repeat("░", width-filled)
}

// HPColor is green above 50%, yellow above 25%, red otherwise.
func HPColor(current, maximum int) string {
	switch {
	case maximum > 0 && current*100 > maximum*50:
		return "#5fd75f"
	case maximum > 0 && current*100 > maximum*25:
		return "#ffd75f"
	default:
		return "#ff5f5f"
	}
}

// ListKey returns the key of a list entry: 1-9 then a-z.
func ListKey(index int) string {
	return string(listKeys[index])
}

// ListIndex is the inverse of ListKey (-1 when the key is not a list key).
func ListIndex(key string) int {
	if len(key) != 1 {
		return -1
	}

	return strings.Index(listKeys, key)
}
