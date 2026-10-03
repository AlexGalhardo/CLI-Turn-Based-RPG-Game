#include "application/ports.hpp"

namespace rpg::application {

// Out-of-line destructors anchor each vtable in this translation unit instead of every file that includes the header.
Clock::~Clock() = default;
SaveRepository::~SaveRepository() = default;
HistoryRepository::~HistoryRepository() = default;
ProfileRepository::~ProfileRepository() = default;

} // namespace rpg::application
