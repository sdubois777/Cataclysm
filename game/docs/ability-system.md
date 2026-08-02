# Ability system: ownership and replication

What the Gameplay Ability System setup in `game/Source/Cataclysm/AbilitySystem/`,
`Character/` and `Player/` does, and why it is built this way.

Read this before changing where an ability system component lives or how it
replicates. Both decisions are expensive to reverse once content depends on them.

## Where the ability system component lives

**It depends on whether the actor survives its own death.**

| Actor | Component owner | Avatar | Why |
|---|---|---|---|
| Player | `ACataclysmPlayerState` | The pawn | The player dies and respawns at the capital repeatedly, at a cost of 5 to 15 days by difficulty. The pawn is destroyed; the player state is not. |
| Enemy | The pawn itself | The pawn | An enemy that dies is gone. Nothing needs to outlive it. |

Putting the player's component on the pawn would mean every death destroyed the
player's attributes, active effects, cooldowns and granted abilities, and every
one of those would have to be saved and restored by hand. On the player state
they simply persist.

The cost of that choice is the owner and the avatar are different objects for the
player, which is what makes initialisation order matter — see below.

## Replication mode

| Actor | Mode | Why |
|---|---|---|
| Player | `Mixed` | The owning client gets full gameplay effect data, which its own interface needs. Other clients get only tags and cues. |
| Enemy | `Minimal` | No client owns an enemy, so no client needs full effect data for one. Tags and cues are enough to drive visuals. |

`Minimal` on enemies is not a micro-optimisation. A dungeon floor can hold a great
many enemies at once, and this setting is the difference between replicating every
gameplay effect on every one of them and replicating almost nothing.

`APlayerState` replicates at 1 Hz by default, which is fine for a score and far too
slow for health bars and cooldowns. `ACataclysmPlayerState` raises it to 100 Hz.

## Where `InitAbilityActorInfo` is called, and why it is in two places

This is the most common source of hard-to-diagnose bugs in a GAS project, so it is
worth stating plainly.

For the player, the component lives on the player state and the avatar is the
pawn. Those two become available at different moments on server and client:

| Side | Call site | Reason |
|---|---|---|
| Server | `ACataclysmPlayerCharacter::PossessedBy` | The player state exists by the time possession happens. |
| Owning client | `ACataclysmPlayerCharacter::OnRep_PlayerState` | `PossessedBy` **does not run on clients**. Without this the client's component never learns its avatar. |

Omitting the client path produces a build where everything works in a single
player editor session and fails in a networked one, with no error — abilities
simply never activate and attribute-driven widgets never update.

For enemies both owner and avatar are the same actor, so there is no ordering
problem and `BeginPlay` is sufficient on both sides.

## Ability execution

Abilities default to `ServerInitiated`: they run on the server and the result
replicates. Co-op is a later milestone, but a system built single-player-shaped
is far more work to make networked than one built this way from the start.

`LocalPredicted` is worth adopting per-ability later for responsiveness on things
the player feels immediately, such as movement abilities. It requires prediction
keys to be handled correctly, so it is not a sensible default.

Instancing defaults to `InstancedPerActor`. Abilities here hold per-activation
state — charge levels, deployable handles, stacking counters — so non-instanced is
not workable, and per-execution allocates more than is needed.

## Damage goes through a meta attribute

Nothing writes to `Health` directly. Damage is applied to the `Damage` meta
attribute, and `UCataclysmAttributeSet::PostGameplayEffectExecute` converts it.

A meta attribute is not replicated and is zeroed after every execution. It exists
so that mitigation happens in exactly one place. The design has a lot of it:
armour, block chance, eight resistances capped at 70%, enemy penetration, energy
shield, and four weapon sub-type modifiers (piercing ignores 20% of armour,
slashing deals 10% more against health, blunt 10% more against armour, magic 10%
more against shields).

If each of those were applied by the effect that dealt the damage, the order of
operations would be defined in dozens of places and would drift. In one function
it can be defined once and tested.

## Granting abilities

`UCataclysmAbilitySet` is a data asset bundling abilities, always-on effects and
attribute sets. `FCataclysmAbilitySetHandles` records everything a grant produced
so it can all be removed again.

That reversibility is required, not decorative. In this design the equipped
weapons determine which skills are available, so equipping and unequipping changes
the granted set constantly. Removal has to be exact rather than a guess about what
came from where.

Granting is server-only. `GiveToAbilitySystem` returns without doing anything if
called without authority.

Attribute sets are granted before abilities and effects, because an effect applied
before its attribute set exists is silently dropped.

## What is not built yet

- **The full attribute model.** `UCataclysmAttributeSet` has health, max health
  and the damage meta attribute only. The eight primary attributes, eight
  resistances, mana, energy shield and the per-class resources are separate work.
- **Input binding.** `ECataclysmAbilitySlot` names the seven slots but nothing
  binds input to them yet.
- **Gameplay tags.** The 117 tags in the design spreadsheet are not yet generated
  into the engine's tag table.
- **Loadout selection.** The design has the player choose skills from an available
  pool and assign them to slots, rather than the weapon fixing each slot. Nothing
  implements that yet.

## Running the tests

Four automation tests cover the attribute defaults, damage routing through the
meta attribute, health clamping at both ends, and the max health floor:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  <path>\Cataclysm.uproject ^
  -ExecCmds="Automation RunTests Cataclysm.AbilitySystem" ^
  -unattended -nopause -nosplash -nullrhi ^
  -testexit="Automation Test Queue Empty" -log
```

They run in about 0.1 seconds once the editor has started, and need no play
session. They do **not** cover replication; that needs a networked play-in-editor
session with two clients and has not been automated.
