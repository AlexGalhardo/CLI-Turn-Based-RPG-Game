--- Filesystem locations: the shared/ folder and the player's data directory (docs/persistence.md).

local base = require("rpg.domain.base")
local filesystem = require("rpg.infrastructure.filesystem")

local paths = {}

paths.SHARED_DIR_ENV = "RPG_SHARED_DIR"
paths.DATA_DIR_ENV = "RPG_DATA_DIR"
paths.DEFAULT_DATA_DIR_NAME = ".cli-turn-based-rpg"

local function source_dir()
	local source = debug.getinfo(1, "S").source
	if source:sub(1, 1) ~= "@" then
		return "."
	end
	return filesystem.parent(source:sub(2)) or "."
end

--- `RPG_SHARED_DIR`, then the tree compiled into the binary, then the first `shared/` above `start`.
function paths.find_shared_dir(start)
	local override = os.getenv(paths.SHARED_DIR_ENV)
	if override ~= nil and override ~= "" then
		return override
	end
	if start == nil and filesystem.has_embedded_assets() then
		return filesystem.EMBEDDED
	end
	local first = start or source_dir()
	local directory = first
	while directory ~= nil do
		local candidate = filesystem.join(directory, "shared")
		if filesystem.exists(filesystem.join(candidate, "data", "balance.json")) then
			return candidate
		end
		directory = filesystem.parent(directory)
	end
	-- A relative start has no more parents to cut: keep climbing from the working directory. An absolute start has
	-- been climbed to its root already, and the working directory is not one of its parents.
	local absolute = first:match("^[/\\]") ~= nil or first:match("^%a:") ~= nil
	local relative = "."
	for _ = 1, absolute and 0 or 8 do
		local candidate = filesystem.join(relative, "shared")
		if filesystem.exists(filesystem.join(candidate, "data", "balance.json")) then
			return candidate
		end
		relative = relative .. "/.."
	end
	return base.raise("FileNotFoundError", "shared/ folder not found; set " .. paths.SHARED_DIR_ENV)
end

--- Pure: `--data-dir`, then `RPG_DATA_DIR`, then `<home>/.cli-turn-based-rpg`.
function paths.resolve_data_dir_from(cli_value, env_value, home)
	if cli_value ~= nil and cli_value ~= "" then
		return cli_value
	end
	if env_value ~= nil and env_value ~= "" then
		return env_value
	end
	return filesystem.join(home, paths.DEFAULT_DATA_DIR_NAME)
end

function paths.resolve_data_dir(cli_value)
	return paths.resolve_data_dir_from(cli_value, os.getenv(paths.DATA_DIR_ENV), filesystem.home_dir())
end

return paths
