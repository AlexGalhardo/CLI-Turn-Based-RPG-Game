--- File access. Standard Lua can open, remove and rename files but cannot create or list a directory, so those
--- calls go to the C host (`rpg_host`, compiled into the `rpg-lua` binary). Under a plain `lua` interpreter (tests,
--- development) the same functions fall back to the system shell.

local base = require("rpg.domain.base")

local filesystem = {}

local EMBEDDED_PREFIX = "@embedded"
local WINDOWS = package.config:sub(1, 1) == "\\"

local loaded, host = pcall(require, "rpg_host")
if not loaded then
	host = nil
end

filesystem.EMBEDDED = EMBEDDED_PREFIX
filesystem.WINDOWS = WINDOWS

--- True when shared/ is compiled into the binary.
function filesystem.has_embedded_assets()
	return host ~= nil and host.asset ~= nil
end

function filesystem.join(first, ...)
	local path = first
	for _, part in ipairs({ ... }) do
		if path:match("[/\\]$") then
			path = path .. part
		else
			path = path .. "/" .. part
		end
	end
	return path
end

function filesystem.parent(path)
	return path:match("^(.*)[/\\][^/\\]+[/\\]?$")
end

local function embedded_name(path)
	if path:sub(1, #EMBEDDED_PREFIX + 1) == EMBEDDED_PREFIX .. "/" then
		return path:sub(#EMBEDDED_PREFIX + 2)
	end
	return nil
end

--- The whole file as a string, or nil when it cannot be read.
function filesystem.read_file(path)
	local name = embedded_name(path)
	if name ~= nil then
		return host ~= nil and host.asset(name) or nil
	end
	local file = io.open(path, "rb")
	if file == nil then
		return nil
	end
	local content = file:read("a")
	file:close()
	return content
end

function filesystem.exists(path)
	local name = embedded_name(path)
	if name ~= nil then
		return host ~= nil and host.asset(name) ~= nil
	end
	local file = io.open(path, "rb")
	if file == nil then
		return false
	end
	file:close()
	return true
end

local function quote(path)
	if WINDOWS then
		return '"' .. path:gsub("/", "\\") .. '"'
	end
	return "'" .. path:gsub("'", "'\\''") .. "'"
end

local function shell(command)
	return os.execute(command .. (WINDOWS and " >NUL 2>NUL" or " >/dev/null 2>&1")) == true
end

function filesystem.is_dir(path)
	if host ~= nil then
		return host.is_dir(path)
	end
	if WINDOWS then
		return shell("if exist " .. quote(path .. "/") .. " (exit 0) else (exit 1)")
	end
	return shell("test -d " .. quote(path))
end

--- Creates the directory and its missing parents.
function filesystem.make_dirs(path)
	if filesystem.is_dir(path) then
		return
	end
	if host ~= nil then
		local parent = filesystem.parent(path)
		if parent ~= nil and parent ~= "" and not parent:match("^%a:$") then
			filesystem.make_dirs(parent)
		end
		host.mkdir(path)
	elseif WINDOWS then
		shell("mkdir " .. quote(path))
	else
		shell("mkdir -p " .. quote(path))
	end
	if not filesystem.is_dir(path) then
		base.raise("OSError", "cannot create directory " .. path)
	end
end

--- File names inside `path` (not recursive), in code-point order. Empty when the directory does not exist.
function filesystem.list_dir(path)
	local names = {}
	if host ~= nil then
		names = host.list_dir(path) or {}
	elseif filesystem.is_dir(path) then
		local pipe = io.popen((WINDOWS and "dir /b " or "ls -1 ") .. quote(path) .. (WINDOWS and " 2>NUL" or ""))
		if pipe ~= nil then
			for line in pipe:lines() do
				local name = line:gsub("\r$", "")
				if name ~= "" then
					names[#names + 1] = name
				end
			end
			pipe:close()
		end
	end
	table.sort(names)
	return names
end

function filesystem.write_file(path, content)
	local file, reason = io.open(path, "wb")
	if file == nil then
		base.raise("OSError", "cannot write " .. path .. ": " .. tostring(reason))
	end
	file:write(content)
	file:close()
end

--- Removes a file; a missing file is not an error.
function filesystem.remove(path)
	os.remove(path)
end

--- Moves `source` over `target` (C's `rename` does not replace an existing file on Windows).
function filesystem.replace(source, target)
	if host ~= nil then
		if not host.replace(source, target) then
			base.raise("OSError", "cannot replace " .. target)
		end
		return
	end
	if WINDOWS then
		os.remove(target)
	end
	local moved, reason = os.rename(source, target)
	if not moved then
		base.raise("OSError", "cannot replace " .. target .. ": " .. tostring(reason))
	end
end

function filesystem.home_dir()
	local home = WINDOWS and os.getenv("USERPROFILE") or os.getenv("HOME")
	return home or os.getenv("HOME") or "."
end

return filesystem
