// Player commands. Each maps 1:1 to a JSON object used by golden files (`{"type": "cast", "spellId": "…"}`).
// A Command is a tagged struct: `type` says which of the other fields are meaningful.
#ifndef RPG_APPLICATION_COMMANDS_H
#define RPG_APPLICATION_COMMANDS_H

#include "domain/enums.h"
#include "domain/json_types.h"

typedef enum {
	CMD_ATTACK,
	CMD_CAST,       // id = spell id
	CMD_USE_POTION, // id = potion id
	CMD_DEFEND,
	CMD_NEXT_FIGHT,
	CMD_BUY_POTION,     // id = potion id, number = quantity
	CMD_SELL_ITEM,      // number = uid
	CMD_EQUIP,          // number = uid
	CMD_UNEQUIP,        // slot
	CMD_BUY_STOCK_ITEM, // number = index
	CMD_END_RUN,        // victory phase: close the won run (history + Hall of Fame)
	CMD_CONTINUE_RUN,   // victory phase: keep playing endlessly after beating the final boss
} CommandType;

typedef struct {
	CommandType type;
	Id id;
	int64_t number;
	Slot slot;
} Command;

Command cmd_attack(void);
Command cmd_cast(const char *spell_id);
Command cmd_use_potion(const char *potion_id);
Command cmd_defend(void);
Command cmd_next_fight(void);
Command cmd_buy_potion(const char *potion_id, int64_t quantity);
Command cmd_sell_item(int64_t uid);
Command cmd_equip(int64_t uid);
Command cmd_unequip(Slot slot);
Command cmd_buy_stock_item(int64_t index);
Command cmd_end_run(void);
Command cmd_continue_run(void);

bool command_equal(const Command *a, const Command *b);
JsonValue *command_to_json(const Command *command);
// Returns false (with `error` set) for an unknown type or a malformed object.
bool command_from_json(const JsonValue *raw, Command *out, JsonError *error);

#endif
