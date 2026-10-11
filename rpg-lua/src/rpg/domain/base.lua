--- Language helpers shared by every layer: classes, typed errors and deterministic list utilities.
--- Lua has no classes, exceptions or stable sort, so the few lines the other ports get from their language live here.

local base = {}

--- A class is a table used as the metatable of its instances: `setmetatable({ ... }, Class)`.
function base.class(name)
	local class = { __name = name }
	class.__index = class
	return class
end

local Error = {}
Error.__index = Error
Error.__tostring = function(self)
	return self.message
end

--- Raises a typed error (the counterpart of a Python exception class): `kind` is the class name.
function base.raise(kind, message)
	error(setmetatable({ kind = kind, message = message }, Error), 0)
end

function base.is_error(value, kind)
	return getmetatable(value) == Error and (kind == nil or value.kind == kind)
end

--- Calls `fn`; returns `true, results…` or `false, error`. Errors whose kind is not listed are raised again.
function base.try(kinds, fn, ...)
	local result = table.pack(pcall(fn, ...))
	if result[1] then
		return table.unpack(result, 1, result.n)
	end
	local raised = result[2]
	for _, kind in ipairs(kinds) do
		if base.is_error(raised, kind) then
			return false, raised
		end
	end
	error(raised, 0)
end

function base.index_of(list, value)
	for index, item in ipairs(list) do
		if item == value then
			return index
		end
	end
	return nil
end

function base.contains(list, value)
	return base.index_of(list, value) ~= nil
end

function base.remove_value(list, value)
	local index = base.index_of(list, value)
	if index ~= nil then
		table.remove(list, index)
	end
	return index ~= nil
end

function base.copy_list(list)
	return table.move(list, 1, #list, 1, {})
end

function base.filter(list, keep)
	local result = {}
	for _, item in ipairs(list) do
		if keep(item) then
			result[#result + 1] = item
		end
	end
	return result
end

function base.map(list, transform)
	local result = {}
	for index, item in ipairs(list) do
		result[index] = transform(item)
	end
	return result
end

--- A sorted copy. `table.sort` is not stable, so the original position breaks ties (Python's `sorted` is stable).
function base.sorted(list, less)
	local decorated = {}
	for index, item in ipairs(list) do
		decorated[index] = { item, index }
	end
	table.sort(decorated, function(a, b)
		if less(a[1], b[1]) then
			return true
		end
		if less(b[1], a[1]) then
			return false
		end
		return a[2] < b[2]
	end)
	local result = {}
	for index, entry in ipairs(decorated) do
		result[index] = entry[1]
	end
	return result
end

--- Keys of a map in code-point order (strings compare byte by byte in the "C" locale Lua starts with).
function base.sorted_keys(map)
	local keys = {}
	for key in pairs(map) do
		keys[#keys + 1] = key
	end
	table.sort(keys)
	return keys
end

function base.by_id(a, b)
	return a.id < b.id
end

function base.count(map)
	local total = 0
	for _ in pairs(map) do
		total = total + 1
	end
	return total
end

return base
