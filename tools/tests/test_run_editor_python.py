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
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from run_editor_python import (  # noqa: E402
    REQUIRED_MODULE_LIBRARIES,
    RUN_LOG,
    CannotRunEditorScript,
    check_preconditions,
    is_worktree,
    missing_module_libraries,
    script_outcome,
    script_started,
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

    def test_a_worktree_is_refused_with_a_message_naming_the_cause(self):
        """Skips in the ordinary checkout, where the binaries are present.

        In a worktree this is the real thing: the same call the runner makes,
        raising rather than starting an editor that would do nothing.
        """
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
