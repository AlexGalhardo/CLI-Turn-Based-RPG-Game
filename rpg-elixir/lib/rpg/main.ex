defmodule Rpg.Main do
  @moduledoc "Entry point of the escript: `rpg-elixir [flags]` (the Python `__main__.py`, the Go `cmd/rpg/main.go`)."

  alias Rpg.Application.GameSession
  alias Rpg.Application.GameSession.Repositories
  alias Rpg.Application.Simulator
  alias Rpg.Domain.Definitions.GameData
  alias Rpg.Infrastructure.{Art, DataLoader, Paths}

  alias Rpg.Infrastructure.Repositories.{
    FileHistoryRepository,
    FileProfileRepository,
    FileSaveRepository,
    SettingsRepository,
    SystemClock
  }

  alias Rpg.Presentation.Cli.CliOptions
  alias Rpg.Presentation.Controller.Services
  alias Rpg.Presentation.Tui.{App, Terminal}
  alias Rpg.Presentation.{Cli, Controller, SimulatorReport}

  @doc "escript entry point."
  @spec main([String.t()]) :: no_return()
  def main(argv), do: System.halt(run(argv))

  @doc "main without halting, so it can be tested. Returns the exit code."
  @spec run([String.t()]) :: non_neg_integer()
  def run(argv) do
    case Cli.parse_args(argv) do
      {:exit, text} ->
        IO.puts(text)
        0

      {:error, message} ->
        IO.puts(:stderr, "rpg: error: #{message}")
        2

      {:ok, %CliOptions{simulate: nil} = options} ->
        run_tui(options)

      {:ok, options} ->
        run_simulator(options)
    end
  end

  @spec run_simulator(CliOptions.t()) :: non_neg_integer()
  def run_simulator(%CliOptions{} = options) do
    data = DataLoader.load_game_data(Paths.shared_source())
    vocations = if options.vocation, do: [options.vocation], else: Enum.map(data.vocations, & &1.id)
    difficulties = if options.difficulty, do: [options.difficulty], else: Enum.map(data.balance.difficulties, & &1.id)
    # Python's `options.seed or 1`: seed 0 also means "start at 1".
    base_seed = if options.seed in [nil, 0], do: 1, else: options.seed

    try do
      summaries = for v <- vocations, d <- difficulties, do: Simulator.simulate(data, v, d, options.simulate, base_seed)
      IO.puts(SimulatorReport.render_report(summaries, data))
      0
    rescue
      error in ArgumentError ->
        IO.puts(:stderr, "error: #{Exception.message(error)}")
        2
    end
  end

  @doc "Wires the real adapters for a data directory."
  @spec build_services(GameData.t(), Path.t()) :: Services.t()
  def build_services(%GameData{} = data, data_dir) do
    %Services{
      data: data,
      shared: Paths.shared_source(),
      settings: SettingsRepository.new(data_dir),
      repositories: %Repositories{
        saves: FileSaveRepository.new(data_dir),
        history: FileHistoryRepository.new(data_dir),
        profile: FileProfileRepository.new(data_dir)
      },
      clock: %SystemClock{},
      version: Rpg.Version.version()
    }
  end

  defp run_tui(options) do
    data = DataLoader.load_game_data(Paths.shared_source())
    services = build_services(data, Paths.resolve_data_dir(options.data_dir))
    controller = Controller.new(services, seed: options.seed, locale_override: options.lang)
    animate = not (options.no_anim or System.get_env("RPG_NO_ANIM") not in [nil, ""])
    app = Terminal.run(App.new(controller, Art.library(services.shared), animate))
    if app.controller.session, do: GameSession.save_and_quit(app.controller.session)
    0
  end
end
