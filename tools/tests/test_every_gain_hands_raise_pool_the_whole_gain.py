"""Every gain to the class resource pool hands `UCataclysmFervour::RaisePool` the whole gain, not only what fits.

WHY THIS EXISTS. The Soul Collector's Overflow counts the Fervour a gain could not fit above the full bar after
reserves, and `RaisePool` is where it is measured (the Death trees' slice C6c, 2026-11-25). Until window death3-w1's
second pass every gain stopped itself at the bar first -- `FMath::Clamp(Before + ..., 0.0f,
Resource->MaximumClassResourceAsked()) - Before` -- so `RaisePool` was only ever handed what fitted and Overflow heard
nothing from a real gain. Its Unreal test called `RaisePool` directly, so nothing failed. Ruled by the coordinating
session: pass the whole gain.

WHAT IS ASSERTED. No function hands `RaisePool` a value clamped to `MaximumClassResourceAsked`: a name assigned from
such a clamp, or from that name less `Before`, is never `RaisePool`'s amount. Read from the source with comments
removed, over every .cpp under game/Source but the tests; the failure prints how many functions it read. A clamp may
still stand for a loss, as `UCataclysmFervour::Move`'s does. The Unreal half is
`Cataclysm.FervourLossAndChain.AGainPastTheBarThroughARealPathGivesOverflowItsExcess`.
"""

from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source"
DEFINITION = re.compile(r"^[A-Za-z_][\w:<>,\*&\s]*?\b((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)\s*\(", re.M)
CLAMPED = re.compile(r"\b(\w+)\s*=\s*FMath::Clamp\(\s*Before\s*\+[^;]*MaximumClassResourceAsked\(\)\)", re.S)
RAISE_POOL_AMOUNT = re.compile(r"RaisePool\([^,;]+,\s*(\w+)\s*\)")


def clamped_names(body: str) -> set[str]:
    """The names a function assigns from a clamp to the bar, and from one of those less `Before`."""
    names = set(CLAMPED.findall(body))
    for name in list(names):
        names |= set(re.findall(r"\b(\w+)\s*=\s*" + name + r"\s*-\s*Before\b", body))
    return names


def code_only(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def functions_calling_raise_pool() -> dict[str, str]:
    found: dict[str, str] = {}
    for path in sorted(SOURCE.rglob("*.cpp")):
        relative = path.relative_to(ROOT).as_posix()
        if "/Tests/" in relative:
            continue
        code = code_only(path.read_text(encoding="utf-8", errors="replace"))
        starts = [(m.start(), m.group(1)) for m in DEFINITION.finditer(code)]
        for index, (start, name) in enumerate(starts):
            end = starts[index + 1][0] if index + 1 < len(starts) else len(code)
            body = code[start:end]
            if "RaisePool(" in body and not name.endswith("::RaisePool"):
                found[f"{relative} {name}"] = body
    return found


def test_no_gain_stops_itself_at_the_bar_before_raise_pool():
    found = functions_calling_raise_pool()
    assert len(found) >= 12, f"the reader found only {len(found)} functions calling RaisePool: {sorted(found)}"
    clamping = sorted(name for name, body in found.items()
                      if set(RAISE_POOL_AMOUNT.findall(body)) & clamped_names(body))
    assert not clamping, (
        f"these functions clamp a gain to the bar before RaisePool, so Overflow never sees what did not fit: "
        f"{clamping}. Scope: {len(found)} functions calling RaisePool, every .cpp under game/Source but the tests.")
