// Package tui renders the UI controller with Bubble Tea and Lip Gloss (layout specified in docs/tui.md).
package tui

import (
	"strconv"
	"strings"
	"time"

	tea "charm.land/bubbletea/v2"
	"charm.land/lipgloss/v2"
	"github.com/mattn/go-runewidth"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/version"
)

// Layout constants (same as the Textual and Ink renderers).
const (
	animationInterval  = 500 * time.Millisecond
	logLines           = 5
	twoColumnThreshold = 4
	columnWidth        = 44
	artWidth           = 32
	topHeight          = 8
	playerHeight       = 5
	logHeight          = 7
	accent             = "#ffa62b"
)

type tickMsg struct{}

// Model is the Bubble Tea model: it only renders the controller and forwards keys.
type Model struct {
	Controller *presentation.Controller
	art        *infrastructure.ArtLibrary
	animate    bool
	tick       int
	cues       []string
	width      int
	height     int
}

// NewModel creates the model with a default 100 × 30 size until the terminal reports its size.
func NewModel(controller *presentation.Controller, art *infrastructure.ArtLibrary, animate bool) Model {
	return Model{Controller: controller, art: art, animate: animate, width: presentation.MinColumns, height: presentation.MinRows}
}

func tickCmd() tea.Cmd {
	return tea.Tick(animationInterval, func(time.Time) tea.Msg { return tickMsg{} })
}

// Init starts the animation timer when animations are enabled.
func (m Model) Init() tea.Cmd {
	if m.animate {
		return tickCmd()
	}

	return nil
}

// KeyName maps Bubble Tea keys to the controller's key names (same names as the other implementations).
func KeyName(msg tea.KeyPressMsg) string {
	switch msg.Code {
	case tea.KeyEnter:
		return "enter"
	case tea.KeyEscape:
		return "escape"
	case tea.KeyBackspace, tea.KeyDelete:
		return "backspace"
	}

	if msg.Text != "" {
		return msg.Text
	}

	return msg.String()
}

// Update handles keys, window size and animation ticks.
func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width, m.height = msg.Width, msg.Height
	case tickMsg:
		m.tick++
		if len(m.cues) > 0 {
			m.cues = m.cues[1:]
		}

		return m, tickCmd()
	case tea.KeyPressMsg:
		if msg.String() == "ctrl+c" {
			return m, tea.Quit
		}

		m.Controller.Press(KeyName(msg))

		if m.Controller.ExitRequested {
			return m, tea.Quit
		}

		m.cues = nil
		if m.animate {
			m.cues = append([]string{}, m.Controller.AnimationCues...)
		}

		m.Controller.AnimationCues = nil
	}

	return m, nil
}

// View renders the whole screen.
func (m Model) View() tea.View {
	view := tea.NewView(m.Render())
	view.AltScreen = true

	return view
}

func style(color string) lipgloss.Style {
	return lipgloss.NewStyle().Foreground(lipgloss.Color(color))
}

func panel(width, height int, content string) string {
	return lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).BorderForeground(lipgloss.Color(accent)).
		Padding(0, 1).Width(width).Height(height).Render(content)
}

// Render returns the screen as a string (also used by tests).
func (m Model) Render() string {
	controller := m.Controller
	if m.width < presentation.MinColumns || m.height < presentation.MinRows {
		return style("#ffd75f").Bold(true).Render(controller.T("app.resize", map[string]any{
			"columns": presentation.MinColumns, "rows": presentation.MinRows,
		}))
	}

	sections := []string{m.top(), panel(m.width, playerHeight, m.playerPanel())}
	used := topHeight + playerHeight

	if !presentation.IsPaged(controller.View) {
		logs := controller.Log[max(0, len(controller.Log)-logLines):]
		sections = append(sections, panel(m.width, logHeight, strings.Join(logs, "\n")))
		used += logHeight
	}

	sections = append(sections, panel(m.width, max(10, m.height-used), m.menuPanel()))

	return lipgloss.JoinVertical(lipgloss.Left, sections...)
}

