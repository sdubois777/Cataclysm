"""Write the War vow options' ruled sentences into the class tree files.

    python tools/apply_war_vow_sentences_2026_11_25.py docs
    python tools/apply_war_vow_sentences_2026_11_25.py --check docs

THE WAR VOW OPTIONS' FIRST SLICE (2026-11-25), on rulings that are not yet in `docs/`:

  - R14, R17 and R18, three Final Oath options the project owner accepted on 2026-11-24 with every War ruling ("I'll
    go with your recommendations on the war rulings"): Eternal Hunger (Berserker), Colossus Eternal and Wrath Incarnate
    (Bulwark).
  - Two replacements the coordinating session ruled on the vow survey of 2026-11-25: Shield Oath's "50 HP" and
    Bloodied but Unbroken's "every 75 HP" were on the old health scale, so each became a share of Maximum HP.
  - The Saboteur's twelve options, approved on 2026-11-24, taken from the "Ready to paste" block of the draft at
    `C:/Projects/Cataclysm/.claude/worktrees/nice-carson-77a188/.claude/windows/saboteur_vows_draft.md`, which is not
    in git. The four oaths have held empty option lists since the passive order fixes of 2026-11-24.

THE UNBREAKABLE WALL IS NOT HERE: the coordinating session put it to the owner, since both of its clauses are already
true in the game.

AN OPTION IS FOUND BY ITS TREE, ITS CAPSTONE AND ITS NAME, and a sentence reading neither the one its ruling was made
against nor the new one is refused, as `tools/apply_passive_order_fixes_2026_11_24.py`'s `Reword` refuses. A Saboteur
oath already holding options other than the twelve is refused. A file already written is not rewritten, and a file
keeps its byte-for-byte serialisation. `--check` writes nothing and exits 1 when a file would change; exit 2 is a
refusal.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import apply_passive_order_fixes_2026_11_24 as order  # noqa: E402


@dataclasses.dataclass(frozen=True)
class OptionReword:
    """Replace one capstone option's sentence; `before` is the sentence the ruling was made against."""
    tree: str
    capstone: str
    option: str
    before: str
    after: str
    ruling: str


OPTION_REWORDS: tuple[OptionReword, ...] = (
    OptionReword(
        "Berserker", "Final Oath", "Eternal Hunger",
        "Your life leech cap is removed entirely. Any HP restored by life leech beyond your maximum is added as "
        "overleech that decays at 1% per second. Life leech effectiveness is increased by 50%.",
        "Life leech can heal you up to 100% above your Maximum HP. Health above your maximum decays at 1% of your "
        "Maximum HP per second while you are not leeching, and damage you take removes it first. Your life leech is "
        "increased by 50%.",
        "R14, accepted by the owner on 2026-11-24"),
    OptionReword(
        "Bulwark", "Final Oath", "Colossus Eternal",
        "All healing, regeneration, and HP bonuses are doubled. While above 50% HP, you are immune to instant-kill "
        "effects. While above 75% HP, your attacks deal bonus damage equal to 2% of your current HP.",
        "Your health regeneration and the healing you receive are doubled, and your increases to Maximum HP count "
        "twice. While above 50% health, a hit that would kill you leaves you at 1 HP instead. While above 75% health, "
        "your attacks deal bonus damage equal to 2% of your current HP.",
        "R17, accepted by the owner on 2026-11-24"),
    OptionReword(
        "Bulwark", "Final Oath", "Wrath Incarnate",
        "Your retaliation damage percentage is added to your total Armor, and your Armor contributes to your "
        "retaliation damage proportional to the damage reduction it provides. When you take a hit that would deal "
        "more than 10% of your Maximum HP in a single strike, you erupt \u2014 instantly retaliating against all "
        "nearby enemies for 200% of your retaliation damage.",
        "Your retaliation damage is increased by 1% for every 100 Armor you have. When a single hit takes more than "
        "10% of your Maximum HP, you retaliate against every enemy within 4 metres for 200% of your retaliation "
        "damage.",
        "R18, accepted by the owner on 2026-11-24"),
    OptionReword(
        "Bulwark", "First Oath", "Shield Oath",
        "Blocking an attack restores 50 HP.",
        "Blocking an attack restores 2% of your Maximum HP.",
        "the coordinating session's ruling on the vow survey, 2026-11-25 (item 9; the 2% is a play-test point)"),
    OptionReword(
        "Bulwark", "Third Oath", "Bloodied but Unbroken",
        "Your retaliation damage is increased by 1% for every 75 HP you are missing.",
        "Your retaliation damage is increased by 1% for every 1% of your Maximum HP you are missing.",
        "the coordinating session's ruling on the vow survey, 2026-11-25 (item 14)"),
)

