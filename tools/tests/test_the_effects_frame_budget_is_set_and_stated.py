"""The effects frame-time budget is set in the engine and stated in the design.

WHY THIS EXISTS. Issue #547. `docs/Niagara_Conventions.md` section 4 states a
performance budget for particle effects, and `game/Config/DefaultEngine.ini` is
where the engine is actually told about it. Those are two files that have to
agree and nothing made them.

**A BUDGET THAT IS ONLY WRITTEN IN A DOCUMENT IS NOT A BUDGET.** Unreal ships
`fx.Budget.GameThread`, `fx.Budget.GameThreadConcurrent` and
`fx.Budget.RenderThread` already set to 2 ms, and ships `fx.Budget.Enabled` and
`fx.Budget.EnabledInEditor` false, so none of them does anything until a project
sets them. Before 2026-08-22 this project had the document sentence and not the
configuration, which is the state this test exists to stop returning to. With
tracking off the Niagara debug display's Budget column reads zero whatever is
happening, so the failure looks like "effects are comfortably within budget".

WHAT IS CHECKED BOTH WAYS. Every budget the configuration sets is stated in the
conventions document at the same figure, and every budget the document states is
set in the configuration. So neither file can be edited alone.

WHAT IS NOT CHECKED. That the figures are the right ones, which is a judgement
recorded in `docs/DECISIONS.md`, and that Unreal applies them, which is engine
behaviour. The automation test
`Cataclysm.Effects.EnemyEffectsAreCulledHardEnoughForTwentyBrutes` is what reads
the effect type assets; continuous integration compiles no C++, so it does not
run on a pull request and this does.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ENGINE_INI = REPO_ROOT / "game" / "Config" / "DefaultEngine.ini"
CONVENTIONS = REPO_ROOT / "docs" / "Niagara_Conventions.md"

#: The section Unreal reads console variable overrides from at start-up. A key
#: in any other section is read as a class default and silently does nothing.
SECTION = "[SystemSettings]"

#: The two switches that turn the budget from three inert numbers into something
#: the engine tracks. Both default false, which is the whole point.
SWITCHES = ("fx.Budget.Enabled", "fx.Budget.EnabledInEditor")

#: `fx.Budget.GameThread=2.0` in the configuration.
INI_BUDGET = re.compile(r"^(fx\.Budget\.\w+)\s*=\s*(\S+)\s*$", re.MULTILINE)

#: | `fx.Budget.GameThread` | 2.0 ms | ... | in the conventions document.
DOC_BUDGET = re.compile(r"^\|\s*`(fx\.Budget\.\w+)`\s*\|\s*([\d.]+)\s*ms\s*\|",
                        re.MULTILINE)


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def section_of(text: str, name: str) -> str:
    """The body of one INI section, or "" when it is not there."""
    start = text.find(name)
    if start < 0:
        return ""
    rest = text[start + len(name):]
    end = rest.find("\n[")
    return rest if end < 0 else rest[:end]


def configured() -> dict[str, str]:
    body = section_of(read(DEFAULT_ENGINE_INI), SECTION)
    if not body:
        pytest.fail(
            f"game/Config/DefaultEngine.ini has no {SECTION} section, so the "
            f"effects frame-time budget issue #547 set is not applied to the "
            f"engine at all.")
    return {m.group(1): m.group(2) for m in INI_BUDGET.finditer(body)}


def documented() -> dict[str, str]:
    return {m.group(1): m.group(2)
            for m in DOC_BUDGET.finditer(read(CONVENTIONS))}


def test_the_budget_is_switched_on_rather_than_only_written_down():
    values = configured()
    for switch in SWITCHES:
        assert switch in values, (
            f"{switch} is not set in game/Config/DefaultEngine.ini. Unreal "
            f"defaults it to false, so the three fx.Budget figures sit in the "
            f"engine doing nothing and the Niagara debug display's Budget "
            f"column reads zero whatever is happening. Issue #547.")
        assert values[switch] == "1", (
            f"{switch} is {values[switch]} rather than 1, so nothing tracks "
            f"what effects cost. Issue #547.")


def test_every_budget_the_engine_is_given_is_stated_in_the_conventions():
    values = {name: value for name, value in configured().items()
              if name not in SWITCHES}
    assert values, (
        "game/Config/DefaultEngine.ini sets no fx.Budget figures. Section 4 of "
        "docs/Niagara_Conventions.md states a frame-time budget for effects and "
        "this is where the engine is told about it. Issue #547.")

    stated = documented()
    for name, value in sorted(values.items()):
        assert name in stated, (
            f"{name} is set to {value} in game/Config/DefaultEngine.ini and is "
            f"not in the budget table in section 4 of "
            f"docs/Niagara_Conventions.md. A budget nobody can read is not one.")
        assert float(stated[name]) == float(value), (
            f"{name} is {value} in game/Config/DefaultEngine.ini and "
            f"{stated[name]} ms in docs/Niagara_Conventions.md. One of the two "
            f"was edited without the other.")


def test_every_budget_the_conventions_state_is_given_to_the_engine():
    values = configured()
    for name, stated in sorted(documented().items()):
        assert name in values, (
            f"docs/Niagara_Conventions.md states a budget of {stated} ms for "
            f"{name} and game/Config/DefaultEngine.ini does not set it, so the "
            f"engine keeps its own default and the document describes something "
            f"that is not happening.")


def test_the_measurement_behind_the_budget_is_recorded():
    # A budget with no measurement behind it is what section 4 said about every
    # number in it before issue #547, and the point of that issue was to stop
    # that being true. The figures are in the document rather than here, but
    # that the section exists at all is checkable.
    text = read(CONVENTIONS)
    for marker in ("MEASURED 2026-08-22", "fx.ParticlePerfStats.RunTest"):
        assert marker in text, (
            f"docs/Niagara_Conventions.md no longer contains {marker!r}. "
            f"Section 4 has to carry both the measurement the budget came from "
            f"and the command that repeats it, or the budget is a guess again. "
            f"Issue #547.")
