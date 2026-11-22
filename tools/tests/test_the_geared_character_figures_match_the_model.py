"""The design document's figures for how long a geared character survives, recomputed from the model.

WHY THIS EXISTS. Issue #2818. `docs/Cataclysm_GDD_v2.md`, in the section "How Long a Geared Character Survives", gives
two tables -- what each of the reference character's four mitigation layers removes, and how many hits and seconds each
enemy needs to kill that character at tier 8 -- and pack figures drawn from the Imp's row. Nothing compared them with the
model. When the flat damage reduction affix moved on 2026-09-04 and enemy damage and attack intervals were retuned
afterwards, five of the seven enemy rows, the damage reduction layer, the four layers together and every pack figure
went stale, and nothing failed. The Brute's row still gave its attack interval from before 2026-08-07.

WHERE THE FIGURES COME FROM, so a reader can recompute any of them:

    the reference character        `reference_build.defender(8)`, `sim/cataclysm_sim/reference_build.py`
    each layer                     `damage.armor_reduction`, `effective_resistance`, `BLOCK_DAMAGE_REDUCTION` and
                                   `effective_damage_reduction`, `sim/cataclysm_sim/damage.py`
    all four together              one less `reference_build.damage_taken_fraction(8)`
    each enemy                     `enemy_stats.stats_on_floor(rarity, 8, "Cataclysm", kind=name)`, at the rarity it
                                   is normally met at: the seven `sim/tests/test_survivability.py` names
    its hits                       `damage.hits_to_kill` with one of its average hits, as that file computes them
    its seconds                    those hits, rounded up, times its attack interval
    a pack of n Imps               one Imp's seconds divided by n

WHAT IS ASSERTED HERE.

    the four-layer table, row by row, and that the four layers multiply to the model's figure, so the table is the
      whole of what a hit loses
    the enemy table names exactly the seven enemies, in order, and each row's hits and seconds are the model's
    the pack sentence under the table
    the two other places the document quotes the pack or the four layers: the Imp's reach, and the enemy ceiling

EACH NUMBER IS COMPARED TO HALF A UNIT IN ITS LAST PRINTED DIGIT, so the document may print 53.3 for 53.28 but not 53.4,
and an integer only exactly. A hit count is printed as an integer, so it must equal the model's.
"""

from __future__ import annotations

import math
import pathlib
import re

import pytest

from cataclysm_sim import damage as dmg
from cataclysm_sim import enemy_stats as es
from cataclysm_sim import reference_build as rb

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SECTION = "### **How Long a Geared Character Survives**"
TIER = 8

#: The enemies a player actually meets, at the rarity each is normally met at: `sim/tests/test_survivability.py`'s list.
MET_AT = (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"),
          ("Brute", "Elite"), ("Corrupted Sentinel", "Legendary"),
          ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))

NUMBER = r"([\d,]+(?:\.\d+)?)"


