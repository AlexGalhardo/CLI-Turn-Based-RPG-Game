// Upgrades older save/settings/history/profile documents to the current schema (docs/persistence.md).
//
// Each migration edits the raw JSON of version N into version N + 1, so the application layer only ever reads the
// current format. Version 1 → 2 is the 1.4.0 "ARPG update". A malformed document is left for the typed readers to
// reject.
#ifndef RPG_INFRASTRUCTURE_MIGRATIONS_H
#define RPG_INFRASTRUCTURE_MIGRATIONS_H

#include "domain/json_types.h"

void migrate_save(JsonValue *document);
void migrate_history(JsonValue *document);
void migrate_profile(JsonValue *document);
void migrate_settings(JsonValue *document);

#endif
