cmake_minimum_required(VERSION 3.28)

project(rpg_cpp VERSION 1.5.0 DESCRIPTION "Endless turn-based RPG for the terminal (C++ port)" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules here: skip the dependency scan (it would also require clang-scan-deps next to Clang).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(RPG_BUILD_TESTS "Build the Catch2 test suite" ON)
option(RPG_COVERAGE "Instrument rpg_core and the tests with clang source-based coverage" OFF)

if(MINGW)
	# Self-contained .exe: no libc++/libunwind DLLs needed next to the binary.
	add_link_options(-static)
endif()

# ── third-party libraries (exact stable releases, hash-pinned) ─────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(
	nlohmann_json
	URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
	URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	SYSTEM
)
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_Declare(
	ftxui
	URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v7.0.3.tar.gz
	URL_HASH SHA256=e7c62ffe19009759821b4f0f8df7f2a6fb83784c3a9f1477d81f56d3ee723c88
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	SYSTEM
)
set(FTXUI_BUILD_DOCS OFF CACHE INTERNAL "")
set(FTXUI_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(FTXUI_BUILD_TESTS OFF CACHE INTERNAL "")
set(FTXUI_ENABLE_INSTALL OFF CACHE INTERNAL "")
set(FTXUI_QUIET ON CACHE INTERNAL "")

FetchContent_MakeAvailable(nlohmann_json ftxui)

# ── shared assets embedded in the binary (the C++ counterpart of Go's go:embed) ─────────────────────────────────────
set(RPG_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
file(GLOB_RECURSE RPG_SHARED_FILES CONFIGURE_DEPENDS
	"${RPG_SHARED_DIR}/data/*.json" "${RPG_SHARED_DIR}/i18n/*.json" "${RPG_SHARED_DIR}/art/*.txt")
set(RPG_EMBEDDED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/shared_files.cpp")
add_custom_command(
	OUTPUT "${RPG_EMBEDDED_SOURCE}"
	COMMAND "${CMAKE_COMMAND}" "-DSHARED_DIR=${RPG_SHARED_DIR}" "-DOUTPUT=${RPG_EMBEDDED_SOURCE}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shared.cmake"
	DEPENDS ${RPG_SHARED_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shared.cmake"
	COMMENT "Embedding shared/ (data, i18n, art)"
	VERBATIM
)

# ── warnings for our own code only ──────────────────────────────────────────────────────────────────────────────────
add_library(rpg_warnings INTERFACE)
if(MSVC)
	target_compile_options(rpg_warnings INTERFACE /W4 /WX)
else()
	target_compile_options(rpg_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(rpg_coverage INTERFACE)
if(RPG_COVERAGE)
	target_compile_options(rpg_coverage INTERFACE -fprofile-instr-generate -fcoverage-mapping)
	target_link_options(rpg_coverage INTERFACE -fprofile-instr-generate)
endif()

# ── core library: every layer except main.cpp ───────────────────────────────────────────────────────────────────────
add_library(rpg_core STATIC
	src/domain/rng.cpp
	src/domain/enums.cpp
	src/domain/definitions.cpp
	src/domain/formulas.cpp
	src/domain/entities.cpp
	src/domain/character.cpp
	src/application/events.cpp
	src/application/commands.cpp
	src/application/statistics.cpp
	src/application/run_state.cpp
	src/application/loot.cpp
	src/application/auto_equip.cpp
	src/application/spawner.cpp
	src/application/progression.cpp
	src/application/battle.cpp
	src/application/merchant.cpp
	src/application/engine.cpp
	src/application/bot.cpp
	src/application/auto_battle.cpp
	src/application/simulator.cpp
	src/application/save_game.cpp
	src/application/profile.cpp
	src/application/ports.cpp
	src/application/game_session.cpp
	src/infrastructure/data_loader.cpp
	src/infrastructure/paths.cpp
	src/infrastructure/i18n.cpp
	src/infrastructure/art.cpp
	src/infrastructure/migrations.cpp
	src/infrastructure/repositories.cpp
	src/presentation/event_text.cpp
	src/presentation/render.cpp
	src/presentation/controller.cpp
	src/presentation/controller_body.cpp
	src/presentation/cli.cpp
	src/presentation/simulator_report.cpp
	src/presentation/tui/app.cpp
	src/main_run.cpp
	"${RPG_EMBEDDED_SOURCE}"
)
target_include_directories(rpg_core PUBLIC src)
target_link_libraries(rpg_core
	PUBLIC nlohmann_json::nlohmann_json ftxui::component ftxui::dom ftxui::screen rpg_coverage
	PRIVATE rpg_warnings)

add_executable(rpg-cpp src/main.cpp)
target_link_libraries(rpg-cpp PRIVATE rpg_core rpg_warnings)

# ── tests ────────────────────────────────────────────────────────────────────────────────────────────────────────────
if(RPG_BUILD_TESTS)
	FetchContent_Declare(
		Catch2
		URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.0.tar.gz
		URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
		DOWNLOAD_EXTRACT_TIMESTAMP ON
		SYSTEM
	)
	FetchContent_MakeAvailable(Catch2)
	list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
	include(CTest)
	include(Catch)

	file(GLOB RPG_TEST_SOURCES CONFIGURE_DEPENDS tests/*/*.cpp)
	add_executable(rpg_tests ${RPG_TEST_SOURCES})
	target_include_directories(rpg_tests PRIVATE tests)
	target_link_libraries(rpg_tests PRIVATE rpg_core rpg_warnings Catch2::Catch2WithMain)
	target_compile_definitions(rpg_tests PRIVATE
		RPG_GOLDEN_DIR="${RPG_SHARED_DIR}/golden"
		RPG_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
	catch_discover_tests(rpg_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "RPG_NO_ANIM=1")
endif()
