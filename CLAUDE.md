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

**After the work, test it thoroughly.** Not only that the change runs, but that
it does what it was supposed to do, and that neighbouring behaviour still holds.
Write tests for the feature you just built. Run `pytest` and `ruff`. Fix bugs you
find in the thing you just built, as you go, in the same change.

**Prove it rather than asserting it.** Show the command you ran and what it
printed. "Tests pass" is not evidence; the test output is. A check that cannot
fail is worthless, so when you add a guard, confirm it actually fails when the
condition it guards against is present.

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
