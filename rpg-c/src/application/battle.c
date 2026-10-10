#include "application/battle.h"

#include "application/progression.h"
#include "domain/character.h"
#include "domain/formulas.h"

#include <string.h>

static Player *player_of(const Battle *battle) { return &battle->state->player; }

static MonsterInstance *monster_of(const Battle *battle) {
	if (!battle->state->has_monster) {
		fatal("battle without a monster");
	}
	return &battle->state->monster;
}

static CharacterSheet sheet_of(const Battle *battle) { return build_sheet(player_of(battle), battle->data); }

static const EnemyClassDef *class_of(const Battle *battle) {
	return balance_enemy_class(&battle->data->balance, monster_of(battle)->enemy_class);
}

static int64_t resistance_of(const Battle *battle, Element element) {
	return data_creature(battle->data, monster_of(battle)->creature_id)->resistances[element];
}

// ── validation ──────────────────────────────────────────────────────────────

int64_t spell_cost(const GameData *data, const Player *player, const SpellDef *spell) {
	const SpellLevelDef *level = spell_level_for_uses(counter_get(&player->spell_uses, spell->id), &data->balance);
	return pct(spell->mana, level->mana_pct);
}

bool battle_validate(const Battle *battle, const Command *command, Event *error) {
	const Player *player = player_of(battle);
	if (command->type == CMD_CAST) {
		const VocationDef *vocation = data_vocation(battle->data, player->vocation_id);
		if (!vocation_has_spell(vocation, command->id)) {
			*error = event_error(ERROR_UNKNOWN_SPELL);
			return false;
		}
		if (player->mp < spell_cost(battle->data, player, data_spell(battle->data, command->id))) {
			*error = event_error(ERROR_NOT_ENOUGH_MANA);
			return false;
		}
	} else if (command->type == CMD_USE_POTION) {
		if (find_potion(battle->data, command->id) == NULL) {
			*error = event_error(ERROR_UNKNOWN_POTION);
			return false;
		}
		if (counter_get(&player->potions, command->id) <= 0) {
			*error = event_error(ERROR_NO_POTION);
			return false;
		}
	}
	return true;
}

// ── statuses ────────────────────────────────────────────────────────────────

static void hit_monster(Battle *battle, int64_t damage) {
	MonsterInstance *monster = monster_of(battle);
	monster->hp = max_i64(0, monster->hp - damage);
}

static int find_status(const StatusList *statuses, const char *status_id) {
	for (int i = 0; i < statuses->count; i++) {
		if (strcmp(statuses->items[i].status_id, status_id) == 0) {
			return i;
		}
	}
	return -1;
}

static void push_status_applied(EventList *events, Target target, const char *status, int64_t turns, int64_t per_turn) {
	Event applied = event_new("status_applied");
	event_str(&applied, "target", target_name(target));
	event_str(&applied, "status", status);
	event_int(&applied, "turns", turns);
	event_int(&applied, "perTurn", per_turn);
	events_push(events, applied);
}

static void apply_status(Battle *battle, Target target, const char *status_id, int64_t per_turn, EventList *events) {
	const StatusDef *definition = data_status(battle->data, status_id);
	StatusList *statuses;
	int64_t cooldown;
	if (target == TARGET_PLAYER) {
		statuses = &player_of(battle)->statuses;
		cooldown = player_of(battle)->stun_cooldown;
	} else {
		statuses = &monster_of(battle)->statuses;
		cooldown = monster_of(battle)->stun_cooldown;
		if (resistance_of(battle, definition->element) == 0) {
			return;
		}
	}

	if (definition->kind == STATUS_STUN) {
		if (cooldown > 0 || find_status(statuses, STATUS_STUN_ID) >= 0) {
			return;
		}
		ActiveStatus stun = {.turns = definition->turns, .per_turn = 0};
		id_set(stun.status_id, STATUS_STUN_ID);
		status_list_push(statuses, stun);
		push_status_applied(events, target, STATUS_STUN_ID, definition->turns, 0);
		return;
	}

	int index = find_status(statuses, status_id);
	if (index < 0) {
		ActiveStatus fresh = {.turns = definition->turns, .per_turn = per_turn};
		id_set(fresh.status_id, status_id);
		status_list_push(statuses, fresh);
		index = statuses->count - 1;
	} else {
		statuses->items[index].turns = definition->turns;
		statuses->items[index].per_turn = max_i64(statuses->items[index].per_turn, per_turn);
	}
	push_status_applied(events, target, status_id, statuses->items[index].turns, statuses->items[index].per_turn);
}

