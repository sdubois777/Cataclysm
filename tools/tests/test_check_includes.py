"""`tools/check_includes.py`: a .cpp that uses a project type its own includes do not reach.

The cases run on a small source tree written for each test, in the layout of `game/Source` (a module folder, then a
folder, then the header), so each test states exactly what is in view. The last test runs the check over the real tree.
"""
from __future__ import annotations

import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import check_includes  # noqa: E402

DEFINED = """#pragma once
struct MOD_API FDefined
{
\tint Value = 0;
};

class MOD_API UThing : public UObject
{
};
"""
FORWARD = """#pragma once
struct FDefined;
class UThing;
"""
CHAIN = """#pragma once
#include "Kinds/Defined.h"
"""


def _tree(tmp_path: pathlib.Path, cpp_text: str) -> tuple[pathlib.Path, pathlib.Path]:
    """A source root holding the three headers and one .cpp with `cpp_text`; returns (root, .cpp)."""
    source = tmp_path / "Source"
    kinds = source / "Mod" / "Kinds"
    kinds.mkdir(parents=True)
    (kinds / "Defined.h").write_text(DEFINED, encoding="utf-8")
    (kinds / "Forward.h").write_text(FORWARD, encoding="utf-8")
    (kinds / "Chain.h").write_text(CHAIN, encoding="utf-8")
    cpp = source / "Mod" / "Uses" / "Uses.cpp"
    cpp.parent.mkdir(parents=True)
    cpp.write_text(cpp_text, encoding="utf-8")
    return source, cpp


def _report(tmp_path: pathlib.Path, cpp_text: str) -> check_includes.FileReport:
    source, cpp = _tree(tmp_path, cpp_text)
    return check_includes.Index(source).check(cpp)


def test_a_type_used_by_value_with_only_a_forward_declaration_in_view_fails(tmp_path):
    """The forge window's build 2 of 2026-11-25: a struct used by value, its header never included."""
    report = _report(tmp_path, '#include "Kinds/Forward.h"\n\nvoid Use()\n{\n\tFDefined Held;\n}\n')
    assert [(use.name, use.line) for use in report.failing()] == [("FDefined", 5)]


def test_a_cast_to_a_class_with_only_a_forward_declaration_in_view_fails(tmp_path):
    """`CataclysmEnemyCommanderTests.cpp` of 2026-09-13: `Cast<T>` needs the class."""
    report = _report(tmp_path, '#include "Kinds/Forward.h"\n\nvoid Use(UObject* Any)\n{\n'
                               '\tUThing* Thing = Cast<UThing>(Any);\n}\n')
    assert [(use.name, use.line) for use in report.failing()] == [("UThing", 5)]
    assert [(use.name, use.line) for use in report.notes()] == [("UThing", 5)]


def test_a_pointer_or_reference_with_only_a_forward_declaration_is_a_note(tmp_path):
    source, cpp = _tree(tmp_path, '#include "Kinds/Forward.h"\n\nvoid Use(const FDefined& Held, UThing* Thing)\n{\n}\n')
    report = check_includes.Index(source).check(cpp)
    assert report.failing() == []
    assert {use.name for use in report.notes()} == {"FDefined", "UThing"}
    assert check_includes.main([str(cpp)], source=source) == 0


def test_a_type_reached_through_another_header_is_in_view(tmp_path):
    report = _report(tmp_path, '#include "Kinds/Chain.h"\n\nvoid Use()\n{\n\tFDefined Held;\n'
                               '\tUThing* Thing = NewObject<UThing>();\n}\n')
    assert report.uses == []


def test_a_type_named_only_in_a_comment_or_a_string_is_not_a_use(tmp_path):
    report = _report(tmp_path, '// FDefined is not used here.\n/* Nor is UThing. */\n'
                               'const TCHAR* Name = TEXT("FDefined");\nconst char* Other = "UThing";\n')
    assert report.uses == []


def test_a_block_comment_keeps_the_line_numbers_of_what_follows(tmp_path):
    report = _report(tmp_path, '#include "Kinds/Forward.h"\n/*\n * Three\n * lines\n */\nvoid Use()\n{\n'
                               '\tFDefined Held;\n}\n')
    assert [(use.name, use.line) for use in report.failing()] == [("FDefined", 8)]


def test_the_exit_code_says_what_was_found_and_a_check_over_nothing_is_not_a_pass(tmp_path, capsys):
    source, cpp = _tree(tmp_path, '#include "Kinds/Forward.h"\n\nvoid Use()\n{\n\tFDefined Held;\n}\n')
    assert check_includes.main([str(cpp)], source=source) == 1
    printed = capsys.readouterr().out
    assert "FAILS: FDefined on line(s) 5" in printed
    assert check_includes.LIMITS in printed
    assert check_includes.main([str(tmp_path / "nothing.cpp")], source=source) == 2


def test_no_cpp_under_game_source_uses_a_type_it_cannot_see(capsys):
    """The whole tree, in the default run: 1.7 to 1.9 s for 589 files when measured on 2026-11-25 on the project's
    machine, under the 5 s the coordinating session allowed for it (the suite's length is issue #2602)."""
    result = check_includes.main(["--all"])
    printed = capsys.readouterr().out
    assert result == 0, printed
    assert " 0 type(s) used that the file cannot see" in printed
