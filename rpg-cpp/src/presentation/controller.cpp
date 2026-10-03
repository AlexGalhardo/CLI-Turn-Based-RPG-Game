#include "presentation/controller.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <random>

#include "application/loot.hpp"
#include "application/merchant.hpp"
#include "domain/character.hpp"
#include "domain/formulas.hpp"
#include "presentation/render.hpp"

namespace rpg::presentation {

namespace {

bool is_battle_view(View view) { return view == View::battle || view == View::spells || view == View::potions; }

bool is_text_input(View view) { return view == View::name || view == View::quantity; }

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
	std::string result;
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i > 0) {
			result += separator;
		}
		result += parts[i];
	}
	return result;
}

// One printable character typed by the player (a single UTF-8 code point that is not a control character).
bool is_printable_character(std::string_view key) {
	if (key.empty() || utf8_length(key) != 1) {
		return false;
	}
	const auto first = static_cast<unsigned char>(key.front());
	return first >= 0x20U && first != 0x7FU;
}

std::string_view trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

} // namespace

bool is_paged(View view) {
	return view == View::hall_of_fame || view == View::bestiary || view == View::achievements ||
	       view == View::character;
}

std::uint64_t random_seed() {
	std::random_device device;
	return static_cast<std::uint64_t>(device());
}

Controller::Controller(Services injected, std::optional<std::uint64_t> seed, std::string_view locale_override,
    std::function<std::uint64_t()> seed_source) :
    services(std::move(injected)), seed_(seed), seed_source_(seed_source ? std::move(seed_source) : random_seed) {
	const infrastructure::Settings settings = services.settings->load();
	std::string initial{infrastructure::default_locale};
	if (!locale_override.empty()) {
		initial = locale_override;
		view = View::title;
	} else if (settings.locale.has_value()) {
		initial = *settings.locale;
		view = View::title;
	}
	set_locale(initial);
}

void Controller::set_locale(std::string_view new_locale) {
	translator_ = std::make_unique<infrastructure::Translator>(*services.shared, new_locale);
	locale = new_locale;
}

std::string Controller::t(std::string_view key, const infrastructure::Params& params) const {
	return translator_->t(key, params);
}

application::SessionContext Controller::session_context() const {
	return application::SessionContext{services.repositories, services.clock, services.version};
}

// ── queries used by renderers ───────────────────────────────────────────────────────────────────────────────────────

std::string Controller::title() const {
	switch (view) {
	case View::language:
		return t("language.title");
	case View::title:
		return t("app.title");
	case View::difficulty:
		return t("new_run.difficulty");
	case View::name:
		return t("new_run.name");
	case View::vocation:
		return t("new_run.vocation");
	case View::merchant: {
		const std::int64_t round = session.has_value() ? session->state().round : 0;
		return round == 0 ? t("merchant.title_start") : t("merchant.title", {{"round", round}});
	}
	case View::buy_potions:
		return t("merchant.buy_potions");
	case View::quantity:
		return t("merchant.quantity", {{"name", services.data->potion(potion_id_).name}});
	case View::sell:
		return t("merchant.sell_items");
	case View::equipment:
		return t("merchant.equipment");
	case View::stock:
		return t("merchant.stock");
	case View::character:
		return t("merchant.character");
	case View::battle:
		return t("battle.title");
	case View::spells:
		return t("battle.spells");
	case View::potions:
		return t("battle.potions");
	case View::game_over:
		return t("gameover.title");
	case View::hall_of_fame:
		return t("menu.hall_of_fame");
	case View::bestiary:
		return t("menu.bestiary");
	case View::achievements:
		return t("menu.achievements");
	}
	return {};
}

std::vector<MenuOption> Controller::options() {
	std::vector<MenuOption> result;
	for (MenuEntry& entry : menu()) {
		result.push_back(std::move(entry.option));
	}
	return result;
}

