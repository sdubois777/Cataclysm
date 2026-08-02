"""Tests for the gameplay tag generator.

Most of these run against a small fixture workbook built in the test, so they do
not depend on the real design data and cannot be broken by a design change. Two
tests at the end do check the real workbook, because the committed tag list being
out of date is exactly the drift this tool exists to prevent.
"""

from __future__ import annotations

import pathlib
import sys

import openpyxl
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import generate_gameplay_tags as gen  # noqa: E402


def make_workbook(path: pathlib.Path, tags, references=None) -> pathlib.Path:
    """Build a fixture workbook with a Tags sheet and optional reference sheets."""
    book = openpyxl.Workbook()
    sheet = book.active
    sheet.title = gen.TAGS_SHEET
    sheet.append(["Tag Name", "Description"])
    for row in tags:
        sheet.append(list(row))

    if references:
        skills = book.create_sheet("Weapon Skills")
        skills.append(["Weapon Type", "Damage Type", "Slot", "Skill Name",
                       "Skill Description", "Tags"])
        for value in references:
            skills.append(["W", "War", "Heavy", "S", "D", value])

    book.save(path)
    return path


class TestReadingTags:
    def test_reads_tags_and_sorts_them(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx", [
            ("Zulu.Last", "last"), ("Alpha.First", "first"),
        ])
        assert gen.read_tags(book) == [("Alpha.First", "first"), ("Zulu.Last", "last")]

    def test_rejects_a_duplicate_tag(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx", [
            ("Element.War", "one"), ("Element.War", "two"),
        ])
        with pytest.raises(gen.TagError, match="defined more than once"):
            gen.read_tags(book)

    def test_rejects_a_tag_with_no_description(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx", [("Element.War", "")])
        with pytest.raises(gen.TagError, match="has no description"):
            gen.read_tags(book)

    @pytest.mark.parametrize("bad", ["Element War", "Element..War", ".Element", "9Element"])
    def test_rejects_an_invalid_tag_name(self, tmp_path, bad):
        book = make_workbook(tmp_path / "w.xlsx", [(bad, "d")])
        with pytest.raises(gen.TagError, match="not a valid tag name"):
            gen.read_tags(book)

    def test_rejects_a_workbook_with_no_tags_sheet(self, tmp_path):
        path = tmp_path / "empty.xlsx"
        openpyxl.Workbook().save(path)
        with pytest.raises(gen.TagError, match="no sheet named"):
            gen.read_tags(path)


class TestImpliedParents:
    def test_parents_are_implied(self):
        """Unreal creates parents automatically, so data may reference them."""
        implied = gen.implied_tags([("Item.Weapon.Sword", "d")])
        assert implied == {"Item", "Item.Weapon", "Item.Weapon.Sword"}

    def test_a_referenced_parent_is_not_reported_as_missing(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx",
                             [("Item.Weapon.Sword", "d")],
                             references=["Item.Weapon"])
        tags = gen.read_tags(book)
        assert gen.check_references(tags, gen.find_references(book)) == []

    def test_an_undefined_tag_is_reported(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx",
                             [("Slot.Ultimate", "d")],
                             references=["Type.Ultimate"])
        tags = gen.read_tags(book)
        problems = gen.check_references(tags, gen.find_references(book))
        assert len(problems) == 1
        assert "Type.Ultimate" in problems[0]
        assert "Weapon Skills" in problems[0]


class TestRendering:
    def test_output_is_a_valid_tag_list(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx", [("Element.War", "Physical, Bleed")])
        text = gen.render(gen.read_tags(book))
        assert "[/Script/GameplayTags.GameplayTagsList]" in text
        assert 'GameplayTagList=(Tag="Element.War",DevComment="Physical, Bleed")' in text

    def test_output_says_it_is_generated(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx", [("A.B", "d")])
        assert "GENERATED FILE" in gen.render(gen.read_tags(book))

    def test_output_is_deterministic(self, tmp_path):
        """Re-running on an unchanged sheet must produce no diff."""
        book = make_workbook(tmp_path / "w.xlsx", [("B.Two", "2"), ("A.One", "1")])
        tags = gen.read_tags(book)
        assert gen.render(tags) == gen.render(tags)

    def test_a_quote_in_a_description_is_refused(self, tmp_path):
        # The ini format cannot carry an unescaped quote. Rather than emit a
        # file Unreal will misparse, refuse to write it.
        with pytest.raises(gen.TagError, match="contains a quote"):
            gen.render([("A.B", 'a "quoted" word')])


class TestCommandLine:
    def test_writes_the_file(self, tmp_path, capsys):
        book = make_workbook(tmp_path / "w.xlsx", [("A.B", "d")])
        out = tmp_path / "sub" / "Tags.ini"
        assert gen.main(["--workbook", str(book), "--output", str(out)]) == 0
        assert out.is_file()
        assert "Wrote 1 tags" in capsys.readouterr().out

    def test_check_passes_when_current(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx", [("A.B", "d")])
        out = tmp_path / "Tags.ini"
        gen.main(["--workbook", str(book), "--output", str(out)])
        assert gen.main(["--workbook", str(book), "--output", str(out), "--check"]) == 0

    def test_check_fails_when_stale(self, tmp_path, capsys):
        book = make_workbook(tmp_path / "w.xlsx", [("A.B", "d")])
        out = tmp_path / "Tags.ini"
        gen.main(["--workbook", str(book), "--output", str(out)])

        # The sheet changes; the committed file does not.
        make_workbook(book, [("A.B", "d"), ("C.D", "e")])
        assert gen.main(["--workbook", str(book), "--output", str(out), "--check"]) == 1
        assert "out of date" in capsys.readouterr().err

    def test_strict_fails_on_an_undefined_reference(self, tmp_path, capsys):
        book = make_workbook(tmp_path / "w.xlsx",
                             [("Slot.Ultimate", "d")],
                             references=["Type.Ultimate"])
        out = tmp_path / "Tags.ini"
        # Without --strict this is only a warning, so the run still succeeds.
        assert gen.main(["--workbook", str(book), "--output", str(out)]) == 0
        assert "WARNING" in capsys.readouterr().err

        assert gen.main(["--workbook", str(book), "--output", str(out),
                         "--strict"]) == 1
        assert "FAIL" in capsys.readouterr().err

    def test_strict_passes_when_every_reference_resolves(self, tmp_path):
        book = make_workbook(tmp_path / "w.xlsx",
                             [("Slot.Ultimate", "d")],
                             references=["Slot.Ultimate"])
        out = tmp_path / "Tags.ini"
        assert gen.main(["--workbook", str(book), "--output", str(out),
                         "--strict"]) == 0

    def test_check_fails_when_the_file_is_missing(self, tmp_path, capsys):
        book = make_workbook(tmp_path / "w.xlsx", [("A.B", "d")])
        missing = tmp_path / "nope.ini"
        assert gen.main(["--workbook", str(book), "--output", str(missing), "--check"]) == 1
        assert "does not exist" in capsys.readouterr().err


class TestAgainstTheRealWorkbook:
    """The committed tag list going stale is the drift this tool prevents."""

    def test_the_committed_tag_list_is_current(self, capsys):
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        assert gen.main(["--check"]) == 0, (
            "game/Config/Tags/CataclysmTags.ini is out of date. "
            "Run: python tools/generate_gameplay_tags.py"
        )

    def test_the_real_tag_vocabulary_is_valid(self):
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        tags = gen.read_tags(gen.WORKBOOK)
        assert len(tags) > 100, "the Tags sheet lost a large number of rows"
        roots = {tag.split(".")[0] for tag, _ in tags}
        assert {"Element", "Type", "Slot", "Item", "Stat",
                "Keyword", "Scope", "Trigger"} <= roots
