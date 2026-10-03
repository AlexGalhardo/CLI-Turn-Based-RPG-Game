// The C++ implementation of the CLI Turn-Based RPG. Everything except argument collection lives in rpg::run().
#include <iostream>
#include <string_view>
#include <vector>

#include "main_run.hpp"

int main(int argc, char** argv) {
	const std::vector<std::string_view> args(argv + 1, argv + argc);
	return rpg::run(args, std::cout, std::cerr);
}