std::vector<std::string> Controller::body_lines() {
	std::vector<std::string> lines = body();
	if (!is_paged(view) || lines.size() <= page_size) {
		return lines;
	}
	const std::size_t pages = (lines.size() + page_size - 1) / page_size;
	page = std::min(page, pages - 1);
	const std::size_t start = page * page_size;
	const std::size_t end = std::min(lines.size(), start + page_size);
	std::vector<std::string> visible(
	    lines.begin() + static_cast<std::ptrdiff_t>(start), lines.begin() + static_cast<std::ptrdiff_t>(end));
	visible.emplace_back();
	visible.push_back(t("menu.page", {{"page", page + 1}, {"pages", pages}}));
	return visible;
}

std::string Controller::input_prompt() const {
	if (is_text_input(view)) {
		return "> " + input_buffer + "_";
	}
	return {};
}

std::string Controller::header() const {
	if (!session.has_value()) {
		return t("app.subtitle");
	}
	const application::RunState& state = session->state();
	const domain::GameData& data = *services.data;
	const std::int64_t tier =
	    domain::round_info(std::max<std::int64_t>(1, state.round), data.balance, data.tier_count()).tier + 1;
	const std::string difficulty = t("difficulty." + state.config.difficulty_id);
	return t("hud.round", {{"round", state.round}, {"tier", tier}, {"difficulty", difficulty}}) + " · " +
	       t("hud.seed", {{"seed", state.seed}});
}

std::vector<std::string> Controller::weak_elements(const domain::MonsterDef& creature, bool weak) const {
	std::vector<std::string> result;
	for (const std::string_view element : domain::kElements) {
		const std::int64_t resistance = creature.resistance(element);
		if ((weak && resistance > 100) || (!weak && resistance < 100)) {
			result.push_back(t("element." + std::string(element)));
		}
	}
	return result;
}

std::string Controller::status_text(const std::vector<domain::ActiveStatus>& statuses) const {
	std::vector<std::string> parts;
	for (const auto& status : statuses) {
		parts.push_back(std::format("{}({})", t("status." + status.status_id), status.turns));
	}
	return join(parts, " ");
}

std::optional<MonsterView> Controller::monster_view() const {
	if (!session.has_value() || !session->state().monster.has_value()) {
		return std::nullopt;
	}
	const domain::MonsterInstance& monster = *session->state().monster;
	const domain::MonsterDef& creature = services.data->creature(monster.creature_id);
	const domain::MonsterAttack* main = &monster.attacks.front();
	for (const auto& attack : monster.attacks) {
		if (attack.weight > main->weight || (attack.weight == main->weight && attack.id > main->id)) {
			main = &attack;
		}
	}

	std::vector<std::string> parts;
	for (const auto& attack : monster.attacks) {
		std::string label = t("element." + attack.element);
		if (!std::ranges::contains(parts, label)) {
			parts.push_back(std::move(label));
		}
	}
	std::string details = join(parts, " · ");
	if (const auto weak = weak_elements(creature, true); !weak.empty()) {
		details += " · " + t("hud.weak", {{"elements", join(weak, ", ")}});
	}
	if (const std::string statuses = status_text(monster.statuses); !statuses.empty()) {
		details += " · " + statuses;
	}
	return MonsterView{creature.name, creature.id, monster.hp, monster.max_hp, monster.is_boss, main->element, details};
}

std::optional<PlayerView> Controller::player_view() const {
	if (!session.has_value()) {
		return std::nullopt;
	}
	const domain::Player& player = session->state().player;
	const domain::CharacterSheet sheet = domain::build_sheet(player, *services.data);
	return PlayerView{
	    .summary = t("hud.player", {{"name", player.name}, {"vocation", t("vocation." + player.vocation_id)},
	                                   {"level", player.level}, {"magicLevel", player.magic_level}}),
	    .gold = t("hud.gold", {{"gold", player.gold}}),
	    .hp = player.hp,
	    .max_hp = sheet.max_hp,
	    .mp = player.mp,
	    .max_mp = sheet.max_mp,
	    .xp = t("hud.xp", {{"xp", player.xp}, {"next", domain::xp_for_level(player.level + 1)}}),
	    .statuses = status_text(player.statuses),
	};
}

