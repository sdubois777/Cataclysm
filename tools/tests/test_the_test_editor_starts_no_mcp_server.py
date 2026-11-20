"""The editors the tools start keep the engine's MCP server off, and the `tests` command fails when one started it.
Issue #2745.

WHY. `game/Config/DefaultEditorPerProjectUserSettings.ini` sets `bAutoStartServer=True`, so every editor process
started the Model Context Protocol server on port 8000, the test editor included. A Claude session that connected to it
during a run made the server log `LogModelContextProtocol: Error: Call to unknown method "server/discover"`, and the
engine counted that Error against whichever test was running: windows `enchant-tp` (2026-10-21) and
`empire-tree-1a-w1` (2026-11-18) each lost a test that way. `unreal_build.MCP_SERVER_OFF` is an `-ini:` override that
both editor launches now pass; `mcp_server_line` and `mcp_failure` read the run's log and fail the command when the
plugin's start-up line is there anyway.

NO EDITOR STARTS HERE. Each launch is tested with `subprocess.run` replaced by a stand-in that records the command, and
every log is a temporary file or a string; no test touches the real `game/Saved/Logs`, which another session's run may
be writing.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import run_editor_python  # noqa: E402
import unreal_build  # noqa: E402
from unreal_build import MCP_NOT_STARTED, MCP_SERVER_OFF, TestOutcome  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT_SETTINGS = REPO_ROOT / "game" / "Config" / "DefaultEditorPerProjectUserSettings.ini"

#: The plugin's start-up line as window hitline-w1's whole-suite log holds it (line 2571, 2026-11-19).
STARTED_LINE = ("[2026.11.19-20.42.24:334][  0]LogModelContextProtocol: Starting MCP server on port 8000 (override "
                "with -ModelContextProtocolPort=N).")


class EditorStarted(Exception):
    """Raised by the stand-in for `subprocess.run` once it has recorded the editor's command."""


