# C++ Implementation (`rpg-cpp/`)

Written from scratch following the Python reference: same layers, file names and behaviour. It passes every
`shared/golden` scenario, its bot issues exactly the commands recorded by the Python bot, its simulator prints the same
report byte for byte, and its run state serialises to the same JSON (saves are interchangeable with Python, TypeScript
and Go; a save written by Python is continued in a test and must end exactly like the Python continuation).

The folder is named `cpp` rather than `c++` because `+` is a metacharacter in GitHub Actions path filters.

## Stack

- C++23, CMake ≥ 3.28 + Ninja, `CMakePresets.json` (`debug`, `release`, `coverage`)
- Compilers: Clang 19+ or GCC 14+ (CI: ubuntu-latest); on Windows, LLVM-MinGW UCRT (`scoop install
  mingw-mstorsjo-llvm-ucrt`), linked statically so `rpg-cpp.exe` needs no DLLs
- Libraries through CMake `FetchContent`, pinned to exact stable releases with a SHA-256 hash:
  [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v7.0.3 (TUI), [nlohmann/json](https://github.com/nlohmann/json)
  v3.12.0 (JSON), [Catch2](https://github.com/catchorg/Catch2) v3.16.0 (tests). Nothing else.
- `-Wall -Wextra -Wpedantic -Werror` on our code (third-party code is included as `SYSTEM`), clang-format
  (`.clang-format`: LLVM base, tabs, 120 columns)
- Coverage: Clang source-based coverage (`-fprofile-instr-generate -fcoverage-mapping`, `llvm-profdata`, `llvm-cov`)

## Commands

```bash
cd rpg-cpp
cmake --preset debug                       # configure (downloads the pinned libraries once)
cmake --build --preset debug               # build rpg-cpp + rpg_tests (warnings are errors)
ctest --preset debug                       # every Catch2 test case as its own ctest test
./build/debug/rpg_tests "[golden]"         # one level: [unit] [integration] [golden] [e2e]
cmake --preset release && cmake --build --preset release
./build/release/rpg-cpp --seed 42          # play (rpg-cpp.exe on Windows)
./build/release/rpg-cpp --simulate 20 --seed 42
clang-format --dry-run --Werror $(find src tests -name '*.cpp' -o -name '*.hpp')

# Coverage (Clang only): ≥ 90% lines on src/domain + src/application
cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage
llvm-profdata merge -sparse build/coverage/profiles/*.profraw -o build/coverage/rpg.profdata
llvm-cov report build/coverage/rpg_tests -instr-profile=build/coverage/rpg.profdata src/domain src/application
```

On Windows (Git Bash) put LLVM-MinGW first on the `PATH` and pass the compiler to the first configure:
`export PATH="$HOME/scoop/apps/mingw-mstorsjo-llvm-ucrt/current/bin:$PATH"` and
`cmake --preset debug -DCMAKE_CXX_COMPILER=clang++`.

## Structure

```
rpg-cpp/
├── CMakeLists.txt / CMakePresets.json / .clang-format
├── cmake/embed_shared.cmake          # generates shared_files.cpp from ../shared (data, i18n, art)
├── src/
│   ├── main.cpp                      # collects argv and calls rpg::run()
│   ├── main_run.{hpp,cpp}            # run(): flags → simulator or TUI (testable, like Go's run())
│   ├── version.hpp
│   ├── assets/shared_files.hpp       # the embedded shared/ tree (the generated .cpp lives in build/)
│   ├── domain/                       # rng, enums, definitions, formulas, entities, character
│   ├── application/                  # engine, battle, merchant, loot, spawner, progression, statistics, run_state,
│   │                                 # save_game, profile, ports, game_session, bot, simulator, commands, events
│   ├── infrastructure/               # data_loader, i18n, art, repositories (+ SystemClock), paths
│   └── presentation/                 # cli, event_text, render, controller (+ controller_body), simulator_report
│       └── tui/app.{hpp,cpp}         # FTXUI renderer of the controller
└── tests/                            # one Catch2 executable (rpg_tests), registered in ctest
    ├── unit/ integration/ golden/ e2e/
    ├── support/                      # helpers (temp dirs, fake clock, fixtures) and the environment listener
    └── fixtures/                     # a save + profile written by the Python reference, and its continuation
```

Every layer is one static library, `rpg_core`, linked by the binary and by the tests. Each `.hpp` declares a module's
public interface and the matching `.cpp` holds the definitions.

## Notes (C++ for readers coming from Python, TypeScript or Go)

- **Value semantics.** `RunState`, `Player`, `ItemInstance`… are plain structs: assigning one copies it deeply.
  The merchant snapshot that Go and TypeScript build by serialising and parsing the state is just
  `snapshot_ = state();` here, and `operator== = default` makes whole states comparable in tests.
- **Header/source split.** Headers are what other files may use; sources are compiled once. Templates
  (`Rng::pick`) and tiny inline helpers live in headers because the compiler needs their bodies at every call.
- **FetchContent instead of a package manager.** CMake downloads the three libraries at configure time from
  hash-checked release tarballs (the same idea as `go.sum` or a lockfile). They are added as `SYSTEM` so their
  warnings never fail our `-Werror` build.
- **Embedding `shared/`.** C++ has no `go:embed`/`import json`; `cmake/embed_shared.cmake` turns every data, i18n
  and art file into a raw string literal (split in chunks at line ends, CRLF normalised) inside a generated
  `shared_files.cpp`. The binary is self-contained; golden files and fixtures are read by the tests at runtime
  through compile definitions (`RPG_GOLDEN_DIR`, `RPG_FIXTURES_DIR`).
- **Closed sets vs data ids.** `Phase` and `View` are `enum class`; elements, slots, stats and every content id are
  `std::string` because they come from JSON and go back to saves.
- **Commands are a `std::variant`** of small structs (`Attack`, `Cast`, `BuyPotion`…) dispatched with `std::visit`,
  the counterpart of the reference's dataclasses. **Events** are a type plus a `std::map` of
  `std::variant<int64_t, string, bool>` fields, compared structurally with the golden JSON.
- **Expected failures are values, bugs are exceptions.** An invalid run config, a CLI usage error or a simulator
  failure come back as `std::expected<T, std::string>`; player mistakes are `error` events; I/O problems and
  `NewerSchemaError` are exceptions that the controller catches and shows; an unknown data id is an
  `UnknownIdError` (a bug in the code or the data, which the data tests rule out).
- **Integer math and the PRNG.** All game math is `std::int64_t`; `pct()` rejects negative operands because C++
  division truncates towards zero. The PRNG uses `std::uint32_t`, whose arithmetic wraps exactly like `Math.imul`.
- **Ordered containers only.** `std::map` iterates in key order and `std::vector` keeps file order, so no game
  decision depends on hash order; `std::map` also writes JSON maps with sorted keys. Never read a counter with
  `operator[]`: it inserts a zero entry that would then appear in the save file (`count_of()` reads safely).
- **Ownership.** Game data and the embedded files are immutable and outlive everything, so the engine, battle and
  controller borrow them through `const` pointers; persistence ports are abstract classes shared through
  `std::shared_ptr`. No raw `new`/`delete` anywhere.
- **JSON key order.** Saves are built with `nlohmann::ordered_json` in the reference's key order and dumped with tab
  indentation, so a C++ save looks like a Python one.
- **E2E tests** drive the real FTXUI component with `Event`s and render it into a 100 × 30 `ftxui::Screen`, then read
  the cells as text — including a whole bot run played through the menus until the Game Over screen.
- **Tests** never touch the real saves: a Catch2 event listener points `RPG_DATA_DIR` at a temporary directory and
  sets `RPG_NO_ANIM=1`; every persistence test uses its own `TempDir`.