// ── input ───────────────────────────────────────────────────────────────────────────────────────────────────────────

void Controller::press(std::string_view key) {
	message.clear();
	if (is_text_input(view)) {
		text_input(key);
		return;
	}
	if (is_paged(view) && (key == "n" || key == "p")) {
		page = key == "n" ? page + 1 : (page == 0 ? 0 : page - 1);
		return;
	}
	if (key == "escape") {
		key = "0";
	}
	for (MenuEntry& entry : menu()) {
		if (entry.option.key == key) {
			entry.action();
			return;
		}
	}
}

void Controller::text_input(std::string_view key) {
	if (key == "escape") {
		input_buffer.clear();
		view = view == View::name ? View::difficulty : View::buy_potions;
	} else if (key == "backspace") {
		utf8_pop_back(input_buffer);
	} else if (key == "enter") {
		submit_text();
	} else if (view == View::name && is_printable_character(key)) {
		if (utf8_length(input_buffer) < max_name_length) {
			input_buffer += key;
		}
	} else if (view == View::quantity && key.size() == 1 && key.front() >= '0' && key.front() <= '9' &&
	           input_buffer.size() < max_quantity_digits) {
		input_buffer += key;
	}
}

void Controller::submit_text() {
	const std::string text{trim(input_buffer)};
	input_buffer.clear();
	if (view == View::name) {
		const std::size_t length = utf8_length(text);
		if (length < 1 || length > max_name_length) {
			message = t("new_run.name_invalid");
			return;
		}
		name_ = text;
		view = View::vocation;
		return;
	}
	view = View::buy_potions;
	std::int64_t quantity = 0;
	const auto [end, parse_error] = std::from_chars(text.data(), text.data() + text.size(), quantity);
	if (parse_error == std::errc{} && end == text.data() + text.size() && quantity > 0) {
		step(application::BuyPotion{potion_id_, quantity});
	}
}

// ── menus ───────────────────────────────────────────────────────────────────────────────────────────────────────────

