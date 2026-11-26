"""The five C++ guard breaks for the ailment ruling's window (ail-w1), approved by the coordinating session on 2026-11-26.

    python .claude/windows/ail_w1_cpp_breaks.py          # the dry run: each anchor matches once; nothing is written

Each break is a condition, never a bare early return. Each function takes a file's text with "\\n" line endings, as
`prove_cpp_guard` hands it, and returns the broken text. `dry_run` reads each file the same way, asks every anchor to
appear exactly once, and checks the break changes the text, writing nothing.

  (a) Resolve step 8: the shield's share asks neither Warded nor the bleed drawback.
  (b) Resolve step 6a2: converted damage is held off like a plain bleed.
  (c) UCataclysmAilments::BlowCanCarryAnAilment: an amount of nought carries an ailment.
  (d) Resolve: a plain bleed against a standing shield is zeroed after the temporary absorb, the order before the
      coordinating session's ruling of 2026-11-26, so the absorb pays for it.
  (e) UCataclysmVitalAttributeSet: a Void Splinter tick is never marked as ignoring the shield.
"""

from __future__ import annotations

import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CALC = "game/Source/Cataclysm/AbilitySystem/CataclysmDamageCalculation.cpp"
AILMENTS = "game/Source/Cataclysm/AbilitySystem/CataclysmAilments.cpp"
VITALS = "game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp"

A_OLD = "const float ShieldShare = !bBypassesShield && bShieldApplies ? 1.0f : 0.0f;"
# STILL READS bShieldApplies AND bBypassesShield, so no local is left unreferenced for the compiler to refuse: for a
# tick that is not a bleed bShieldApplies is true, so the last clause is always true and the share is the first two.
A_NEW = ("const float ShieldShare = !Hit.bIgnoresEnergyShield && !Hit.bIsBleed && (bShieldApplies || !bBypassesShield) "
         "? 1.0f : 0.0f;")

B_OLD = "\tif (Hit.bIsBleed && !Hit.bIsConvertedDamage && !Hit.bIgnoresEnergyShield\n"
B_NEW = "\tif (Hit.bIsBleed && !Hit.bIgnoresEnergyShield\n"

C_OLD = "return Health > 0.0f && Landed > 0.0f;"
C_NEW = "return Health > 0.0f && Landed >= 0.0f;"

# (d) in two places: the hold-off at 6a2 only notes the bleed, and step 8 zeroes it, after the absorb has paid.
D_OLD_1 = "\t\tDamage = 0.0f;\n\t}\n\n\t// 6b. THE TEMPORARY ABSORB, BEFORE EVERY POOL."
D_NEW_1 = "\t\tbHeldOffForTheBreak = true;\n\t}\n\n\t// 6b. THE TEMPORARY ABSORB, BEFORE EVERY POOL."
D_OLD_2 = "\t// 6a2. A BLEED AGAINST A STANDING ENERGY SHIELD DEALS NOTHING, AND USES NOTHING UP."
D_NEW_2 = "\tbool bHeldOffForTheBreak = false;\n" + D_OLD_2
D_OLD_3 = "\t// THE WHOLE OF A BLOW THE SHIELD APPLIES TO, and none of one that bypasses it"
D_NEW_3 = "\tif (bHeldOffForTheBreak)\n\t{\n\t\tDamage = 0.0f;\n\t}\n" + D_OLD_3

E_OLD = "Hit.bIgnoresEnergyShield = VoidSplinterTick.IsValid() && Granted.HasTag(VoidSplinterTick);"
E_NEW = ("Hit.bIgnoresEnergyShield = VoidSplinterTick.IsValid() && Granted.HasTag(VoidSplinterTick) "
         "&& Hit.Damage < 0.0f;")


def _swap(text: str, pairs: list[tuple[str, str]]) -> str:
    for old, new in pairs:
        if text.count(old) != 1:
            raise ValueError(f"the anchor appears {text.count(old)} times, not once: {old[:70]!r}")
        text = text.replace(old, new)
    return text


BREAKS: dict[str, dict[str, object]] = {
    "a": {CALC: lambda t: _swap(t, [(A_OLD, A_NEW)])},
    "b": {CALC: lambda t: _swap(t, [(B_OLD, B_NEW)])},
    "c": {AILMENTS: lambda t: _swap(t, [(C_OLD, C_NEW)])},
    "d": {CALC: lambda t: _swap(t, [(D_OLD_1, D_NEW_1), (D_OLD_2, D_NEW_2), (D_OLD_3, D_NEW_3)])},
    "e": {VITALS: lambda t: _swap(t, [(E_OLD, E_NEW)])},
}


def combined() -> dict[str, object]:
    """Every break at once, one function per file, applied in the order a, b, c, d, e."""
    by_file: dict[str, list] = {}
    for name in "abcde":
        for path, edit in BREAKS[name].items():
            by_file.setdefault(path, []).append(edit)

    def chain(edits):
        def apply(text):
            for edit in edits:
                text = edit(text)
            return text
        return apply

    return {path: chain(edits) for path, edits in by_file.items()}


def _read(path: str) -> str:
    with open(REPO_ROOT / path, encoding="utf-8", newline=None) as handle:
        return handle.read()


def dry_run() -> list[str]:
    report = []
    for name, edits in BREAKS.items():
        for path, edit in edits.items():
            before = _read(path)
            after = edit(before)
            if after == before:
                raise ValueError(f"break {name} changed nothing in {path}")
            report.append(f"({name}) {path}: every anchor matched once; the break changes the text")
    for path, edit in combined().items():
        before = _read(path)
        if edit(before) == before:
            raise ValueError(f"the combined break changed nothing in {path}")
        report.append(f"combined {path}: applies cleanly")
    return report


if __name__ == "__main__":
    try:
        print("\n".join(dry_run()))
    except ValueError as error:
        print(f"DRY RUN FAILED: {error}")
        sys.exit(1)
