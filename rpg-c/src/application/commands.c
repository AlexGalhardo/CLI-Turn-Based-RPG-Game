#include "application/commands.h"

#include <string.h>

static const char *const TYPE_NAMES[] = {
    [CMD_ATTACK] = "attack",
    [CMD_CAST] = "cast",
    [CMD_USE_POTION] = "potion",
    [CMD_DEFEND] = "defend",
    [CMD_NEXT_FIGHT] = "next_fight",
    [CMD_BUY_POTION] = "buy_potion",
    [CMD_SELL_ITEM] = "sell_item",
    [CMD_EQUIP] = "equip",
    [CMD_UNEQUIP] = "unequip",
    [CMD_BUY_STOCK_ITEM] = "buy_stock_item",
    [CMD_END_RUN] = "end_run",
    [CMD_CONTINUE_RUN] = "continue_run",
};

static Command command_of(CommandType type) {
	Command command;
	memset(&command, 0, sizeof(command));
	command.type = type;
	return command;
}

static Command command_with_id(CommandType type, const char *id, int64_t number) {
	Command command = command_of(type);
	id_set(command.id, id);
	command.number = number;
	return command;
}

static Command command_with_number(CommandType type, int64_t number) {
	Command command = command_of(type);
	command.number = number;
	return command;
}

Command cmd_attack(void) { return command_of(CMD_ATTACK); }
Command cmd_cast(const char *spell_id) { return command_with_id(CMD_CAST, spell_id, 0); }
Command cmd_use_potion(const char *potion_id) { return command_with_id(CMD_USE_POTION, potion_id, 0); }
Command cmd_defend(void) { return command_of(CMD_DEFEND); }
Command cmd_next_fight(void) { return command_of(CMD_NEXT_FIGHT); }
Command cmd_buy_potion(const char *potion_id, int64_t quantity) {
	return command_with_id(CMD_BUY_POTION, potion_id, quantity);
}
Command cmd_sell_item(int64_t uid) { return command_with_number(CMD_SELL_ITEM, uid); }
Command cmd_equip(int64_t uid) { return command_with_number(CMD_EQUIP, uid); }
Command cmd_unequip(Slot slot) {
	Command command = command_of(CMD_UNEQUIP);
	command.slot = slot;
	return command;
}
Command cmd_buy_stock_item(int64_t index) { return command_with_number(CMD_BUY_STOCK_ITEM, index); }
Command cmd_end_run(void) { return command_of(CMD_END_RUN); }
Command cmd_continue_run(void) { return command_of(CMD_CONTINUE_RUN); }

bool command_equal(const Command *a, const Command *b) {
	return a->type == b->type && strcmp(a->id, b->id) == 0 && a->number == b->number && a->slot == b->slot;
}

JsonValue *command_to_json(const Command *command) {
	JsonValue *object = json_object();
	json_set(object, "type", json_string(TYPE_NAMES[command->type]));
	switch (command->type) {
	case CMD_CAST:
		json_set(object, "spellId", json_string(command->id));
		break;
	case CMD_USE_POTION:
		json_set(object, "potionId", json_string(command->id));
		break;
	case CMD_BUY_POTION:
		json_set(object, "potionId", json_string(command->id));
		json_set(object, "quantity", json_int(command->number));
		break;
	case CMD_SELL_ITEM:
	case CMD_EQUIP:
		json_set(object, "uid", json_int(command->number));
		break;
	case CMD_UNEQUIP:
		json_set(object, "slot", json_string(slot_name(command->slot)));
		break;
	case CMD_BUY_STOCK_ITEM:
		json_set(object, "index", json_int(command->number));
		break;
	case CMD_ATTACK:
	case CMD_DEFEND:
	case CMD_NEXT_FIGHT:
	case CMD_END_RUN:
	case CMD_CONTINUE_RUN:
		break;
	}
	return object;
}

bool command_from_json(const JsonValue *raw, Command *out, JsonError *error) {
	const char *type = json_read_str(raw, "type", error);
	if (error->failed) {
		return false;
	}
	int found = -1;
	for (size_t i = 0; i < ARRAY_LEN(TYPE_NAMES); i++) {
		if (strcmp(TYPE_NAMES[i], type) == 0) {
			found = (int)i;
		}
	}
	if (found < 0) {
		json_error_set(error, "unknown command type: %.40s", type);
		return false;
	}
	*out = command_of((CommandType)found);
	switch (out->type) {
	case CMD_CAST:
		json_read_text(raw, "spellId", out->id, ID_SIZE, error);
		break;
	case CMD_USE_POTION:
		json_read_text(raw, "potionId", out->id, ID_SIZE, error);
		break;
	case CMD_BUY_POTION:
		json_read_text(raw, "potionId", out->id, ID_SIZE, error);
		out->number = json_read_int(raw, "quantity", error);
		break;
	case CMD_SELL_ITEM:
	case CMD_EQUIP:
		out->number = json_read_int(raw, "uid", error);
		break;
	case CMD_UNEQUIP: {
		const char *slot = json_read_str(raw, "slot", error);
		if (!error->failed && !slot_parse(slot, &out->slot)) {
			json_error_set(error, "unknown slot: %.40s", slot);
		}
		break;
	}
	case CMD_BUY_STOCK_ITEM:
		out->number = json_read_int(raw, "index", error);
		break;
	case CMD_ATTACK:
	case CMD_DEFEND:
	case CMD_NEXT_FIGHT:
	case CMD_END_RUN:
	case CMD_CONTINUE_RUN:
		break;
	}
	return !error->failed;
}
