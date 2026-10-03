defmodule Rpg.Presentation.Tui.App do
  @moduledoc """
  Hand-written ANSI renderer of the UI controller (layout specified in docs/tui.md).

  This module is pure, in the Elm style the Go port uses with Bubble Tea: `handle_key/2`, `tick/1` and `resize/3`
  return a new app, and `render/1` returns the whole screen as a string. The impure terminal loop lives in
  `Rpg.Presentation.Tui.Terminal`, so the screen is testable without a terminal (`render_plain/1` drops the colours).
  """

  alias Rpg.Infrastructure.Art
  alias Rpg.Infrastructure.Art.ArtLibrary
  alias Rpg.Presentation.{Controller, Render}

  @log_lines 5
  @two_column_threshold 4
  @column_width 44
  @art_width 32
  @top_height 8
  @player_height 5
  @log_height 7
  @accent "#ffa62b"
  @key_color "#5fd7ff"
  @title_art {"families", "dragon"}

  @enforce_keys [:controller, :art]
  defstruct [:controller, :art, animate: false, tick: 0, cues: [], width: 100, height: 30]

  @type t :: %__MODULE__{
          controller: Controller.t(),
          art: ArtLibrary.t(),
          animate: boolean(),
          tick: non_neg_integer(),
          cues: [String.t()],
          width: pos_integer(),
          height: pos_integer()
        }

  # A styled piece of text: {style, text}. Styles: :bold, :italic, :underline, {:fg, "#rrggbb"}.
  @type span :: {[term()], String.t()}
  @type line :: [span()]

  @spec new(Controller.t(), ArtLibrary.t(), boolean()) :: t()
  def new(%Controller{} = controller, %ArtLibrary{} = art, animate) do
    %__MODULE__{controller: controller, art: art, animate: animate}
  end

  # ── update ──────────────────────────────────────────────────────────────────

  @doc "Forwards a key to the controller. Returns `{app, :quit}` when the player asked to leave."
  @spec handle_key(t(), String.t()) :: {t(), :continue | :quit}
  def handle_key(%__MODULE__{} = app, "ctrl+c"), do: {app, :quit}

  def handle_key(%__MODULE__{} = app, key) do
    controller = Controller.press(app.controller, key)

    if controller.exit_requested do
      {%{app | controller: controller}, :quit}
    else
      cues = if app.animate, do: controller.animation_cues, else: []
      {%{app | controller: %{controller | animation_cues: []}, cues: cues}, :continue}
    end
  end

  @doc "Animation timer (500 ms): advances the idle loop and consumes one one-shot cue."
  @spec tick(t()) :: t()
  def tick(%__MODULE__{} = app), do: %{app | tick: app.tick + 1, cues: Enum.drop(app.cues, 1)}

  @spec resize(t(), pos_integer(), pos_integer()) :: t()
  def resize(%__MODULE__{} = app, width, height), do: %{app | width: width, height: height}

  # ── rendering ───────────────────────────────────────────────────────────────

  @doc "The whole screen with ANSI colours, one string with `\\n` between rows."
  @spec render(t()) :: String.t()
  def render(%__MODULE__{} = app), do: app |> screen() |> Enum.map_join("\n", &to_ansi/1)

  @doc "The whole screen without colours (used by tests)."
  @spec render_plain(t()) :: String.t()
  def render_plain(%__MODULE__{} = app), do: app |> screen() |> Enum.map_join("\n", &to_plain/1)

  @spec screen(t()) :: [line()]
  defp screen(%__MODULE__{controller: controller} = app) do
    if app.width < Render.min_columns() or app.height < Render.min_rows() do
      message = Controller.t(controller, "app.resize", columns: Render.min_columns(), rows: Render.min_rows())
      [[{[:bold, {:fg, "#ffd75f"}], message}]]
    else
      used = @top_height + @player_height
      top = top(app)
      player = panel(app.width, @player_height, player_lines(controller))

      {log, used} =
        if Controller.paged?(controller.view) do
          {[], used}
        else
          lines = controller.log |> Enum.take(-@log_lines) |> Enum.map(&[{[], &1}])
          {panel(app.width, @log_height, lines), used + @log_height}
        end

      menu = panel(app.width, max(10, app.height - used), menu_lines(controller))
      top ++ player ++ log ++ menu
    end
  end

  defp border(text), do: [{[{:fg, @accent}], text}]

  defp panel(width, height, lines) do
    inner = width - 4
    rows = lines |> Enum.take(height - 2) |> pad_rows(height - 2)

    [border("╭" <> String.duplicate("─", width - 2) <> "╮")] ++
      Enum.map(rows, &(border("│ ") ++ fit(&1, inner) ++ border(" │"))) ++
      [border("╰" <> String.duplicate("─", width - 2) <> "╯")]
  end

  defp pad_rows(rows, count), do: rows ++ List.duplicate([], max(0, count - length(rows)))

  defp top(%__MODULE__{controller: controller} = app) do
    header = Controller.header(controller)
    fill = max(0, app.width - String.length(header) - 5)
    first = border("╭─ " <> header <> " " <> String.duplicate("─", fill) <> "╮")
    {frame, art_style, info} = top_content(app)

    art = Enum.map(frame, &[{art_style, &1}])
    count = @top_height - 2
    art = art |> Enum.take(count) |> pad_rows(count)
    info = info |> Enum.take(count) |> pad_rows(count)

    rows = Enum.zip_with(art, info, fn art_line, info_line -> fit(art_line, @art_width) ++ info_line end)
    [first] ++ tl(panel(app.width, @top_height, rows))
  end

  defp top_content(%__MODULE__{controller: controller, art: art} = app) do
    case Controller.monster_view(controller) do
      nil ->
        {folder, name} = @title_art
        frame = Art.frame_for(Art.load_file(art, folder, name), "idle", app.tick)

        info = [
          [{[:bold], Controller.t(controller, "app.title")}],
          [{[:italic], Controller.t(controller, "app.subtitle")}],
          [],
          [{[], "v#{Rpg.Version.version()} · Elixir"}]
        ]

        {frame, [:bold, {:fg, "#5fd75f"}], info}

      monster ->
        creature = Rpg.Domain.Definitions.GameData.creature(controller.services.data, monster.creature_id)
        animation = List.first(app.cues) || "idle"
        frame = Art.frame_for(Art.for_creature(art, creature), animation, app.tick)
        element_color = Render.color_hex(monster.element) || "#ffffff"
        style = if animation == "hurt", do: [:bold, {:fg, "#ff5f5f"}], else: [{:fg, element_color}]

        boss =
          if monster.is_boss, do: [{[:bold, {:fg, "#d75fff"}], Controller.t(controller, "hud.boss") <> " "}], else: []

        info = [
          boss ++ [{[:bold], String.upcase(monster.name)}],
          [
            {[:bold], "HP "},
            {[{:fg, Render.color_hex(Render.hp_color(monster.hp, monster.max_hp))}],
             Render.bar(monster.hp, monster.max_hp)},
            {[], "  #{monster.hp}/#{monster.max_hp}"}
          ],
          [{[{:fg, element_color}], monster.details}]
        ]

        {frame, style, info}
    end
  end

  defp player_lines(controller) do
    case Controller.player_view(controller) do
      nil ->
        []

      player ->
        statuses = if player.statuses != "", do: [{[{:fg, "#ff5f5f"}], "   " <> player.statuses}], else: []

        [
          [{[], player.summary}, {[{:fg, "#ffd75f"}], "   " <> player.gold}] ++ statuses,
          [
            {[:bold], "HP "},
            {[{:fg, Render.color_hex(Render.hp_color(player.hp, player.max_hp))}],
             Render.bar(player.hp, player.max_hp)},
            {[:bold], "  #{player.hp}/#{player.max_hp}"}
          ],
          [
            {[:bold], "MP "},
            {[{:fg, Render.color_hex("blue")}], Render.bar(player.mp, player.max_mp)},
            {[], "  #{player.mp}/#{player.max_mp}   #{player.xp}"}
          ]
        ]
    end
  end

  defp menu_lines(controller) do
    options = Controller.options(controller)
    columns = if length(options) > @two_column_threshold, do: 2, else: 1

    option_rows =
      options
      |> Enum.chunk_every(columns)
      |> Enum.map(fn row ->
        row
        |> Enum.flat_map(fn option ->
          label =
            if columns == 1,
              do: option.label,
              else: option.label |> String.slice(0, @column_width - 1) |> String.pad_trailing(@column_width)

          color = Render.color_hex(option.color)

          [
            {[:bold, {:fg, @key_color}], "[#{String.upcase(option.key)}] "},
            {if(color, do: [{:fg, color}], else: []), label}
          ]
        end)
        |> trim_trailing()
      end)

    prompt = Controller.input_prompt(controller)

    [[{[:bold, :underline], Controller.title(controller)}]] ++
      Enum.map(Controller.body_lines(controller), &[{[], &1}]) ++
      if(options != [], do: [[] | option_rows], else: []) ++
      if(prompt, do: [[], [{[:bold], prompt}]], else: []) ++
      if(controller.message != "", do: [[], [{[:bold, {:fg, "#ff5f5f"}], controller.message}]], else: [])
  end

  defp trim_trailing(spans) do
    case List.last(spans) do
      {style, text} -> List.replace_at(spans, -1, {style, String.trim_trailing(text)})
      nil -> spans
    end
  end

  # Truncates a line to `width` visible characters and pads it with spaces.
  defp fit(spans, width) do
    {fitted, used} =
      Enum.reduce(spans, {[], 0}, fn {style, text}, {acc, used} ->
        piece = String.slice(text, 0, max(0, width - used))
        {[{style, piece} | acc], used + String.length(piece)}
      end)

    Enum.reverse(fitted) ++ [{[], String.duplicate(" ", width - used)}]
  end

  defp to_plain(line), do: Enum.map_join(line, fn {_style, text} -> text end)

  defp to_ansi(line) do
    Enum.map_join(line, fn
      {[], text} -> text
      {_style, ""} -> ""
      {style, text} -> sgr(style) <> text <> "\e[0m"
    end)
  end

  defp sgr(style) do
    codes =
      Enum.map(style, fn
        :bold -> "1"
        :italic -> "3"
        :underline -> "4"
        {:fg, "#" <> hex} -> "38;2;" <> rgb(hex)
      end)

    "\e[" <> Enum.join(codes, ";") <> "m"
  end

  defp rgb(hex) do
    <<r::binary-size(2), g::binary-size(2), b::binary-size(2)>> = hex
    Enum.map_join([r, g, b], ";", &Integer.to_string(String.to_integer(&1, 16)))
  end
