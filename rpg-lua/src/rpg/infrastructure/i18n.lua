--- Flat key → template translations from shared/i18n (English is the default and the fallback).

local base = require("rpg.domain.base")
local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")

local i18n = {}

i18n.DEFAULT_LOCALE = "en"
i18n.SUPPORTED_LOCALES = { "en", "pt-BR" }

local function load(shared_dir, locale)
	local path = filesystem.join(shared_dir, "i18n", locale .. ".json")
	local text = filesystem.read_file(path)
	if text == nil then
		base.raise("FileNotFoundError", "cannot read " .. path)
	end
	local messages = {}
	for key, value in pairs(json.json_obj(json.decode(text))) do
		messages[key] = json.json_str(value)
	end
	return messages
end

--- Text of a parameter, written like the reference does (`True`/`False` for booleans).
local function text_of(value)
	if value == true then
		return "True"
	elseif value == false then
		return "False"
	end
	return tostring(value)
end

local Translator = base.class("Translator")
i18n.Translator = Translator

function Translator.new(shared_dir, locale)
	locale = locale or i18n.DEFAULT_LOCALE
	if not base.contains(i18n.SUPPORTED_LOCALES, locale) then
		base.raise("ValueError", "unsupported locale: " .. tostring(locale))
	end
	local fallback = load(shared_dir, i18n.DEFAULT_LOCALE)
	return setmetatable({
		locale = locale,
		_fallback = fallback,
		_messages = locale == i18n.DEFAULT_LOCALE and fallback or load(shared_dir, locale),
	}, Translator)
end

function Translator:has(key)
	return self._messages[key] ~= nil or self._fallback[key] ~= nil
end

local function present(template)
	return template ~= nil and template ~= ""
end

--- Missing keys render as the key itself; missing params keep their `{placeholder}`.
function Translator:t(key, params)
	local template = self._messages[key]
	if not present(template) then
		template = self._fallback[key]
	end
	if not present(template) then
		template = key
	end
	params = params or {}
	return (
		template:gsub("{([%w_]+)}", function(name)
			local value = params[name]
			if value == nil then
				return nil
			end
			return text_of(value)
		end)
	)
end

return i18n
