"""No field of the player's floor effects is both written by a per-floor rule and assigned over by the beat.

Issue #1765. `ACataclysmDungeonGameMode::ApplyChangingFloorEffects` builds the player's floor effects from
`UCataclysmDungeonModifierEffects::PlayerEffectsFor`, which holds every per-floor rule's figure, and then writes the
rules a beat changes on top. A beat field written with a plain `=` erases whatever a per-floor rule put in the same
field, and only on a floor that carries a beat rule as well, so nothing else would notice. The convention since is one
field per rule, and a field two rules share is added to with `+=`, as Eternal Chorus's is beside Rule of Chaos's.

FOUND FROM THE CODE, NOT TYPED HERE. The fields `PlayerEffectsFor` writes and the fields the applier assigns with a
plain `=` are read out of the two function bodies, comments stripped, and no field may be in both.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
DUNGEON = ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
MODE_CPP = DUNGEON / "CataclysmDungeonGameMode.cpp"
EFFECTS_CPP = DUNGEON / "CataclysmDungeonModifierEffects.cpp"
WRITTEN = r"\bEffects\.(\w+)\s*(?:\+|-|\*|/)?=(?!=)"
ASSIGNED = r"\bEffects\.(\w+)\s*=(?!=)"


def read(path):
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def body_of(text, owner, name):
    """The body of `owner::name`, defined at column 0, to its closing brace at column 0."""
    starts = [m.end() for m in re.finditer(rf"^\w[\w:<>&*\s]*?\b{owner}::{name}\(", text, re.M)]
    assert len(starts) == 1, f"{owner}::{name} defined {len(starts)} times"
    return text[starts[0]:text.find("\n}\n", starts[0])]


def per_floor_fields():
    body = strip_comments(body_of(read(EFFECTS_CPP), "UCataclysmDungeonModifierEffects", "PlayerEffectsFor"))
    return set(re.findall(WRITTEN, body))


def applier():
    return strip_comments(body_of(read(MODE_CPP), "ACataclysmDungeonGameMode", "ApplyChangingFloorEffects"))


def test_the_check_reads_both_functions():
    """Its scope, so an empty read cannot pass as no overlap: both sides hold fields, and the applier builds from
    `PlayerEffectsFor` and adds to the Chorus field the two rules share."""
    body = applier()
    assigned = set(re.findall(ASSIGNED, body))
    assert "PlayerEffectsFor(" in body
    assert len(per_floor_fields()) >= 10, sorted(per_floor_fields())
    assert len(assigned) >= 10, sorted(assigned)
    assert "ChorusCooldownLongerPercent" in per_floor_fields()
    assert re.search(r"\bEffects\.ChorusCooldownLongerPercent\s*\+=", body)


def test_no_field_a_per_floor_rule_writes_is_assigned_over_by_the_beat():
    overlap = sorted(per_floor_fields() & set(re.findall(ASSIGNED, applier())))
    assert not overlap, (
        f"{overlap}: a per-floor rule writes these in PlayerEffectsFor and ApplyChangingFloorEffects assigns over them "
        "with '=', which erases the per-floor figure on a floor carrying a beat rule. Give the beat rule its own "
        "field, or add with '+='. Issue #1765.")
