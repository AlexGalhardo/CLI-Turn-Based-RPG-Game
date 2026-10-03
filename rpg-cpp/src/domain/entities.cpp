#include "domain/entities.hpp"

#include <algorithm>

namespace rpg::domain {

std::int64_t count_of(const CountMap& counts, std::string_view key) {
	const auto found = counts.find(key);
	return found == counts.end() ? 0 : found->second;
}

const MonsterAttack& MonsterInstance::attack(std::string_view attack_id) const {
	const auto found = std::ranges::find(attacks, attack_id, &MonsterAttack::id);
	if (found == attacks.end()) {
		throw UnknownIdError(attack_id);
	}
	return *found;
}

} // namespace rpg::domain
