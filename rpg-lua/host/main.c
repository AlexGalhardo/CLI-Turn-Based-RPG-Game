// The executable: a Lua interpreter with the game inside.
//
// It creates a Lua state, registers the `rpg_host` module and a searcher that makes `require` find the Lua sources
// compiled into the binary, then calls `rpg.main_run.run(arguments)` and exits with the code it returns.
//
// Built with RPG_SCRIPT_RUNNER it becomes `rpg-lua-script <file.lua> [arguments]`: the same interpreter and host
// module, but running a file from disk. The test suite uses it, so the tests exercise the real host functions.
#include "embedded.h"
#include "lauxlib.h"
#include "lualib.h"
#include "rpg_host.h"

#include <stdio.h>
#include <string.h>

// A `package.searchers` entry: require("rpg.domain.rng") loads the embedded source of that module.
static int embedded_searcher(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	for (size_t i = 0; i < EMBEDDED_MODULE_COUNT; i++) {
		const EmbeddedFile *module = &EMBEDDED_MODULES[i];
		if (strcmp(module->name, name) == 0) {
			// "=name" is the chunk name shown in error messages.
			lua_pushfstring(L, "=%s", name);
			if (luaL_loadbuffer(L, module->content, module->size, lua_tostring(L, -1)) != LUA_OK) {
				return lua_error(L);
			}
			lua_pushstring(L, name);
			return 2;
		}
	}
	lua_pushfstring(L, "no embedded module '%s'", name);
	return 1;
}

static void install_host(lua_State *L) {
	// package.preload.rpg_host = luaopen_rpg_host
	luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
	lua_pushcfunction(L, luaopen_rpg_host);
	lua_setfield(L, -2, "rpg_host");
	lua_pop(L, 1);

	// table.insert(package.searchers, 2, embedded_searcher): right after the preload searcher.
	lua_getglobal(L, "table");
	lua_getfield(L, -1, "insert");
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "searchers");
	lua_remove(L, -2);
	lua_pushinteger(L, 2);
	lua_pushcfunction(L, embedded_searcher);
	lua_call(L, 3, 0);
	lua_pop(L, 1);
}

// Message handler of lua_pcall. A typed error (a table with `message`, see domain/base.lua) is an expected failure
// and keeps only its message; anything else is a bug and gets the stack traceback.
static int traceback(lua_State *L) {
	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "message");
		if (lua_isstring(L, -1)) {
			return 1;
		}
		lua_settop(L, 1);
	}
	luaL_traceback(L, L, luaL_tolstring(L, 1, NULL), 1);
	return 1;
}

// Pushes the list of command-line arguments, `first` at index 1 (and the one before it at index 0).
static void push_arguments(lua_State *L, int argc, char **argv, int first) {
	lua_createtable(L, argc, 1);
	for (int i = first - 1; i < argc; i++) {
		lua_pushstring(L, argv[i]);
		lua_rawseti(L, -2, i - first + 1);
	}
}

int main(int argc, char **argv) {
	lua_State *L = luaL_newstate();
	if (L == NULL) {
		fputs("rpg: not enough memory\n", stderr);
		return 1;
	}
	luaL_openlibs(L);
	install_host(L);
	lua_pushcfunction(L, traceback);

	int results = 1;
	int status;
#ifdef RPG_SCRIPT_RUNNER
	if (argc < 2) {
		fputs("usage: rpg-lua-script <file.lua> [arguments]\n", stderr);
		return 2;
	}
	push_arguments(L, argc, argv, 2);
	lua_setglobal(L, "arg");
	status = luaL_loadfile(L, argv[1]);
	if (status == LUA_OK) {
		status = lua_pcall(L, 0, results, 1);
	}
#else
	lua_getglobal(L, "require");
	lua_pushliteral(L, "rpg.main_run");
	status = lua_pcall(L, 1, 1, 1);
	if (status == LUA_OK) {
		lua_getfield(L, -1, "run");
		push_arguments(L, argc, argv, 1);
		status = lua_pcall(L, 1, results, 1);
	}
#endif
	int code = 0;
	if (status != LUA_OK) {
		fprintf(stderr, "rpg: %s\n", lua_tostring(L, -1));
		code = 1;
	} else if (lua_isinteger(L, -1)) {
		code = (int)lua_tointeger(L, -1);
	}
	lua_close(L);
	return code;
}
