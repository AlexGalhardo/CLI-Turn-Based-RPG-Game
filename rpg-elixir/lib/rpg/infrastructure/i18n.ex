defmodule Rpg.Infrastructure.I18n do
  @moduledoc "Flat key → template translations from shared/i18n (English is the default and the fallback)."

  alias Rpg.Domain.JsonTypes
  alias Rpg.Infrastructure.Assets

  @default_locale "en"
  @supported_locales ["en", "pt-BR"]
  @placeholder ~r/\{(\w+)\}/u

  defmodule Translator do
    @moduledoc false
    @enforce_keys [:locale, :messages, :fallback]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            locale: String.t(),
            messages: %{String.t() => String.t()},
            fallback: %{String.t() => String.t()}
          }
  end

  @spec default_locale() :: String.t()
  def default_locale, do: @default_locale

  @spec supported_locales() :: [String.t()]
  def supported_locales, do: @supported_locales

  @spec new(Assets.source(), String.t()) :: Translator.t()
  def new(source, locale \\ @default_locale) do
    if locale not in @supported_locales, do: raise(ArgumentError, "unsupported locale: #{locale}")
    fallback = load(source, @default_locale)
    messages = if locale == @default_locale, do: fallback, else: load(source, locale)
    %Translator{locale: locale, messages: messages, fallback: fallback}
  end

  defp load(source, locale) do
    {:ok, text} = Assets.read(source, "i18n/#{locale}.json")
    text |> JSON.decode!() |> JsonTypes.obj() |> Map.new(fn {key, value} -> {key, JsonTypes.str(value)} end)
  end

  @spec has?(Translator.t(), String.t()) :: boolean()
  def has?(%Translator{} = translator, key) do
    Map.has_key?(translator.messages, key) or Map.has_key?(translator.fallback, key)
  end

  @doc "Missing keys render as the key itself; missing params keep their `{placeholder}`."
  @spec t(Translator.t(), String.t(), map() | keyword()) :: String.t()
  def t(%Translator{} = translator, key, params \\ %{}) do
    params = Map.new(params, fn {name, value} -> {to_string(name), value} end)
    template = non_empty(translator.messages[key]) || non_empty(translator.fallback[key]) || key

    Regex.replace(@placeholder, template, fn whole, name ->
      case Map.fetch(params, name) do
        {:ok, value} -> to_text(value)
        :error -> whole
      end
    end)
  end

  defp non_empty(""), do: nil
  defp non_empty(value), do: value

  defp to_text(value) when is_binary(value), do: value
  defp to_text(true), do: "True"
  defp to_text(false), do: "False"
  defp to_text(value), do: to_string(value)
end
