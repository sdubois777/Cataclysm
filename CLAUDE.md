# Cataclysm

An ARPG dungeon crawler with real-time empire management and roguelike
meta-progression. The game is `game/`, an Unreal Engine 5.8 C++ project.
`sim/` is a separate headless Python model of the empire layer, used to derive
the numbers the design documents leave open; it does not build with the game and
should be kept alive after systems are ported, because re-tuning there is much
cheaper than re-tuning in the editor.

## Layout

| Path | What it is |
|---|---|
| `game/` | The Unreal project. See `game/README.md` for engine version and build commands. |
| `game/Source/Cataclysm/` | Primary game module: player, combat, abilities, items, dungeons, interface. |
| `game/Source/CataclysmEmpire/` | Empire and strategy layer: day clock, cities, surges, dungeon timers. Must not depend on the `Cataclysm` module. |
| `game/Source/CataclysmEditor/` | Editor-only tooling: data import and validation. Never in a packaged build. |
| `sim/cataclysm_sim/` | The simulation package. `config.py` holds every tunable number, `world.py` the empire graph, `engine.py` the day loop, `policies.py` seven ways to play, `scoring.py` the power model. |
| `sim/experiments.py` | The tuning sweeps and the report. Slow. See below. |
| `sim/tests/` | Fast tests. This is what continuous integration runs. |
| `sim/verify_scoring_port.py` | Proves `scoring.py` still matches its source. |
| `sim/analyse_*.py` | One-off analysis scripts. |
| `docs/` | The design documents. Authoritative; edit them directly. |

## Commands

Run from the repository root unless stated otherwise.

```bash
python -m pytest                          # fast test suite, about 7 seconds
python -m ruff check .                    # lint
```

```bash
# Unreal, run from game/. The engine bundles its own .NET 10; invoking
# UnrealBuildTool.exe directly fails without a system-wide .NET 10 install.
# Use the engine batch files. game/README.md has the full commands.
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" CataclysmEditor Win64 Development -Project="$PWD/Cataclysm.uproject" -WaitMutex
```

```bash
cd sim && python -m cataclysm_sim.scoring     # power model self-test
cd sim && python verify_scoring_port.py       # compare against the real source
cd sim && python experiments.py               # full sweeps -- SLOW, see below
```

`experiments.py` runs about 25,000 simulated campaigns and takes roughly 18
minutes. Do not run it to check whether a change works; run `pytest`. Run it
when you have deliberately changed the power model, the day loop, or a tuning
constant, and you need to see what moved. Run it in the background and keep
working. Python block-buffers when redirected, so use `python -u` if you want
to watch progress.

## Rules that are easy to get wrong

**The power model is a copy, not an original.** `sim/cataclysm_sim/scoring.py`
is a port of `src/utils/calculateScores.tsx` in the separate, private
`sdubois777/DungeonSimulator` repository. That file is authoritative. Never
hand-edit the constants in `scoring.py` to change balance. To update it: copy
the values from the reference, then run `verify_scoring_port.py` and confirm it
reports every value matching. This copy has silently drifted twice before.

The reference is found by locating this checkout's repository root the way git
does and looking for a `DungeonSimulator` directory beside it, so it resolves
from a linked git worktree as well as from an ordinary checkout. Set
`CATACLYSM_SCORING_REFERENCE` to point at a specific file instead. When it is
not found the checks report a skip that names the path they tried; a skip means
the drift check did not run, not that it passed.

**`docs/` is the design, and it is authoritative.** Edit those files directly when
the design changes. They began as exports from a Google Drive folder, but as of
2026-08-02 the repository copies are the source of truth and are **not** synced
back to Drive. Treat the Drive originals as historical. A design decision is not
real until it is in `docs/`.

**One dungeon floor costs exactly one day.** Depth and time are the same axis, so
a dungeon cannot be made cheaper without also being made poorer. Resolve timers
scale with depth for the same reason. `sim/README.md` lists the rules that are
fixed by design rather than swept.

**The simulation has no runtime dependencies and should keep none.** It is pure
standard library Python. `sim/requirements-dev.txt` is for testing and linting
only.

## How to work

**Before writing code, do a deep reconnaissance.** Read the code that the change
touches and the code that calls it. Check whether the behaviour you are about to
add already exists. Look at git history when something looks odd rather than
guessing why it is the way it is. State what you found before proposing a change.
Use plan mode or subagents for anything that spans more than a couple of files,
so the reading does not crowd out the work.

**Research the genre before proposing any formula, affix or mechanic.** When the
question is how something *should* work — how attack speed combines, what an
affix is worth, how dual wielding resolves — look up how shipped games in the
genre do it before answering. Path of Exile, Last Epoch, Torchlight Infinite and
Diablo have all solved these problems in public, and a shape that survived
contact with real players is evidence in a way an invented one is not. Name the
sources in the proposal and record them in `docs/DECISIONS.md` alongside the
decision, so the next person can see why the shape was chosen and not only what
was chosen.

Do not invent a formula on the fly. The best structural decision in this project
came from exactly this route: the three-bucket damage pipeline
`(base + flat) x (1 + increases) x more1 x more2` was adopted after research
showed all three of those games use the same skeleton under different names.

Say plainly which parts the research settles and which are genuinely specific to
this game and cannot be read off another. Those still need a judgement, and it
should be labelled as one rather than presented as derived. **Always state the
single recommendation you landed on**, not only the options.

This governs structure and formula shape. The constants inside them are still
tuned against real play rather than argued to death first.

**After the work, test it thoroughly.** Not only that the change runs, but that
it does what it was supposed to do, and that neighbouring behaviour still holds.
Write tests for the feature you just built. Run `pytest` and `ruff`. Fix bugs you
find in the thing you just built, as you go, in the same change.

