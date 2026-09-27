/**
 * Static imports of shared/: `bun build --compile` embeds these files in the executable, so the binary needs no
 * repository checkout. JSON is parsed as untrusted input by the data loader.
 */

import bosses_demodras from "../../../shared/art/bosses/demodras.txt" with { type: "text" };
import bosses_dharalion from "../../../shared/art/bosses/dharalion.txt" with { type: "text" };
import bosses_ferumbras from "../../../shared/art/bosses/ferumbras.txt" with { type: "text" };
import bosses_ghazbaran from "../../../shared/art/bosses/ghazbaran.txt" with { type: "text" };
import bosses_morgaroth from "../../../shared/art/bosses/morgaroth.txt" with { type: "text" };
import bosses_munster from "../../../shared/art/bosses/munster.txt" with { type: "text" };
import bosses_orshabaal from "../../../shared/art/bosses/orshabaal.txt" with { type: "text" };
import bosses_the_horned_fox from "../../../shared/art/bosses/the_horned_fox.txt" with { type: "text" };
import bosses_the_old_widow from "../../../shared/art/bosses/the_old_widow.txt" with { type: "text" };
import bosses_zulazza_the_corruptor from "../../../shared/art/bosses/zulazza_the_corruptor.txt" with { type: "text" };
import families_aberration from "../../../shared/art/families/aberration.txt" with { type: "text" };
import families_aquatic from "../../../shared/art/families/aquatic.txt" with { type: "text" };
import families_beast from "../../../shared/art/families/beast.txt" with { type: "text" };
import families_construct from "../../../shared/art/families/construct.txt" with { type: "text" };
import families_demon from "../../../shared/art/families/demon.txt" with { type: "text" };
import families_dragon from "../../../shared/art/families/dragon.txt" with { type: "text" };
import families_elemental from "../../../shared/art/families/elemental.txt" with { type: "text" };
import families_giant from "../../../shared/art/families/giant.txt" with { type: "text" };
import families_humanoid from "../../../shared/art/families/humanoid.txt" with { type: "text" };
import families_insect from "../../../shared/art/families/insect.txt" with { type: "text" };
import families_mage from "../../../shared/art/families/mage.txt" with { type: "text" };
import families_orc from "../../../shared/art/families/orc.txt" with { type: "text" };
import families_plant from "../../../shared/art/families/plant.txt" with { type: "text" };
import families_reptile from "../../../shared/art/families/reptile.txt" with { type: "text" };
import families_rodent from "../../../shared/art/families/rodent.txt" with { type: "text" };
import families_undead from "../../../shared/art/families/undead.txt" with { type: "text" };
import achievements from "../../../shared/data/achievements.json";
import affixes from "../../../shared/data/affixes.json";
import balance from "../../../shared/data/balance.json";
import bosses from "../../../shared/data/bosses.json";
import families from "../../../shared/data/families.json";
import items from "../../../shared/data/items.json";
import monsters from "../../../shared/data/monsters.json";
import potions from "../../../shared/data/potions.json";
import spells from "../../../shared/data/spells.json";
import statuses from "../../../shared/data/statuses.json";
import vocations from "../../../shared/data/vocations.json";
import en from "../../../shared/i18n/en.json";
import ptBR from "../../../shared/i18n/pt-BR.json";

export interface SharedFiles {
	readonly data: Readonly<Record<string, unknown>>;
	readonly i18n: Readonly<Record<string, unknown>>;
}

export const EMBEDDED: SharedFiles = {
	data: { achievements, affixes, balance, bosses, families, items, monsters, potions, spells, statuses, vocations },
	i18n: { en, "pt-BR": ptBR },
};

/** Art files keyed by `<folder>/<name>`. A new family or boss needs a line here (see the add-game-content skill). */
export const EMBEDDED_ART: Readonly<Record<string, string>> = {
	"families/aberration": families_aberration,
	"families/aquatic": families_aquatic,
	"families/beast": families_beast,
	"families/construct": families_construct,
	"families/demon": families_demon,
	"families/dragon": families_dragon,
	"families/elemental": families_elemental,
	"families/giant": families_giant,
	"families/humanoid": families_humanoid,
	"families/insect": families_insect,
	"families/mage": families_mage,
	"families/orc": families_orc,
	"families/plant": families_plant,
	"families/reptile": families_reptile,
	"families/rodent": families_rodent,
	"families/undead": families_undead,
	"bosses/demodras": bosses_demodras,
	"bosses/dharalion": bosses_dharalion,
	"bosses/ferumbras": bosses_ferumbras,
	"bosses/ghazbaran": bosses_ghazbaran,
	"bosses/morgaroth": bosses_morgaroth,
	"bosses/munster": bosses_munster,
	"bosses/orshabaal": bosses_orshabaal,
	"bosses/the_horned_fox": bosses_the_horned_fox,
	"bosses/the_old_widow": bosses_the_old_widow,
	"bosses/zulazza_the_corruptor": bosses_zulazza_the_corruptor,
};