static int64_t status_damage(Battle *battle, Target target, int64_t per_turn, Element element) {
	if (target == TARGET_PLAYER) {
		return max_i64(1, pct(per_turn, 100 - sheet_of(battle).protections[element]));
	}
	int64_t resistance = resistance_of(battle, element);
	return resistance == 0 ? 0 : max_i64(1, pct(per_turn, resistance));
}

static void tick(Battle *battle, Target target, StatusList *statuses, EventList *events) {
	// Index-based walk: an expired status is removed in place, so the index only advances when nothing was removed.
	int index = 0;
	while (index < statuses->count) {
		ActiveStatus *status = &statuses->items[index];
		const StatusDef *definition = data_status(battle->data, status->status_id);
		if (definition->kind != STATUS_DOT) {
			index++;
			continue;
		}
		int64_t damage = status_damage(battle, target, status->per_turn, definition->element);
		if (target == TARGET_PLAYER) {
			player_of(battle)->hp = max_i64(0, player_of(battle)->hp - damage);
		} else {
			hit_monster(battle, damage);
		}
		Event ticked = event_new("status_ticked");
		event_str(&ticked, "target", target_name(target));
		event_str(&ticked, "status", status->status_id);
		event_int(&ticked, "damage", damage);
		events_push(events, ticked);
		status->turns--;
		if (status->turns <= 0) {
			Event expired = event_new("status_expired");
			event_str(&expired, "target", target_name(target));
			event_str(&expired, "status", status->status_id);
			events_push(events, expired);
			status_list_remove(statuses, index);
		} else {
			index++;
		}
	}
}

static bool consume_stun(StatusList *statuses) {
	int index = find_status(statuses, STATUS_STUN_ID);
	if (index < 0) {
		return false;
	}
	status_list_remove(statuses, index);
	return true;
}

// ── step 1: player action ───────────────────────────────────────────────────

static bool monster_dodges(Battle *battle, EventList *events) {
	if (!rng_chance(battle->rng, class_of(battle)->dodge)) {
		return false;
	}
	events_push(events, event_new("monster_dodged"));
	return true;
}

// Physical hits only: the monster takes nothing and reflects part of the hit (no mitigation).
static bool monster_parries(Battle *battle, int64_t damage, Element element, EventList *events) {
	if (element != ELEMENT_PHYSICAL || !rng_chance(battle->rng, class_of(battle)->parry)) {
		return false;
	}
	Player *player = player_of(battle);
	int64_t reflected = max_i64(1, pct(damage, battle->data->balance.parry_reflect_pct));
	player->hp = max_i64(0, player->hp - reflected);
	Event parried = event_new("monster_parried");
	event_int(&parried, "reflected", reflected);
	events_push(events, parried);
	return true;
}

static int64_t roll_crit(Battle *battle, int64_t damage, const CharacterSheet *sheet, bool *crit) {
	*crit = rng_chance(battle->rng, sheet->crit_chance);
	if (*crit) {
		return pct(damage, battle->data->balance.crit_multiplier_pct + sheet->crit_damage);
	}
	return damage;
}

static int64_t resisted(Battle *battle, int64_t damage, Element element) {
	int64_t resistance = resistance_of(battle, element);
	if (resistance == 0) {
		return 0;
	}
	return max_i64(1, pct(damage, resistance));
}

