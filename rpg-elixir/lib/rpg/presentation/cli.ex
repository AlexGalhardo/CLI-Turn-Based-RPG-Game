defmodule Rpg.Presentation.Cli do
  @moduledoc "Command-line flags, identical in every implementation (docs/tui.md). Usage errors exit with code 2."

  alias Rpg.Infrastructure.I18n

  defmodule CliOptions do
    @moduledoc false
    defstruct seed: nil, lang: nil, no_anim: false, data_dir: nil, simulate: nil, vocation: nil, difficulty: nil

    @type t :: %__MODULE__{
            seed: non_neg_integer() | nil,
            lang: String.t() | nil,
            no_anim: boolean(),
            data_dir: String.t() | nil,
            simulate: pos_integer() | nil,
            vocation: String.t() | nil,
            difficulty: String.t() | nil
          }
  end

  # Mirrors the argparse help of the Python reference.
  @help """
  usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]
             [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]
             [--difficulty DIFFICULTY]

  Endless turn-based RPG for the terminal.

  options:
    -h, --help            show this help message and exit
    --version             show program's version number and exit
    --seed SEED           deterministic run
    --lang {en,pt-BR}     override the saved language
    --no-anim             disable animations
    --data-dir DATA_DIR   saves/profile location
    --simulate N          run N headless bot games and print a report
    --vocation VOCATION   (simulator) restrict to one vocation
    --difficulty DIFFICULTY
                          (simulator) restrict to one difficulty\
  """

  @switches [
    help: :boolean,
    version: :boolean,
    seed: :string,
    lang: :string,
    no_anim: :boolean,
    data_dir: :string,
    simulate: :string,
    vocation: :string,
    difficulty: :string
  ]

  @spec help() :: String.t()
  def help, do: @help

  @doc """
  Parses the arguments. Returns `{:ok, options}`, `{:exit, text}` for `--help`/`--version`, or `{:error, message}` for
  a usage error.
  """
  @spec parse_args([String.t()]) :: {:ok, CliOptions.t()} | {:exit, String.t()} | {:error, String.t()}
  def parse_args(argv) do
    case OptionParser.parse(argv, strict: @switches, aliases: [h: :help]) do
      {parsed, [], []} -> interpret(parsed)
      {_parsed, [extra | _], []} -> {:error, "unrecognized arguments: #{extra}"}
      {_parsed, _rest, [{flag, nil} | _]} -> {:error, "unrecognized arguments: #{flag}"}
      {_parsed, _rest, [{flag, _value} | _]} -> {:error, "argument #{flag}: expected one argument"}
    end
  end

  defp interpret(parsed) do
    cond do
      parsed[:help] -> {:exit, @help}
      parsed[:version] -> {:exit, "rpg #{Rpg.Version.version()} (elixir)"}
      true -> build(parsed)
    end
  end

  defp build(parsed) do
    with {:ok, lang} <- lang(parsed[:lang]),
         {:ok, seed} <- number(parsed[:seed], "--seed", 0, "must be >= 0"),
         {:ok, simulate} <- number(parsed[:simulate], "--simulate", 1, "must be > 0") do
      {:ok,
       %CliOptions{
         seed: seed,
         lang: lang,
         no_anim: parsed[:no_anim] == true,
         data_dir: parsed[:data_dir],
         simulate: simulate,
         vocation: parsed[:vocation],
         difficulty: parsed[:difficulty]
       }}
    end
  end

  defp lang(nil), do: {:ok, nil}

  defp lang(value) do
    if value in I18n.supported_locales(),
      do: {:ok, value},
      else: {:error, "argument --lang: invalid choice: '#{value}' (choose from 'en', 'pt-BR')"}
  end

  defp number(nil, _flag, _minimum, _message), do: {:ok, nil}

  defp number(text, flag, minimum, message) do
    case Integer.parse(text) do
      {value, ""} when value >= minimum -> {:ok, value}
      _ -> {:error, "argument #{flag}: #{message}"}
    end
  end
end