std::vector<Controller::MenuEntry> Controller::menu() {
	const domain::GameData& data = *services.data;
	std::vector<MenuEntry> entries;
	switch (view) {
	case View::language:
		for (std::size_t i = 0; i < infrastructure::supported_locales.size(); ++i) {
			const std::string choice{infrastructure::supported_locales[i]};
			entries.push_back(
			    {{std::to_string(i + 1), t("language." + choice), ""}, [this, choice] { choose_language(choice); }});
		}
		return entries;
	case View::title:
		return title_menu();
	case View::difficulty:
		for (std::size_t i = 0; i < data.balance.difficulties.size(); ++i) {
			const std::string id = data.balance.difficulties[i].id;
			const std::string label = t("difficulty." + id) + " — " + t("difficulty." + id + ".description");
			entries.push_back({{std::to_string(i + 1), label, ""}, [this, id] { choose_difficulty(id); }});
		}
		entries.push_back(back(View::title));
		return entries;
	case View::vocation:
		for (std::size_t i = 0; i < data.vocations.size(); ++i) {
			const std::string id = data.vocations[i].id;
			const std::string label = t("vocation." + id) + " — " + t("vocation." + id + ".description");
			entries.push_back({{std::to_string(i + 1), label, ""}, [this, id] { choose_vocation(id); }});
		}
		entries.push_back(back(View::difficulty));
		return entries;
	case View::merchant:
		return {
		    {{"1", t("merchant.buy_potions"), ""}, go_to(View::buy_potions)},
		    {{"2", t("merchant.sell_items"), ""}, go_to(View::sell)},
		    {{"3", t("merchant.equipment"), ""}, go_to(View::equipment)},
		    {{"4", t("merchant.stock"), ""}, go_to(View::stock)},
		    {{"5", t("merchant.character"), ""}, go_to(View::character)},
		    {{"0", t("merchant.next_fight"), ""}, command(application::NextFight{})},
		    {{"q", t("battle.save_quit"), ""}, [this] { save_and_quit(); }},
		};
	case View::buy_potions:
		entries = potion_shop();
		entries.push_back(back(View::merchant));
		return entries;
	case View::sell:
		entries = sell_menu();
		entries.push_back(back(View::merchant));
		return entries;
	case View::equipment:
		entries = equipment_menu();
		entries.push_back(back(View::merchant));
		return entries;
	case View::stock:
		entries = stock_menu();
		entries.push_back(back(View::merchant));
		return entries;
	case View::character:
		entries.push_back(back(View::merchant));
		return entries;
	case View::battle:
		return {
		    {{"1", t("battle.attack"), ""}, command(application::Attack{})},
		    {{"2", t("battle.spells"), ""}, go_to(View::spells)},
		    {{"3", t("battle.potions"), ""}, go_to(View::potions)},
		    {{"4", t("battle.defend"), ""}, command(application::Defend{})},
		    {{"q", t("battle.save_quit"), ""}, [this] { save_and_quit(); }},
		};
	case View::spells:
		entries = spell_menu();
		entries.push_back(back(View::battle));
		return entries;
	case View::potions:
		entries = battle_potions();
		entries.push_back(back(View::battle));
		return entries;
	case View::game_over:
		return {
		    {{"1", t("gameover.new_run"), ""}, [this] { new_run(); }},
		    {{"2", t("gameover.title_screen"), ""}, go_to(View::title)},
		};
	case View::hall_of_fame:
	case View::bestiary:
	case View::achievements:
		entries.push_back(back(View::title));
		return entries;
	case View::name:
	case View::quantity:
		return entries;
	}
	return entries;
}

Controller::MenuEntry Controller::back(View target) { return {{"0", t("menu.back"), ""}, go_to(target)}; }

std::function<void()> Controller::go_to(View target) {
	return [this, target] {
		view = target;
		page = 0;
	};
}

std::function<void()> Controller::command(application::Command command) {
	return [this, command = std::move(command)] { step(command); };
}

std::vector<Controller::MenuEntry> Controller::title_menu() {
	std::vector<MenuEntry> entries;
	bool has_save = false;
	try {
		has_save = services.repositories.saves->load().has_value();
	} catch (const std::exception&) {
		has_save = false; // an unreadable or newer save can't be continued
	}
	if (has_save) {
		entries.push_back({{"1", t("menu.continue"), ""}, [this] { continue_run(); }});
	}
	entries.push_back({{"2", t("menu.new_run"), ""}, [this] { new_run(); }});
	entries.push_back({{"3", t("menu.hall_of_fame"), ""}, go_to(View::hall_of_fame)});
	entries.push_back({{"4", t("menu.bestiary"), ""}, go_to(View::bestiary)});
	entries.push_back({{"5", t("menu.achievements"), ""}, go_to(View::achievements)});
	entries.push_back({{"6", t("menu.language"), ""}, [this] { open_language(); }});
	entries.push_back({{"0", t("menu.quit"), ""}, [this] { exit_requested = true; }});
	return entries;
}

std::string Controller::item_label(
    std::string_view key, const domain::ItemInstance& item, infrastructure::Params params) const {
	const domain::ItemDef& definition = services.data->item(item.item_id);
	params.try_emplace("name", definition.name);
	params.try_emplace("rarity", t("rarity." + item.rarity));
	params.try_emplace("slot", t("slot." + definition.slot));
	return t(key, params);
}

