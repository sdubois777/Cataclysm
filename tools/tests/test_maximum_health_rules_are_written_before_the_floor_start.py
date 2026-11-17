"""Every dungeon-long rule that changes the player's maximum health is written back before the floor starts.

Issue #2190, ruled 2026-10-01. `ApplyFloorRulesToPlayer` replaces the player's floor modifiers wholesale, which takes
off every rule a beat writes, and `floor_start` -- "You start every dungeon floor at 30%-50% of your maximum HP" --
reads the maximum right after. `WriteTheMaximumHealthRulesBack` calls the step of each rule that writes maximum health,
so the floor starts with the maximum those rules leave. It is a list, and this keeps it whole.

FOUND FROM THE CODE, NOT TYPED HERE. The maximum-health fields are read off `FCataclysmPlayerFloorEffects`; the
statements that fill them are read out of `ApplyChangingFloorEffects`, the builder every beat-written rule goes
through, including calls into `UCataclysmDungeonModifierEffects` functions that fill a field; the applied state each
statement reads is followed to the `Step...` function that writes it; and that step must be called in
`WriteTheMaximumHealthRulesBack`. A new rule that writes maximum health on the beat, and is not added there, fails here.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
DUNGEON = ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
MODE_CPP = DUNGEON / "CataclysmDungeonGameMode.cpp"
EFFECTS_H = DUNGEON / "CataclysmDungeonModifierEffects.h"
EFFECTS_CPP = DUNGEON / "CataclysmDungeonModifierEffects.cpp"
DEFINITION = r"^\w[\w:<>&*\s]*?\b{owner}::(\w+)\("


def read(path):
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def functions(text, owner):
    """(name, body) for each function of `owner` defined at column 0, its body to the closing brace at column 0."""
    found = []
    for m in re.finditer(DEFINITION.format(owner=owner), text, re.M):
        end = text.find("\n}\n", m.end())
        found.append((m.group(1), text[m.end():end]))
    return found


def body_of(text, owner, name):
    bodies = [body for found, body in functions(text, owner) if found == name]
    assert len(bodies) == 1, f"{owner}::{name} defined {len(bodies)} times"
    return bodies[0]


def to_statement_end(text, start):
    """From `start` to the first `;` outside brackets: a statement, or a local's whole definition, lambda included."""
    depth = 0
    for i in range(start, len(text)):
        ch = text[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == ";" and depth <= 0:
            return text[start:i + 1]
    raise AssertionError("statement never ends")


def maximum_health_fields():
    header = read(EFFECTS_H)
    start = header.index("struct CATACLYSM_API FCataclysmPlayerFloorEffects")
    struct = header[start:header.index("\n};\n", start)]
    return set(re.findall(r"\bfloat\s+(\w*MaxHealth\w*)\s*=", struct))


def filler_functions(fields):
    """`UCataclysmDungeonModifierEffects` functions that write one of the fields into the effects they are given."""
    text = strip_comments(read(EFFECTS_CPP))
    return {name for name, body in functions(text, "UCataclysmDungeonModifierEffects")
            if any(re.search(rf"\b\w+\.{field}\s*=(?!=)", body) for field in fields)}


def builder_statements(fields, fillers):
    """Each statement in the builder that fills a maximum-health field, with the definitions of the locals it uses."""
    builder = body_of(strip_comments(read(MODE_CPP)), "ACataclysmDungeonGameMode", "ApplyChangingFloorEffects")
    starts = [m.start() for m in re.finditer(r"\bEffects\.(\w+)\s*=(?!=)", builder) if m.group(1) in fields]
    starts += [m.start() for m in re.finditer(r"\bUCataclysmDungeonModifierEffects::(\w+)\(", builder)
               if m.group(1) in fillers]
    statements = []
    for start in sorted(starts):
        text = to_statement_end(builder, start)
        reached = text
        for name in set(re.findall(r"\b([A-Za-z_]\w*)\b", text)):
            definition = re.search(rf"\b(?:const\s+)?(?:auto|float|int32|bool)\s+{name}\s*=", builder)
            if definition:
                reached += "\n" + to_statement_end(builder, definition.start())
        statements.append((" ".join(text.split())[:80], reached))
    return statements


def writer_steps(state):
    """The `Step...` functions of the game mode that assign this applied state."""
    mode = strip_comments(read(MODE_CPP))
    return {name for name, body in functions(mode, "ACataclysmDungeonGameMode")
            if name.startswith("Step") and re.search(rf"\b{state}\s*=(?!=)", body)}


def test_every_beat_written_maximum_health_rule_is_written_back_before_the_floor_starts():
    fields = maximum_health_fields()
    assert len(fields) >= 6, f"read only {sorted(fields)} off FCataclysmPlayerFloorEffects"
    fillers = filler_functions(fields)
    assert fillers, "no UCataclysmDungeonModifierEffects function fills a maximum-health field"
    statements = builder_statements(fields, fillers)
    assert len(statements) >= 5, f"read only {len(statements)} maximum-health statements in the builder"

    mode = strip_comments(read(MODE_CPP))
    write_back = body_of(mode, "ACataclysmDungeonGameMode", "WriteTheMaximumHealthRulesBack")
    called = set(re.findall(r"\b(Step\w+)\(", write_back))

    # THE FLOOR'S OWN ROWS: a statement reading no applied state is the floor's static rules, which
    # `ApplyFloorRulesTo` -- inside `ApplyFloorRulesToPlayer` -- applies itself, so they are on the maximum before the
    # start. Accepted only when that function makes the same call.
    rules = body_of(mode, "ACataclysmDungeonGameMode", "ApplyFloorRulesTo")
    missing = []
    every_step = set()
    static = 0
    for first_line, reached in statements:
        states = set(re.findall(r"\b(\w+Applied)\b", reached))
        if not states:
            call = re.match(r"UCataclysmDungeonModifierEffects::(\w+)\(", first_line)
            assert call and re.search(rf"\bUCataclysmDungeonModifierEffects::{call.group(1)}\(", rules), (
                f"the builder statement `{first_line}` reads no applied state and is not a call the floor's rules make")
            static += 1
            continue
        steps = set().union(*(writer_steps(state) for state in states))
        assert steps, f"no Step function writes {sorted(states)}, read by `{first_line}`"
        every_step |= steps
        missing += [f"{step} (writes {sorted(states)} for `{first_line}`)" for step in sorted(steps - called)]
    assert len(every_step) >= 4, f"followed only {sorted(every_step)}"
    assert static <= 1, f"{static} statements read the floor's own rows; expected the one PlayerEffectsFor call"
    assert not missing, "WriteTheMaximumHealthRulesBack does not call: " + "; ".join(missing)


def test_every_caller_applies_the_floor_rules_through_the_one_function():
    """`ApplyFloorRulesToPlayer` is called only from `ApplyFloorRulesKeepingHealth`, so no caller skips a step."""
    mode = strip_comments(read(MODE_CPP))
    found = functions(mode, "ACataclysmDungeonGameMode")
    outside = [name for name, body in found
               if re.search(r"\bApplyFloorRulesToPlayer\(\)\s*;", body) and name != "ApplyFloorRulesKeepingHealth"]
    assert not outside, f"ApplyFloorRulesToPlayer is called directly by {outside}"
    callers = {name for name, body in found if re.search(r"\bApplyFloorRulesKeepingHealth\(\)\s*;", body)}
    assert {"StartPlay", "GoToFloor", "LeaveEmpireDungeon"} <= callers, f"callers read: {sorted(callers)}"
