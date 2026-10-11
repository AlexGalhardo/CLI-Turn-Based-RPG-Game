--- Upgrades older save/settings/history/profile documents to the current schema (docs/persistence.md).
---
--- Each migration takes the raw JSON of version N and returns version N + 1, so the application layer only ever reads
--- the current format. Version 1 → 2 is the 1.4.0 "ARPG update".

local base = require("rpg.domain.base")
local json = require("rpg.domain.json_types")

local json_obj, json_int = json.json_obj, json.json_int

local REMOVED_RARITY = "epic"
local REPLACEMENT_RARITY = "legendary"

local migrations = {}

local function version(document)
	return json_int(document.schemaVersion == nil and 1 or document.schemaVersion)
end

local function rename_rarities(items)
	if not json.is_array(items) then
		return
	end
	for _, raw in ipairs(items) do
		local item = json_obj(raw)
		if item.rarity == REMOVED_RARITY then
			item.rarity = REPLACEMENT_RARITY
		end
	end
end

local function stats_v1_to_v2(raw)
	local stats = json_obj(raw)
	json.set_default(stats, "itemsAutoEquipped", 0)
	json.set_default(stats, "elitesKilled", 0)
	json.set_default(stats, "potionsDropped", json.object())
	local dropped = json_obj(stats.itemsDropped)
	if dropped[REMOVED_RARITY] ~= nil then
		local epic = json_int(json.remove(dropped, REMOVED_RARITY))
		dropped[REPLACEMENT_RARITY] = json_int(dropped[REPLACEMENT_RARITY] or 0) + epic
		local sorted = {}
		for index, key in ipairs(base.sorted_keys(dropped)) do
			sorted[index] = { key, dropped[key] }
		end
		stats.itemsDropped = json.object(sorted)
	end
	rename_rarities(stats.droppedItems)
end

local function run_v1_to_v2(raw)
	local run = json_obj(raw)
	json.set_default(json_obj(run.config), "autoEquip", false)
	json.set_default(run, "won", false)
	local monster = run.monster
	if json.is_object(monster) then
		json.set_default(monster, "enemyClass", monster.isBoss == true and "boss" or "normal")
	end
	local player = json_obj(run.player)
	rename_rarities(player.bag)
	local equipment = player.equipment
	if json.is_object(equipment) then
		for _, item in pairs(equipment) do
			rename_rarities(json.array({ item }))
		end
	end
	rename_rarities(run.merchantStock)
	stats_v1_to_v2(run.stats)
end

function migrations.migrate_save(document)
	if version(document) < 2 then
		run_v1_to_v2(document.run)
		json.set(document, "schemaVersion", 2)
	end
	return document
end

function migrations.migrate_history(document)
	if version(document) < 2 then
		json.set_default(document, "won", false)
		stats_v1_to_v2(document.stats)
		json.set(document, "schemaVersion", 2)
	end
	return document
end

function migrations.migrate_profile(document)
	if version(document) < 2 then
		local hall = document.hallOfFame
		if json.is_array(hall) then
			for _, entry in ipairs(hall) do
				json.set_default(json_obj(entry), "won", false)
			end
		end
		json.set(document, "schemaVersion", 2)
	end
	return document
end

function migrations.migrate_settings(document)
	if version(document) < 2 then
		json.set_default(document, "autoEquip", false)
		json.set_default(document, "battleSpeed", 1)
		json.set(document, "schemaVersion", 2)
	end
	return document
end

return migrations
