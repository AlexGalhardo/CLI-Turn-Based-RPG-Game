#pragma once

#include <nlohmann/json.hpp>

namespace rpg::infrastructure {

// Upgrades older save/settings/history/profile documents to the current schema (docs/persistence.md).
//
// Each migration takes the raw JSON of version N and returns version N + 1, so the application layer only ever reads
// the current format. Version 1 → 2 is the 1.4.0 "ARPG update". The documents are modified in place and returned.

nlohmann::json& migrate_save(nlohmann::json& document);
nlohmann::json& migrate_history(nlohmann::json& document);
nlohmann::json& migrate_profile(nlohmann::json& document);
nlohmann::json& migrate_settings(nlohmann::json& document);

} // namespace rpg::infrastructure
