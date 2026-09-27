package infrastructure

import (
	"errors"
	"io/fs"
	"strings"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Frame is one ASCII art frame.
type Frame []string

// Animations maps an animation name (idle, attack, hurt) to its frames.
type Animations map[string][]Frame

// ErrArtFormat is returned for malformed art files.
var ErrArtFormat = errors.New("invalid art file")

// ParseArt parses `@animation` headers with frames separated by `%%` (docs/data-format.md).
func ParseArt(text string) (Animations, error) {
	animations := Animations{}

	var current string

	for line := range strings.SplitSeq(strings.TrimRight(text, "\n"), "\n") {
		switch {
		case strings.HasPrefix(line, "@"):
			current = strings.TrimSpace(line[1:])
			animations[current] = []Frame{{}}
		case line == "%%":
			if current == "" {
				return nil, errors.Join(ErrArtFormat, errors.New("frame separator before any @animation"))
			}

			animations[current] = append(animations[current], Frame{})
		case current == "":
			return nil, errors.Join(ErrArtFormat, errors.New("art content before the first @animation"))
		default:
			frames := animations[current]
			frames[len(frames)-1] = append(frames[len(frames)-1], line)
		}
	}

	return animations, nil
}

// ArtLibrary loads and caches art from the shared tree.
type ArtLibrary struct {
	shared fs.FS
	cache  map[string]Animations
}

// NewArtLibrary creates a library over the shared tree.
func NewArtLibrary(shared fs.FS) *ArtLibrary {
	return &ArtLibrary{shared: shared, cache: map[string]Animations{}}
}

// ForCreature returns the family art, or the boss's own art.
func (l *ArtLibrary) ForCreature(creature *domain.MonsterDef) Animations {
	if creature.IsBoss {
		return l.LoadFile("bosses", creature.ID)
	}

	return l.LoadFile("families", creature.Family)
}

// LoadFile returns the animations of shared/art/<folder>/<name>.txt (empty when missing or malformed).
func (l *ArtLibrary) LoadFile(folder, name string) Animations {
	key := folder + "/" + name
	if animations, ok := l.cache[key]; ok {
		return animations
	}

	animations := Animations{}

	if content, err := fs.ReadFile(l.shared, "art/"+key+".txt"); err == nil {
		if parsed, parseErr := ParseArt(string(content)); parseErr == nil {
			animations = parsed
		}
	}

	l.cache[key] = animations

	return animations
}

// FrameFor returns the frame of an animation at a tick, falling back to idle; nil when there is no art.
func FrameFor(animations Animations, animation string, tick int) Frame {
	frames := animations[animation]
	if len(frames) == 0 {
		frames = animations["idle"]
	}

	if len(frames) == 0 {
		return nil
	}

	return frames[tick%len(frames)]
}
