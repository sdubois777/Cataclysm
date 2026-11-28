"""Write the ruled sentences of the War trees work (2026-11-27) into the War tree files, by node id.

    python tools/apply_war_trees_sentences_2026_11_27.py
    python tools/apply_war_trees_sentences_2026_11_27.py --check

THE WAR RULINGS (Q20), accepted by the project owner on 2026-11-24 ("I'll go with your recommendations on the war
rulings") and recorded word for word in `docs/DECISIONS.md` ("The War trees' rulings pass (Q20)"). That entry says the
node sentences in the tree files are NOT changed there: "the slice that writes each node's rows rewrites its sentence
by node name, with the rows". This script is that rewrite for the War trees work of 2026-11-27, and it grows with each
slice that writes a ruled node's rows.

  - R8, Hemorrhage (`Bulwark_keystone_tw_t1`), with its row in `tools/reapply_war_trees_data_rows_workbook.py`.
  - R9, Blood Price (`Bulwark_basic_tw_t1_002`), the same.

IDEMPOTENT AND REFUSING: a sentence already holding the ruled text is left alone; one holding the sentence the ruling
was made against is written; one holding anything else is refused and no file is written. Every sentence is checked
before any is written. The files keep their byte-for-byte serialisation: only the sentence's JSON string is replaced.
`--check` writes nothing and exits 1 when a sentence would be written; exit 2 is a refusal.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DOCS = REPO_ROOT / "docs"


@dataclass(frozen=True)
class Sentence:
    ruling: str
    tree: str
    node: str
    before: str
    after: str


SENTENCES: tuple[Sentence, ...] = (
    Sentence("R8", "Bulwark", "keystone_tw_t1",
             "Your retaliation damage no longer deals instant damage. Instead it is converted into a bleed whose total "
             "damage equals the amount that would have been reflected.",
             "Your retaliation also applies Bleed to the attacker, at twice its normal strength."),
    Sentence("R9", "Bulwark", "basic_tw_t1_002",
             "+5% bleed damage per point for each bleed stack on the target.",
             "+5% bleed damage per point."),
    # ENGINE SLICE E1'S THREE (2026-11-27): two War rulings, and the coordinating session's reading of Outnumbered's
    # "all nearby enemies" as every enemy within 4 metres, the radius R7 gave Vengeful Discharge.
    Sentence("R10", "Bulwark", "basic_tw_t1_003",
             "+3% retaliation damage per point for each bleed stack on any nearby enemy. At 5 points: Enemies with 5 or "
             "more bleed stacks take 20% increased damage from all sources.",
             "+3% retaliation damage per point for each bleeding enemy within 4 metres, up to 5 enemies. At 5 points: "
             "your hits deal 20% increased damage to bleeding enemies."),
    Sentence("R13", "Berserker", "leech_003",
             "+1% life leech per point on critical melee strikes only. Critical strike leech is not subject to the "
             "normal leech cap. Excess leech beyond your HP cap is stored and added as bonus Fervour generation on your "
             "next hit, up to 10 Fervour per point.",
             "+1% life leech per point on critical melee strikes. While your health is full, your critical melee "
             "strikes generate 1 additional Fervour per point."),
    Sentence("Outnumbered (coordinator, 2026-11-27)", "Bulwark", "basic_tw_stem_004",
             "+2% retaliation damage per point for each unique enemy that has hit you in the last 10 seconds. While at "
             "maximum Fervour, your retaliation damage hits all nearby enemies rather than just your attacker.",
             "+2% retaliation damage per point for each unique enemy that has hit you in the last 10 seconds. While at "
             "maximum Fervour, your retaliation damage hits every enemy within 4 metres rather than just your "
             "attacker."),
    # ENGINE SLICE E2'S (2026-11-27): three War rulings, and three of the coordinating session's readings.
    Sentence("R5", "Bulwark", "keystone_jg_t2",
             "Once every 10 seconds, your next attack deals bonus damage equal to 5% of your Maximum HP as flat damage. "
             "Requires 20,000 Maximum HP to activate.",
             "Once every 10 seconds, your next attack deals bonus damage equal to 5% of your Maximum HP."),
    Sentence("R12", "Berserker", "dw_003",
             "Dual wield only. Alternating main hand and offhand attacks increases damage by 2% per point up to 6 stacks. "
             "While in Frenzy, each alternating strike costs 3 Fervour but the damage bonus per stack is doubled.",
             "Dual wield only. Using a different melee skill from the one you used last increases your damage by 2% per "
             "point, stacking up to 6 times. Using the same melee skill twice in a row removes the stacks. While in "
             "Frenzy, each stack you gain costs 3 Fervour and the damage per stack is doubled."),
    Sentence("R3", "Bulwark", "basic_jg_stem_004",
             "+200 flat HP per point for every 10,000 Maximum HP you have. Requires 10,000 Maximum HP to activate.",
             "+10 flat Maximum HP per point for every 1,000 Maximum HP you have from other sources."),
    Sentence("R16", "Bulwark", "keystone_tw_t2",
             "Convert all of your Armor into flat HP. For every 1% Armor lost this way, gain 3% Maximum HP. You can no "
             "longer equip shields. Gain 50% increased Fervour from all sources.",
             "While you do not hold a shield: your Armor is 0, you gain 1% increased Maximum HP for every 50 Armor you "
             "would have had, and you gain 50% increased Fervour from all sources."),
    Sentence("R27", "Bulwark", "basic_jg_t1_003",
             "+3% damage reduction per point while crowd controlled (multiplicative). At 4 points: Taking a crowd "
             "control effect retaliates with 500 flat damage.",
             "+3% damage reduction per point while crowd controlled (multiplicative). At 4 points: each crowd control "
             "effect applied to you deals damage equal to 3% of your Maximum HP to the enemy that applied it."),
    Sentence("R4", "Bulwark", "keystone_jg_t1",
             "While above 15,000 Maximum HP, you are immune to all crowd control effects. Enemies that attempt to crowd "
             "control you instead take 500 flat damage.",
             "While above 50% health, you are immune to crowd control. Each crowd control effect you are immune to this "
             "way deals damage equal to 3% of your Maximum HP to the enemy that applied it."),
    Sentence("Fortified Mind (coordinator, 2026-11-27)", "Bulwark", "basic_ic_t3_001",
             "+3% Fervour generation from blocking per point. At 5 points: While at maximum Fervour, blocking deals 200 "
             "flat damage back to the attacker.",
             "+3% Fervour generation from blocking per point. At 5 points: While at maximum Fervour, blocking deals "
             "damage equal to 3% of your Maximum HP back to the attacker."),
    Sentence("Shield Slam (coordinator, 2026-11-27)", "Bulwark", "basic_ic_t2_003",
             "+2% damage per point for every 10% Block Chance you have. At 6 points: Whenever you take a hit while your "
             "block streak is active, your next attack deals bonus flat damage equal to 5% of your Armor value.",
             "+2% damage per point for every 10% Block Chance you have. At 6 points: after you block, your next attack "
             "deals bonus damage equal to 5% of your Armor."),
    Sentence("R11", "Berserker", "dw_002",
             "Dual wield only. Every 5th consecutive melee attack triggers an additional offhand strike dealing 50% weapon damage per point.",
             "Dual wield only. Every 5th consecutive melee attack deals 50% more damage per point."),
    Sentence("Berserker's Rage (owner, 2026-11-27)", "Berserker", "twoh_ks_002",
             "Two-handed weapons only. Effects that trigger at maximum Fervour no longer drain your Fervour when they fire.",
             "Two-handed weapons only. Berserking spends half your Fervour instead of all of it."),
    Sentence("Unstoppable Force (owner, 2026-11-27)", "Berserker", "dw_ks_002",
             "Dual wield only. Killing an enemy while in Frenzy immediately resets the Frenetic auto-strike timer. "
             "Additionally, entering Frenzy instantly triggers three bonus strikes at full damage.",
             "Dual wield only. Killing an enemy while in Frenzy triggers one Frenetic strike. Additionally, entering "
             "Frenzy strikes the nearest enemy three times at full damage."),
    Sentence("Killing Blow (coordinator, 2026-11-27)", "Berserker", "twoh_015",
             "Two-handed weapons only. +6% critical strike multiplier per point on melee attacks. At 10 points: when you attack at maximum Fervour, all enemies below 25% HP are instantly killed.",
             "Two-handed weapons only. +6% critical strike multiplier per point on melee attacks. At 10 points: when you attack at maximum Fervour, all enemies within 8 metres below 25% HP are instantly killed."),
    Sentence("Momentum (coordinator, 2026-11-27)", "Berserker", "twoh_002",
             "Two-handed weapons only. Each consecutive melee hit on the same target increases your damage by 3% per "
             "point up to 5 consecutive hits. While in Wrath, this bonus is gained twice as fast. Switching targets "
             "resets the bonus.",
             "Two-handed weapons only. Each consecutive melee hit on the same target increases your damage by 3% per "
             "point up to 5 consecutive hits. While in Wrath, this bonus is doubled. Switching targets resets the "
             "bonus."),
    Sentence("Unrelenting (coordinator, 2026-11-27)", "Berserker", "dw_015",
             "Dual wield only. Killing an enemy refunds 20% of your maximum Fervour per point up to a maximum of 60%.",
             "Dual wield only. Killing an enemy refunds 10% of your maximum Fervour per point."),
    Sentence("Unbreakable Will (coordinator, 2026-11-27)", "Bulwark", "basic_ic_t1_003",
             "+2% of blocked damage is added to your next attack per point. At 5 points: This bonus is tripled if you "
             "block 5 times without attacking.",
             "+2% of blocked damage is added to your next attack per point. At 5 points: blocked damage is added at "
             "three times the rate."),
    Sentence("Carnage (coordinator, 2026-11-27)", "Berserker", "dw_017",
             "Dual wield only. Killing an enemy while in Frenzy extends the current Frenzy duration by 1 second per "
             "point, up to a maximum extension of 6 seconds. This can trigger multiple times per Frenzy.",
             "Dual wield only. Killing an enemy while in Frenzy restores Fervour equal to 1 second of Frenzy's drain "
             "per point. This can happen at most once every 1 second."),
    Sentence("Spreading Thorns (coordinator, 2026-11-27)", "Bulwark", "basic_tw_t3_002",
             "+1 meter to retaliation aura radius per point. At 4 points: Enemies killed within the aura explode, "
             "dealing retaliation damage to nearby enemies.",
             "+1 meter to retaliation aura radius per point. At 4 points: Enemies killed within the aura explode, "
             "dealing 100% of your retaliation damage to every enemy within 4 meters."),
    Sentence("Phantom Blades (coordinator, 2026-11-27)", "Berserker", "dw_ks_003",
             "Dual wield only. Each melee strike creates a phantasmal echo that replays the attack 0.75 seconds later "
             "at 60% damage. Phantom strikes can trigger leech and critical strikes independently but do not generate "
             "Fervour.",
             "Dual wield only. Each melee hit creates a phantasmal echo that strikes the same enemy 0.75 seconds later "
             "for 60% of the attack's damage. Echoes can trigger leech and critical strikes independently, do not "
             "generate Fervour and create no echoes."),
    Sentence("Blood for Blood (coordinator, 2026-11-27)", "Bulwark", "keystone_tw_final",
             "50% of all damage you take is reflected back to the attacker. This reflection ignores resistances and "
             "cannot be blocked or evaded. The reflection percentage increases by 1% for every 5 Fervour currently "
             "held, up to a maximum of 100% at full Fervour.",
             "50% of all damage you take is reflected back to the attacker. This reflection ignores resistances and "
             "cannot be blocked or evaded. The reflection percentage increases by 1% for every 2% of your maximum "
             "Fervour currently held, up to 100% at full Fervour."),
    Sentence("Spiteful (coordinator, 2026-11-27)", "Bulwark", "basic_tw_stem_003",
             "+3% Fervour generated from taking hits per point. At 5 points: When you reach maximum Fervour from taking hits, your next retaliation deals 50% bonus damage.",
             "+3% Fervour generated from taking hits per point. At 5 points: when your Fervour becomes full, your next "
             "retaliation deals 50% more damage."),
    Sentence("The Inevitable (coordinator, 2026-11-27)", "Bulwark", "keystone_jg_t3",
             "After 10 seconds in continuous combat, you emit an aura that deals flat damage per second to all nearby "
             "enemies equal to 1% of your Maximum HP. This damage increases by 0.5% of your Maximum HP for every "
             "additional 10 seconds in combat, up to a maximum of 3% of your Maximum HP per second.",
             "After 10 seconds in continuous combat, you emit an aura that deals flat damage per second to all enemies "
             "within 4 metres equal to 1% of your Maximum HP. This damage increases by 0.5% of your Maximum HP for "
             "every additional 10 seconds in combat, up to a maximum of 3% of your Maximum HP per second."),
)


class SentenceError(Exception):
    """A sentence holds neither the text the ruling was made against nor the ruled text."""


def apply(folder: pathlib.Path = DOCS, check_only: bool = False) -> list[str]:
    """Write every pending sentence; return the report. The last line says whether anything was or would be."""
    report: list[str] = []
    pending: dict[pathlib.Path, str] = {}
    for each in SENTENCES:
        path = folder / f"{each.tree}_Class_Tree_Final.json"
        text = pending.get(path) or path.read_bytes().decode("utf-8")
        found = [node for node in json.loads(text)["nodes"] if node.get("id") == each.node]
        if len(found) != 1:
            raise SentenceError(f"{each.ruling}: the {each.tree} tree has {len(found)} nodes with the id "
                                f"{each.node!r}. Nothing was written")
        current = found[0]["data"]["description"]
        where = f"{each.ruling} {each.tree} {each.node}"
        if current == each.after:
            report.append(f"{where}: already written; left alone")
            continue
        if current != each.before:
            raise SentenceError(f"{where} says {current!r}, which is neither the sentence the ruling was made "
                                f"against nor the ruled one; somebody has written it since. Nothing was written")
        old, new = json.dumps(each.before)[1:-1], json.dumps(each.after)[1:-1]
        if text.count(old) != 1:
            raise SentenceError(f"{where}: the old sentence appears {text.count(old)} times in {path.name}. "
                                f"Nothing was written")
        pending[path] = text.replace(old, new)
        report.append(f"{where}: {'to write' if check_only else 'written'}")
    if not check_only:
        for path, text in pending.items():
            path.write_bytes(text.encode("utf-8"))
    if not pending:
        report.append("every sentence is already written; nothing written")
    elif check_only:
        report.append(f"{len(report)} sentence(s) checked; something would be written; nothing written (--check)")
    else:
        report.append("written")
    return report


def take_out(folder: pathlib.Path) -> None:
    """Put the tree files in `folder` back as they stood before this script: each ruled sentence replaced by the one
    the ruling was made against. FOR TESTS, as `tools/reapply_war_trees_data_rows_workbook.py`'s `take_out` is. A file
    that does not hold the ruled sentence once is refused, and nothing is written."""
    pending: dict[pathlib.Path, str] = {}
    for each in SENTENCES:
        path = folder / f"{each.tree}_Class_Tree_Final.json"
        text = pending.get(path) or path.read_bytes().decode("utf-8")
        new, old = json.dumps(each.after)[1:-1], json.dumps(each.before)[1:-1]
        if text.count(new) != 1:
            raise SentenceError(f"{each.ruling}: {path.name} holds the ruled sentence {text.count(new)} times, so it "
                                f"cannot be taken out. Nothing was written")
        pending[path] = text.replace(new, old)
    for path, text in pending.items():
        path.write_bytes(text.encode("utf-8"))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if a sentence would be written")
    parser.add_argument("--docs", type=pathlib.Path, default=DOCS)
    args = parser.parse_args(argv)
    try:
        report = apply(args.docs, check_only=args.check)
    except SentenceError as error:
        print(f"REFUSED: {error}")
        return 2
    print("\n".join(report))
    return 1 if args.check and not report[-1].startswith("every sentence") else 0


if __name__ == "__main__":
    sys.exit(main())
