cmake_minimum_required(VERSION 3.28)

project(rpg_lua VERSION 1.9.0 DESCRIPTION "Endless turn-based RPG for the terminal (Lua port)" LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RPG_BUILD_TESTS "Build the test runner and register the tests" ON)

if(MINGW)
	# Self-contained .exe: no runtime DLLs needed next to the binary.
	add_link_options(-static)
endif()

# ── the Lua interpreter: the official source, pinned by version and checksum ────────────────────────────────────────
# The game is written in Lua; what ships is one binary, so the interpreter is compiled in instead of asking the
# player to install it.
include(FetchContent)
FetchContent_Declare(lua
	URL https://www.lua.org/ftp/lua-5.5.0.tar.gz
	URL_HASH SHA256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
	DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/src/*.c")
# lua.c and luac.c are the stand-alone interpreter and compiler: each has its own main().
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}/src")
if(UNIX)
	# popen, mkstemp and friends in the standard library (io.popen is used by the tests).
	target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)
	target_link_libraries(lua_static PUBLIC m)
endif()

# ── Lua modules and shared assets embedded in the binary ────────────────────────────────────────────────────────────
set(RPG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(RPG_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
file(GLOB_RECURSE RPG_EMBEDDED_FILES CONFIGURE_DEPENDS "${RPG_SOURCE_DIR}/rpg/*.lua"
	"${RPG_SHARED_DIR}/data/*.json" "${RPG_SHARED_DIR}/i18n/*.json" "${RPG_SHARED_DIR}/art/*.txt")
set(RPG_EMBEDDED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded.c")
add_custom_command(
	OUTPUT "${RPG_EMBEDDED_SOURCE}"
	COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${RPG_SOURCE_DIR}" "-DSHARED_DIR=${RPG_SHARED_DIR}"
		"-DOUTPUT=${RPG_EMBEDDED_SOURCE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake"
	DEPENDS ${RPG_EMBEDDED_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake"
	COMMENT "Embedding src/ (Lua modules) and shared/ (data, i18n, art)"
	VERBATIM
)

# ── the host: every warning is an error (the Lua sources above keep their own warning level) ────────────────────────
add_library(rpg_host STATIC host/rpg_host.c)
target_include_directories(rpg_host PUBLIC host)
target_link_libraries(rpg_host PUBLIC lua_static)
target_compile_options(rpg_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(NOT WIN32)
	# opendir, mkdir, termios, poll… are POSIX, not ISO C: ask the C library to declare them.
	target_compile_definitions(rpg_host PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif()

add_executable(rpg-lua host/main.c "${RPG_EMBEDDED_SOURCE}")
target_link_libraries(rpg-lua PRIVATE rpg_host)
target_compile_options(rpg-lua PRIVATE -Wall -Wextra -Wpedantic -Werror)

# ── tests: written in Lua (tests/), run by the same interpreter and host as the game ────────────────────────────────
if(RPG_BUILD_TESTS)
	include(CTest)
	add_executable(rpg-lua-script host/main.c host/no_embedded.c)
	target_compile_definitions(rpg-lua-script PRIVATE RPG_SCRIPT_RUNNER)
	target_link_libraries(rpg-lua-script PRIVATE rpg_host)
	target_compile_options(rpg-lua-script PRIVATE -Wall -Wextra -Wpedantic -Werror)
	foreach(level unit integration golden e2e)
		add_test(NAME ${level}
			COMMAND rpg-lua-script "${CMAKE_CURRENT_SOURCE_DIR}/tests/run.lua" ${level}/
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
		set_tests_properties(${level} PROPERTIES ENVIRONMENT "RPG_BINARY=$<TARGET_FILE:rpg-lua>;RPG_NO_ANIM=1")
	endforeach()
endif()
