--- The in-tree JSON module: saves must be written exactly like the reference's
--- `json.dumps(document, indent="\t", ensure_ascii=False)`.

local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/json"

T.test(GROUP, "decodes every value type", function()
	local document =
		json.decode('{"s": "x", "i": -5, "f": 1.5, "e": 1e3, "t": true, "n": null, "l": [1, "a"], "o": {}}')
	T.ok(json.is_object(document))
	T.no(json.is_array(document))
	T.eq(document.s, "x")
	T.eq(document.i, -5)
	T.eq(math.type(document.i), "integer")
	T.eq(document.f, 1.5)
	T.eq(math.type(document.f), "float")
	T.eq(document.e, 1000.0)
	T.eq(math.type(document.e), "float")
	T.eq(document.t, true)
	T.eq(document.n, json.null)
	T.ok(json.is_array(document.l))
	T.no(json.is_object(document.l))
	T.eq(document.l, { 1, "a" })
	T.ok(json.is_object(document.o))
	T.eq(document.o, {})
end)

T.test(GROUP, "decodes scalar documents and ignores whitespace", function()
	T.eq(json.decode("7"), 7)
	T.eq(json.decode(' "a" '), "a")
	T.eq(json.decode("false"), false)
	T.eq(json.decode("null"), json.null)
	T.eq(json.decode("0"), 0)
	T.eq(json.decode("9007199254740993"), 9007199254740993)
	T.eq(json.decode("9223372036854775807"), math.maxinteger)
	local document = json.decode(' \r\n\t{ "a" :\n[ 1 ,2 ]\t} \n')
	T.eq(json.encode(document), '{"a": [1, 2]}')
	local empty = json.decode("[ ]")
	T.ok(json.is_array(empty))
	T.eq(#empty, 0)
end)

T.test(GROUP, "string escapes", function()
	local value = json.decode('"\\"\\\\\\/\\b\\f\\n\\r\\t\\u00e9\\u20AC\\ud83d\\ude00\\u0001\\u007f"')
	-- " \ / BS FF LF CR TAB, é (2 bytes), € (3 bytes), U+1F600 from a surrogate pair (4 bytes), 0x01, 0x7F.
	T.eq(value, '"\\/\b\f\n\r\t\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\x01\x7F')
	-- Like ensure_ascii=False: only quotes, backslashes and control characters are escaped.
	T.eq(json.encode(value), '"\\"\\\\/\\b\\f\\n\\r\\t\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\\u0001\x7F"')
	T.eq(json.encode("\x1F\x02"), '"\\u001f\\u0002"')
end)

T.test(GROUP, "raw utf8 passes through untouched", function()
	local text = '{"n\xC3\xA3o": "\xF0\x9F\x98\x80"}'
	local document = json.decode(text)
	T.eq(document["n\xC3\xA3o"], "\xF0\x9F\x98\x80")
	T.eq(json.encode(document), text)
end)

T.test(GROUP, "keys keep the document order", function()
	local document = json.decode('{"b": 1, "a": 2, "c": {"z": 1, "y": 2}}')
	T.eq(json.keys(document), { "b", "a", "c" })
	T.eq(json.keys(document.c), { "z", "y" })
	T.eq(json.keys(json.object({ { "y", 1 }, { "x", 2 } })), { "y", "x" })
	-- A plain Lua table has no order of its own: code-point order.
	T.eq(json.keys({ b = 1, a = 2, B = 3 }), { "B", "a", "b" })
end)

T.test(GROUP, "compact round trip keeps the key order", function()
	local text = '{"b": 1, "a": [true, false, null, -5, "x"], "c": {}, "d": []}'
	T.eq(json.encode(json.decode(text)), text)
end)

T.test(GROUP, "pretty output uses tabs like the reference", function()
	local document = json.decode('{"b":1,"a":[true,false,null,-5,"x"],"c":{},"d":[],"e":{"f":[[]]}}')
	T.eq(
		json.encode(document, "\t"),
		table.concat({
			"{",
			'\t"b": 1,',
			'\t"a": [',
			"\t\ttrue,",
			"\t\tfalse,",
			"\t\tnull,",
			"\t\t-5,",
			'\t\t"x"',
			"\t],",
			'\t"c": {},',
			'\t"d": [],',
			'\t"e": {',
			'\t\t"f": [',
			"\t\t\t[]",
			"\t\t]",
			"\t}",
			"}",
		}, "\n")
	)
	T.eq(json.encode(7, "\t"), "7")
	T.eq(json.encode(json.object(), "\t"), "{}")
	T.eq(json.encode(json.array({}), "\t"), "[]")
	T.eq(json.encode(json.null, "\t"), "null")
end)

--- Files written by the reference parse and print back byte for byte.
T.test(GROUP, "pretty output reproduces python files", function()
	for _, name in ipairs({ "python_save.json", "python_profile.json", "python_save_v1_migrated.json" }) do
		local text = filesystem.read_file(helpers.fixture(name))
		T.ok(text ~= nil, "cannot read " .. name)
		-- A Windows checkout may convert the line endings.
		text = text:gsub("\r\n", "\n")
		T.ok(json.encode(json.decode(text), "\t") .. "\n" == text, name .. " does not round trip")
	end
end)

T.test(GROUP, "encodes built values", function()
	local document = json.object({
		{ "name", "Ana" },
		{ "skipped", nil },
		{ "level", 3 },
		{ "ratio", 0.5 },
		{ "alive", true },
		{ "cause", json.null },
		{ "bag", json.array({}) },
		{ "list", { 1, 2 } },
		{ "nested", json.object({ { "k", json.array({ "v" }) } }) },
	})
	T.eq(json.keys(document), { "name", "level", "ratio", "alive", "cause", "bag", "list", "nested" })
	T.eq(
		json.encode(document),
		'{"name": "Ana", "level": 3, "ratio": 0.5, "alive": true, "cause": null, "bag": [], "list": [1, 2], '
			.. '"nested": {"k": ["v"]}}'
	)
	-- An unmarked empty table is an object; a plain table is written in code-point key order.
	T.eq(json.encode({}), "{}")
	T.eq(json.encode({ b = 1, a = 2 }), '{"a": 2, "b": 1}')
	T.eq(json.encode(math.mininteger), "-9223372036854775808")
	T.raises("TypeError", function()
		json.encode({ callback = print })
	end, "function")
end)

T.test(GROUP, "set set_default remove and opt", function()
	local document = json.object({ { "a", 1 } })
	json.set(document, "b", 2)
	json.set(document, "a", 10)
	T.eq(json.keys(document), { "a", "b" })
	T.eq(json.encode(document), '{"a": 10, "b": 2}')

	json.set_default(document, "a", 99)
	json.set_default(document, "c", json.null)
	T.eq(document.a, 10)
	T.eq(json.encode(document), '{"a": 10, "b": 2, "c": null}')

	T.eq(json.opt(document, "a"), 10)
	T.eq(json.opt(document, "c"), nil)
	T.eq(json.opt(document, "missing"), nil)

	T.eq(json.remove(document, "a"), 10)
	T.eq(json.remove(document, "missing"), nil)
	T.eq(document.a, nil)
	T.eq(json.keys(document), { "b", "c" })
	T.eq(json.encode(document), '{"b": 2, "c": null}')
	-- A removed key that comes back goes to the end, like in a Python dict.
	json.set(document, "a", 1)
	T.eq(json.keys(document), { "b", "c", "a" })

	local decoded = json.decode('{"x": 1}')
	json.set(decoded, "w", 2)
	T.eq(json.encode(decoded), '{"x": 1, "w": 2}')
end)

T.test(GROUP, "equality ignores the key order", function()
	local a = json.decode('{"a": 1, "b": [1, {"c": null}], "d": "x"}')
	local b = json.decode('{"d": "x", "b": [1, {"c": null}], "a": 1}')
	T.ok(json.equal(a, b))
	T.ok(json.equal(a, a))
	T.ok(json.equal(json.null, json.null))
	T.ok(json.equal(json.decode("[]"), json.array({})))
	T.ok(json.equal(3, 3))
	T.no(json.equal(a, json.decode('{"a": 1, "b": [1, {"c": null}]}')))
	T.no(json.equal(a, json.decode('{"a": 1, "b": [1, {"c": null}], "d": "x", "e": 0}')))
	T.no(json.equal(a, json.decode('{"a": 1, "b": [1, {"c": 0}], "d": "x"}')))
	T.no(json.equal(json.decode("[1, 2]"), json.decode("[2, 1]")))
	T.no(json.equal(json.decode("[1, 2]"), json.decode("[1, 2, 3]")))
	T.no(json.equal(json.decode("[1, 2, 3]"), json.decode("[1, 2]")))
	T.no(json.equal(json.null, json.object()))
	T.no(json.equal(json.object(), json.null))
	T.no(json.equal(1, "1"))
	T.no(json.equal(json.decode('{"a": null}'), json.decode("{}")))
end)

T.test(GROUP, "null is a distinct printable value", function()
	T.eq(tostring(json.null), "null")
	T.ne(json.null, nil)
	T.ne(json.null, false)
	T.no(json.is_object(json.null))
	T.no(json.is_array(json.null))
	T.eq(next(json.null), nil)
end)

T.test(GROUP, "typed readers", function()
	local object, list = json.object({ { "a", 1 } }), json.array({ 1 })
	T.eq(json.json_obj(object), object)
	T.eq(json.json_list(list), list)
	T.eq(json.json_int(5), 5)
	T.eq(json.json_str("x"), "x")
	T.eq(json.json_bool(false), false)

	local rejected = {
		{ json.json_obj, list, "expected object, got list" },
		{ json.json_obj, json.null, "expected object, got null" },
		{ json.json_obj, nil, "expected object, got null" },
		{ json.json_obj, "x", "expected object, got string" },
		{ json.json_list, object, "expected list, got object" },
		{ json.json_list, 1, "expected list, got int" },
		{ json.json_int, true, "expected int, got boolean" },
		{ json.json_int, "1", "expected int, got string" },
		{ json.json_int, 1.5, "expected int, got number" },
		{ json.json_int, json.null, "expected int, got null" },
		{ json.json_str, 1, "expected str, got int" },
		{ json.json_str, nil, "expected str, got null" },
		{ json.json_bool, 0, "expected bool, got int" },
		{ json.json_bool, "true", "expected bool, got string" },
	}
	for _, case in ipairs(rejected) do
		T.raises("TypeError", function()
			case[1](case[2])
		end, case[3])
	end
end)

T.test(GROUP, "invalid documents are rejected", function()
	local invalid = {
		"",
		"   ",
		"{",
		"[",
		"[1,]",
		"[1 2]",
		'{"a" 1}',
		'{"a": 1,}',
		'{"a": 1 "b": 2}',
		"{a: 1}",
		"{1: 2}",
		'{"a"}',
		'"abc',
		'"\\x"',
		'"\\u12"',
		'"\\uZZZZ"',
		"tru",
		"nul",
		"True",
		"-",
		"1 2",
		"[] []",
		"{} x",
		"'a'",
		"@",
	}
	for _, text in ipairs(invalid) do
		local succeeded, raised = pcall(json.decode, text)
		T.no(succeeded, "accepted " .. T.show(text))
		T.ok(type(raised) == "table" and raised.kind == "JsonError", "wrong error for " .. T.show(text))
	end
	T.raises("JsonError", function()
		json.decode(nil)
	end, "expected JSON text")
	T.raises("JsonError", function()
		json.decode({})
	end, "expected JSON text")
	-- The message carries the line of the error.
	T.raises("JsonError", function()
		json.decode('{\n\t"a": 1,\n\t"b": ?\n}')
	end, "at line 3")
end)
