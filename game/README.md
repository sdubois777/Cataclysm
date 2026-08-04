# Cataclysm — Unreal project

The game. `sim/` at the repository root is a separate Python model of the empire
layer, used to derive tuning numbers; it is not part of this project and does not
build with it.

## Requirements

| | |
|---|---|
| Engine | **Unreal Engine 5.8.1** (`++UE5+Release-5.8`, changelist 56057345) |
| Compiler | Visual Studio 2022 with the **Desktop development with C++** workload |
| Windows SDK | 10.0.22621 or later |
| Git LFS | Required. Assets are stored through it; see `.gitattributes`. |

The engine bundles its own .NET 10 runtime. A system-wide .NET 10 install is not
needed, and running `UnrealBuildTool.exe` directly will fail without one — use the
batch files below, which set up the bundled runtime.

## Build

Generate Visual Studio project files. They are not committed, because
UnrealBuildTool regenerates them from the `.uproject` and the `Source` tree:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe" ^
  "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" ^
  -projectfiles -project="%CD%\Cataclysm.uproject" -game -rocket -progress
```

Build the editor:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
  CataclysmEditor Win64 Development -Project="%CD%\Cataclysm.uproject" -WaitMutex
```

Build the packaged game:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
  Cataclysm Win64 Development -Project="%CD%\Cataclysm.uproject" -WaitMutex
```

A clean build of both targets takes about 90 seconds on the reference machine
(Ryzen 7 9800X3D, 32 GB, RTX 3070). That will grow substantially as real code
lands.

## Module layout

Three modules, split so that the strategy layer stays testable on its own and
editor tooling can never reach a packaged build.

| Module | Type | What belongs in it |
|---|---|---|
| `Cataclysm` | Runtime | The primary game module. Player, combat, abilities, items, dungeons, user interface. |
| `CataclysmEmpire` | Runtime | The empire and strategy layer: the day clock, cities, surges, dungeon timers and resolution, the empire upgrade tree. |
| `CataclysmEditor` | Editor | Editor-only tooling: data import and validation, asset actions, the spreadsheet-to-DataTable pipeline. |

Two targets:

- `Source/Cataclysm.Target.cs` — the packaged game. Ships `Cataclysm` and
  `CataclysmEmpire` only.
- `Source/CataclysmEditor.Target.cs` — the editor. Adds `CataclysmEditor` on top.

### Why the empire layer is its own module

It is a port of the Python rig in `sim/`, whose rules are deliberately plain
arithmetic on plain structs so they transliterate directly. Keeping it separate
means it can be tested without combat, rendering or input, and it makes the
dependency direction explicit.

**`CataclysmEmpire` must not depend on `Cataclysm`.** The dependency runs one way:
the game module may use the empire layer, never the reverse. `CataclysmEditor`
depends on both.

## What is here

The Gameplay Ability System is wired up.
`Source/Cataclysm/Cataclysm.Build.cs` takes `GameplayAbilities`, `GameplayTags`
and `GameplayTasks` as public dependencies, and `Source/Cataclysm/AbilitySystem/`
holds an ability system component, five attribute sets (primary attributes,
vitals, combat, resistances, class resource), the damage calculation, seven
shared skill templates that the designed skills are configured from, and the
two-team friend-or-foe model. `game/docs/ability-system.md` explains where the
component lives and why.

`Content/` holds generated assets, not authored ones:

| Path | What it is |
|---|---|
| `Content/Maps/L_Sandbox.umap` | A flat test level: a floor, a directional light and sky, a player start and a navigation bounds volume. Built by `tools/generate_input_assets.py`, which owns it. The five training dummies are not in the map — `ACataclysmGameMode` spawns them in a ring at play time. |
| `Data/` | Fourteen DataTable assets imported from the workbook. See below. |
| `Input/` | The Enhanced Input mapping contexts, ten input actions and the input config data asset. Built by `tools/generate_input_assets.py`. |

## What is not here yet

- **No art assets of any kind.** The player and every enemy are engine primitive
  meshes from `/Engine/BasicShapes/`. There are no character models, animations,
  authored materials, particle systems or sounds. The asset and animation
  pipelines are still being chosen (issues
  [#17](https://github.com/sdubois777/Cataclysm/issues/17),
  [#18](https://github.com/sdubois777/Cataclysm/issues/18) and
  [#19](https://github.com/sdubois777/Cataclysm/issues/19)).
- **No procedural dungeon generation.** `L_Sandbox` is the only map (issue
  [#40](https://github.com/sdubois777/Cataclysm/issues/40)).
- **No heads-up display and no interface screens.** Nothing in the project uses
  UMG yet, so health, cooldowns and slots are invisible in a play session (issue
  [#49](https://github.com/sdubois777/Cataclysm/issues/49)).
- **No save or persistence.** There is no `USaveGame` anywhere, so a play session
  keeps nothing (issue
  [#21](https://github.com/sdubois777/Cataclysm/issues/21)).
- **No empire layer runtime.** `CataclysmEmpire` is a module with a build file
  and nothing in it; the day clock, cities and surges are still only the Python
  model in `sim/` (issue
  [#42](https://github.com/sdubois777/Cataclysm/issues/42)).

## Running the tests

The C++ tests are Unreal automation tests, under
`Source/Cataclysm/Tests/`. They need no play session and no rendering. Run them
all from this folder:

```bash
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "$PWD/Cataclysm.uproject" \
  -ExecCmds="Automation RunTests Cataclysm" \
  -unattended -nopause -nosplash -nullrhi \
  -testexit="Automation Test Queue Empty" -log
