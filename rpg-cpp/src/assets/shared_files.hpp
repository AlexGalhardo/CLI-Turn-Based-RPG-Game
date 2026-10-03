#pragma once

#include <map>
#include <string>
#include <string_view>

namespace rpg::assets {

// A read-only file tree: relative path ("data/monsters.json", "art/families/orc.txt") → content.
// std::less<> enables lookups by std::string_view without building a temporary std::string.
using SharedFs = std::map<std::string, std::string_view, std::less<>>;

// The shared/ tree compiled into the binary by cmake/embed_shared.cmake (counterpart of Go's go:embed).
const SharedFs& embedded_shared();

} // namespace rpg::assets
