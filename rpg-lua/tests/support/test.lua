--- The test harness: Lua has no test framework in its standard library, so this is the whole of it.
---
---   local T = require("support.test")
---   T.test("unit/rng", "roll is inclusive", function() T.eq(Rng.new(1):roll(3, 3), 3) end)
---
--- `tests/run.lua` loads every `*_test.lua`, runs the registered tests and reports the failures.

local T = { tests = {} }

function T.test(group, name, body)
	T.tests[#T.tests + 1] = { name = group .. "/" .. name, body = body }
end

local function show(value, depth)
	depth = depth or 0
	if type(value) == "string" then
		return string.format("%q", value)
	end
	if type(value) ~= "table" or depth > 3 then
		return tostring(value)
	end
	local keys = {}
	for key in pairs(value) do
		keys[#keys + 1] = key
	end
	table.sort(keys, function(a, b)
		return tostring(a) < tostring(b)
	end)
	local parts = {}
	for _, key in ipairs(keys) do
		parts[#parts + 1] = tostring(key) .. " = " .. show(value[key], depth + 1)
	end
	return "{ " .. table.concat(parts, ", ") .. " }"
end
T.show = show

local function fail(message, level)
	error({ test_failure = true, message = message, trace = debug.traceback("", (level or 1) + 1) }, 0)
end

--- Deep equality: tables are equal when they have the same keys with equal values.
local function same(a, b)
	if a == b then
		return true
	end
	if type(a) ~= "table" or type(b) ~= "table" then
		return false
	end
	for key, value in pairs(a) do
		if not same(value, b[key]) then
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
T.same = same

function T.eq(actual, expected, message)
	if not same(actual, expected) then
		fail(
			string.format(
				"%sexpected %s\n       got %s",
				message and (message .. ": ") or "",
				show(expected),
				show(actual)
			),
			2
		)
	end
end

function T.ne(actual, unexpected, message)
	if same(actual, unexpected) then
		fail(string.format("%sdid not expect %s", message and (message .. ": ") or "", show(actual)), 2)
	end
end

function T.ok(condition, message)
	if not condition then
		fail(message or "expected a true value", 2)
	end
end

function T.no(condition, message)
	if condition then
		fail(message or "expected a false value", 2)
	end
end

--- `text` contains the plain substring `part`.
function T.contains(text, part, message)
	if type(text) ~= "string" or text:find(part, 1, true) == nil then
		fail(
			string.format("%sexpected %s to contain %s", message and (message .. ": ") or "", show(text), show(part)),
			2
		)
	end
end

function T.lacks(text, part, message)
	if type(text) == "string" and text:find(part, 1, true) ~= nil then
		fail(
			string.format(
				"%sexpected %s not to contain %s",
				message and (message .. ": ") or "",
				show(text),
				show(part)
			),
			2
		)
	end
end

--- `body` must raise a typed error of `kind` (see `base.raise`) whose message contains `part`.
function T.raises(kind, body, part)
	local succeeded, raised = pcall(body)
	if succeeded then
		fail("expected a " .. kind .. " error, none was raised", 2)
	end
	if type(raised) ~= "table" or raised.kind ~= kind then
		fail("expected a " .. kind .. " error, got " .. show(type(raised) == "table" and raised.message or raised), 2)
	end
	if part ~= nil then
		T.contains(raised.message, part)
	end
	return raised
end

--- Runs the tests whose name contains `filter` (all of them when nil). Returns the number of failures.
function T.run(filter, output)
	output = output or io.stdout
	local passed, failures = 0, {}
	for _, case in ipairs(T.tests) do
		if filter == nil or case.name:find(filter, 1, true) ~= nil then
			local succeeded, raised = xpcall(case.body, function(err)
				if type(err) == "table" and err.test_failure then
					return err
				end
				local text = type(err) == "table" and tostring(err.message or err) or tostring(err)
				return { message = "error: " .. text, trace = debug.traceback("", 2) }
			end)
			if succeeded then
				passed = passed + 1
			else
				failures[#failures + 1] = { name = case.name, message = raised.message, trace = raised.trace }
			end
		end
	end
	for _, failure in ipairs(failures) do
		output:write(string.format("FAIL %s\n  %s\n%s\n", failure.name, failure.message, failure.trace))
	end
	output:write(string.format("%d passed, %d failed\n", passed, #failures))
	return #failures, passed
end

return T