#: The Saboteur's twelve options, approved on 2026-11-24, by capstone identifier: (name, sentence), in order.
SABOTEUR_OPTIONS: dict[str, tuple[tuple[str, str], ...]] = {
    "capstone_25": (
        ("Baited", "When one of your traps arms, every enemy within 8 metres of it is drawn to it: it goes for the trap "
                   "instead of whatever it was attacking, for 4 seconds or until the trap goes off."),
        ("Walking Arsenal", "Your gadgets walk after you, staying within 8 metres of you, and keep firing as they move."),
        ("Lightning Rod", "A hit that would strike you has a 3% chance for each trap and gadget you have on the "
                          "battlefield, up to 30%, to strike the nearest of them within 10 metres of you instead. A "
                          "trap struck this way goes off."),
    ),
    "capstone_50": (
        ("Trail of Wire", "Each time you use your movement skill, the last trap you placed is placed again where the "
                          "movement began, armed at once."),
        ("Scrap Mines", "When one of your gadgets expires or is destroyed, it leaves a trap where it stood. The trap "
                        "fires that gadget's attack once, at double damage, at the first enemy to come within 3 metres."),
        ("Cat's Cradle", "Each of your traps and gadgets is joined by a wire to every other one within 10 metres. An "
                         "enemy crossing a wire is pinned for 1 second, no more than once every 4 seconds, and the "
                         "nearer end answers: a trap goes off, a gadget fires at it."),
    ),
    "capstone_100": (
        ("Creeping Mines", "Your armed traps creep toward the nearest enemy within 10 metres of them, at 3 metres a "
                           "second."),
        ("Bolted Together", "A gadget you place within 4 metres of another of yours is bolted onto it: one gadget, with "
                            "their health added together, that fires all their attacks with 10% more damage for each "
                            "gadget in it."),
        ("Counterfire", "Each hit you evade is answered by one of your traps or gadgets for every 2 you have on the "
                        "battlefield, nearest the attacker first: a gadget fires at it, a trap goes off. No more than "
                        "once a second."),
    ),
    "capstone_200": (
        ("Where They Fall", "An enemy killed within 10 metres of one of your traps or gadgets leaves an armed copy of "
                            "the last trap you placed where it fell. The copy reserves 1 Fervour and lasts 4 seconds."),
        ("Magnum Opus", "The gadget you have had standing longest uses every skill you use, at its own target and with "
                        "your full damage. When it is gone, the next longest-standing takes its place."),
        ("Crossfire", "Every hit you land is repeated from each of your traps and gadgets within 15 metres of the "
                      "enemy, up to 15 of them, at 10% of its damage. A trap that repeats a hit does not go off."),
    ),
}
SABOTEUR = "Saboteur"


def apply_option_reword(data: dict, fix: OptionReword, tree: str) -> list[str]:
    capstone = order.node_named(data, fix.capstone, tree)
    found = [option for option in capstone["data"].get("options", []) if option.get("name") == fix.option]
    if len(found) != 1:
        raise order.Refused(f"{tree}: {fix.capstone!r} has {len(found)} options named {fix.option!r}")
    now = found[0].get("description", "")
    if now == fix.after:
        return []
    if now != fix.before:
        raise order.Refused(f"{tree}: {fix.capstone!r} option {fix.option!r} reads {now!r}, which is neither the "
                            f"sentence {fix.ruling} was made against nor the new one")
    found[0]["description"] = fix.after
    return [f"reworded {fix.capstone!r} option {fix.option!r} ({fix.ruling})"]


def apply_saboteur_options(data: dict) -> list[str]:
    changes = []
    for identifier, options in SABOTEUR_OPTIONS.items():
        found = [node for node in data["nodes"] if node.get("id") == identifier]
        if len(found) != 1:
            raise order.Refused(f"{SABOTEUR}: {len(found)} nodes have the identifier {identifier!r}")
        held = found[0]["data"].get("options", [])
        wanted = [{"name": name, "description": sentence} for name, sentence in options]
        if held == wanted:
            continue
        if held:
            raise order.Refused(f"{SABOTEUR}: {identifier} already holds the options "
                                f"{[option.get('name') for option in held]}, which are not the twelve approved on "
                                f"2026-11-24")
        found[0]["data"]["options"] = wanted
        changes.append(f"filled {identifier}'s options: {', '.join(name for name, _ in options)}")
    return changes


def plan_file(path: pathlib.Path) -> order.Outcome:
    """What writing the sentences would do to one tree file, without writing it."""
    raw = path.read_bytes()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise order.Refused(f"{path.name} is not a JSON tree file: {error}") from error
    tree = order.tree_name(data, path)
    fixes = [fix for fix in OPTION_REWORDS if fix.tree == tree]
    if not fixes and tree != SABOTEUR:
        return order.Outcome(path, tree, [], None)
    form = order.format_of(raw, data)
    if form is None:
        raise order.Refused(f"{path.name}: no two-space JSON serialisation reproduces the file byte for byte")
    changes = [line for fix in fixes for line in apply_option_reword(data, fix, tree)]
    if tree == SABOTEUR:
        changes += apply_saboteur_options(data)
    new_bytes = form.write(data) if changes else None
    if new_bytes == raw:
        new_bytes = None
    return order.Outcome(path, tree, changes, new_bytes)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="+", help="tree JSON files, or directories holding *_Class_Tree_*.json")
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 if a file would change")
    args = parser.parse_args(argv)
    try:
        outcomes = [plan_file(path) for path in order.tree_files(args.paths)]
    except order.Refused as refusal:
        print(f"REFUSED: {refusal}. Nothing was written.", file=sys.stderr)
        return 2
    pending = [outcome for outcome in outcomes if outcome.new_bytes is not None]
    for outcome in outcomes:
        if outcome.new_bytes is None:
            continue
        print(f"{outcome.path}: {'would change' if args.check else 'changed'}:")
        for line in outcome.changes:
            print(f"  {line}")
    if args.check:
        print(f"{len(pending)} file(s) would change.")
        return 1 if pending else 0
    for outcome in pending:
        outcome.path.write_bytes(outcome.new_bytes)
    print(f"{len(pending)} file(s) changed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