def flat(text: str) -> str:
    """The text with every run of whitespace made one space, so a sentence is found however it is wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    return GDD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def section(document: str) -> str:
    assert SECTION in document, f"the design document has no section {SECTION!r}"
    start = document.index(SECTION)
    end = document.find("\n### ", start + len(SECTION))
    return document[start:end if end != -1 else len(document)]


def table_after(section: str, header: str) -> list[list[str]]:
    """The body rows of the Markdown table whose header row begins `header`, each as its cells."""
    lines = section.splitlines()
    starts = [i for i, line in enumerate(lines) if line.startswith(header)]
    assert len(starts) == 1, f"expected one table headed {header!r}, found {len(starts)}"
    rows = []
    for line in lines[starts[0] + 2:]:
        if not line.startswith("|"):
            break
        rows.append([cell.strip() for cell in line.strip().strip("|").split("|")])
    return rows


def printed(text: str) -> tuple[float, float]:
    """A number as the document prints it, and half a unit in its last printed digit."""
    figure = text.replace(",", "")
    decimals = len(figure.split(".")[1]) if "." in figure else 0
    return float(figure), 0.5 * 10 ** -decimals + 1e-9


def agrees(text: str, model: float) -> bool:
    value, tolerance = printed(text)
    return abs(value - model) <= tolerance


def number_in(cell: str) -> str:
    found = re.search(NUMBER, cell.replace("*", ""))
    assert found, f"no number in {cell!r}"
    return found.group(1)


# --------------------------------------------------------------------------
# What the model says
# --------------------------------------------------------------------------

def layers() -> dict[str, float]:
    """What each of the reference character's four layers removes, in percent, and the character's own figures."""
    against = rb.defender(TIER)
    return {
        "armor points": against.armor,
        "armor": dmg.armor_reduction(against.armor, TIER),
        "resistance": dmg.effective_resistance(against.resistance_to("Demonic"), 0.0),
        "block chance": against.block_chance,
        "block": against.block_chance * dmg.BLOCK_DAMAGE_REDUCTION / 100.0,
        "damage reduction": dmg.effective_damage_reduction(against.damage_reduction),
        "all four": 100.0 * (1.0 - rb.damage_taken_fraction(TIER)),
    }


def enemy_row(name: str, rarity: str) -> tuple[int, float]:
    """Hits, rounded up, and seconds for one enemy to kill the reference character at tier 8."""
    e = es.stats_on_floor(rarity, TIER, "Cataclysm", kind=name)
    hits = dmg.hits_to_kill(dmg.Attacker(damage=e.average_damage_per_hit, damage_type=e.damage_type),
                            rb.defender(TIER))
    return math.ceil(hits), math.ceil(hits) * e.attack_interval


def one_imp_seconds() -> float:
    return enemy_row("Imp", "Common")[1]


# --------------------------------------------------------------------------
# The four layers
# --------------------------------------------------------------------------

def test_the_four_layers_multiply_to_what_a_hit_loses():
    """The table is the whole of it: the four layers, multiplied, are the model's figure. If a fifth layer were added
    to the model, or one stopped multiplying, the table would be missing something and this says so."""
    model = layers()
    landed = ((1 - model["armor"] / 100) * (1 - model["resistance"] / 100) * (1 - model["block"] / 100)
              * (1 - model["damage reduction"] / 100))
    assert landed == pytest.approx(rb.damage_taken_fraction(TIER), rel=1e-9), (
        f"the four layers leave {landed:.4%} of a hit and the model {rb.damage_taken_fraction(TIER):.4%}")


def test_the_four_layer_table_is_the_models(section):
    model = layers()
    rows = {row[0]: row[1] for row in table_after(section, "| Layer | What it removes |")}
    assert len(rows) == 5, f"the table has {len(rows)} rows: {list(rows)}"

    armor = [label for label in rows if label.startswith("Armor")]
    assert len(armor) == 1, f"no single armour row in {list(rows)}"
    stated = re.search(r"Armor, " + NUMBER + r" points against the tier (\d+) curve", armor[0])
    assert stated and stated.group(2) == str(TIER), f"the armour row reads {armor[0]!r}"
    assert agrees(stated.group(1), model["armor points"]), (
        f"the armour row says {stated.group(1)} points; the reference character has {model['armor points']:.2f}")
    assert agrees(number_in(rows[armor[0]]), model["armor"]), (
        f"the armour row says {rows[armor[0]]}; the model removes {model['armor']:.2f}%")

    assert agrees(number_in(rows["Resistance, at the cap"]), model["resistance"]), (
        f"the resistance row says {rows['Resistance, at the cap']}; the model removes {model['resistance']:.2f}%")

    block = [label for label in rows if label.startswith("Block chance")]
    assert len(block) == 1, f"no single block row in {list(rows)}"
    chance = re.search(r"Block chance " + NUMBER + r"%, removing half a hit each time", block[0])
    assert chance and agrees(chance.group(1), model["block chance"]), (
        f"the block row reads {block[0]!r}; the reference character blocks {model['block chance']}%")
    assert dmg.BLOCK_DAMAGE_REDUCTION == 50.0, (
        f"a block removes {dmg.BLOCK_DAMAGE_REDUCTION}% of a hit, and the row says half")
    assert agrees(number_in(rows[block[0]]), model["block"]), (
        f"the block row says {rows[block[0]]}; the model removes {model['block']:.2f}% on average")

    assert agrees(number_in(rows["Damage reduction"]), model["damage reduction"]), (
        f"the damage reduction row says {rows['Damage reduction']}; the model removes "
        f"{model['damage reduction']:.2f}%")

    assert agrees(number_in(rows["**All four together**"]), model["all four"]), (
        f"the table says the four together remove {rows['**All four together**']}; the model removes "
        f"{model['all four']:.2f}%")


# --------------------------------------------------------------------------
# The enemies
# --------------------------------------------------------------------------

def test_the_enemy_table_names_the_seven_enemies_a_player_meets(section):
    rows = table_after(section, "| Enemy at tier 8 |")
    assert [row[0] for row in rows] == [f"{rarity} {name}" for name, rarity in MET_AT], (
        f"the table names {[row[0] for row in rows]}")


@pytest.mark.parametrize("name,rarity", MET_AT, ids=[f"{r} {n}" for n, r in MET_AT])
def test_each_enemy_row_is_the_models(section, name, rarity):
    rows = {row[0]: row[1:] for row in table_after(section, "| Enemy at tier 8 |")}
    label = f"{rarity} {name}"
    assert label in rows, f"the table has no row for the {label}"
    hits, seconds = enemy_row(name, rarity)
    stated_hits, stated_seconds = rows[label]
    assert int(stated_hits) == hits, f"the {label} row says {stated_hits} hits; the model gives {hits}"
    assert agrees(stated_seconds, seconds), (
        f"the {label} row says {stated_seconds} seconds; the model gives {seconds:.2f}")


# --------------------------------------------------------------------------
# The pack figures, and the other places that quote this section
# --------------------------------------------------------------------------

def test_the_pack_sentence_under_the_table_is_the_models(section):
    stated = re.search(r"One Imp takes " + NUMBER + r" seconds to kill a geared character; ten take " + NUMBER
                       + r" seconds and twenty take " + NUMBER, flat(section))
    assert stated, "the section no longer says how long one, ten and twenty Imps take"
    one = one_imp_seconds()
    for text, pack in zip(stated.groups(), (1, 10, 20), strict=True):
        assert agrees(text, one / pack), (
            f"the section says {pack} Imp(s) take {text} s; the model gives {one / pack:.2f}")


def test_the_imps_reach_quotes_the_same_pack_figures(document):
    """The Imp's reach is set from these figures (the Imp subsection, "The Imp's reach is 1.32 metres"), so it quotes
    them, and a stale copy there was as wrong as the table."""
    text = flat(document)
    stated = re.search(r"ten Imps kill a geared character in " + NUMBER + r" seconds and twenty in " + NUMBER
                       + r"\. One rank is seven Imps, and seven take " + NUMBER + r" seconds", text)
    assert stated, "the Imp's reach no longer quotes how long ten, twenty and seven Imps take"
    one = one_imp_seconds()
    for text_figure, pack in zip(stated.groups(), (10, 20, 7), strict=True):
        assert agrees(text_figure, one / pack), (
            f"the Imp's reach says {pack} Imps take {text_figure} s; the model gives {one / pack:.2f}")

    pack = re.search(r"names ten as the pack that kills a geared character in " + NUMBER + r" seconds", text)
    assert pack and agrees(pack.group(1), one / 10), (
        f"the pack size's reason quotes {pack.group(1) if pack else 'nothing'} s for ten Imps; the model gives "
        f"{one / 10:.2f}")


def test_the_enemy_ceiling_quotes_what_the_player_stops(document):
    """The enemy ceiling is defined against this section's figure, and quotes it."""
    stated = re.search(r"How Long a Geared Character Survives\* stops " + NUMBER + r"% of what is thrown at them",
                       flat(document))
    assert stated, "the enemy ceiling no longer quotes what the reference character stops"
    model = layers()["all four"]
    assert agrees(stated.group(1), model), (
        f"the enemy ceiling quotes {stated.group(1)}%; the reference character stops {model:.2f}%")
    assert es.ENEMY_MITIGATION_CEILING < model, "and the ceiling must sit below it"
