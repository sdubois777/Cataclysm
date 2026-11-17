"""`tools/run_editor_python.py` must not report a run that did nothing as a success.

WHY THIS FILE EXISTS. Issue #279. `tools/generate_datatable_assets.py` runs
inside the Unreal editor's Python interpreter. Started against a git worktree,
the editor cannot load the project's C++ modules, because `game/Binaries/` is
gitignored and a worktree therefore has none. It runs for about twenty seconds,
writes nothing, and exits without saying why. The loop that works this backlog
runs in worktrees, so this was silent every time.

WHAT IS CHECKED HERE. The deciding, not the running. Every test runs against log
text captured from real runs on this machine, so none of them needs Unreal
Engine, an editor or a built project, and they run in continuous integration
with the rest of the fast suite. The captures below are verbatim apart from
being trimmed to the lines that matter.

WHAT IS NOT CHECKED HERE. That `run()` invokes the engine correctly. That needs
the engine. The pull request for issue #279 has the four end-to-end runs: a
worktree, a script that succeeds, a script that fails, and a script that does not
exist.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from run_editor_python import (  # noqa: E402
    EDITOR_CMD,
    REQUIRED_MODULE_LIBRARIES,
    RUN_LOG,
    CannotRunEditorScript,
    changes_between,
    check_preconditions,
    describe_changes,
    is_worktree,
    missing_binaries_message,
    missing_module_libraries,
    script_outcome,
    script_started,
    working_tree_state,
)

#: The script path used in all three captures below.
PROBE = pathlib.Path(
    r"C:\Users\iamst\AppData\Local\Temp\claude\scratchpad\probe_editor_python.py")

#: A real run that worked. Captured 2026-08-05 from the ordinary checkout at
#: C:\Projects\Cataclysm with the editor open.
#:
#: THE FOUR LogPython LINES ARE THE POINT. They are engine plugins running their
#: own start-up scripts, and they appear whether or not the -script= file ever
#: executes. An earlier version of this runner treated any LogPython line as
#: evidence the script had run, and it reported the failing capture below as a
#: success.
LOG_SUCCEEDED = r"""
[2026.08.05-17.52.10:083][  0]LogPython: Python enabled via CVar 'Engine.Python.IsEnabledByDefault'
[2026.08.05-17.52.10:083][  0]LogPython: Using Python 3.11.8
[2026.08.05-17.52.10:477][  0]LogPython: Display: Running start-up script C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/Toolsets/StateTreeToolset/Content/Python/init_unreal.py... started...
[2026.08.05-17.52.10:574][  0]LogPython: [MetaHumanGenerator] Toolset registered successfully
[2026.08.05-17.52.10:643][  0]LogPythonScriptCommandlet: Display: Running Python script: C:\Users\iamst\AppData\Local\Temp\claude\scratchpad\probe_editor_python.py
[2026.08.05-17.52.10:643][  0]LogPython: run_editor_python probe: the Python interpreter ran
[2026.08.05-17.52.10:643][  0]LogPythonScriptCommandlet: Display: Python script executed successfully
[2026.08.05-17.52.10:643][  0]LogCore: Engine exit requested (reason: Commandlet PythonScriptCommandlet_0 finished execution (result 0))
"""

#: A real run of a script that called `unreal.log_error` and raised SystemExit(1),
#: which is exactly how both generators report a failure. Captured the same day.
LOG_FAILED = r"""
[2026.08.05-17.53.07:419][  0]LogPython: Using Python 3.11.8
[2026.08.05-17.53.07:419][  0]LogPythonScriptCommandlet: Display: Running Python script: C:\Users\iamst\AppData\Local\Temp\claude\scratchpad\probe_editor_python.py
[2026.08.05-17.53.07:419][  0]LogPython: Error: run_editor_python probe: this is the deliberate failure
[2026.08.05-17.53.07:420][  0]LogPythonScriptCommandlet: Error: Python script executed with errors
[2026.08.05-17.53.07:420][  0]LogInit: Display: LogPythonScriptCommandlet: Error: Python script executed with errors
"""

#: What a worktree produces: an editor that started, gave up before the Python
#: plugin, and exited normally. This is the whole of the evidence a person got
#: before this runner existed. The commandline echo is why searching the log for
#: the word "python" is not a check.
LOG_NEVER_STARTED = r"""
[2026.08.05-15.40.02:001][  0]LogCsvProfiler: Display: Metadata set : commandline="" C:\Projects\Cataclysm\.claude\worktrees\wt\game\Cataclysm.uproject -run=pythonscript -script=C:\Projects\Cataclysm\.claude\worktrees\wt\tools\generate_datatable_assets.py -unattended -nopause -nosplash""
[2026.08.05-15.40.21:877][  0]LogShaderCompilers: Display: Exiting ShaderCompilingThread
[2026.08.05-15.40.22:115][  0]LogExit: Exiting.
"""


class TestDecidingWhetherTheScriptRan:
    def test_a_run_that_never_reached_the_script_is_not_a_success(self):
        """The worktree case. This is the failure the whole file exists for."""
        assert script_started(LOG_NEVER_STARTED, PROBE) is False

    def test_engine_start_up_scripts_are_not_evidence(self):
        """LogPython appears without the -script= file ever running.

        Four LogPython lines are in LOG_SUCCEEDED before the script starts, and
        they are there in any run that gets as far as loading plugins. Treating
        the category as evidence is what made an earlier version of this runner
        report the failing capture as a success.
        """
        before_the_script = LOG_SUCCEEDED.split("LogPythonScriptCommandlet")[0]
        assert "LogPython:" in before_the_script
        assert script_started(before_the_script, PROBE) is False

    def test_a_run_that_started_the_script_is_recognised(self):
        assert script_started(LOG_SUCCEEDED, PROBE) is True
        assert script_started(LOG_FAILED, PROBE) is True

    def test_a_different_script_does_not_count(self):
        """A -script= the engine resolved to something else is not this run."""
        other = PROBE.with_name("generate_datatable_assets.py")
        assert script_started(LOG_SUCCEEDED, other) is False

    def test_separators_and_case_do_not_matter(self):
        """The engine logs Windows separators and its own capitalisation."""
        forward = pathlib.Path(str(PROBE).replace("\\", "/").upper())
        assert script_started(LOG_SUCCEEDED, forward) is True


class TestDecidingWhetherTheScriptWorked:
    def test_success_is_read_from_the_commandlet(self):
        assert script_outcome(LOG_SUCCEEDED) == "succeeded"

    def test_a_script_that_raised_is_a_failure_not_a_success(self):
        """`unreal.log_error` then `raise SystemExit(1)`, which both generators do.

        The capture holds the error line twice, once as itself and once echoed by
        LogInit, and the success line not at all.
        """
        assert script_outcome(LOG_FAILED) == "failed"

    def test_neither_line_is_reported_as_unknown_rather_than_success(self):
        """An editor killed part way through must not read as a clean run."""
        assert script_outcome(LOG_NEVER_STARTED) == "unknown"

    def test_an_error_beats_a_success_line(self):
        """A script that logged an error and then somehow both is not a success."""
        assert script_outcome(LOG_SUCCEEDED + LOG_FAILED) == "failed"


class TestPreconditions:
    def test_this_checkout_is_correctly_identified(self, tmp_path):
        """A `.git` file means a worktree; a `.git` directory means it is not."""
        ordinary = tmp_path / "ordinary"
        (ordinary / ".git").mkdir(parents=True)
        assert is_worktree(ordinary) is False

        worktree = tmp_path / "worktree"
        worktree.mkdir()
        (worktree / ".git").write_text("gitdir: elsewhere\n", encoding="utf-8")
        assert is_worktree(worktree) is True

        assert is_worktree(tmp_path / "not-a-checkout") is False

    def test_every_module_in_the_uproject_is_required(self):
        """The list must not drift from the project's own Modules list."""
        import json
        project = pathlib.Path(__file__).resolve().parents[2] / "game" / "Cataclysm.uproject"
        modules = json.loads(project.read_text(encoding="utf-8"))["Modules"]
        expected = {f"UnrealEditor-{module['Name']}.dll" for module in modules}
        assert set(REQUIRED_MODULE_LIBRARIES) == expected

    def test_a_worktree_is_told_it_is_a_worktree(self):
        """The message must name the cause, not just say something is missing.

        Built from the message function rather than from check_preconditions(),
        so it runs everywhere. Continuous integration has no engine installed, so
        going through check_preconditions() there reaches the "editor is not at"
        branch and never tests this at all.
        """
        message = missing_binaries_message(list(REQUIRED_MODULE_LIBRARIES),
                                           worktree=True)
        assert "GIT WORKTREE" in message
        assert "gitignored" in message
        assert "279" in message
        assert "game/README.md" in message
        for name in REQUIRED_MODULE_LIBRARIES:
            assert name in message

    def test_an_ordinary_checkout_is_told_to_build(self):
        """A missing build in the ordinary checkout is a different instruction."""
        message = missing_binaries_message(["UnrealEditor-Cataclysm.dll"],
                                           worktree=False)
        assert "Build.bat" in message
        assert "GIT WORKTREE" not in message

    def test_the_real_check_refuses_this_checkout_when_it_has_no_binaries(self):
        """The same call the runner makes. Skips where it cannot say anything.

        Needs both an installed engine and a checkout with no built modules,
        which is a worktree on the development machine and nowhere else.
        """
        if not EDITOR_CMD.is_file():
            pytest.skip(f"no Unreal engine at {EDITOR_CMD}")
        if not missing_module_libraries():
            pytest.skip("this checkout has its modules built, so nothing is missing")
        with pytest.raises(CannotRunEditorScript) as raised:
            check_preconditions()
        message = str(raised.value)
        assert "Binaries" in message
        assert "279" in message
        if is_worktree():
            assert "GIT WORKTREE" in message


