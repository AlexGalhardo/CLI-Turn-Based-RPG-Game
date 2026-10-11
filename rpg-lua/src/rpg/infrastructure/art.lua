--- Parses shared/art files: `@animation` headers, frames separated by `%%` (docs/data-format.md).
--- Animations: animation name → list of frames; a frame is a list of lines.

local base = require("rpg.domain.base")
local filesystem = require("rpg.infrastructure.filesystem")

local art = {}

function art.parse_art(text)
	local animations = {}
	local current
	local content = text:gsub("\r\n", "\n"):gsub("\n+$", "")
	for line in (content .. "\n"):gmatch("(.-)\n") do
		if line:sub(1, 1) == "@" then
			current = { {} }
			animations[line:sub(2):match("^%s*(.-)%s*$")] = current
		elseif line == "%%" then
			if current == nil then
				base.raise("ValueError", "frame separator before any @animation")
			end
			current[#current + 1] = {}
		elseif current == nil then
			base.raise("ValueError", "art content before the first @animation")
		else
			local frame = current[#current]
			frame[#frame + 1] = line
		end
	end
	return animations
end

local ArtLibrary = base.class("ArtLibrary")
art.ArtLibrary = ArtLibrary

function ArtLibrary.new(shared_dir)
	return setmetatable({ shared_dir = shared_dir, _cache = {} }, ArtLibrary)
end

function ArtLibrary:for_creature(creature)
	if creature.is_boss then
		return self:load_file("bosses", creature.id)
	end
	return self:load_file("families", creature.family)
end

function ArtLibrary:load_file(folder, name)
	local path = filesystem.join(self.shared_dir, "art", folder, name .. ".txt")
	if self._cache[path] == nil then
		local text = filesystem.read_file(path)
		if text == nil then
			base.raise("FileNotFoundError", "cannot read " .. path)
		end
		self._cache[path] = art.parse_art(text)
	end
	return self._cache[path]
end

--- Frame of `animation` at animation tick `tick`, falling back to idle; empty when there is no art.
function art.frame_for(animations, animation, tick)
	local frames = animations[animation]
	if frames == nil or #frames == 0 then
		frames = animations.idle
	end
	if frames == nil or #frames == 0 then
		return {}
	end
	return frames[tick % #frames + 1]
end

return art