def test_the_automation_run_starts_its_editor_with_the_mcp_server_off(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The test editor's command carries the override, and the override names the setting the project file sets.

    THE SECTION AND THE FILE ARE READ FROM THE PROJECT'S OWN SETTINGS FILE, so an override that named a section or a
    file the engine does not read would fail here rather than leave the server running: the engine matches `-ini:<Name>:`
    against the base name of the file it loads (`FConfigFile::OverrideFromCommandline`), which is this file's name
    without its "Default" prefix.
    """
    settings = PROJECT_SETTINGS.read_text(encoding="utf-8")
    section = "[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]"
    assert re.search(r"^" + re.escape(section) + r"\s*$", settings, re.M), (
        f"{PROJECT_SETTINGS.name} no longer holds {section}; the override would name a section nothing reads")
    assert re.search(r"^bAutoStartServer=True\s*$", settings, re.M), settings
    name = PROJECT_SETTINGS.stem.removeprefix("Default")
    assert MCP_SERVER_OFF == f"-ini:{name}:{section}:bAutoStartServer=False", MCP_SERVER_OFF

    log = tmp_path / "Logs" / "Cataclysm.log"
    log.parent.mkdir(parents=True)
    monkeypatch.setattr(unreal_build, "TEST_LOG", log)
    monkeypatch.setattr(unreal_build, "EDITOR_CMD", pathlib.Path(__file__))
    commands: list[list[str]] = []

    def editor(command, *args, **kwargs):
        commands.append(list(command))
        raise EditorStarted

    monkeypatch.setattr(unreal_build.subprocess, "run", editor)
    with pytest.raises(EditorStarted):
        unreal_build.run_automation_tests("Cataclysm.Anything.")
    assert len(commands) == 1, commands
    assert MCP_SERVER_OFF in commands[0], (
        f"the test editor is started without {MCP_SERVER_OFF}, so it starts the MCP server: {commands[0]}")


def test_the_editor_python_run_starts_its_editor_with_the_mcp_server_off(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The editor `run_editor_python.py` starts for the generators carries the same override. It started the server
    as the test editor did (the forge's asset run, 2026-11-19, "Starting MCP server on port 8000")."""
    script = tmp_path / "a_script.py"
    script.write_text("print('nothing')\n", encoding="utf-8")
    monkeypatch.setattr(run_editor_python, "RUN_LOG", tmp_path / "Logs" / "run_editor_python.log")
    monkeypatch.setattr(run_editor_python, "check_preconditions", lambda: None)
    monkeypatch.setattr(run_editor_python, "working_tree_state", lambda: {})
    commands: list[list[str]] = []

    def editor(command, *args, **kwargs):
        commands.append(list(command))
        raise EditorStarted

    monkeypatch.setattr(run_editor_python.subprocess, "run", editor)
    with pytest.raises(EditorStarted):
        run_editor_python.run(script)
    assert len(commands) == 1, commands
    assert "-run=pythonscript" in commands[0], commands[0]
    assert MCP_SERVER_OFF in commands[0], (
        f"the editor-Python run is started without {MCP_SERVER_OFF}, so it starts the MCP server: {commands[0]}")


def test_the_plugins_start_up_line_is_read_and_its_absence_is_said() -> None:
    """The real start-up line gives its port; a log without it, and one with only the plugin's other lines, say so."""
    assert unreal_build.mcp_server_line(f"LogInit: Display: something\n{STARTED_LINE}\nmore\n") == (
        "MCP server: started on port 8000")
    assert unreal_build.mcp_server_line(STARTED_LINE.replace("8000", "8123")) == "MCP server: started on port 8123"
    others = ("[2026.11.19-20.42.24:334][  0]LogModelContextProtocol: Tool search enabled: registered 3 meta-tools (1 "
              "toolsets discoverable via list_toolsets)\n"
              "LogPluginManager: Mounting Engine plugin ModelContextProtocol\n")
    assert unreal_build.mcp_server_line(others) == MCP_NOT_STARTED
    assert unreal_build.mcp_server_line("") == MCP_NOT_STARTED
    assert unreal_build.mcp_failure(MCP_NOT_STARTED) is None
    message = unreal_build.mcp_failure("MCP server: started on port 8000")
    assert message is not None and "port 8000" in message and "Issue #2745" in message, message


PASSED = TestOutcome(3, ("a", "b", "c"), ())
CLEAN = "Test saves: 3 written and deleted, 0 left behind"


def test_a_run_whose_editor_started_the_server_fails_though_every_test_passed(
        tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """The exit code, and the `tests` command against a stand-in run whose log holds the start-up line, fail; the same
    run without that line passes. The MCP line has no default, as the save line has none."""
    started = unreal_build.mcp_server_line(STARTED_LINE)
    assert unreal_build.exit_code_for(PASSED, CLEAN, started) == 1
    assert unreal_build.exit_code_for(PASSED, CLEAN, MCP_NOT_STARTED) == 0
    with pytest.raises(TypeError):
        unreal_build.exit_code_for(PASSED, CLEAN)  # type: ignore[call-arg]

    log = tmp_path / "Logs" / "Cataclysm.log"
    log.parent.mkdir(parents=True)
    monkeypatch.setattr(unreal_build, "TEST_LOG", log)
    monkeypatch.setattr(unreal_build, "declared_tests", lambda: ())
    monkeypatch.setattr(unreal_build, "current_commit", lambda: "0" * 40)
    passing = ("LogAutomationController: Display: Test Completed. Result={Success} Name={One}\r\n"
               "LogAutomationController: Display: ...Automation Test Queue Empty 1 tests performed.\r\n"
               f"[2026.11.19-20.50.00:000][  0]LogCataclysm: Display: {CLEAN}\r\n")

    def command(log_text: str) -> tuple[int, str]:
        def stand_in(prefix: str = "Cataclysm", *args, **kwargs) -> TestOutcome:
            log.write_text(log_text, encoding="utf-8", newline="")
            return unreal_build.parse_test_log(log_text)

        monkeypatch.setattr(unreal_build, "run_automation_tests", stand_in)
        capsys.readouterr()
        code = unreal_build.main(["tests", "--no-build"])
        return code, capsys.readouterr().out

    code, printed = command(STARTED_LINE + "\r\n" + passing)
    assert code == 1, printed
    assert "Tests: 1 tests performed, 1 succeeded, 0 failed" in printed, printed
    assert "MCP server: started on port 8000" in printed, printed
    assert "FAILED: the test editor started the engine's MCP server (started on port 8000)" in printed, printed

    code, printed = command(passing)
    assert code == 0, printed
    assert MCP_NOT_STARTED in printed and "FAILED" not in printed, printed