end

defmodule Rpg.Presentation.Tui.Terminal do
  @moduledoc """
  The thin impure shell around `Rpg.Presentation.Tui.App`: raw keyboard input, the animation timer and drawing.

  A reader process blocks on stdin and sends each chunk as a message; the main loop receives keys, ticks and redraws.
  Raw mode uses OTP 28's `:shell.start_interactive({:noshell, :raw})`; when it is unavailable (no TTY) the game
  falls back to line input, where each line is a sequence of keys followed by Enter.
  """

  alias Rpg.Presentation.Tui.App

  @animation_ms 500
  @escape_timeout_ms 30

  @spec run(App.t()) :: App.t()
  def run(%App{} = app) do
    :io.setopts(:standard_io, encoding: :unicode, binary: true)
    raw = raw_mode()
    IO.write("\e[?1049h\e[?25l")
    parent = self()
    reader = spawn_link(fn -> read_loop(parent, raw) end)
    if app.animate, do: :timer.send_interval(@animation_ms, :tick)

    try do
      app |> draw() |> loop()
    after
      Process.unlink(reader)
      Process.exit(reader, :kill)
      IO.write("\e[0m\e[?25h\e[?1049l")
    end
  end

  defp raw_mode do
    try do
      :shell.start_interactive({:noshell, :raw}) == :ok
    rescue
      _ -> false
    catch
      _, _ -> false
    end
  end

  defp read_loop(parent, raw) do
    case if(raw, do: IO.getn(:stdio, "", 1), else: IO.gets(:stdio, "")) do
      :eof ->
        send(parent, {:keys, ["ctrl+c"]})

      {:error, _} ->
        send(parent, {:keys, ["ctrl+c"]})

      data when raw ->
        send(parent, {:input, data})
        read_loop(parent, raw)

      line ->
        keys = line |> String.trim_trailing("\n") |> String.trim_trailing("\r") |> String.graphemes()
        send(parent, {:keys, keys ++ ["enter"]})
        read_loop(parent, raw)
    end
  end

  defp loop(app) do
    receive do
      :tick ->
        app |> App.tick() |> draw() |> loop()

      {:keys, keys} ->
        handle_keys(app, keys)

      {:input, "\e"} ->
        # A lone ESC is the Escape key unless the rest of an escape sequence arrives right after it.
        receive do
          {:input, "[" <> _} -> app |> skip_sequence() |> loop()
          {:input, "O"} -> app |> skip_sequence() |> loop()
        after
          @escape_timeout_ms -> handle_keys(app, ["escape"])
        end

      {:input, data} ->
        handle_keys(app, decode(data))
    end
  end

  # Arrow keys and other sequences are ignored, like in the other ports' controllers.
  defp skip_sequence(app) do
    receive do
      {:input, <<char>>} when char in ?0..?9 or char == ?; -> skip_sequence(app)
      {:input, _final} -> app
    after
      @escape_timeout_ms -> app
    end
  end

  defp decode(data) do
    data
    |> String.graphemes()
    |> Enum.map(fn
      key when key in ["\r", "\n", "\r\n"] -> "enter"
      key when key in ["\d", "\b"] -> "backspace"
      "\u0003" -> "ctrl+c"
      key -> key
    end)
  end

  defp handle_keys(app, []), do: app |> draw() |> loop()

  defp handle_keys(app, [key | rest]) do
    case App.handle_key(app, key) do
      {app, :quit} -> app
      {app, :continue} -> handle_keys(app, rest)
    end
  end

  defp draw(app) do
    app = App.resize(app, size(:io.columns(), 100), size(:io.rows(), 30))
    frame = app |> App.render() |> String.replace("\n", "\e[K\r\n")
    IO.write(["\e[H", frame, "\e[K\e[J"])
    app
  end

  defp size({:ok, value}, _default), do: value
  defp size(_error, default), do: default
end
