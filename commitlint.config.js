module.exports = {
	extends: ["@commitlint/config-conventional"],
	rules: {
		"scope-enum": [
			2,
			"always",
			["python", "typescript", "golang", "shared", "docs", "ci", "setups", "deps", "release", "repo"],
		],
	},
};
