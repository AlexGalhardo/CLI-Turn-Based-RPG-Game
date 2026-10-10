defmodule Rpg.MixProject do
  use Mix.Project

  @version "1.6.0"

  def project do
    [
      app: :rpg,
      version: @version,
      elixir: "~> 1.18",
      elixirc_paths: elixirc_paths(Mix.env()),
      start_permanent: false,
      deps: [],
      escript: [main_module: Rpg.Main, name: "rpg-elixir", path: "bin/rpg-elixir"],
      test_coverage: [
        summary: [threshold: 80],
        ignore_modules: [~r/^Rpg\.Test\./, Rpg.Presentation.Tui.Terminal]
      ]
    ]
  end

  def application do
    [extra_applications: []]
  end

  defp elixirc_paths(:test), do: ["lib", "test/support"]
  defp elixirc_paths(_env), do: ["lib"]
end