func (m Model) top() string {
	controller := m.Controller
	header := controller.Header()
	fill := max(0, m.width-runewidth.StringWidth(header)-5)
	border := style(accent).Render("╭─ " + header + " " + strings.Repeat("─", fill) + "╮")

	monster := controller.MonsterView()
	animation := "idle"

	if len(m.cues) > 0 {
		animation = m.cues[0]
	}

	var (
		frame    infrastructure.Frame
		artStyle lipgloss.Style
		info     []string
	)

	if monster == nil {
		frame = infrastructure.FrameFor(m.art.LoadFile("families", "dragon"), "idle", m.tick)
		artStyle = style("#5fd75f").Bold(true)
		info = []string{
			lipgloss.NewStyle().Bold(true).Render(controller.T("app.title", nil)),
			lipgloss.NewStyle().Italic(true).Render(controller.T("app.subtitle", nil)),
			"",
			"v" + version.Version + " · Go",
		}
	} else {
		creature := controller.Services.Data.Creature(monster.CreatureID)
		frame = infrastructure.FrameFor(m.art.ForCreature(creature), animation, m.tick)

		artStyle = style(presentation.ElementColors[monster.Element])
		if animation == "hurt" {
			artStyle = style("#ff5f5f").Bold(true)
		}

		name := lipgloss.NewStyle().Bold(true).Render(strings.ToUpper(monster.Name))
		if monster.IsBoss {
			name = style("#d75fff").Bold(true).Render(controller.T("hud.boss", nil)+" ") + name
		}

		info = []string{
			name,
			lipgloss.NewStyle().Bold(true).Render("HP ") + style(presentation.HPColor(monster.HP, monster.MaxHP)).Render(presentation.Bar(monster.HP, monster.MaxHP, presentation.BarWidth)) +
				"  " + strconv.Itoa(monster.HP) + "/" + strconv.Itoa(monster.MaxHP),
			style(presentation.ElementColors[monster.Element]).Render(monster.Details),
		}
	}

	art := lipgloss.NewStyle().Width(artWidth).Render(artStyle.Render(strings.Join(frame, "\n")))
	content := lipgloss.JoinHorizontal(lipgloss.Top, art, strings.Join(info, "\n"))
	box := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).BorderTop(false).BorderForeground(lipgloss.Color(accent)).
		Padding(0, 1).Width(m.width).Height(topHeight - 1).Render(content)

	return border + "\n" + box
}

func (m Model) playerPanel() string {
	player := m.Controller.PlayerView()
	if player == nil {
		return ""
	}

	summary := player.Summary + style("#ffd75f").Render("   "+player.Gold)
	if player.Statuses != "" {
		summary += style("#ff5f5f").Render("   " + player.Statuses)
	}

	bold := lipgloss.NewStyle().Bold(true)
	hp := bold.Render("HP ") + style(presentation.HPColor(player.HP, player.MaxHP)).Render(presentation.Bar(player.HP, player.MaxHP, presentation.BarWidth)) +
		bold.Render("  "+strconv.Itoa(player.HP)+"/"+strconv.Itoa(player.MaxHP))
	mp := bold.Render("MP ") + style("#5f87ff").Render(presentation.Bar(player.MP, player.MaxMP, presentation.BarWidth)) +
		"  " + strconv.Itoa(player.MP) + "/" + strconv.Itoa(player.MaxMP) + "   " + player.XP

	return summary + "\n" + hp + "\n" + mp
}

func optionColor(color string) string {
	if value, ok := presentation.RarityColors[color]; ok {
		return value
	}

	if color == "heal" {
		return "#5fd75f"
	}

	for element, value := range presentation.ElementColors {
		if string(element) == color {
			return value
		}
	}

	return ""
}

func (m Model) menuPanel() string {
	controller := m.Controller
	lines := []string{lipgloss.NewStyle().Bold(true).Underline(true).Render(controller.Title())}

	lines = append(lines, controller.BodyLines()...)

	options := controller.Options()
	if len(options) > 0 {
		lines = append(lines, "")
	}

	columns := 1
	if len(options) > twoColumnThreshold {
		columns = 2
	}

	for start := 0; start < len(options); start += columns {
		var row strings.Builder

		for _, option := range options[start:min(len(options), start+columns)] {
			label := option.Label
			if columns > 1 {
				label = runewidth.FillRight(runewidth.Truncate(label, columnWidth-1, ""), columnWidth)
			}

			rendered := label
			if color := optionColor(option.Color); color != "" {
				rendered = style(color).Render(label)
			}

			row.WriteString(style("#5fd7ff").Bold(true).Render("["+strings.ToUpper(option.Key)+"] ") + rendered)
		}

		lines = append(lines, strings.TrimRight(row.String(), " "))
	}

	if prompt := controller.InputPrompt(); prompt != "" {
		lines = append(lines, "", lipgloss.NewStyle().Bold(true).Render(prompt))
	}

	if controller.Message != "" {
		lines = append(lines, "", style("#ff5f5f").Bold(true).Render(controller.Message))
	}

	if controller.Err != nil {
		lines = append(lines, "", style("#ff5f5f").Render(controller.Err.Error()))
	}

	return strings.Join(lines, "\n")
}