def test_the_run_log_is_not_the_editors_own_log():
    """A running editor holds Cataclysm.log open, and deleting it raises.

    Observed: PermissionError [WinError 32] on the first end-to-end attempt.
    Reading it afterwards would also give this runner the interactive editor's
    output rather than the commandlet's.
    """
    assert RUN_LOG.name != "Cataclysm.log"
    assert RUN_LOG.parent.name == "Logs"


# ---------------------------------------------------------------------------
# What the run left changed
#
# WHY THIS EXISTS. Issue #414. Running the editor dirties the working tree in
# ways the script did not ask for: it re-saves assets it merely happened to
# load. .uasset is stored in git LFS, so an incidental re-save is a new binary
# object rather than a readable diff. It was caught once by checking the working
# tree by hand before committing, and would be very easy to miss.
#
# It also used to rewrite game/Config/DefaultEditor.ini with about 57 kilobytes
# of asset-viewer preview scene profiles. That file is gitignored as of issue
# #427 and can no longer reach this report at all, so the annotation that named
# it went with it.
# ---------------------------------------------------------------------------


class TestWhatTheRunChanged:
    def test_a_run_that_changed_nothing_reports_nothing(self):
        """Silence is the ordinary case and must stay silent.

        A report printed after every run, most of which change nothing beyond
        the script's own output, is a warning people learn to skip.
        """
        assert changes_between({}, {}) == []
        assert describe_changes([]) == ""

    def test_a_file_the_run_touched_is_reported(self):
        before = {}
        after = {"game/Config/DefaultEngine.ini": " M"}

        changed = changes_between(before, after)
        assert changed == [("game/Config/DefaultEngine.ini", " M")]

        report = describe_changes(changed)
        assert "game/Config/DefaultEngine.ini" in report
        assert "1 file(s) changed" in report

    def test_a_file_that_was_already_dirty_is_not_blamed_on_the_run(self):
        """THE ASSERTION THAT MAKES THE REPORT WORTH READING.

        Somebody part way through a change runs a generator. Everything they
        were already editing is modified before the editor starts. Reporting all
        of it would bury the one file the editor really touched, which is the
        only thing this exists to surface.
        """
        already = {"sim/cataclysm_sim/enemy_stats.py": " M",
                   "game/Source/Cataclysm/Character/CataclysmBruteCharacter.cpp": " M"}
        after = dict(already)
        after["game/Content/Enemies/Demonic/Brute/ABP_Brute.uasset"] = " M"

        changed = changes_between(already, after)
        assert changed == [
            ("game/Content/Enemies/Demonic/Brute/ABP_Brute.uasset", " M")]

    def test_a_status_that_changed_is_reported_even_though_the_path_is_not_new(self):
        """Untracked-then-written is a different fact from already-modified."""
        before = {"game/Content/Enemies/Demonic/Brute/AM_Brute_Stomp.uasset": "??"}
        after = {"game/Content/Enemies/Demonic/Brute/AM_Brute_Stomp.uasset": " M"}

        assert changes_between(before, after) == [
            ("game/Content/Enemies/Demonic/Brute/AM_Brute_Stomp.uasset", " M")]

    def test_a_binary_asset_is_flagged_as_unreviewable(self):
        """The whole reason an incidental re-save matters more than a text change."""
        for path in ("game/Content/Enemies/Demonic/Brute/ABP_Brute.uasset",
                     "game/Content/Maps/L_Sandbox.umap"):
            report = describe_changes([(path, " M")])
            assert "git LFS" in report, path

        text_only = describe_changes([("sim/cataclysm_sim/enemy_stats.py", " M")])
        assert "git LFS" not in text_only

    def test_the_report_says_how_to_discard_a_tracked_change(self):
        """A list with nothing to do about it is only half of a report."""
        report = describe_changes([("game/Config/DefaultEngine.ini", " M")])
        assert "git checkout --" in report
        assert "git clean" not in report, (
            "a run that modified only tracked files was told how to remove an "
            "untracked one, which is advice for a case that did not happen")

    def test_an_untracked_file_is_given_the_command_that_works_on_it(self):
        """`git checkout --` does nothing at all to an untracked file.

        A newly written asset is untracked, and that is the case most likely to
        be committed by accident. Sending somebody to a command that reports
        success and leaves the file in place would be worse than saying nothing.
        """
        report = describe_changes(
            [("game/Content/Enemies/Demonic/Brute/AM_New.uasset", "??")])
        assert "git clean" in report
        assert "git checkout --" not in report

    def test_a_run_with_both_kinds_is_given_both_commands(self):
        report = describe_changes([
            ("game/Config/DefaultEngine.ini", " M"),
            ("game/Content/Enemies/Demonic/Brute/AM_New.uasset", "??"),
        ])
        assert "git checkout --" in report
        assert "git clean" in report

    def test_reading_the_working_tree_gives_paths_relative_to_the_repository(self):
        """Against the real repository, so the parsing is checked on real output.

        Nothing is asserted about WHICH files are dirty -- that depends on what
        the person running the tests happens to be doing. What is asserted is
        that whatever comes back is shaped the way the report expects.
        """
        state = working_tree_state()
        for path, code in state.items():
            assert not path.startswith(("/", '"')), path
            assert "\\" not in path, f"{path} should use forward slashes"
            assert len(code) == 2, f"{path} has status {code!r}"

    def test_it_survives_git_not_being_available(self, monkeypatch):
        """A reporting aid must never stop a generator from running.

        The editor scripts are the only way to build several assets in this
        project, so a runner that refused to work without git would be a worse
        problem than the one it reports.
        """
        import run_editor_python

        def refuse(*args, **kwargs):
            raise OSError("git is not installed")

        # PATCHES THE subprocess MODULE ITSELF, because `run_editor_python`
        # imports the module rather than the function. monkeypatch puts it back
        # when the test ends, which is why this does not leak into the rest of
        # the session.
        monkeypatch.setattr(run_editor_python.subprocess, "run", refuse)
        assert working_tree_state() == {}

    def test_a_git_that_answers_with_an_error_is_not_believed(self, monkeypatch):
        """Not every failure raises, and a failed git can still print something.

        THE FAKE PRINTS A LINE ON PURPOSE. An earlier version of this test had it
        exit non-zero with empty output, and then deleting the exit-code check
        changed nothing -- the parse of an empty string returns an empty result
        either way, so the test passed against broken code. Output plus a
        non-zero code is what tells the two apart, and it is also what git really
        does when it fails part way through reading a repository.
        """
        import subprocess

        import run_editor_python

        def refuse(*args, **kwargs):
            return subprocess.CompletedProcess(
                args=[], returncode=128,
                stdout=" M game/Config/DefaultEngine.ini\n",
                stderr="fatal: not a git repository")

        monkeypatch.setattr(run_editor_python.subprocess, "run", refuse)
        assert working_tree_state() == {}, (
            "a git that reported failure was believed anyway, so a run could "
            "report changes read out of a broken answer")


