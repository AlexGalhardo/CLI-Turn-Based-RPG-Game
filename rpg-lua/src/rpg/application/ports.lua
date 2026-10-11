--- Ports implemented by the infrastructure layer (Dependency Inversion: the application owns the interfaces).
--- Lua has no interface declarations: a port is any table with these methods (duck typing), documented here.
---
---   Clock              now() -> seconds since the Unix epoch (UTC)
---   SaveRepository     load() -> SaveGame or nil · save(save) · delete()
---   HistoryRepository  add(record) · list() -> { RunRecord, … }
---   ProfileRepository  load() -> Profile · save(profile)
---   Repositories       { saves = SaveRepository, history = HistoryRepository, profile = ProfileRepository }

local ports = {}

local METHODS = {
	clock = { "now" },
	saves = { "load", "save", "delete" },
	history = { "add", "list" },
	profile = { "load", "save" },
}

--- Fails early, with the missing method's name, when `implementation` does not satisfy the port `name`.
function ports.check(name, implementation)
	for _, method in ipairs(METHODS[name]) do
		if type(implementation) ~= "table" or type(implementation[method]) ~= "function" then
			error(string.format("port %s: missing method %s", name, method), 2)
		end
	end
	return implementation
end

return ports
