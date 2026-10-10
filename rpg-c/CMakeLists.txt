cmake_minimum_required(VERSION 3.28)

project(rpg_c VERSION 1.8.0 DESCRIPTION "Endless turn-based RPG for the terminal (C port)" LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Plain ISO C plus the POSIX functions the infrastructure layer needs (declared through feature-test macros below).
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RPG_BUILD_TESTS "Build the test suite" ON)

if(MINGW)
	# Self-contained .exe: no runtime DLLs needed next to the binary.
	add_link_options(-static)
endif()

# ── shared assets embedded in the binary (the C counterpart of Go's go:embed) ───────────────────────────────────────
set(RPG_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
file(GLOB_RECURSE RPG_SHARED_FILES CONFIGURE_DEPENDS
	"${RPG_SHARED_DIR}/data/*.json" "${RPG_SHARED_DIR}/i18n/*.json" "${RPG_SHARED_DIR}/art/*.txt")
set(RPG_EMBEDDED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/shared_files.c")
add_custom_command(
	OUTPUT "${RPG_EMBEDDED_SOURCE}"
	COMMAND "${CMAKE_COMMAND}" "-DSHARED_DIR=${RPG_SHARED_DIR}" "-DOUTPUT=${RPG_EMBEDDED_SOURCE}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shared.cmake"
	DEPENDS ${RPG_SHARED_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shared.cmake"
	COMMENT "Embedding shared/ (data, i18n, art)"
	VERBATIM
)

# ── warnings: every warning is an error ─────────────────────────────────────────────────────────────────────────────
add_library(rpg_warnings INTERFACE)
target_compile_options(rpg_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
if(NOT WIN32)
	# opendir, mkdir, termios, nanosleep… are POSIX, not ISO C: ask the C library to declare them.
	target_compile_definitions(rpg_warnings INTERFACE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif()

# ── core library: every layer except main.c ─────────────────────────────────────────────────────────────────────────
file(GLOB RPG_CORE_SOURCES CONFIGURE_DEPENDS
	src/domain/*.c src/application/*.c src/infrastructure/*.c src/presentation/*.c src/presentation/tui/*.c)
add_library(rpg_core STATIC ${RPG_CORE_SOURCES} src/main_run.c "${RPG_EMBEDDED_SOURCE}")
target_include_directories(rpg_core PUBLIC src)
target_link_libraries(rpg_core PRIVATE rpg_warnings)

add_executable(rpg-c src/main.c)
target_link_libraries(rpg-c PRIVATE rpg_core rpg_warnings)

# ── tests: one executable with an in-tree harness, one ctest entry per level ────────────────────────────────────────
if(RPG_BUILD_TESTS)
	include(CTest)
	file(GLOB RPG_TEST_SOURCES CONFIGURE_DEPENDS tests/*/*.c)
	add_executable(rpg_tests ${RPG_TEST_SOURCES})
	target_include_directories(rpg_tests PRIVATE tests)
	target_link_libraries(rpg_tests PRIVATE rpg_core rpg_warnings)
	target_compile_definitions(rpg_tests PRIVATE
		RPG_GOLDEN_DIR="${RPG_SHARED_DIR}/golden"
		RPG_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
		RPG_BINARY_PATH="$<TARGET_FILE:rpg-c>")
	add_dependencies(rpg_tests rpg-c)
	foreach(level unit integration golden e2e)
		add_test(NAME ${level} COMMAND rpg_tests ${level}/)
	endforeach()
endif()
