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

`Content/` holds generated assets, third-party asset packs, and a place for
authored work. [`docs/content-layout.md`](docs/content-layout.md) is the full
convention: where things go, what they are called, and the three rules that are
easy to get wrong.

**Generated. A generator owns these and overwrites them wholesale.** Do not edit
them by hand.

| Path | What it is |
|---|---|
| `Content/Maps/L_Sandbox.umap` | A flat test level: a floor, a directional light and sky, a player start and a navigation bounds volume. Built by `tools/generate_input_assets.py`, which owns it. No enemy is in the map — `ACataclysmGameMode` spawns them at play time. |
| `Content/Data/` | Twenty-four DataTable assets: twenty-two imported from the design workbook, and two from the simulation package's enemy model. See below. |
| `Content/Input/` | The Enhanced Input mapping contexts, ten input actions and the input config data asset. Built by `tools/generate_input_assets.py`. |

**Not in git.**

| Path | What it is |
|---|---|
| `Content/Paragon*/` | Six free Fab character packs, 17.31 GB, supplying the art for the vertical slice enemies. Re-acquired from Fab rather than committed. [`docs/enemy-source-assets.md`](docs/enemy-source-assets.md) says which pack plays which enemy. |
| `Content/Developers/` | Unreal's per-user sandbox. Not project content. |

**Authored, and committed.**

| Path | What it is |
|---|---|
| `Content/Enemies/<Cataclysm>/<Enemy>/` | Blueprints, animation Blueprints, retargeted animations and material instances for one enemy. Only `Enemies/Demonic/Brute/` exists so far and it is still empty, because the Brute is entirely C++. |

**Work derived from a third-party pack must not be saved inside that pack's
folder.** Those folders are excluded from git, so an asset saved there is dropped
by `git add` with no error and no warning. Guarded by
`tools/tests/test_third_party_packs_are_not_committed.py`.

## What is not here yet

