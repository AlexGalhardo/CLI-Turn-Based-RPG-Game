#include "application/loot.hpp"

#include <algorithm>
#include <set>

#include "domain/formulas.hpp"

namespace rpg::application {

bool can_use(const domain::ItemDef& item, const domain::VocationDef& vocation) {
	if (item.slot == domain::slot::weapon) {
		return std::ranges::contains(vocation.weapon_types, item.type);
	}
	if (item.slot == domain::slot::shield) {
		return std::ranges::contains(vocation.shield_types, item.type);
	}
	return true;
}

std::vector<std::int64_t> rarity_weights(
    const domain::GameData& data, const std::string& table, const domain::DifficultyDef& difficulty) {
	const auto& weights = data.balance.rarity_weights.at(table);
	std::vector<std::int64_t> result;
	result.reserve(data.balance.rarities.size());
	for (const domain::RarityDef& rarity : data.balance.rarities) {
		const auto found = weights.find(rarity.id);
		std::int64_t weight = found == weights.end() ? 0 : found->second;
		if (rarity.id != "common") {
			weight = domain::pct(weight, difficulty.non_common_weight_pct);
		}
		result.push_back(weight);
	}
	return result;
}

std::optional<domain::ItemInstance> generate_item(
    const domain::GameData& data, domain::Rng& rng, const ItemRequest& request) {
	const std::int64_t lowest_tier = std::max<std::int64_t>(0, request.tier - 1);
	std::vector<const domain::ItemDef*> candidates;
	for (const domain::ItemDef& item : data.items) {
		if (lowest_tier <= item.tier && item.tier <= request.tier && can_use(item, *request.vocation)) {
			candidates.push_back(&item);
		}
	}
	if (candidates.empty()) {
		return std::nullopt;
	}

	std::ranges::sort(candidates, {}, &domain::ItemDef::id);
	const domain::ItemDef& base = *rng.pick(candidates);
	const domain::RarityDef& rarity =
	    data.balance.rarities[rng.weighted(rarity_weights(data, request.table, *request.difficulty))];
	const std::int64_t affix_count = rng.roll(rarity.affix_min, rarity.affix_max);

	std::vector<domain::AffixRoll> rolls;
	std::set<domain::Stat, std::less<>> used_stats;
	for (std::int64_t i = 0; i < affix_count; ++i) {
		std::vector<const domain::AffixDef*> pool;
		for (const domain::AffixDef& affix : data.affixes) {
			if (std::ranges::contains(affix.slots, base.slot) && !used_stats.contains(affix.stat)) {
				pool.push_back(&affix);
			}
		}
		if (pool.empty()) {
			break;
		}
		std::ranges::sort(pool, {}, &domain::AffixDef::id);
		const domain::AffixDef& affix = *rng.pick(pool);
		used_stats.insert(affix.stat);
		rolls.push_back(domain::AffixRoll{affix.stat, rng.roll(affix.min, affix.max) + request.tier * affix.per_tier});
	}

	return domain::ItemInstance{
	    .uid = request.uid, .item_id = base.id, .rarity = rarity.id, .tier = request.tier, .affixes = std::move(rolls)};
}

} // namespace rpg::application