static void leech(Battle *battle, int64_t damage, const CharacterSheet *sheet, EventList *events) {
	Player *player = player_of(battle);
	int64_t hp_gain = min_i64(pct(damage, sheet->life_leech), sheet->max_hp - player->hp);
	int64_t mp_gain = min_i64(pct(damage, sheet->mana_leech), sheet->max_mp - player->mp);
	if (hp_gain <= 0 && mp_gain <= 0) {
		return;
	}
	hp_gain = max_i64(0, hp_gain);
	mp_gain = max_i64(0, mp_gain);
	player->hp += hp_gain;
	player->mp += mp_gain;
	Event leeched = event_new("leeched");
	event_int(&leeched, "hp", hp_gain);
	event_int(&leeched, "mp", mp_gain);
	events_push(events, leeched);
}

static void melee(Battle *battle, EventList *events) {
	CharacterSheet sheet = sheet_of(battle);
	if (monster_dodges(battle, events)) {
		return;
	}
	int64_t damage = pct(rng_roll(battle->rng, sheet.melee_min, sheet.melee_max), 100 + sheet.physical_damage);
	bool crit;
	damage = roll_crit(battle, damage, &sheet, &crit);
	damage = resisted(battle, damage, sheet.weapon_element);
	if (monster_parries(battle, damage, sheet.weapon_element, events)) {
		return;
	}
	hit_monster(battle, damage);
	Event attacked = event_new("player_attacked");
	event_int(&attacked, "damage", damage);
	event_bool(&attacked, "crit", crit);
	event_str(&attacked, "element", element_name(sheet.weapon_element));
	events_push(events, attacked);
	leech(battle, damage, &sheet, events);
}

static void cast(Battle *battle, const SpellDef *spell, EventList *events) {
	Player *player = player_of(battle);
	const Balance *balance = &battle->data->balance;
	CharacterSheet sheet = sheet_of(battle);
	const SpellLevelDef *level = spell_level_for_uses(counter_get(&player->spell_uses, spell->id), balance);
	int64_t cost = pct(spell->mana, level->mana_pct);
	player->mp -= cost;
	if (spell->kind == SPELL_ATTACK && monster_dodges(battle, events)) {
		after_cast(battle->data, player, spell, cost, events);
		return;
	}
	int64_t bonus = player->level * spell->per_level + player->magic_level * spell->per_magic_level;
	int64_t amount = pct(
	    pct(rng_roll(battle->rng, spell->min + bonus, spell->max + bonus), level->effect_pct), 100 + sheet.spell_power);

	if (spell->kind == SPELL_ATTACK) {
		bool crit;
		int64_t damage = roll_crit(battle, amount, &sheet, &crit);
		damage = resisted(battle, damage, spell->element);
		if (monster_parries(battle, damage, spell->element, events)) {
			after_cast(battle->data, player, spell, cost, events);
			return;
		}
		hit_monster(battle, damage);
		Event spell_cast = event_new("spell_cast");
		event_str(&spell_cast, "spellId", spell->id);
		event_int(&spell_cast, "damage", damage);
		event_bool(&spell_cast, "crit", crit);
		event_str(&spell_cast, "element", element_name(spell->element));
		event_int(&spell_cast, "mana", cost);
		events_push(events, spell_cast);
		leech(battle, damage, &sheet, events);
		const Level3Bonus *bonus_effect = &spell->level3_bonus;
		if (level->level == 3 && bonus_effect->has_status && rng_chance(battle->rng, bonus_effect->chance)) {
			int64_t per_turn = max_i64(1, pct(damage, balance->spell_status_damage_pct));
			apply_status(battle, TARGET_MONSTER, bonus_effect->status, per_turn, events);
		}
	} else {
		int64_t healed = min_i64(amount, sheet.max_hp - player->hp);
		player->hp += healed;
		Event spell_healed = event_new("spell_healed");
		event_str(&spell_healed, "spellId", spell->id);
		event_int(&spell_healed, "amount", healed);
		event_int(&spell_healed, "mana", cost);
		events_push(events, spell_healed);
		if (level->level == 3 && spell->level3_bonus.cleanse) {
			for (int i = 0; i < player->statuses.count; i++) {
				Event expired = event_new("status_expired");
				event_str(&expired, "target", target_name(TARGET_PLAYER));
				event_str(&expired, "status", player->statuses.items[i].status_id);
				events_push(events, expired);
			}
			player->statuses.count = 0;
		}
	}
	after_cast(battle->data, player, spell, cost, events);
}