- **Seven characters have art. Everything else is an engine primitive.** The
  Brute wears the Paragon Rampage model, the Abyssal Warden wears GruxMolten
  from the Paragon Grux pack, the Hellhound wears IggyScorch, the Succubus
  wears SM_Countess from the Paragon Countess pack, the Gatekeeper wears
  Sevarog, and the Imp and
  the Corrupted Sentinel wear the melee and siege lane minions from the Paragon
  Minions pack; the player,
  the summoned imp and the training dummies are still primitive meshes from
  `/Engine/BasicShapes/`. **The Hellhound's mesh is two creatures**, a goblin
  riding a fire-breathing mount, because the pack holds one skeletal mesh for
  the pair and there is no separate mount to load; whether the rider should be
  hidden is issue
  [#756](https://github.com/sdubois777/Cataclysm/issues/756). **The Imp is worn
  at the size it was authored, which makes it 1.76 metres tall**, because its
  shoulders measure 63.5 cm apart against the 60 cm its designed body radius
  gives it — so the mesh already is the width the design asks for, and scaling
  it down to look small would need a walk played faster than the engine allows.
  Whether a pack of ten person-sized imps reads as a swarm is issue
  [#760](https://github.com/sdubois777/Cataclysm/issues/760). The six free
  Paragon packs that will play the seven vertical slice enemies are downloaded
  into `Content/` but are excluded from git, so on a fresh clone both fall back
  to a primitive and say so in the log. **Only the Brute has an animation
  Blueprint.** The other three play single clips instead, so their attacks are
  visible but their walks do not blend and are rate-scaled to the speed each
  creature is designed to move at. Which pack plays which enemy, and
  the animation durations measured from them, are in
  [`docs/enemy-source-assets.md`](docs/enemy-source-assets.md). There are still
  no authored materials, particle systems or sounds, and the asset and animation
  pipelines for everything Paragon does not cover are still being chosen (issues
  [#17](https://github.com/sdubois777/Cataclysm/issues/17),
  [#18](https://github.com/sdubois777/Cataclysm/issues/18) and
  [#19](https://github.com/sdubois777/Cataclysm/issues/19)).
- **A dungeon floor generates and can be walked, and nothing else about a
  dungeon exists.** `L_Dungeon` builds one floor from a seed when play begins
  and stands the player on it. The floor is a grid of four-metre cells carved
  by one of three layout families, drawn as untextured blocks, with a
  navigation mesh over it that a character can path across. **There are no
  enemies on it, the stairs down do nothing, and there is no dungeon** — no
  floor count, no boss, no timer, no empire layer. `L_Sandbox` is still where
  creatures are fought (issues
  [#40](https://github.com/sdubois777/Cataclysm/issues/40) and
  [#41](https://github.com/sdubois777/Cataclysm/issues/41)).
- **No interface screens, and only combat is visible on screen.** Nothing in the
  project uses UMG. `ACataclysmHUD` draws three things on the canvas — a bar over
  creatures that have been hurt, a floating number where each blow lands, and the
  player's own health in the bottom left corner (issue
  [#518](https://github.com/sdubois777/Cataclysm/issues/518)). Skill slots,
  cooldowns, the empire status bar, the minimap and every screen are still absent
  (issue [#49](https://github.com/sdubois777/Cataclysm/issues/49)), and the port
  of the combat overlay to UMG is issue
  [#650](https://github.com/sdubois777/Cataclysm/issues/650).
- **The game saves itself and cannot load what it saved.** Three `USaveGame`
  records in `Source/Cataclysm/Save/` are written as JSON, with a schema
  version and a migration chain (issue
  [#529](https://github.com/sdubois777/Cataclysm/issues/529)); `UCataclysmSaveWriter` writes them on a 15 second
  clock and immediately when a fight starts, a creature dies, health falls
  through half, or an item moves, and **a death is written before the frame
  ends** (issue [#750](https://github.com/sdubois777/Cataclysm/issues/750)); and a live floor can be read into a
  record and put back with every creature's damage kept (issue [#751](https://github.com/sdubois777/Cataclysm/issues/751)).
  **Nothing reads a save back at start-up**, because nothing chooses between
  starting a run and continuing one, so `ACataclysmGameMode` begins a fresh
  run each session and its files are never read.
- **Almost no empire layer.** `CataclysmEmpire` holds one thing:
  `UCataclysmDayClock`, which advances the day, counts every dungeon's resolve
  timer down and pauses the one the player is standing in (issue
  [#41](https://github.com/sdubois777/Cataclysm/issues/41)). Cities, surges, the
  consequence of a dungeon resolving and the empire upgrade tree are all still
  only the Python model in `sim/` (issue
  [#42](https://github.com/sdubois777/Cataclysm/issues/42)). Nothing in the game
  advances that clock either: `UCataclysmRunSave` carries an `int32 Day` that
  nothing computes.

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

Two of the twenty-four do not come from the workbook. `EnemyArchetypes.csv` and
`EnemyRarities.csv` are built from `sim/cataclysm_sim/enemy_stats.py`, which is
where the enemy stat block is designed and where its self-checks live. The same
generator writes them and the same `--check` compares them, so editing either
CSV by hand achieves nothing.

```bash
python ../tools/generate_datatables.py
python ../tools/run_editor_python.py tools/generate_datatable_assets.py
```

**`tools/run_editor_python.py` starts the editor for you and checks the run.** It
refuses to start when the project's compiled C++ modules are missing, and it
fails afterwards unless the editor's own log says the script both started and
finished without errors. Run the editor by hand and neither is checked. It works
with the editor open.

**It also lists every file the run left changed.** Running the editor dirties the
working tree in ways the script did not ask for: it re-saves assets it merely
happened to load. A re-saved `.uasset` is a new git LFS object rather than a
readable diff, so one committed by accident inside a pull request about something
else cannot be reviewed by anybody. The list is printed after the run, flags the
binary entries, and gives the command that discards each kind. It reports and
does not revert: the script's own output is a change to the working tree too, and
only the person who ran it knows which is which. Issue #414.

The editor also rewrites `Config/DefaultEditor.ini` with about 57 kilobytes of
asset-viewer preview scene profiles. That file is gitignored, so it does not
reach the report or a commit. Its only committed content used to be
`bAllowMultiplePIEInstances=True`, which issue
[#427](https://github.com/sdubois777/Cataclysm/issues/427) established did
nothing: the name appears nowhere in Unreal 5.8's source, and `UEditorEngine` is
declared `config=Engine` so it would not read the Editor ini anyway.

**It does not work from a git worktree, and it says so instead of doing nothing.**
The editor cannot load a project whose C++ modules are not built, `game/Binaries/`
is gitignored, so a worktree never has one. Before this check existed the editor
started, ran for about twenty seconds, wrote nothing and exited normally, and the
only sign was `tools/tests/test_datatable_assets_are_current.py` failing later
about stale assets. That was issue
[#279](https://github.com/sdubois777/Cataclysm/issues/279). To regenerate a table
while working in a worktree: make the same CSV change in the ordinary checkout,
run the generator there, copy the changed asset out of `Content/Data/` and the
whole of `Data/datatable_asset_sources.json` back into the worktree, then
`git restore game/` in the ordinary checkout to leave it clean.

**Building the worktree its own binaries is not the fix**, and neither is sharing
the ordinary checkout's through a junction. Those binaries are compiled from
`Source/`, and a worktree exists to hold a different version of that tree, so the
editor would load C++ that does not match the source beside it and report
nothing.

**If you do run the editor by hand, `-script=` must be an absolute path.** A
relative one resolves from the engine's own binaries directory rather than from
this folder, and fails with `Could not load Python file 'C:/Program Files/Epic
Games/UE_5.8/Engine/Binaries/tools/...'`.

**It rebuilds only the tables whose CSV changed**, as of issue
[#444](https://github.com/sdubois777/Cataclysm/issues/444). It used to rewrite
every asset on every run, which left a reviewer one modified binary file per
table and no way to tell which one carried the change. There is nothing to
restore afterwards, because the tables it skipped were never touched. Set
`CATACLYSM_REBUILD_ALL_DATATABLES` to any non-empty value to rebuild every asset
regardless, which is what an asset hand-edited in the editor needs.

**Close the interactive editor before running it.** An open editor holds the
`.uasset` files, and Windows then refuses the rename Unreal performs when it
saves a package, with error code 32. This is the same class of problem as
`Build.bat` refusing to run while the editor is open, but it used to be silent:
the failure reached the log only as a warning, the run reported a rebuild it had
not performed, and the record then said the asset was current when it was not.
As of issue [#587](https://github.com/sdubois777/Cataclysm/issues/587) the run
exits non-zero and names each asset it could not write. Whether it happens
depends on which assets the editor has loaded, so a run that worked yesterday
with the editor open is not evidence that it will work today.

**Check `git status --short Content/Data/` afterwards** and confirm the asset you
expected to change is in the list, not only `Data/datatable_asset_sources.json`.

Run the second whenever the first changes anything. Neither script can compare
bytes to tell you it is needed, because a `.uasset` carries generated
identifiers that differ between runs. The automation test
`Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt` compares each asset
against the CSV it came from instead, and names the script to run when they
disagree.

**Two checks tell you the second script was skipped, and they are not the same.**

| Check | Runs where | Proves |
|---|---|---|
| `Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt` | Unreal automation tests, needs the editor | The asset's rows match the CSV |
| `tools/tests/test_datatable_assets_are_current.py` | `python -m pytest`, needs nothing | The generator was run since the CSV last changed |

The second exists because nothing on a pull request runs the first: continuous
integration runs the Python tests only. It reads
`Data/datatable_asset_sources.json`, which the asset generator writes at the end
of a successful run, holding the SHA-256 of each CSV at the moment its asset was
built. That file is generated; do not edit it by hand. Issue #226 has the two
pull requests that merged with stale assets and prompted it.

**The input assets and the sandbox level.**

```bash
python ../tools/run_editor_python.py tools/generate_input_assets.py
```

Both scripts overwrite every property of every asset they own, so an asset
edited by hand in the editor loses that edit on the next run.
## Regenerating after changing modules

Editing any `.Build.cs` or `.Target.cs`, or adding a module to the `.uproject`,
requires regenerating project files before building. UnrealBuildTool compiles
those files into a rules assembly, and a stale assembly produces confusing errors
that look like missing symbols.

## Toolset plugins, and why `AllToolsets` is not all of them

Unreal 5.8 ships toolset plugins in
`Engine/Plugins/Experimental/Toolsets`. Each one exposes a slice of the editor to
an agent through Epic's Model Context Protocol server. The project enables
`ModelContextProtocol` and `AllToolsets` in `Cataclysm.uproject`.

**`AllToolsets` does not enable all of them, despite the name.** It is an
aggregator whose own `.uplugin` lists 21 toolsets. The directory holds 26 besides
the aggregator itself, so **five are outside it** and have to be named in
`Cataclysm.uproject` one at a time:

| Toolset plugin | What it exposes | Enabled here |
|---|---|---|
| `MetaHumanGenerator` | Creating and editing MetaHuman characters | **Yes.** Issue #267 |
| `ChaosClothAssetToolset` | Cloth simulation assets | No. Nothing uses cloth yet |
| `LiveCodingToolset` | Recompiling C++ from inside the editor | No. `tools/unreal_build.py` already builds, and Live Coding cannot add or remove a class |
| `MVVMToolset` | Model-View-ViewModel bindings for the interface | No. Interface work has not started; issues #49 and #136 |
| `SequencerAnimMixerToolset` | Animation mixing in Sequencer | No. The animation pipeline is undecided; issue #18 |

Each enabled plugin costs editor startup time and adds to the surface an agent
has to reason about, so they are enabled when something needs them rather than
in advance.

### What `MetaHumanGenerator` actually does

Its whole surface is nine functions: `create`, `begin_edit`, `end_edit`, and a
getter and setter each for body shape, skin tone and eye colour. It creates a
character and adjusts three things about it. It does not sculpt faces, and it does
not do hair or clothing.

It matters anyway because **a MetaHuman arrives rigged**. The asset pipeline
research on issue #17 found that the hard part of generating a character is not
the mesh but that a generated mesh has no skeleton and no skin weights. It
produces humans only, so it does nothing for the non-humanoid half of the
bestiary.

**Enabling it also enables `MetaHumanCharacter`, `MetaHumanCoreTech` and
`MetaHumanSDK`**, which are its declared dependencies and were all disabled
before. All three ship with the engine.

**A plugin change only takes effect on the next editor restart.** The plugin was
added to `Cataclysm.uproject` by editing the file; `PluginToolset.SetPluginEnabled`
does the same thing through the running editor and also needs a restart.
