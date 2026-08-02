"""Prove cataclysm_sim.scoring is equivalent to DungeonSimulator's calculateScores.tsx.

    python verify_scoring_port.py

The self-test inside scoring.py checks two things: that this file's arithmetic
still produces known values, and that the six constant tables still match the
reference. Neither can catch a change to the reference's *formula* -- only to its
numbers. This script closes that gap by executing the real TypeScript and
comparing every output against the Python port across a large grid of inputs.

It needs Node.js and a checkout of DungeonSimulator. Without either it reports
that it skipped and exits 0, so it is safe to wire into CI before those exist.
Point it at a specific reference with the CATACLYSM_SCORING_REFERENCE
environment variable.

Exit codes: 0 = equivalent or skipped, 1 = a real mismatch.
"""

from __future__ import annotations

import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

from cataclysm_sim import scoring

DUNGEON_TYPES = ("Basic", "Quest", "Fallen City", "Cataclysm")
SUBTYPES = ("None", "Timed", "Horde", "Sacrificial",
            "Cow Level", "Elite", "Siege", "Volatile")
TOTAL_FLOORS = (1, 7, 20, 50, 113, 150)
MODIFIERS = (0, 37.5, -12)
RARITIES = ("Common", "Elite", "Legendary", "Herald", "Boss", "Cataclysm Boss")


def build_cases() -> list[list]:
    """Every combination worth checking, including the awkward edges.

    totalFloors=1 forces currentFloor == totalFloors == middleFloor; the negative
    modifier drives scores below zero; 37.5 produces the exact .5 sums that
    exposed the rounding difference between Python and JavaScript.
    """
    cases = []
    for tier in range(1, 9):
        for dtype in DUNGEON_TYPES:
            for subtype in SUBTYPES:
                for total in TOTAL_FLOORS:
                    for frac in (0.0, 0.5, 1.0):
                        cur = max(1, min(total, round(total * frac) or 1))
                        for mod in MODIFIERS:
                            cases.append([tier, dtype, subtype, total, cur, mod])
    return cases


def to_javascript(tsx: str) -> str:
    """Strip the TypeScript-only syntax so Node can run the reference as-is.

    Deliberately textual: the point is to execute the reference's own arithmetic,
    not a paraphrase of it. Only type annotations are removed.
    """
    js = re.sub(r"export type [\s\S]*?\n};\n", "", tsx)
    js = js.replace("export const", "const")
    js = re.sub(r":\s*Record<[^>]+>", "", js)
    js = js.replace("(inputs: DungeonInputs): Results =>", "(inputs) =>")
    js = js.replace("(targetFloor: number)", "(targetFloor)")
    js = js.replace("(rarityKey: keyof typeof rarityWeights)", "(rarityKey)")
    return js


def run_reference(tsx_path: pathlib.Path, cases: list[list]) -> list[list[int]]:
    js = to_javascript(tsx_path.read_text(encoding="utf-8"))
    js += f"\nconst CASES = {json.dumps(cases)};\n"
    js += """
const out = CASES.map(([t, dt, st, tf, cf, m]) => {
  const r = calculateScores({tier: t, dungeonType: dt, subtype: st,
                             totalFloors: tf, currentFloor: cf, modifierScore: m});
  return [r.dungeonScore, r.commonEnemyScore, r.eliteEnemyScore,
          r.legendaryEnemyScore, r.heraldEnemyScore, r.bossEnemyScore,
          r.cataclysmBossEnemyScore];
});
console.log(JSON.stringify(out));
"""
    with tempfile.TemporaryDirectory() as td:
        script = pathlib.Path(td) / "reference.mjs"
        script.write_text(js, encoding="utf-8")
        proc = subprocess.run([shutil.which("node"), str(script)],
                              capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"node failed running the reference:\n{proc.stderr[:2000]}")
    return json.loads(proc.stdout)


def main() -> int:
    ref = scoring.reference_path()
    if ref is None:
        print("SKIPPED: calculateScores.tsx not found.")
        print(f"  Set {scoring.REFERENCE_ENV_VAR} to its path.")
        return 0
    if shutil.which("node") is None:
        print("SKIPPED: Node.js is not on PATH, so the reference cannot be executed.")
        print(f"  The constant-table check in scoring.py still covers {ref.name}.")
        return 0

    cases = build_cases()
    print(f"Reference: {ref}")
    print(f"Comparing {len(cases)} input combinations x {1 + len(RARITIES)} outputs "
          f"= {len(cases) * (1 + len(RARITIES))} values.")

    expected = run_reference(ref, cases)
    if len(expected) != len(cases):
        # strict=True below would raise, but this says what actually went wrong.
        print(f"\nFAIL: the reference returned {len(expected)} results "
              f"for {len(cases)} inputs.")
        return 1

    mismatches = []
    for (tier, dtype, subtype, total, cur, mod), want in zip(cases, expected,
                                                             strict=True):
        scores = scoring.enemy_scores(tier, dtype, subtype, total, cur, mod)
        got = ([scoring.dungeon_score(tier, dtype, subtype, total, mod)]
               + [scores[r] for r in RARITIES])
        if got != want:
            mismatches.append(((tier, dtype, subtype, total, cur, mod), want, got))

    if not mismatches:
        print("\nPASS: scoring.py reproduces calculateScores.tsx exactly.")
        return 0

    print(f"\nFAIL: {len(mismatches)} of {len(cases)} input combinations disagree.")
    print(f"  columns: dungeonScore, {', '.join(RARITIES)}")
    for args, want, got in mismatches[:20]:
        tier, dtype, subtype, total, cur, mod = args
        print(f"\n  T{tier} {dtype}/{subtype} {total} floors, floor {cur}, modifier {mod}")
        print(f"    reference : {want}")
        print(f"    scoring.py: {got}")
        print(f"    delta     : {[g - w for g, w in zip(got, want, strict=True)]}")
    if len(mismatches) > 20:
        print(f"\n  ... and {len(mismatches) - 20} more.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
