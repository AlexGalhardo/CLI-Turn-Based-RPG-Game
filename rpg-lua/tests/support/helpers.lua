--- Shared fixtures: the loaded game data, temporary directories and the balance overrides of the reference
--- `conftest.py` (`calm`, `with_enemy_class`, `with_test_items`).

local GameEngine = require("rpg.application.engine")
local run_state = require("rpg.application.run_state")
local base = require("rpg.domain.base")
local definitions = require("rpg.domain.definitions")
local json = require("rpg.domain.json_types")
local data_loader = require("rpg.infrastructure.data_loader")
local filesystem = require("rpg.infrastructure.filesystem")
local paths = require("rpg.infrastructure.paths")

local helpers = { root = "." }

local shared_dir, game_data
local temp_dirs = {}

function helpers.shared_dir()
	if shared_dir == nil then
		shared_dir = paths.find_shared_dir(helpers.root)
	end
	return shared_dir
end

function helpers.data()
	if game_data == nil then
		game_data = data_loader.load_game_data(helpers.shared_dir())
	end
	return game_data
end

function helpers.fixture(name)
	return filesystem.join(helpers.root, "tests", "fixtures", name)
end

function helpers.read_json(path)
	local text = filesystem.read_file(path)
	if text == nil then
		error("cannot read " .. path)
	end
	return json.decode(text)
end

--- `options`: vocation ("warrior"), difficulty ("normal"), seed (42), data, auto_equip.
function helpers.new_engine(options)
	options = options or {}
	local config = run_state.RunConfig.new(
		"Tester",
		options.vocation or "warrior",
		options.difficulty or "normal",
		options.auto_equip
	)
	return GameEngine.new_run(options.data or helpers.data(), config, options.seed or 42)
end

--- Overrides fields of one `balance.enemyClasses` row (chances of 0/100 make the RNG outcome certain).
function helpers.with_enemy_class(data, class_id, overrides)
	local balance = data.balance
	local classes = {}
	for index, row in ipairs(balance.enemy_classes) do
		classes[index] = row.id == class_id and definitions.replace(row, overrides) or row
	end
	return data:with({ balance = definitions.replace(balance, { enemy_classes = classes }) })
end

function helpers.with_balance(data, overrides)
	return data:with({ balance = definitions.replace(data.balance, overrides) })
end

--- No monster dodge, parry, crit or heal, and no elites: the pre-M8 fight, for tests of other mechanics.
function helpers.calm(data)
	for _, enemy_class in ipairs(data.balance.enemy_classes) do
		data = helpers.with_enemy_class(data, enemy_class.id, { dodge = 0, parry = 0, crit = 0, heal = 0 })
	end
	return helpers.with_balance(data, { elite_chance_pct = 0 })
end

local function test_item(id, name, slot, kind, stats, stat_order)
	return {
		id = id,
		name = name,
		slot = slot,
		type = kind,
		tier = 0,
		stats = stats,
		stat_order = stat_order,
		value = 100,
	}
end

--- Adds deterministic test items (one per slot with every stat) without touching the shared files.
function helpers.with_test_items(data)
	local items = base.copy_list(data.items)
	local extra = {
		test_item("test_helmet", "Test Helmet", "helmet", "helmet", { armor = 10, maxHp = 50 }, { "armor", "maxHp" }),
		test_item("test_ring", "Test Ring", "ring", "ring", { critChance = 80, dodge = 90 }, { "critChance", "dodge" }),
		test_item("test_axe", "Test Axe", "weapon", "axe", { attack = 20 }, { "attack" }),
		test_item("test_rod", "Test Rod", "weapon", "rod", { attack = 1 }, { "attack" }),
	}
	for _, item in ipairs(extra) do
		items[#items + 1] = item
	end
	return data:with({ items = items })
end

local counter = 0

--- A fresh directory that `cleanup` removes when the run ends. Tests never touch the real save directory.
function helpers.temp_dir()
	counter = counter + 1
	local parent = os.getenv("RPG_TEST_TMP") or os.getenv("TMPDIR") or os.getenv("TEMP") or "/tmp"
	local path =
		string.format("%s/rpg-lua-test-%d-%d-%d", parent:gsub("\\", "/"), os.time(), math.random(1, 999999), counter)
	filesystem.make_dirs(path)
	temp_dirs[#temp_dirs + 1] = path
	return path
end

function helpers.cleanup()
	for _, path in ipairs(temp_dirs) do
		if filesystem.WINDOWS then
			os.execute('rmdir /s /q "' .. path:gsub("/", "\\") .. '" >NUL 2>NUL')
		else
			os.execute("rm -rf '" .. path .. "'")
		end
	end
	temp_dirs = {}
end

--- A clock that advances 10 s before returning, like the reference FakeClock (seconds since the epoch, UTC).
function helpers.fake_clock(start)
	local clock = { time = start or 1767225600 }
	function clock:now()
		self.time = self.time + 10
		return self.time
	end
	return clock
end

return helpers
