module.exports = {
	extends: ["@commitlint/config-conventional"],
	rules: {
		"scope-enum": [
			2,
			"always",
			[
				"python",
				"typescript",
				"golang",
				"rust",
				"elixir",
				"cpp",
				"c",
				"asm",
				"shared",
				"docs",
				"ci",
				"setups",
				"deps",
				"release",
				"repo",
			],
		],
	},
};
