"""The C++ guard breaks for window aur-w1 (the auras' mana reservation), registered with the coordinating session on
2026-11-27 and run together in ONE prove_cpp_guard call, as it ruled.

    python .claude/windows/aur_w1_cpp_breaks.py      # the dry run: each anchor matches once; nothing is written

Each is a changed expression or condition, never an early return.
  a  the 75% cap on summed reductions is taken out (`ReservedAfterReductions`)
  b  a held aura's reservation is never asked again once set (`RepriceAuraReservations`)
  c  an aura switching on is never refused for want of room (`UCataclysmAuraSkill::CheckCost`)
"""
from __future__ import annotations

import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
COMMAND = "game/Source/Cataclysm/AbilitySystem/CataclysmCommand.cpp"
COMPONENT = "game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp"
TEMPLATES = "game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplates.cpp"

A_OLD = "const float Held = FMath::Min(PercentLess, MostReservePercentLess);"
A_NEW = "const float Held = PercentLess;"
B_OLD = "\t\t\tHeld.Amount = Ability->ManaReservationFor(this);"
B_NEW = "\t\t\tif (Held.Amount <= 0.0f) { Held.Amount = Ability->ManaReservationFor(this); }"
C_OLD = "\treturn HasRoomToReserve(AbilitySystem);"
C_NEW = "\treturn HasRoomToReserve(AbilitySystem) || bHeld == false;"


def _swapper(old: str, new: str):
    def swap(text: str) -> str:
        if text.count(old) != 1:
            raise ValueError(f"the anchor {old[:60]!r} appears {text.count(old)} times, not once")
        return text.replace(old, new)
    return swap


BREAKS = {
    "a": {COMMAND: _swapper(A_OLD, A_NEW)},
    "b": {COMPONENT: _swapper(B_OLD, B_NEW)},
    "c": {TEMPLATES: _swapper(C_OLD, C_NEW)},
}


def combined() -> dict[str, object]:
    """Every file's edit; the three breaks are in three different files."""
    edits: dict[str, object] = {}
    for name in ("a", "b", "c"):
        for file, edit in BREAKS[name].items():
            assert file not in edits, file
            edits[file] = edit
    return edits


def dry_run() -> list[str]:
    lines = []
    for name in ("a", "b", "c"):
        for file, edit in BREAKS[name].items():
            text = (REPO_ROOT / file).read_text(encoding="utf-8")
            if edit(text) == text:
                raise ValueError(f"break {name} changed nothing")
            lines.append(f"({name}) {file}: the anchor matched once; the break changes the text")
    return lines


if __name__ == "__main__":
    try:
        print("\n".join(dry_run()))
    except ValueError as error:
        print(f"DRY RUN FAILED: {error}")
        sys.exit(1)