**Prove it rather than asserting it.** Show the command you ran and what it
printed. "Tests pass" is not evidence; the test output is. A check that cannot
fail is worthless, so when you add a guard, confirm it actually fails when the
condition it guards against is present.

**Use `tools/prove_guard.py` to do that, rather than a script written on the
spot.** Breaking a file, running the tests and writing the original bytes back is
the right procedure and it has a trap in it: Python decides whether a compiled
copy in `__pycache__` is current from the source's modification time and size,
and a break-and-restore usually changes neither, because `0.0` is the same length
as `5.0` and both writes land inside the filesystem's timestamp granularity. The
run then reads compiled code that does not match the file on disk. It goes wrong
in both directions and both produce *misleading evidence* rather than an obvious
error: a failure attributed to the wrong break, or a break the run never saw at
all, which reads as a guard that does not fire.

```python
import sys
sys.path.insert(0, "tools")
from prove_guard import break_and_run

result = break_and_run(
    {"sim/cataclysm_sim/character.py":
        lambda t: t.replace("DEFAULT_SKILL_CRIT_CHANCE = 5.0",
                            "DEFAULT_SKILL_CRIT_CHANCE = 0.0")},
    ["python", "-m", "pytest", "sim/tests", "-q"],
)
print(result.summary)   # the last line pytest printed
assert result.failed    # the guard noticed
```

That prints the name of the test that noticed:

```
FAILED sim/tests/test_character.py::test_the_default_critical_strike_chance_is_a_default_not_a_floor
```

Make the edit surgical. A blanket `text.replace("5.0", "0.0")` changes every
occurrence, and a module that then fails to import gives a collection error
instead of the failing test name, which says nothing about whether the guard
works.

It clears every `__pycache__` first, runs with bytecode writing off so the run
cannot poison the next case, and restores every file in a `finally` so a crash
does not leave the repository broken. Issue #159 has the incident that produced
it.

**For a C++ guard, use `tools/unreal_build.py` instead.** The same class of
problem exists for compiled C++ and it is worse, because the build tells you it
succeeded. Restoring a source file with a tool that preserves its modification
time — `shutil.copy2`, `cp -p` — leaves it looking older than the object built
from the broken version, so UnrealBuildTool prints `Result: Succeeded`, compiles
nothing, and the test runs against the broken binary. Reading the source and
reading the build output both say everything is fine. Issue #139 has the incident.

```python
import sys
sys.path.insert(0, "tools")
from unreal_build import prove_cpp_guard

result = prove_cpp_guard(
    {"game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp":
        lambda t: t.replace("GetWorld(), Firer, Previous, Current, BodyRadiusCm);",
                            "GetWorld(), Firer, Current, Current, BodyRadiusCm);")},
    test_prefix="Cataclysm.Skills",
)
print(result.summary)   # which tests failed, read from the log
assert result.failed    # the guard noticed
```

It breaks the files, builds, refuses to go on unless the build actually compiled
them, runs the automation tests, restores the files with a modification time
forced past the break, and rebuilds. Four builds' worth of time, so narrow
`test_prefix` to the tests that matter.

Three facts about the Unreal build and test commands that will otherwise cost a
cycle each:

- **`Build.bat` refuses to run while the editor is open.** Live Coding holds the
  binaries. Close the editor, build, run the tests, reopen it. Closing it also
  removes the `mcp__unreal__*` tools until it is back.
- **`Result: Succeeded` is not evidence that anything was built**, and grepping
  for `Result:` alone hides the reason a build failed. Read the whole tail of the
  output. `unreal_build.build()` returns which files were compiled.
- **The automation test command writes nothing useful to standard output.**
  Redirecting it captures only the software development kit validation banner.
  The results are in `game/Saved/Logs/Cataclysm.log`.
  `unreal_build.run_automation_tests()` reads that file.

**Say what did not work.** If a test fails, or you skipped part of the task, or
the evidence for a result was compromised, say so plainly and first. Do not
report partial work as finished.

## Tracking work with GitHub issues

Issues are the work log. The `gh` command line tool is authenticated; use it
rather than the web interface.

```bash
gh issue list                    # what is open
gh issue view 12                 # read one
gh issue create --title "..." --body "..." --label bug
gh issue close 12 --comment "Fixed in #34"
```

- **When you find a problem you are not fixing right now, open an issue.** This
  applies to bugs, stale numbers, missing tests, and design questions the code
  raises. Do not leave findings only in a chat message.
- **Write issues to be self-contained.** Name files and give enough context to
  act on without the conversation that produced them. Someone reading it cold
  should be able to start.
- **Close issues as the work lands**, referencing the pull request. Do not batch
  this up for later.
- **One issue, one concern.** Split a finding that has two independent causes.

## Repository conventions

- **`development` is the working branch. `main` is production.** Branch from
  `development` and open pull requests back into `development`. `main` only
  receives release merges from `development`.
- Name branches `fix/`, `feat/`, `docs/`, `setup/`, or `chore/` followed by a
  short description.
- Never commit directly to `development` or `main`. Open a pull request.
- **Closing an issue needs a manual `gh issue close`.** GitHub only honours
  `Closes #N` in a pull request body when it merges into the repository's
  *default* branch. This project merges into `development`, so it does not fire.
- Continuous integration runs lint and the fast tests on every pull request into
  either branch. Do not open one you know is failing; run `pytest` and
  `ruff check .` first.
- Explain in the pull request body what changed and why, and include the
  evidence that it works.
- Do not commit compiled Python, build output, or engine intermediates. See
  `.gitignore`.