static void drink(Battle *battle, const char *potion_id, EventList *events) {
	Player *player = player_of(battle);
	CharacterSheet sheet = sheet_of(battle);
	const PotionDef *potion = data_potion(battle->data, potion_id);
	counter_add(&player->potions, potion_id, -1);
	int64_t amount = rng_roll(battle->rng, potion->min, potion->max);
	int64_t restored;
	if (potion->resource == RESOURCE_HP) {
		restored = min_i64(amount, sheet.max_hp - player->hp);
		player->hp += restored;
	} else {
		restored = min_i64(amount, sheet.max_mp - player->mp);
		player->mp += restored;
	}
	Event used = event_new("potion_used");
	event_str(&used, "potionId", potion_id);
	event_int(&used, "amount", restored);
	event_str(&used, "resource", resource_name(potion->resource));
	events_push(events, used);
}

static void player_action(Battle *battle, const Command *command, EventList *events) {
	switch (command->type) {
	case CMD_ATTACK:
		melee(battle, events);
		break;
	case CMD_CAST:
		cast(battle, data_spell(battle->data, command->id), events);
		break;
	case CMD_USE_POTION:
		drink(battle, command->id, events);
		break;
	case CMD_DEFEND:
		player_of(battle)->defending = true;
		events_push(events, event_new("player_defended"));
		break;
	default:
		fatal("not a battle command");
	}
}

// ── step 3: monster phase ───────────────────────────────────────────────────

static void resolve_monster_attack(Battle *battle, const MonsterAttack *attack, bool charged, EventList *events) {
	Player *player = player_of(battle);
	CharacterSheet sheet = sheet_of(battle);
	const Balance *balance = &battle->data->balance;
	if (rng_chance(battle->rng, sheet.dodge)) {
		Event dodged = event_new("attack_dodged");
		event_str(&dodged, "attackId", attack->id);
		events_push(events, dodged);
		return;
	}
	int64_t damage = rng_roll(battle->rng, attack->min, attack->max);
	if (charged) {
		damage = pct(damage, balance->boss_charge_damage_pct);
	}
	if (attack->element == ELEMENT_PHYSICAL && rng_chance(battle->rng, sheet.parry)) {
		int64_t reflected = max_i64(1, pct(damage, balance->parry_reflect_pct));
		hit_monster(battle, reflected);
		Event parried = event_new("attack_parried");
		event_str(&parried, "attackId", attack->id);
		event_int(&parried, "reflected", reflected);
		events_push(events, parried);
		return;
	}
	bool crit = rng_chance(battle->rng, class_of(battle)->crit);
	if (crit) {
		damage = pct(damage, balance->crit_multiplier_pct);
	}
	if (attack->element == ELEMENT_PHYSICAL) {
		damage = armor_mitigation(damage, sheet.armor);
	}
	damage = pct(damage, 100 - sheet.protections[attack->element]);
	if (player->defending) {
		damage = pct(damage, balance->defend_damage_pct);
	}
	damage = max_i64(1, damage);
	player->hp = max_i64(0, player->hp - damage);
	Event attacked = event_new("monster_attacked");
	event_str(&attacked, "attackId", attack->id);
	event_int(&attacked, "damage", damage);
	event_str(&attacked, "element", element_name(attack->element));
	event_bool(&attacked, "charged", charged);
	event_bool(&attacked, "crit", crit);
	events_push(events, attacked);
	if (attack->has_status && rng_chance(battle->rng, attack->status.chance)) {
		int64_t per_turn = max_i64(1, pct(damage, attack->status.damage_pct));
		apply_status(battle, TARGET_PLAYER, attack->status.status, per_turn, events);
	}
}

