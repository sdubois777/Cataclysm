"""Write the Soul Collector keystone rework's rows into the design workbook's Passive Effects sheet.

    python tools/reapply_soul_collector_keystones_rework_workbook.py
    python tools/reapply_soul_collector_keystones_rework_workbook.py --check
    python tools/reapply_soul_collector_keystones_rework_workbook.py --workbook <copy.xlsx> --no-generate

ISSUE #2951, the rework of 2026-11-27. The owner's words, the five answers and the rulings are in `docs/DECISIONS.md`;
the sentences these rows stand behind are written into the tree file by
`tools/apply_soul_collector_keystones_rework_2026_11_27.py`. Twenty nodes change: the starting node, eight keystones and
eleven small nodes. Every number is a judgement under the owner's delegation, the small nodes' the coordinating
session's ruling within the owner's approval.

THE WORKBOOK CANNOT BE MERGED, so this script is how the change reaches it: it rewrites a node only when the node holds
exactly the rows it held on `development` 79d3da751 (each node's `before`), leaves alone one already holding the new
rows, and refuses anything else. It uses the machinery of `tools/reapply_death_trees_workbook.py` (`plan_effects` with
`Node.before`), checks by reading the workbook back that no other sheet or cell moved, and `--check` writes nothing and
exits 1 when something would be written.

AFTERWARDS the DataTable assets need the editor step,
`python tools/run_editor_python.py tools/generate_datatable_assets.py`.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import openpyxl

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reapply_death_trees_workbook as base  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"

ReapplyError = base.ReapplyError
R = base.Row
A = base.A
Node = base.Node
SC = base.SC
HELD = base.HELD
OWN = base.OWN
RUNG_IS = base.RUNG_IS
RUNG_AT_LEAST = base.RUNG_AT_LEAST
SIPHONED = base.SIPHONED
damage = base.damage

#: The condition the starting node's damage rows now state: Death Made Flesh takes them away.
ADDS_DAMAGE = "fervour_held_adds_damage"

#: The starting node's rows on `development` 79d3da751: its damage, its Fervour by rung, its decay and the decay's grace.
STARTING_BEFORE = (
    R("attack_damage", "more", 1, scale=HELD, step=2),
    R("spell_damage", "more", 1, scale=HELD, step=2),
    R("fervour_per_kill", "flat", 1, cond=RUNG_IS, cond_value=0),
    R("fervour_per_kill", "flat", 3, cond=RUNG_IS, cond_value=1),
    R("fervour_per_kill", "flat", 5, cond=RUNG_IS, cond_value=2),
    R("fervour_per_kill", "flat", 8, cond=RUNG_IS, cond_value=3),
    R("fervour_per_kill", "flat", 15, cond=RUNG_AT_LEAST, cond_value=4),
    R("fervour_kill_decay_per_second", "flat", 5),
    R("fervour_kill_decay_grace_seconds", "flat", 4, cond=RUNG_IS, cond_value=0),
    R("fervour_kill_decay_grace_seconds", "flat", 8, cond=RUNG_AT_LEAST, cond_value=1),
)

NODES: tuple[base.Node, ...] = (
    # THE STARTING NODE: its damage stated under `fervour_held_adds_damage`, so Death Made Flesh can take it away, and
    # answer 5, "Kills by your minions, gadgets and traps count as your kills", the stat Death's Toll held.
    Node(SC + "basic_spine_000", "Fervour",
         (R("attack_damage", "more", 1, scale=HELD, step=2, cond=ADDS_DAMAGE),
          R("spell_damage", "more", 1, scale=HELD, step=2, cond=ADDS_DAMAGE))
         + STARTING_BEFORE[2:]
         + (R("follower_kills_count_as_yours", "flat", 1),),
         before=(STARTING_BEFORE,)),

    # THE EIGHT KEYSTONES.
    # Gathered Souls: a stack on each enemy death within 6 metres (the event's Event Value), 2% attack speed and 2%
    # movement speed each, up to 10, one 6 second window that each stack restarts.
    Node(SC + "keystone_spine_001", "Gathered Souls",
         (R("attack_speed", "increased", 2, scale=OWN, step=1, cap=10, event="nearby_death", stack_seconds=6,
            event_value=6),
          R("movement_speed", "increased", 2, scale=OWN, step=1, cap=10, event="nearby_death", stack_seconds=6,
            event_value=6)),
         before=((R("follower_kills_count_as_yours", "flat", 1),),)),
    # Unquiet Souls: each 5 Fervour decayed, a strike of 40% of weapon damage on the strongest enemy within 10 metres.
    Node(SC + "keystone_spine_002", "Unquiet Souls",
         (A("strike_strongest_near", 40, event="fervour_decayed"),),
         before=((R("decay_given_back_on_kill_seconds", "flat", 3),),)),
    # Death Made Flesh, the tree's one conversion: the starting node's damage taken away, and 1% less damage taken for
    # every 4 Fervour held, up to 25 steps.
    Node(SC + "keystone_spine_003", "Death Made Flesh",
         (R("fervour_held_adds_no_damage", "flat", 1),
          R("damage_taken", "more", -1, scale=HELD, step=4, cap=25)),
         before=((R("fervour_kill_decay_per_second", "removed", 1),
                  R("health_percent_lost_per_fervour_lost", "flat", 2)),)),
    # Heavy Urn: 0.5% more damage for each Fervour held above 50.
    Node(SC + "keystone_a_kB", "Heavy Urn",
         damage("more", 0.5, scale=HELD, step=1, scale_offset=50),
         before=((R("fervour_kill_decay_per_second", "more", -50),),)),
    # Tolling Count: a 100% weapon damage nova within 5 metres each time Fervour rises to an opened threshold.
    Node(SC + "keystone_a_kC", "Tolling Count",
         (A("smite_nearby", 100, event="fervour_threshold_reached"),),
         before=((R("fervour_threshold_linger_seconds", "flat", 5),),)),
    # Trophy of the Strong: 20% more damage for 10 seconds after killing an Elite or stronger, one stack.
    Node(SC + "keystone_b_kB", "Trophy of the Strong",
         damage("more", 20, scale=OWN, step=1, cap=1, event="kill", stack_seconds=10, cond=RUNG_AT_LEAST,
                cond_value=1),
         before=((R("fervour_per_kill", "more", 100, cond=RUNG_AT_LEAST, cond_value=1),),)),
    # Reaper's Rhythm: a chain of 5 or more kills, kills within 2 seconds of each other, is one step of 5 stacks capped
    # at 5: 15% more damage and 10% less damage taken, none below 5.
    Node(SC + "keystone_b_kC", "Reaper's Rhythm",
         damage("more", 15, scale=OWN, step=5, cap=5, event="kill", stack_seconds=2)
         + (R("damage_taken", "more", -10, scale=OWN, step=5, cap=5, event="kill", stack_seconds=2),),
         before=((R("fervour_hold_restarted_by_any_gain", "flat", 1),),)),
    # Hungering Siphon: Drain's own stat, 0.5% of weapon damage a second for every 5 Fervour held.
    Node(SC + "keystone_c_kA", "Hungering Siphon",
         (R("siphon_drain_weapon_percent", "flat", 0.5, scale=HELD, step=5),),
         before=((R("siphon_fervour_per_second", "flat", 0.5, scale=SIPHONED, step=1, cap=8),),)),

    # THE ELEVEN SMALL NODES.
    Node(SC + "basic_spine_001", "Tithe of the Dead",
         damage("increased", 1, cond="target_within_metres", cond_value=6),
         before=((R("fervour_per_kill", "increased", 2),),)),
    Node(SC + "basic_spine_004", "Grave Patience",
         (A("health", 0.2, event="fervour_decayed", fraction_of="maximum"),),
         before=((R("fervour_kill_decay_grace_seconds", "flat", 1, min_points=5),
                  R("fervour_kill_decay_grace_seconds", "flat", 1, min_points=10)),)),
    Node(SC + "basic_spine_007", "Claimed",
         damage("increased", 1, cond="seconds_after_kill", cond_value=4),
         before=(tuple(R("fervour_claimed_within_seconds", "flat", 1, min_points=points) for points in (2, 4, 6, 8)),)),
    Node(SC + "basic_a_b1", "Miser's Grip",
         damage("increased", 2, cond="class_resource_points_at_least", cond_value=50),
         before=((R("fervour_decay_floor_percent", "flat", 3),),)),
    Node(SC + "basic_a_b2", "Cold Storage",
         (R("damage_taken", "increased", -1, cond="class_resource_points_at_least", cond_value=50),),
         before=((R("fervour_kill_decay_per_second", "increased", -5),),)),
    Node(SC + "basic_b_stem0", "Gleaning",
         (A("strike_strongest_near", 50, event="kill", chance_percent=5),),
         before=((R("fervour_per_kill_extra_chance", "flat", 5),),)),
    Node(SC + "basic_b_stem2", "Thinning the Herd",
         damage("increased", 1, scale="enemies_in_reach", step=2, reach=8, cap=5),
         before=((R("fervour_per_kill", "increased", 1, scale="enemies_in_reach", step=1, reach=8, cap=10),),)),
    Node(SC + "basic_b_b1", "Tear Loose",
         (A("health", 1, event="kill", fraction_of="maximum", cond=RUNG_AT_LEAST, cond_value=1),),
         before=((A("class_resource", 1, cond=RUNG_AT_LEAST, cond_value=1, event="hit_dealt", fraction_of="points",
                    chance_percent=2),),)),
    Node(SC + "basic_b_b2", "Vigil Over the Strong",
         (R("attack_speed", "increased", 1, cond="enemy_of_rarity_in_reach", cond_value=1, reach=15),),
         before=((R("fervour_kill_decay_per_second", "increased", -5, cond="enemy_of_rarity_in_reach", cond_value=1,
                    reach=15),),)),
    Node(SC + "basic_b_c0", "Gathering Pace",
         (R("attack_speed", "increased", 0.5, scale=OWN, step=1, cap=10, event="kill", stack_seconds=2),),
         before=((R("class_resource_generation", "increased", 2, cond="fervour_gain_not_from_a_kill"),),)),
    Node(SC + "basic_b_c1", "Borrowed Time",
         (R("damage_taken", "increased", -0.5, scale=OWN, step=2, cap=10, event="kill", stack_seconds=2),),
         before=((R("fervour_hold_extended_on_gain_seconds", "flat", 1, min_points=3),
                  R("fervour_hold_extended_on_gain_seconds", "flat", 1, min_points=6)),)),
)


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Rewrite the twenty nodes' rows in the workbook at `path`; return the report."""
    book = openpyxl.load_workbook(path)
    base.refuse_what_a_save_would_lose(book)
    effects = base.plan_effects(book[base.EFFECTS_SHEET], NODES)
    if not effects.changed:
        return ["every row of the Soul Collector keystone rework is already written; nothing written"]
    if check_only:
        return effects.report + [f"{effects.changed} change(s) to make; nothing written (--check)"]

    others = {s.title: base._snapshot(s) for s in book.worksheets if s.title != base.EFFECTS_SHEET}
    base._write(book[base.EFFECTS_SHEET], effects)
    book.save(path)

    # READ BACK FROM DISK: every other sheet as it was, and the rows as planned.
    after = openpyxl.load_workbook(path)
    if [s.title for s in after.worksheets] != [s.title for s in book.worksheets]:
        raise ReapplyError("the sheets changed order or name on saving")
    for title, cells in others.items():
        if base._snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and it was not written")
    base._read_back(after, effects, base.EFFECTS_SHEET)
    return effects.report + [f"wrote {effects.changed} change(s); {base.EFFECTS_SHEET!r} now holds "
                             f"{len(effects.new)} rows (was {len(effects.old)}), and no other sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true", help="write the rows without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if report[-1].endswith("(--check)") else 0
    if args.no_generate:
        return 0
    result = subprocess.run([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)], cwd=REPO_ROOT)
    if result.returncode != 0:
        return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
