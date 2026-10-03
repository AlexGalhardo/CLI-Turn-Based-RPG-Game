defmodule Rpg.Infrastructure.Assets do
  @moduledoc """
  Access to the shared/ tree (data, i18n, art).

  A *shared source* is either `:embedded` — the files compiled into the BEAM at build time, so the escript is
  self-contained — or a directory path (tests and `RPG_SHARED_DIR`). The Go port does the same with `go:embed`.
  """

  @shared_root Path.expand("../../../../shared", __DIR__)
  @patterns ["data/*.json", "i18n/*.json", "art/**/*.txt"]

  @files (for pattern <- @patterns, path <- Path.wildcard(Path.join(@shared_root, pattern)), into: %{} do
            {Path.relative_to(path, @shared_root), File.read!(path)}
          end)

  for path <- Map.keys(@files), do: @external_resource(Path.join(@shared_root, path))

  @type source :: :embedded | Path.t()

  # Recompiles when a shared file is added or removed (edits are covered by @external_resource).
  def __mix_recompile__? do
    current = for pattern <- @patterns, path <- Path.wildcard(Path.join(@shared_root, pattern)), do: path
    MapSet.new(current, &Path.relative_to(&1, @shared_root)) != MapSet.new(Map.keys(@files))
  end

  @doc "Reads a file by its path relative to shared/ (forward slashes)."
  @spec read(source(), String.t()) :: {:ok, binary()} | {:error, term()}
  def read(:embedded, relative) do
    case Map.fetch(@files, relative) do
      {:ok, content} -> {:ok, content}
      :error -> {:error, :enoent}
    end
  end

  def read(directory, relative) when is_binary(directory), do: File.read(Path.join(directory, relative))

  @spec exists?(source(), String.t()) :: boolean()
  def exists?(:embedded, relative), do: Map.has_key?(@files, relative)
  def exists?(directory, relative), do: File.exists?(Path.join(directory, relative))

  @doc "Human-readable location of a file, for error messages."
  @spec describe(source(), String.t()) :: String.t()
  def describe(:embedded, relative), do: "shared/" <> relative
  def describe(directory, relative), do: Path.join(directory, relative)
end