```

Narrow it by replacing `Cataclysm` with any prefix of a test name, for example
`Cataclysm.AbilitySystem` or `Cataclysm.Skills`.

**The results are not on standard output.** Redirecting the command captures only
the SDK validation banner. The run writes to `Saved/Logs/Cataclysm.log`; read the
counts from there:

```bash
grep -cE "Test Completed. Result=\{Success\}" Saved/Logs/Cataclysm.log
grep -E "Test Completed. Result=\{Fail" Saved/Logs/Cataclysm.log
grep -E "Automation Test Queue Empty" Saved/Logs/Cataclysm.log
```

**The editor cannot be open while `Build.bat` runs.** Live Coding holds the
binaries and the build refuses to start. Close the editor, build, run the tests,
reopen it.

## Regenerating the content the workbook produces

Two kinds of asset in `Content/` are generated rather than authored, and both
need the editor, because they run inside its Python interpreter. Run them from
this folder.

**The data tables.** `docs/All_Things_Cataclysm.xlsx` is turned into CSV files
under `Data/` by `tools/generate_datatables.py`, which needs no editor. Those
CSV files are the reviewable form: they are text, a pull request shows a diff of
them, and `--check` compares them. They are **not content**. Nothing in the
engine can reference one, `Data/` is not cooked, and a packaged build does not
contain them. So they are then imported as DataTable assets under `/Game/Data/`:

```bash
python ../tools/generate_datatables.py
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "$PWD/Cataclysm.uproject" -run=pythonscript \
  -script="$PWD/../tools/generate_datatable_assets.py" \
  -unattended -nopause -nosplash
```

**`-script=` must be an absolute path.** A relative one resolves from the engine's
own binaries directory rather than from this folder, and fails with
`Could not load Python file 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/tools/...'`.

**It rewrites all fourteen assets even when one CSV changed**, because a `.uasset`
carries generated identifiers that differ between runs. Check
`git status --short Content/Data/` afterwards and `git restore` every asset whose
CSV did not change, or each run adds Git LFS storage for nothing.

Run the second whenever the first changes anything. Neither script can compare
bytes to tell you it is needed, because a `.uasset` carries generated
identifiers that differ between runs. The automation test
`Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt` compares each asset
against the CSV it came from instead, and names the script to run when they
disagree.

**The input assets and the sandbox level.**

```bash
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "$PWD/Cataclysm.uproject" -run=pythonscript \
  -script="../tools/generate_input_assets.py" -unattended -nopause -nosplash
```

Both scripts overwrite every property of every asset they own, so an asset
edited by hand in the editor loses that edit on the next run.
## Regenerating after changing modules

Editing any `.Build.cs` or `.Target.cs`, or adding a module to the `.uproject`,
requires regenerating project files before building. UnrealBuildTool compiles
those files into a rules assembly, and a stale assembly produces confusing errors
that look like missing symbols.
