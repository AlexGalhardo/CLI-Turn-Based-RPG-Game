import { describe, expect, test } from "bun:test";
import { parseReport, winRates } from "./balance";

const REPORT = `vocation  difficulty  runs  wins  win %  min  p10  median  p90  max  avg lvl  top killers
--------  ----------  ----  ----  -----  ---  ---  ------  ---  ---  -------  -----------
warrior   easy        4     2     50%    81   81   100     100  100  54       Ferumbras (1)
archer    easy        4     4     100%   100  100  100     100  100  64
`;

describe("balance gate", () => {
	test("reads the simulator report rows", () => {
		expect(parseReport(REPORT)).toEqual([
			{ vocation: "warrior", difficulty: "easy", runs: 4, wins: 2 },
			{ vocation: "archer", difficulty: "easy", runs: 4, wins: 4 },
		]);
	});

	test("averages the per-vocation win rates across chunks", () => {
		const rows = [...parseReport(REPORT), { vocation: "warrior", difficulty: "easy", runs: 4, wins: 4 }];
		// warrior 6/8 = 75%, archer 4/4 = 100% → mean 87.5%
		expect(winRates(rows).get("easy")).toBe(87.5);
	});
});
