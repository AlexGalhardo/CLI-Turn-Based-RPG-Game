#include "application/progression.h"

#include "domain/character.h"
#include "domain/formulas.h"

void gain_experience(const GameData *data, Player *player, int64_t amount, EventList *events) {
	player->xp += amount;
	Event gained = event_new("xp_gained");
	event_int(&gained, "amount", amount);
	event_int(&gained, "total", player->xp);
	events_push(events, gained);

	const VocationDef *vocation = data_vocation(data, player->vocation_id);
	while (player->xp >= xp_for_level(player->level + 1)) {
		player->level++;
		CharacterSheet sheet = build_sheet(player, data);
		player->hp = min_i64(sheet.max_hp, player->hp + vocation->hp_per_level);
		player->mp = min_i64(sheet.max_mp, player->mp + vocation->mp_per_level);
		Event level_up = event_new("level_up");
		event_int(&level_up, "level", player->level);
		event_int(&level_up, "maxHp", sheet.max_hp);
		event_int(&level_up, "maxMp", sheet.max_mp);
		events_push(events, level_up);
	}
}

void after_cast(const GameData *data, Player *player, const SpellDef *spell, int64_t mana_cost, EventList *events) {
	const Balance *balance = &data->balance;
	int64_t uses_before = counter_get(&player->spell_uses, spell->id);
	counter_set(&player->spell_uses, spell->id, uses_before + 1);
	const SpellLevelDef *before = spell_level_for_uses(uses_before, balance);
	const SpellLevelDef *after = spell_level_for_uses(uses_before + 1, balance);
	if (after->level != before->level) {
		Event level_up = event_new("spell_level_up");
		event_str(&level_up, "spellId", spell->id);
		event_int(&level_up, "level", after->level);
		events_push(events, level_up);
	}

	player->mana_spent += mana_cost;
	while (player->mana_spent >= mana_for_magic_level(player->magic_level, balance)) {
		player->magic_level++;
		Event magic_up = event_new("magic_level_up");
		event_int(&magic_up, "magicLevel", player->magic_level);
		events_push(events, magic_up);
	}
}