std::vector<Controller::MenuEntry> Controller::potion_shop() {
	const application::RunState& state = session->state();
	std::vector<MenuEntry> entries;
	const std::vector<std::string> potions = application::available_potions(state, *services.data);
	for (std::size_t index = 0; index < potions.size(); ++index) {
		const std::string potion_id = potions[index];
		const domain::PotionDef& potion = services.data->potion(potion_id);
		const std::string label = t("merchant.potion_option",
		    {{"name", potion.name}, {"price", potion.price}, {"count", state.player.potion_count(potion_id)}});
		entries.push_back({{list_key(index), label, ""}, [this, potion_id] { ask_quantity(potion_id); }});
	}
	return entries;
}

std::vector<Controller::MenuEntry> Controller::sell_menu() {
	std::vector<MenuEntry> entries;
	const auto& bag = session->state().player.bag;
	for (std::size_t index = 0; index < bag.size(); ++index) {
		const domain::ItemInstance& item = bag[index];
		const std::string label =
		    item_label("merchant.sell_option", item, {{"gold", domain::item_value(item, *services.data)}});
		entries.push_back({{list_key(index), label, item.rarity}, command(application::SellItem{item.uid})});
	}
	return entries;
}

std::vector<Controller::MenuEntry> Controller::equipment_menu() {
	const domain::Player& player = session->state().player;
	const domain::VocationDef& vocation = services.data->vocation(player.vocation_id);
	std::vector<MenuEntry> entries;
	const auto add = [&](std::string label, const std::string& color, application::Command action) {
		entries.push_back({{list_key(entries.size()), std::move(label), color}, command(std::move(action))});
	};
	for (const auto& item : player.bag) {
		if (application::can_use(services.data->item(item.item_id), vocation)) {
			add(item_label("merchant.equip_option", item, {}), item.rarity, application::Equip{item.uid});
		}
	}
	for (const std::string_view slot : domain::kSlots) {
		if (const auto found = player.equipment.find(slot); found != player.equipment.end()) {
			add(item_label("merchant.unequip_option", found->second, {}), found->second.rarity,
			    application::Unequip{std::string(slot)});
		}
	}
	return entries;
}

std::vector<Controller::MenuEntry> Controller::stock_menu() {
	std::vector<MenuEntry> entries;
	const auto& stock = session->state().merchant_stock;
	for (std::size_t index = 0; index < stock.size(); ++index) {
		const domain::ItemInstance& item = stock[index];
		const std::string label =
		    item_label("merchant.stock_option", item, {{"gold", application::stock_price(item, *services.data)}});
		entries.push_back({{list_key(index), label, item.rarity},
		    command(application::BuyStockItem{static_cast<std::int64_t>(index)})});
	}
	return entries;
}

std::vector<Controller::MenuEntry> Controller::spell_menu() {
	const domain::GameData& data = *services.data;
	const domain::Player& player = session->state().player;
	std::vector<MenuEntry> entries;
	const auto& spells = data.vocation(player.vocation_id).spells;
	for (std::size_t index = 0; index < spells.size(); ++index) {
		const domain::SpellDef& spell = data.spell(spells[index]);
		const std::int64_t uses = domain::count_of(player.spell_uses, spell.id);
		const domain::SpellLevelDef& level = domain::spell_level_for_uses(uses, data.balance.spell_levels);
		const std::string label = t("battle.spell_option",
		    {{"name", spell.name}, {"words", spell.words}, {"mana", domain::pct(spell.mana, level.mana_pct)},
		        {"level", level.level}, {"uses", uses}});
		const std::string color = spell.kind == "heal" ? std::string("heal") : spell.element;
		entries.push_back({{list_key(index), label, color}, command(application::Cast{spell.id})});
	}
	return entries;
}

std::vector<Controller::MenuEntry> Controller::battle_potions() {
	const domain::Player& player = session->state().player;
	std::vector<MenuEntry> entries;
	for (const domain::PotionDef& potion : services.data->potions) {
		if (player.potion_count(potion.id) <= 0) {
			continue;
		}
		const std::string label =
		    t("battle.potion_option", {{"name", potion.name}, {"count", player.potion_count(potion.id)}});
		entries.push_back({{list_key(entries.size()), label, ""}, command(application::UsePotion{potion.id})});
	}
	return entries;
}

