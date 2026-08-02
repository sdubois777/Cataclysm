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

- `Cataclysm.Target.cs` — the packaged game. Ships `Cataclysm` and
  `CataclysmEmpire` only.
- `CataclysmEditor.Target.cs` — the editor. Adds `CataclysmEditor` on top.

### Why the empire layer is its own module

It is a port of the Python rig in `sim/`, whose rules are deliberately plain
arithmetic on plain structs so they transliterate directly. Keeping it separate
means it can be tested without combat, rendering or input, and it makes the
dependency direction explicit.

**`CataclysmEmpire` must not depend on `Cataclysm`.** The dependency runs one way:
the game module may use the empire layer, never the reverse. `CataclysmEditor`
depends on both.

## What is not here yet

The Gameplay Ability System is not wired up. `GameplayAbilities` ships with the
engine and is present, but it is deliberately absent from the build dependencies
so that the first compile proved the project skeleton alone. That work is tracked
separately.

`Content/` is empty apart from a placeholder. There are no maps, no Blueprints
and no assets.

## Regenerating after changing modules

Editing any `.Build.cs` or `.Target.cs`, or adding a module to the `.uproject`,
requires regenerating project files before building. UnrealBuildTool compiles
those files into a rules assembly, and a stale assembly produces confusing errors
that look like missing symbols.
