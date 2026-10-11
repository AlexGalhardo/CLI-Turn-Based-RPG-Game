--- JSON reader/writer and typed helpers to read untrusted JSON (data files, saves).
--- Lua ships no JSON library and its tables have no key order, so this module keeps what the save format needs:
--- objects remember the order of their keys, arrays are told apart from objects, and `null` is a value.

local base = require("rpg.domain.base")

local json = {}

--- JSON `null` (a Lua table cannot hold `nil`).
json.null = setmetatable({}, {
	__tostring = function()
		return "null"
	end,
})

-- Side tables with weak keys: the tables themselves stay plain, so `pairs` only sees the real content.
local key_order = setmetatable({}, { __mode = "k" })
local arrays = setmetatable({}, { __mode = "k" })

--- Marks `list` as a JSON array (needed for empty lists) and returns it.
function json.array(list)
	arrays[list] = true
	return list
end

--- Builds an object from `{ { key, value }, … }`, keeping that key order. Pairs without a value are skipped.
function json.object(pairs_list)
	local object, order = {}, {}
	for _, pair in ipairs(pairs_list or {}) do
		if pair[2] ~= nil then
			object[pair[1]] = pair[2]
			order[#order + 1] = pair[1]
		end
	end
	key_order[object] = order
	return object
end

function json.is_array(value)
	return type(value) == "table" and arrays[value] == true
end

function json.is_object(value)
	return type(value) == "table" and value ~= json.null and not arrays[value]
end

--- Keys in insertion order; code-point order for a table that was not built by this module.
function json.keys(object)
	local order = key_order[object]
	if order == nil then
		return base.sorted_keys(object)
	end
	return order
end

function json.set(object, key, value)
	local order = key_order[object]
	if order ~= nil and object[key] == nil then
		order[#order + 1] = key
	end
	object[key] = value
end

--- Python's `dict.setdefault`: a missing key gets `value`.
function json.set_default(object, key, value)
	if object[key] == nil then
		json.set(object, key, value)
	end
end

function json.remove(object, key)
	local order = key_order[object]
	if order ~= nil then
		base.remove_value(order, key)
	end
	local value = object[key]
	object[key] = nil
	return value
end

--- The value of `key`, or `nil` when it is missing or `null` (Python's `data.get(key)`).
function json.opt(object, key)
	local value = rawget(object, key)
	if value == json.null then
		return nil
	end
	return value
end

-- ── typed readers ───────────────────────────────────────────────────────────

local function type_name(value)
	if value == nil or value == json.null then
		return "null"
	end
	if json.is_array(value) then
		return "list"
	end
	if type(value) == "table" then
		return "object"
	end
	if math.type(value) == "integer" then
		return "int"
	end
	return type(value)
end

function json.json_obj(value)
	if not json.is_object(value) then
		base.raise("TypeError", "expected object, got " .. type_name(value))
	end
	return value
end

function json.json_list(value)
	if not json.is_array(value) then
		base.raise("TypeError", "expected list, got " .. type_name(value))
	end
	return value
end

function json.json_int(value)
	if math.type(value) ~= "integer" then
		base.raise("TypeError", "expected int, got " .. type_name(value))
	end
	return value
end

function json.json_str(value)
	if type(value) ~= "string" then
		base.raise("TypeError", "expected str, got " .. type_name(value))
	end
	return value
end

function json.json_bool(value)
	if type(value) ~= "boolean" then
		base.raise("TypeError", "expected bool, got " .. type_name(value))
	end
	return value
end

-- ── reader ──────────────────────────────────────────────────────────────────

local ESCAPES = { ['"'] = '"', ["\\"] = "\\", ["/"] = "/", b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" }

local function fail(text, position, message)
	local line = select(2, text:sub(1, position):gsub("\n", "")) + 1
	base.raise("JsonError", string.format("%s at line %d", message, line))
end

local function skip_space(text, position)
	return text:find("[^ \t\r\n]", position) or #text + 1
end

local function read_unicode(text, position)
	local hex = text:match("^%x%x%x%x", position)
	if hex == nil then
		fail(text, position, "invalid \\u escape")
	end
	return tonumber(hex, 16), position + 4
end

local function read_string(text, position)
	local parts = {}
	local cursor = position + 1
	while true do
		local stop = text:find('["\\]', cursor)
		if stop == nil then
			fail(text, position, "unterminated string")
		end
		parts[#parts + 1] = text:sub(cursor, stop - 1)
		if text:sub(stop, stop) == '"' then
			return table.concat(parts), stop + 1
		end
		local escape = text:sub(stop + 1, stop + 1)
		if escape == "u" then
			local code
			code, cursor = read_unicode(text, stop + 2)
			-- A pair of UTF-16 surrogates encodes one code point above U+FFFF.
			if code >= 0xD800 and code <= 0xDBFF and text:sub(cursor, cursor + 1) == "\\u" then
				local low, after = read_unicode(text, cursor + 2)
				if low >= 0xDC00 and low <= 0xDFFF then
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00)
					cursor = after
				end
			end
			parts[#parts + 1] = utf8.char(code)
		elseif ESCAPES[escape] ~= nil then
			parts[#parts + 1] = ESCAPES[escape]
			cursor = stop + 2
		else
			fail(text, stop, "invalid escape")
		end
	end
end

local function read_number(text, position)
	local literal = text:match("^-?%d+%.?%d*[eE]?[+-]?%d*", position)
	if literal == nil then
		fail(text, position, "invalid number")
	end
	local number = tonumber(literal)
	if number == nil then
		fail(text, position, "invalid number")
	end
	return number, position + #literal
end

local read_value

local function read_array(text, position)
	local list = json.array({})
	local cursor = skip_space(text, position + 1)
	if text:sub(cursor, cursor) == "]" then
		return list, cursor + 1
	end
	while true do
		list[#list + 1], cursor = read_value(text, cursor)
		cursor = skip_space(text, cursor)
		local separator = text:sub(cursor, cursor)
		if separator == "]" then
			return list, cursor + 1
		end
		if separator ~= "," then
			fail(text, cursor, "expected ',' or ']'")
		end
		cursor = cursor + 1
	end
end

local function read_object(text, position)
	local object = json.object()
	local cursor = skip_space(text, position + 1)
	if text:sub(cursor, cursor) == "}" then
		return object, cursor + 1
	end
	while true do
		cursor = skip_space(text, cursor)
		if text:sub(cursor, cursor) ~= '"' then
			fail(text, cursor, "expected a string key")
		end
		local key
		key, cursor = read_string(text, cursor)
		cursor = skip_space(text, cursor)
		if text:sub(cursor, cursor) ~= ":" then
			fail(text, cursor, "expected ':'")
		end
		local value
		value, cursor = read_value(text, cursor + 1)
		json.set(object, key, value)
		cursor = skip_space(text, cursor)
		local separator = text:sub(cursor, cursor)
		if separator == "}" then
			return object, cursor + 1
		end
		if separator ~= "," then
			fail(text, cursor, "expected ',' or '}'")
		end
		cursor = cursor + 1
	end
end

local LITERALS = { { "true", true }, { "false", false }, { "null", json.null } }

function read_value(text, position)
	local cursor = skip_space(text, position)
	local first = text:sub(cursor, cursor)
	if first == "{" then
		return read_object(text, cursor)
	elseif first == "[" then
		return read_array(text, cursor)
	elseif first == '"' then
		return read_string(text, cursor)
	elseif first == "-" or first:match("%d") then
		return read_number(text, cursor)
	end
	for _, literal in ipairs(LITERALS) do
		if text:sub(cursor, cursor + #literal[1] - 1) == literal[1] then
			return literal[2], cursor + #literal[1]
		end
	end
	return fail(text, cursor, "unexpected character")
end

--- Parses one JSON document. Raises a `JsonError` on invalid input.
function json.decode(text)
	if type(text) ~= "string" then
		base.raise("JsonError", "expected JSON text")
	end
	local value, cursor = read_value(text, 1)
	if skip_space(text, cursor) <= #text then
		fail(text, cursor, "unexpected content after the document")
	end
	return value
end

-- ── writer ──────────────────────────────────────────────────────────────────

local STRING_ESCAPES = {
	['"'] = '\\"',
	["\\"] = "\\\\",
	["\b"] = "\\b",
	["\f"] = "\\f",
	["\n"] = "\\n",
	["\r"] = "\\r",
	["\t"] = "\\t",
}

local function write_string(text)
	return '"'
		.. text:gsub('[\0-\31"\\]', function(character)
			return STRING_ESCAPES[character] or string.format("\\u%04x", character:byte())
		end)
		.. '"'
end

local function is_list(value)
	if arrays[value] then
		return true
	end
	return key_order[value] == nil and #value > 0
end

local function write_value(value, indent, depth, out)
	local kind = type(value)
	if value == json.null then
		out[#out + 1] = "null"
	elseif kind == "boolean" then
		out[#out + 1] = tostring(value)
	elseif kind == "number" then
		out[#out + 1] = math.type(value) == "integer" and string.format("%d", value) or tostring(value)
	elseif kind == "string" then
		out[#out + 1] = write_string(value)
	elseif kind == "table" then
		local list = is_list(value)
		local keys = list and value or json.keys(value)
		if #keys == 0 then
			out[#out + 1] = list and "[]" or "{}"
			return
		end
		local newline = indent and ("\n" .. indent:rep(depth + 1)) or ""
		out[#out + 1] = list and "[" or "{"
		for index, key in ipairs(keys) do
			if index > 1 then
				out[#out + 1] = indent and "," or ", "
			end
			out[#out + 1] = newline
			if list then
				write_value(key, indent, depth + 1, out)
			else
				out[#out + 1] = write_string(key) .. ": "
				write_value(value[key], indent, depth + 1, out)
			end
		end
		out[#out + 1] = indent and ("\n" .. indent:rep(depth)) or ""
		out[#out + 1] = list and "]" or "}"
	else
		base.raise("TypeError", "cannot write a " .. kind .. " as JSON")
	end
end

--- Serialises like Python's `json.dumps(value, indent=indent, ensure_ascii=False)`, so saves look the same.
function json.encode(value, indent)
	local out = {}
	write_value(value, indent, 0, out)
	return table.concat(out)
end

-- ── comparison ──────────────────────────────────────────────────────────────

--- Structural equality (object key order is ignored): what the golden tests compare.
function json.equal(a, b)
	if a == b then
		return true
	end
	if type(a) ~= "table" or type(b) ~= "table" or a == json.null or b == json.null then
		return false
	end
	for key, value in pairs(a) do
		if not json.equal(value, b[key]) then
			return false
		end
	end
	for key in pairs(b) do
		if a[key] == nil then
			return false
		end
	end
	return true
end

return json