// ── actions ─────────────────────────────────────────────────────────────────────────────────────────────────────────

void Controller::choose_language(const std::string& new_locale) {
	set_locale(new_locale);
	try {
		services.settings->save(infrastructure::Settings{new_locale});
	} catch (const std::exception& failure) {
		error = failure.what();
	}
	view = language_return_;
}

void Controller::open_language() {
	language_return_ = View::title;
	view = View::language;
}

void Controller::new_run() {
	session.reset();
	view = View::difficulty;
}

void Controller::choose_difficulty(const std::string& difficulty_id) {
	difficulty_ = difficulty_id;
	input_buffer.clear();
	view = View::name;
}

void Controller::choose_vocation(const std::string& vocation_id) {
	const std::uint64_t seed = seed_.has_value() ? *seed_ : seed_source_();
	const application::RunConfig config{name_, vocation_id, difficulty_};
	try {
		auto started = application::GameSession::start(*services.data, config, seed, session_context());
		if (!started.has_value()) {
			error = started.error();
			return;
		}
		session.emplace(std::move(started->first));
		log.clear();
		record(application::StepResult{std::move(started->second), {}});
		view = View::merchant;
	} catch (const std::exception& failure) {
		error = failure.what();
	}
}

void Controller::continue_run() {
	try {
		std::optional<application::GameSession> resumed =
		    application::GameSession::resume(*services.data, session_context());
		error.reset();
		if (!resumed.has_value()) {
			return;
		}
		session.emplace(std::move(*resumed));
	} catch (const std::exception& failure) {
		error = failure.what();
		return;
	}
	log.clear();
	const application::RunState& state = session->state();
	push_log(t("menu.welcome_back", {{"name", state.player.name}, {"round", state.round}}));
	view = View::merchant;
}

void Controller::ask_quantity(const std::string& potion_id) {
	potion_id_ = potion_id;
	input_buffer.clear();
	view = View::quantity;
}

void Controller::save_and_quit() {
	if (session.has_value()) {
		try {
			session->save_and_quit();
		} catch (const std::exception& failure) {
			error = failure.what();
		}
	}
	session.reset();
	view = View::title;
}

void Controller::step(const application::Command& command) {
	application::StepResult result;
	try {
		result = session->step(command);
	} catch (const std::exception& failure) {
		error = failure.what();
		return;
	}
	record(result);
	const domain::Phase phase = session->state().phase;
	if (phase == domain::Phase::battle) {
		view = View::battle;
	} else if (phase == domain::Phase::game_over) {
		view = View::game_over;
	} else if (is_battle_view(view)) {
		view = View::merchant;
	}
}

void Controller::push_log(std::string line) {
	log.push_back(std::move(line));
	if (log.size() > max_log_lines) {
		log.erase(log.begin(), log.end() - static_cast<std::ptrdiff_t>(max_log_lines));
	}
}

void Controller::record(const application::StepResult& result) {
	const EventFormatter events_text = formatter();
	const application::RunState& state = session->state();
	std::vector<std::string> cues;
	for (const application::Event& event : result.events) {
		std::string text = events_text.format(event, state);
		if (event.type == "error") {
			message = std::move(text);
			continue;
		}
		push_log(std::move(text));
		if ((event.type == "player_attacked" || event.type == "spell_cast") && event.integer("damage") != 0) {
			cues.emplace_back("hurt");
		} else if (event.type == "monster_attacked") {
			cues.emplace_back("attack");
		}
	}
	for (const domain::AchievementDef& achievement : result.achievements) {
		const std::string achievement_name = t("achievement." + achievement.id + ".name");
		push_log(t("achievement.unlocked", {{"name", achievement_name}}));
	}
	animation_cues = std::move(cues);
}

} // namespace rpg::presentation
