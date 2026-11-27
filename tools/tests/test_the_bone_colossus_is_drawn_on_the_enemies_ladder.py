"""The Bone Colossus is drawn on the same ladder of size for health as the enemies' rarity steps.

THE OWNER, 2026-11-27, of the Necromancer's Bone Colossus vow: "it should start larger, and grow with the same scale
as enemies". Until then its size was the cube root of its health over the health it rose with, between 0.6 and 3, so
it rose at a normal minion's size. Now `UCataclysmBoneGiants::StepColossusOf` asks
`UCataclysmEnemyRarity::BodyScaleForHealthMultiple` for the size of a `ColossusStartsAtStep` creature's health
multiple, carried by its health.

WHY FROM PYTHON: continuous integration compiles no C++, so the automation tests
`Cataclysm.EnemyRarity.ABodyWithARungsHealthIsDrawnAtThatRungsSize` and
`Cataclysm.Remains.TheBoneColossusRisesAtALegendarysSizeAndShrinksOnTheEnemiesLadder` never run on a pull request.
"""

from __future__ import annotations

import csv
import math
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GIANTS_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem" / "CataclysmBoneGiants.cpp"
GIANTS_H = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem" / "CataclysmBoneGiants.h"
RARITIES = REPO_ROOT / "game" / "Data" / "EnemyRarities.csv"


def function_body(text: str, signature: str) -> str:
    """A function's code with its comments stripped, so a comment naming a call cannot satisfy a check for it."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"
    body = re.sub(r"/\*.*?\*/", " ", text[start:end], flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", body)


def _cpp() -> str:
    return GIANTS_CPP.read_text(encoding="utf-8").replace("\r\n", "\n")


def _rungs() -> list[tuple[int, float, float]]:
    """(step, health multiple of Common's, body scale), in step order."""
    with open(RARITIES, encoding="utf-8", newline="") as handle:
        rows = sorted(csv.DictReader(handle), key=lambda row: int(row["Step"]))
    common = float(rows[0]["HealthPerScore"])
    return [(int(row["Step"]), float(row["HealthPerScore"]) / common, float(row["BodyScale"])) for row in rows]


def _starts_at_step() -> int:
    found = re.search(r"static\s+constexpr\s+int32\s+ColossusStartsAtStep\s*=\s*(\d+)\s*;",
                      GIANTS_H.read_text(encoding="utf-8"))
    assert found, "CataclysmBoneGiants.h no longer declares ColossusStartsAtStep"
    return int(found.group(1))


def test_its_size_is_asked_of_the_enemies_ladder():
    body = function_body(_cpp(), "void UCataclysmBoneGiants::StepColossusOf")
    assert "UCataclysmEnemyRarity::BodyScaleForHealthMultiple(" in body
    assert "ColossusStartsAtStep" in body
    assert "1.0f / 3.0f" not in body, "the cube root it replaced is still there"


def test_it_is_drawn_at_its_size_as_it_rises():
    """The rise steps it before saying it rose, so it is never drawn at a normal minion's size for a step."""
    body = function_body(_cpp(), "void UCataclysmBoneGiants::RemainsLanded")
    assert body.find("StepColossusOf(Player)") != -1
    assert body.rfind("StepColossusOf(Player)") < body.find("Colossus rose on the remains")


def test_it_starts_larger_than_a_minion_and_no_larger_than_the_ladder_goes():
    """The owner's "start larger": the starting step is above Common and on the table. CONTROL: Common is 1."""
    rungs = {step: scale for step, _, scale in _rungs()}
    step = _starts_at_step()
    assert rungs[0] == 1.0
    assert step in rungs and rungs[step] > 1.0, (step, rungs)


def test_the_ladder_is_geometric_so_log_interpolation_follows_it_exactly():
    """The C++ interpolates in log-log space between rungs, which reproduces the rungs and is a single power law only
    while health and size each climb by one ratio. CONTROL: the power is not the 1/3 of the cube root it replaced."""
    rungs = _rungs()
    powers = [math.log(b[2] / a[2]) / math.log(b[1] / a[1]) for a, b in zip(rungs, rungs[1:], strict=False)]
    assert max(powers) - min(powers) < 1e-3, powers
    assert abs(powers[0] - 1.0 / 3.0) > 0.01, powers[0]


def test_its_reach_grows_with_its_body():
    """A minion's reach is centre to centre, so a Colossus drawn larger reaches by the reach it rose with plus its
    capsule's radius times its size over one (the coordinating session, 2026-11-27). CONTROL: the rise records the
    reach it rose with, which the step builds on."""
    step = function_body(_cpp(), "void UCataclysmBoneGiants::StepColossusOf")
    assert "Colossus->ReachCm = Tally->ColossusStartReach + (Scale - 1.0f) * Body->GetUnscaledCapsuleRadius();" in step
    rise = function_body(_cpp(), "void UCataclysmBoneGiants::RemainsLanded")
    assert "Tally.ColossusStartReach = Risen->ReachCm;" in rise