# ---------------------------------------------------------------------------
# The editor's own config file stays out of git
# ---------------------------------------------------------------------------


class TestTheEditorConfigFileIsNotCommitted:
    """Issue #427.

    `game/Config/DefaultEditor.ini` held one line,
    `[/Script/UnrealEd.EditorEngine] bAllowMultiplePIEInstances=True`, and it did
    nothing. The name appears nowhere in Unreal 5.8's source, so no UPROPERTY
    could match it, and `UEditorEngine` is declared `UCLASS(config=Engine)` at
    Engine/Source/Editor/UnrealEd/Classes/Editor/EditorEngine.h:399, so it reads
    the Engine ini hierarchy rather than the Editor one. Two independent reasons
    it could not take effect.

    With that line gone the file has no content this project needs, and the
    engine rewrites it with about 57 kilobytes of asset-viewer preview scene
    profiles every interactive run. That was issue #414's noise.

    WHAT WOULD BRING IT BACK. Somebody running the editor, seeing an untracked
    file appear in a folder full of committed ones, and adding it. The ignore
    rule stops `git add -A`; this catches a deliberate `git add -f`.
    """

    CONFIG = "game/Config/DefaultEditor.ini"

    def _git(self, *arguments: str) -> subprocess.CompletedProcess:
        return subprocess.run(["git", *arguments], cwd=REPO_ROOT,
                              capture_output=True, text=True, check=False)

    def test_it_is_not_tracked(self) -> None:
        listed = self._git("ls-files", "--error-unmatch", self.CONFIG)
        if listed.returncode == 128 and "not a git repository" in listed.stderr:
            pytest.skip("not a git checkout")
        assert listed.returncode != 0, (
            f"{self.CONFIG} is tracked again. The engine rewrites it with about "
            f"57 kilobytes of preview scene profiles on every interactive editor "
            f"run, so it lands in whatever pull request is open at the time. "
            f"Issues #414 and #427.")

    def test_it_is_ignored(self) -> None:
        ignored = self._git("check-ignore", "-q", self.CONFIG)
        if ignored.returncode == 128:
            pytest.skip("not a git checkout")
        assert ignored.returncode == 0, (
            f"{self.CONFIG} is not covered by .gitignore, so the next person to "
            f"run the editor and type `git add -A` commits it. Issues #414 and "
            f"#427.")