static void monster_phase(Battle *battle, EventList *events) {
	MonsterInstance *monster = monster_of(battle);
	const Balance *balance = &battle->data->balance;
	tick(battle, TARGET_MONSTER, &monster->statuses, events);
	if (monster->hp <= 0) {
		return;
	}
	if (consume_stun(&monster->statuses)) {
		monster->stun_cooldown = STUN_COOLDOWN_TURNS;
		events_push(events, event_new("monster_stunned"));
		return;
	}
	// A healing monster does nothing else this turn; a boss does not advance its pattern.
	if (monster->hp < monster->max_hp && rng_chance(battle->rng, class_of(battle)->heal)) {
		int64_t healed = min_i64(pct(monster->max_hp, balance->monster_heal_pct), monster->max_hp - monster->hp);
		monster->hp += healed;
		Event heal = event_new("monster_healed");
		event_int(&heal, "amount", healed);
		events_push(events, heal);
		return;
	}

	if (monster->is_boss) {
		int64_t every = balance->boss_telegraph_every;
		int64_t position = monster->boss_actions % (every + 1);
		monster->boss_actions++;
		const MonsterDef *creature = data_creature(battle->data, monster->creature_id);
		if (creature->has_charge_attack && position == every - 1) {
			const MonsterAttack *charge = monster_attack(monster, creature->charge_attack);
			Event telegraph = event_new("boss_telegraph");
			event_str(&telegraph, "attackId", charge->id);
			event_str(&telegraph, "element", element_name(charge->element));
			events_push(events, telegraph);
			return;
		}
		if (creature->has_charge_attack && position == every) {
			resolve_monster_attack(battle, monster_attack(monster, creature->charge_attack), true, events);
			return;
		}
	}

	int64_t weights[MAX_ATTACKS];
	for (int i = 0; i < monster->attack_count; i++) {
		weights[i] = monster->attacks[i].weight;
	}
	size_t index = rng_weighted(battle->rng, weights, (size_t)monster->attack_count);
	resolve_monster_attack(battle, &monster->attacks[index], false, events);
}

// ── step 5: end of turn ─────────────────────────────────────────────────────

// Returns true when the player died from status ticks.
static bool end_of_turn(Battle *battle, EventList *events) {
	Player *player = player_of(battle);
	tick(battle, TARGET_PLAYER, &player->statuses, events);
	if (player->hp <= 0) {
		return true;
	}
	CharacterSheet sheet = sheet_of(battle);
	int64_t hp_gain = max_i64(0, min_i64(sheet.hp_regen, sheet.max_hp - player->hp));
	int64_t mp_gain = max_i64(0, min_i64(sheet.mp_regen, sheet.max_mp - player->mp));
	player->hp += hp_gain;
	player->mp += mp_gain;
	if (hp_gain > 0 || mp_gain > 0) {
		Event regenerated = event_new("regenerated");
		event_int(&regenerated, "hp", hp_gain);
		event_int(&regenerated, "mp", mp_gain);
		events_push(events, regenerated);
	}
	player->defending = false;
	player->stun_cooldown = max_i64(0, player->stun_cooldown - 1);
	MonsterInstance *monster = monster_of(battle);
	monster->stun_cooldown = max_i64(0, monster->stun_cooldown - 1);
	battle->state->turn++;
	return false;
}

// ── turn ────────────────────────────────────────────────────────────────────

// Steps 2 and 4: a parried hit can kill the attacker, so the player is checked first.
static bool death_check(Battle *battle, BattleOutcome *outcome) {
	if (player_of(battle)->hp <= 0) {
		*outcome = BATTLE_DEFEAT;
		return true;
	}
	if (monster_of(battle)->hp <= 0) {
		*outcome = BATTLE_VICTORY;
		return true;
	}
	return false;
}

BattleOutcome battle_play_turn(Battle *battle, const Command *command, EventList *events) {
	BattleOutcome outcome;
	player_action(battle, command, events);
	for (;;) {
		if (death_check(battle, &outcome)) {
			return outcome;
		}
		monster_phase(battle, events);
		if (death_check(battle, &outcome)) {
			return outcome;
		}
		if (end_of_turn(battle, events)) {
			return BATTLE_DEFEAT;
		}
		// A stunned player loses the next action: the monster simply acts again.
		if (!consume_stun(&player_of(battle)->statuses)) {
			return BATTLE_ONGOING;
		}
		player_of(battle)->stun_cooldown = STUN_COOLDOWN_TURNS;
		events_push(events, event_new("player_stunned"));
	}
}
