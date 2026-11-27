// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "GenericTeamAgentInterface.h"
#include "CataclysmCharacterBase.generated.h"

class UAttributeSet;
class UCataclysmAbilitySet;
class UCataclysmAbilitySystemComponent;

/**
 * One thing an enemy can do, described so the brain can choose between them.
 *
 * DESCRIPTION AND EXECUTION ARE SEPARATE ON PURPOSE. This says when an ability
 * may be used; ACataclysmCharacterBase::UseEnemyAbility does it. The controller
 * needs only the description, so it can pick for any enemy without knowing what
 * any of them actually do, and a test can check the choosing without anything
 * being cast.
 *
 * NOT THE GAMEPLAY ABILITY SYSTEM, and that is worth saying out loud because
 * the player's skills are. No enemy has a granted ability, an ability set or a
 * cooldown effect, and building that is a change of its own. This is the
 * smallest thing that lets one enemy choose between three attacks by range.
 */
USTRUCT(BlueprintType)
struct FCataclysmEnemyAbility
{
	GENERATED_BODY()

	/** What it is called. For logs and test failures, not for logic. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	FName Name;

	/**
	 * How far the target must be for this to be worth using, in centimetres.
	 *
	 * A ranged attack sets this to the melee reach: there is no sense throwing
	 * a rock at something already being hit. A melee attack leaves it at zero.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float MinRangeCm = 0.0f;

	/** How far it reaches, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float MaxRangeCm = 0.0f;

	/** Seconds before it may be used again. Zero is always available. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float CooldownSeconds = 0.0f;

	/**
	 * Seconds the enemy stands committed before it lands, in which the player
	 * can walk clear. Zero lands at once.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float WindUpSeconds = 0.0f;

	/**
	 * What shape of ground marker warns of it, if any.
	 *
	 * THE SAME ENUMERATION A PLAYER SKILL USES, because the model says an enemy
	 * ability carries "exactly what a player skill row carries, so an ability
	 * can be executed by the same code and its telegraph marker drawn from its
	 * own numbers". A Strike marks a circle around the creature; a Projectile
	 * marks the lane it will fly down.
	 *
	 * None draws nothing, which is right for anything the player answers by
	 * interrupting rather than by walking out of.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	ECataclysmSkillShape Shape = ECataclysmSkillShape::None;

	/**
	 * Whether it flies in an arc and lands where it was aimed, rather than
	 * travelling flat and hitting whatever it passes on the way.
	 *
	 * IT CHANGES WHAT THE MARKER MEANS, which is why it is here rather than
	 * only inside the ability's own code. A flat projectile is warned about
	 * with a lane, because everything along the lane is in danger. One that
	 * arcs passes over all of that and endangers only where it comes down, so
	 * it is warned about with a circle there instead.
	 *
	 * Pairing a ground marker with a flat projectile is a known mistake rather
	 * than a matter of taste. Issue #459, and the sources are recorded with the
	 * decision in docs/DECISIONS.md.
	 *
	 * THE SHAPE STAYS Projectile EITHER WAY. A lob is still a projectile, and
	 * `ACataclysmEnemyController::AbilityNeedsFacing` reads that shape to decide
	 * whether the creature must be pointed at its target before it may begin,
	 * which a lob must be exactly as a flat throw must.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	bool bArcsOntoItsTarget = false;

	/**
	 * Whether its wind-up draws no marker on the ground at all. Issue #2808.
	 *
	 * A QUICK CAST SETS IT: a flat bolt fired `QuickCastSeconds` after the
	 * decision, whose warning is the bolt in flight. The owner's playtest of
	 * 2026-11-21: "sometimes enemies should be able to just shoot a fireball at
	 * you as long as you can see it". The Attack Telegraphs subsection of
	 * `docs/Cataclysm_GDD_v2.md` describes it as the second kind of attack.
	 *
	 * A FLAG RATHER THAN A ZERO RADIUS, because `ShowWindUpMarker` draws a lane
	 * for every flat Projectile from its shape alone, and because the radius is
	 * also the blast the bolt makes where it stops.
	 *
	 * THE SHAPE STAYS Projectile, so `AbilityNeedsFacing` still turns the
	 * creature to its target before the cast begins, and the shot is still aimed
	 * where the target stood when it began.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	bool bDrawsNoMarker = false;

	/**
	 * How long a quick cast takes from the decision to the bolt leaving, in
	 * seconds. A copy of `QUICK_CAST_SECONDS` in
	 * `sim/cataclysm_sim/enemy_abilities.py`, which states why: quicker than
	 * every caster monster found in Path of Exile (0.85 to 1.33 s), and the
	 * reaction allowance plus a tenth so the cast itself is long enough to see.
	 * Issue #2808.
	 */
	static constexpr float QuickCastSeconds = 0.5f;

	/**
	 * How wide the marked area is, in centimetres. A radius for a Strike, the
	 * projectile's own radius for a Projectile.
	 *
	 * THIS IS NOT A SECOND COPY OF THE ABILITY'S SIZE, and it must never become
	 * one. Fill it from the same constant the ability's own code uses. A marker
	 * that showed a different area from the one that hurts is worse than no
	 * marker, because the player would have learnt to trust it.
	 *
	 * Below one metre nothing is drawn: see
	 * ACataclysmTelegraphMarker::SmallestUsefulRadiusCm.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float MarkerRadiusCm = 0.0f;

	/**
	 * Which phase of the fight this becomes available in. 1 from the start.
	 *
	 * **A PHASE SELECTS WHICH ABILITIES ARE IN THE ROTATION AND CHANGES NO
	 * NUMBER.** That is the finding the whole design leans on, from the
	 * research recorded with issue #354 in `docs/DECISIONS.md`: across ten
	 * shipped bosses in Path of Exile and Last Epoch, not one gains damage,
	 * armour, attack speed or critical strike at a transition. Escalation is
	 * adding a named ability. So this field is the whole of what a phase does,
	 * and the two-layer rule -- rarity scales magnitude, archetype sets
	 * behaviour -- survives a multi-phase boss untouched.
	 *
	 * **PHASES ADD, THEY NEVER TAKE AWAY.** An ability available from phase N
	 * stays available in every later phase, so
	 * `ACataclysmEnemyController::ChooseAbility` asks whether this is at most
	 * the creature's current phase rather than equal to it.
	 *
	 * ONE IS THE DEFAULT AND EVERY CREATURE BUT THE BOSS LEAVES IT THERE. A
	 * creature with no phases is a creature permanently in phase 1, which
	 * costs one comparison and needs no special case anywhere.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 Phase = 1;

	/**
	 * Whether this entry IS the creature's basic attack, which a silence leaves usable. Ruled 2026-10-17 by the
	 * owner (O1): a silenced creature loses every ability except its basic attack.
	 *
	 * THREE CREATURES' BASIC ATTACKS ARE ENTRIES HERE, because each is telegraphed: the Corrupted Sentinel's Siege
	 * Bolt, the Gatekeeper's Dread Cleave and the Succubus's Soulfire. Each of the three overrides `AttackTarget` to
	 * do nothing, so without this a silence would leave them no attack at all. Every other creature's basic attack is
	 * its `AttackTarget` swing, which silence does not reach, and leaves this false on every entry.
	 *
	 * FALSE IS THE DEFAULT, so an ability added later is silenced unless its creature says otherwise.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	bool bIsBasicAttack = false;
};

/**
 * Shared base for anything with abilities: the player and every enemy.
 *
 * Does not own an ability system component. Where the component lives differs
 * between the player (player state, survives respawn) and enemies (the pawn
 * itself), so ownership is decided by the subclass and reached through
 * GetAbilitySystemComponent().
 *
 * DOES carry the side it is on, and here rather than in the subclasses, because
 * both need one and a character with no side is hostile to everything including
 * its own kind. Each subclass sets the value in its constructor; see
 * `ECataclysmTeam`.
 */
UCLASS(Abstract)
class CATACLYSM_API ACataclysmCharacterBase : public ACharacter,
											  public IAbilitySystemInterface,
											  public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ACataclysmCharacterBase();

	/**
	 * How large one of the engine's basic shapes is, in centimetres.
	 *
	 * `/Engine/BasicShapes/Cylinder` and the rest occupy a 100cm cube, so a
	 * scale of 1 is 100cm and a component sized to a capsule has to divide by
	 * this. Both the player and every enemy build a stand-in body from those
	 * shapes because this project's Content folder holds no meshes at all.
	 *
	 * HERE RATHER THAN IN EACH .cpp, and that is not tidiness. It was a
	 * `constexpr` in an anonymous namespace in two different .cpp files, and
	 * Unreal's unity build concatenates .cpp files into one translation unit --
	 * so the two anonymous namespaces merged and the second definition was a
	 * redefinition error. It compiled only while the build happened to put the
	 * two files in different unity chunks, which is not something a change can
	 * control.
	 */
	static constexpr float BasicShapeSize = 100.0f;

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface

	// ----------------------------------------------------------------------
	// What ACataclysmEnemyController needs in order to drive this character.
	//
	// FIVE VIRTUALS ON THE SHARED BASE RATHER THAN AN INTERFACE, and the reason
	// is that the controller has to work for two classes that have nothing else
	// in common -- a monster and a summoned imp -- and both already derive from
	// here. The defaults make the base inert: a reach of zero means a character
	// is never in range of anything, a roam radius of zero means it never
	// wanders, and AttackTarget does nothing. The player character inherits
	// those defaults and is possessed by a player controller rather than this
	// one, so nothing drives it.
	// ----------------------------------------------------------------------

	/** How close it must be to hit something, in centimetres. Zero never reaches. */
	virtual float AttackReachCm() const { return 0.0f; }

	/** How far it notices a target, in centimetres. Zero notices nothing. */
	virtual float SightRadiusCm() const { return 0.0f; }

	/**
	 * What this character multiplies the distance it notices a target by.
	 *
	 * ONE UNLESS THE FLOOR IT IS STANDING ON SAYS OTHERWISE.
	 * `ACataclysmDungeonGameMode::PopulateFloor` sets it from
	 * `FCataclysmFloorBrief::SightRadiusMultiplier` as each creature is placed,
	 * which is how a Horde dungeon's much larger aggro range reaches its
	 * creatures without any creature's own default being touched.
	 *
	 * **HERE AND NOT ON `ACataclysmEnemyCharacter`, BECAUSE THAT IS NOT WHERE
	 * THE PROBLEM WAS.** `ACataclysmEnemyCharacter::SightRadiusCm` returns its
	 * `NoticeRadiusCm`, but five of the seven designed creatures OVERRIDE
	 * `SightRadiusCm` to return their own compile-time constant and never read
	 * that field at all -- the Imp, the Hellhound, the Succubus, the Corrupted
	 * Sentinel and the Gatekeeper. Setting `NoticeRadiusCm` on a spawned Imp
	 * changes nothing it does. A separate multiplier applied where the radius is
	 * READ works for all seven and cannot be defeated by an override.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Character")
	float SightRadiusMultiplier = 1.0f;

	/**
	 * When this character came under a commander's command, in world seconds:
	 * a minion when it was summoned, a thrall when it was subjugated. -1 for one
	 * never commanded. What A Second Self asks for "the minion you have held
	 * longest". Issue #1515.
	 */
	float CommandedSinceSeconds = -1.0f;

	/** Whether this is its commander's Second Self. Set by
	 *  `UCataclysmSecondSelf::Step`, and it stays set while it stands. */
	bool bIsSecondSelf = false;

	/**
	 * Whether no blow may kill this character just now, because Subjugate is taking it. Issue #2856, the owner's
	 * ruling of 2026-11-22: "ok, well it should still take them even if it would kill".
	 *
	 * SET ONLY AROUND SUBJUGATE'S OWN BLOW, AND ONLY ON A TARGET IT COULD TAKE: `UCataclysmSummonSkill::Possess` sets
	 * it before the blow and clears it after, when nothing would refuse the take but the blow's own outcome. Read by
	 * `UCataclysmCommand::IsHeldAliveForATake`, which `UCataclysmVitalAttributeSet` asks where it holds a creature in
	 * a friendly Unholy Sigil at one health. False at every other moment, so nothing else is ever held by it.
	 */
	bool bHeldAliveForATake = false;

	/**
	 * Which rows of `game/Data/EnemyModifiers.csv` this character carries as its own.
	 *
	 * ON EVERY CHARACTER AND NOT ONLY ON A CREATURE, since issue #2899 (the owner's ruling of 2026-11-25: "Everything
	 * that isn't a new ability that would require a button push should work for a player"). A creature's list is drawn
	 * by its rung (`ACataclysmEnemyCharacter::DrawModifiersForRarity`, issue #742) and shown by the hover panel; a
	 * player's is empty, and a player wears modifiers lent by Wearer of the Dead instead, which its ability system
	 * records (`UCataclysmAbilitySystemComponent::NoteLentModifier`). An effect that works on any wearer asks
	 * `UCataclysmEnemyModifiers::CarriedBy`, which reads both, and never this list alone.
	 *
	 * Until issue #2899 this list was on `ACataclysmEnemyCharacter`, where it began as names the panel printed and
	 * nothing more (issue #740); the effects that are built since are each said beside their numbers in
	 * `UCataclysmEnemyModifiers`, and a name whose effect is not built still does nothing.
	 *
	 * EditInstanceOnly, as a creature's rung is: which modifiers a creature carries is the encounter's business, and a
	 * list baked into a Blueprint would be a class-wide answer to a per-encounter question. ANYTHING TYPED IS KEPT AND
	 * THE DRAW TOPS UP AROUND IT.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<FName> ModifierRows;

	/**
	 * What the Death modifiers keep on whoever wears them, a creature or a player (issue #2899, 2026-11-26; they were
	 * on `ACataclysmEnemyCharacter` from 2026-11-24 until then). PUBLIC so a test can drive the rules, statics on
	 * `UCataclysmEnemyModifiers`, a step at a time. NONE IS SAVED: a floor restored from a save starts each clock again.
	 *
	 * Spectral Shift's clock and the seconds it has left out of existence; Shadow Step's seconds left seen and whether
	 * it is Shadow Step that hid a creature (so it never shows one the vision system hid; a player is never hidden from
	 * its own screen, only from creatures' sight); Lingering Decay's clock; when Funeral Pyre last placed a pyre,
	 * negative until the first; and Undying Will on a wearer that is not a creature: whether its one rising is spent
	 * and whether it has risen, which its blows read (a creature rises as a new creature the dungeon places instead).
	 */
	float SecondsSinceSpectralShift = 0.0f;
	float SpectralShiftSecondsLeft = 0.0f;
	float ShadowStepSeenSecondsLeft = 0.0f;
	bool bHiddenByShadowStep = false;
	float SecondsSinceDecayZone = 0.0f;
	float FuneralPyreLastAt = -1.0f;
	bool bUndyingWillSpent = false;
	bool bRoseThroughUndyingWill = false;

	/**
	 * How far it actually notices a target, once its floor's multiplier is
	 * applied. This is what the controller asks, and `SightRadiusCm` is the
	 * creature's own figure that it scales.
	 *
	 * NOT VIRTUAL, ON PURPOSE. A subclass that overrode this could go back to
	 * ignoring the multiplier, which is the exact fault the multiplier exists to
	 * route around.
	 *
	 * A NEGATIVE MULTIPLIER IS CLAMPED TO ZERO rather than turned into a
	 * negative radius, because "notices nothing" is a real answer and a negative
	 * distance is not.
	 */
	float NoticesFromCm() const
	{
		return SightRadiusCm() * FMath::Max(0.0f, SightRadiusMultiplier);
	}

	/** Seconds between one of its attacks and the next. */
	virtual float SecondsBetweenAttacks() const { return 1.0f; }

	/**
	 * Which phase of its fight this character is in. One unless it says
	 * otherwise.
	 *
	 * HERE RATHER THAN ON THE ENEMY, so the controller can ask any character
	 * it drives without a cast, the same arrangement the other five hooks on
	 * this class use. A character with no phases answers 1 for ever and every
	 * ability, whose default phase is also 1, is available to it.
	 */
	virtual int32 CurrentPhase() const { return 1; }

	/**
	 * Its health just changed. Inert here.
	 *
	 * THE SAME SHAPE AS `HandleDeath`, and called from the same two places in
	 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` -- every write
	 * to health goes through one of them. A creature with health-triggered
	 * phases overrides this; everything else pays one virtual call per hit.
	 *
	 * WHY NOT FROM Tick. A phase decides which ability the brain may choose,
	 * and the brain thinks on its own schedule, so a phase that arrived a
	 * frame late could be a phase that arrived after the choice it should
	 * have changed. Noticing on the hit removes the question.
	 */
	virtual void HealthChanged() {}

	/**
	 * How far from where it started it wanders with nothing in sight, in
	 * centimetres. Zero never roams, which is the default.
	 *
	 * OPT-IN RATHER THAN OPT-OUT, AND THAT IS A BEHAVIOUR DECISION RATHER THAN
	 * A STYLE ONE. Two of the three characters this controller drives should
	 * not wander. A summoned imp exists to fight what its summoner is fighting,
	 * and one that strolled off between fights would be a bug rather than a
	 * feature. A Corrupted Sentinel is designed stationary. Making the base
	 * inert means adding roaming changed the behaviour of exactly the one
	 * character that asked for it, and every existing test that asserts a
	 * monster with nothing in sight is Idle still passes unedited.
	 */
	virtual float RoamRadiusCm() const { return 0.0f; }

	/**
	 * Whether this character stays where it was put instead of walking.
	 *
	 * THE DEPLOYED-GADGET HALF OF A DISTINCTION THE DESIGN ALREADY MAKES.
	 * `docs/Cataclysm_GDD_v2.md`, under "Count applies to summons, not to
	 * deployables": "A summon spawns at the caster and walks... A deployable
	 * is placed in a pattern." A bolt turret, a ballista and a spike trap
	 * all state a move speed of zero in
	 * `game/Data/MinionTypes.csv` and `ACataclysmMinion::Spawn` turns that
	 * into this answer; an imp states 4.4 and a mote 5.5, so both answer
	 * false.
	 *
	 * FALSE HERE, WHICH IS THE ANSWER FOR EVERY MONSTER AND FOR THE PLAYER.
	 * Only a deployed gadget says otherwise.
	 *
	 * HERE RATHER THAN ON THE MINION, so the controller can ask any character
	 * it drives without a cast -- the same arrangement the hooks above use.
	 */
	virtual bool StaysWhereItIsPut() const { return false; }

	/**
	 * True when this character must start no hostile action at all.
	 *
	 * WHAT IT STOPS, AND WHERE. Its own swing and its abilities, refused in
	 * `ACataclysmEnemyController`; and the modifier auras and the charge,
	 * refused in `UCataclysmEnemyModifiers`. Those are two separate systems
	 * and a creature has to be silent in both, which is why the answer lives
	 * here rather than in either of them.
	 *
	 * WHAT IT DOES NOT STOP, DELIBERATELY. Moving, turning and being targeted;
	 * and anything that fires because this character was HIT -- retaliation,
	 * the brand that goes off after enough blows land, the charm on whoever
	 * struck. **Responding to being attacked is not attacking**, and a
	 * creature that cannot be safely hit is a different thing from one that
	 * attacks. Issue #1680 records that reading.
	 *
	 * FALSE HERE, WHICH IS THE ANSWER FOR THE PLAYER AND FOR EVERY CREATURE
	 * BUT FOUR. A floor's Field Medic, Demonic Guide's guide, the Plaguebearer
	 * and an echo of Echoes of the Past say otherwise
	 * (`ACataclysmEnemyCharacter::TakesNoHostileAction`). This comment said
	 * "but one" until issue #2740, two rules after it had stopped being true.
	 *
	 * NOT AN ATTACK A RULE ORDERS. The game mode makes an echo's one attack
	 * (`StrikeAsARuleOrders`, `UseEnemyAbility`), and neither asks this.
	 *
	 * HERE RATHER THAN ON THE ENEMY, so the controller can ask any character
	 * it drives without a cast -- the same arrangement `StaysWhereItIsPut`
	 * above uses and for the same reason.
	 */
	virtual bool TakesNoHostileAction() const { return false; }

	/**
	 * Whether this character can shoot at `Target` from where it stands: nothing solid between them. The brain uses no
	 * ability and makes no attack on a target it has no clear shot at, and closes on it instead.
	 *
	 * TRUE FOR EVERY CHARACTER BUT ONE THAT SAYS OTHERWISE, so nothing that fought before 2026-11-27 fights differently.
	 * The Corrupted Sentinel says otherwise since the owner's ruling that it walks ("Walk everywhere", 2026-11-27): it
	 * walks toward the player, stops when it has a clear shot within its bolt's range, and walks again when it loses one.
	 * HERE RATHER THAN ON THE ENEMY, for the reason `TakesNoHostileAction` gives.
	 */
	virtual bool HasAClearShotAt(const AActor* Target) const { return true; }

	/**
	 * True when this character is nobody's target: no search finds it, on any side, and no blow lands on it. Issue
	 * #2860.
	 *
	 * FALSE HERE, WHICH IS THE ANSWER FOR THE PLAYER AND FOR EVERY CREATURE BUT ONE. Demonic Guide's guide says
	 * otherwise (`ACataclysmEnemyCharacter::IsNeverATarget`). The owner's play-test of 2026-11-23: "sometimes he
	 * manages to die. My minions are constantly trying to attack him even though they usually do 0 damage."
	 *
	 * WHERE IT IS ASKED. `UCataclysmTargeting::MatchesAttitude` and that file's side-less search refuse it, so it is
	 * neither an enemy nor an ally of anybody: a minion's or a thrall's choice of target, a skill's search, a click and
	 * a floor rule's sweep all go through there. `UCataclysmSkillEffects::IsUntargetable` answers true for it, so a
	 * blow aimed at it directly lands nothing. `ACataclysmEnemyCharacter::HandleDeath` refuses it, so a rule that kills
	 * by calling the death outright does not kill it either.
	 *
	 * NOT `TakesNoHostileAction`, which is what a character does. This is what may be done to it.
	 */
	virtual bool IsNeverATarget() const { return false; }

	// ----------------------------------------------------------------------
	// Which bodies do not block one another. Issue #2861.
	//
	// The owner's play-test of 2026-11-23: "He can't walk through my minions, and neither can I, which is annoying.
	// There shouldn't be collision between player and minions." ("He" is Demonic Guide's guide.)
	//
	// THE PLAYER, WHAT THE PLAYER COMMANDS AND THE GUIDE PASS THROUGH ONE ANOTHER, AND EVERY OTHER PAIR IS AS IT WAS:
	// a creature still blocks all three and all three still block a creature, and two commanded bodies still block
	// each other. Path of Exile 2 lets a summoner walk through its own minions, and Last Epoch's patch 1.3.3 fixed a
	// minion blocking the player's movement as a bug; `docs/DECISIONS.md` has the sources.
	//
	// BY THE ENGINE'S MASK FILTER, NOT BY A COLLISION CHANNEL, a judgement. A body carries one of the three bits below
	// as its capsule's mask filter, and ignores the other two when it moves (`UPrimitiveComponent::SetMoveIgnoreMask`):
	// a moving body skips a body whose mask filter shares a bit with its own move-ignore mask. Every movement sweep the
	// character movement component makes -- the move, the floor check, the push out of an overlap -- reads that mask.
	// A channel would mean giving the capsule another object type, and every target search in the game asks for
	// `ECC_Pawn` by object type (`UCataclysmTargeting::Gather`, `UCataclysmTargetCandidates::CapsuleReachesInto`), so
	// a minion moved onto another channel would stop being found by the creatures that fight it.
	// ----------------------------------------------------------------------

	/** The bit the player's capsule carries. */
	static constexpr uint8 PlayerBodyMaskBit = 1;

	/** The bit a minion's or a thrall's capsule carries: anything on the player's side that is not a player. */
	static constexpr uint8 CommandedBodyMaskBit = 2;

	/** The bit Demonic Guide's guide's capsule carries. */
	static constexpr uint8 GuideBodyMaskBit = 4;

	/** All three, which is what a body carrying one of them moves through, less its own. */
	static constexpr uint8 PassThroughBodyMaskBits = PlayerBodyMaskBit | CommandedBodyMaskBit | GuideBodyMaskBit;

	/**
	 * Which of the three bits this character's capsule carries, or nought for a body every body blocks.
	 *
	 * HERE: `CommandedBodyMaskBit` for a character on the player's side, so a summoned minion and a thrall answer it
	 * from the side they are on, and nought for every other. `ACataclysmPlayerCharacter` answers `PlayerBodyMaskBit`
	 * and `ACataclysmEnemyCharacter` answers `GuideBodyMaskBit` for the floor's guide.
	 */
	virtual uint8 PassThroughBodyMaskBit() const;

	/** What a body carrying `OwnBit` moves through: the other two of the three, or nothing for a bit of nought. */
	static uint8 MovesThroughMaskFor(uint8 OwnBit);

	/**
	 * Writes the capsule's mask filter and move-ignore mask from `PassThroughBodyMaskBit`.
	 *
	 * CALLED WHEREVER THE ANSWER CAN CHANGE: at `BeginPlay`, so the player and every creature start right; from
	 * `SetGenericTeamId`, so a summoned minion takes its summoner's side's body and a thrall changes body with the
	 * side it is taken to (and back, should anything ever give it back); and by `ACataclysmDungeonGameMode::
	 * PlaceTheGuide` once it has made a creature the floor's guide.
	 */
	void RefreshPassThroughBody();

	/**
	 * A HOSTILE CREATURE'S BODY, 8: the Shadow's Hollow Step, the Death trees' slice C6e (2026-11-25), as ruled (H1).
	 * Put on the capsule's mask filter of a creature on the monsters' side whose own pass-through bit is nought, and
	 * NOT one of `PassThroughBodyMaskBits`, so nothing moves through it unless `SetMovesThroughHostiles` says so, and
	 * the creature itself still moves through nothing (`MovesThroughMaskFor(0)` is nought).
	 */
	static constexpr uint8 HostileBodyMaskBit = 8;

	/**
	 * While true, this character's moves pass through hostile creatures as well as through what its own bit already
	 * passes through: Hollow Step's movement skill, for the length of the move only. False restores the mask
	 * `RefreshPassThroughBody` writes.
	 */
	void SetMovesThroughHostiles(bool bThrough);

	/** Whether this character's moves pass through hostile creatures now. */
	bool MovesThroughHostiles() const;

	/** Hit the given target once. Called by the controller when it is in reach. */
	virtual void AttackTarget(AActor* Target) {}

	/**
	 * Called once when this character's health first reaches zero.
	 *
	 * INERT ON THE BASE, AND THAT IS NOW ONLY ABOUT MINIONS. The enemy override
	 * marks the creature dead and removes it from the level (issue #517); the
	 * player override marks the player dead, stops them, and stands them back up
	 * after a delay (issue #570). ACataclysmMinion is the one subclass left with
	 * neither, and it leaves the level by its own lifespan rather than by damage.
	 *
	 * WHAT A PLAYER'S DEATH STILL OWES, and it is why the player override stops
	 * where it does: the designed penalty is days off the empire clock, a
	 * per-piece equipment drop chance and a respawn at the capital, and the
	 * running game has no day clock, no equipped inventory and no capital. The
	 * corruption cost and the Last Stand mechanic (#43) are not designed at all.
	 *
	 * CALLED FROM UCataclysmVitalAttributeSet::PostGameplayEffectExecute, which
	 * is the one place in the project that knows a hit has landed and what it
	 * left behind.
	 */
	virtual void HandleDeath() {}

	/**
	 * Show this character using a skill. Issue #1126.
	 *
	 * CALLED FROM `UCataclysmSkillTemplate::CommitAndBegin`, which every one of
	 * the eight skill shapes calls first, so one call there reaches all of them
	 * and the basic attack too. That is the same place and the same reasoning as
	 * `UCataclysmCastEffect::PlayFor`, which issue #811 put there to give every
	 * skill the beat at the caster that none of them had.
	 *
	 * A VIRTUAL RATHER THAN A HELPER, UNLIKE THE CAST EFFECT, because the clip
	 * depends on the skeleton and the skeletons differ. The player wears the
	 * Mannequin; `ACataclysmHellhoundCharacter` wears a Paragon body and also
	 * uses a skill template, so a Mannequin clip played on it would be an
	 * animation for a skeleton it does not have.
	 *
	 * DOING NOTHING IS THE CORRECT DEFAULT AND NOT AN OMISSION. Every enemy that
	 * plays an attack animation already does it from its own class, in
	 * `AttackTarget` or `UseEnemyAbility`, with clips from its own pack. This
	 * exists for the player, which had no such path because until issue #1124 it
	 * had no skeleton to play anything on.
	 */
	virtual void PlayAttackAnimation() {}

	/**
	 * How long from now until the swing just started lands its blow.
	 *
	 * WHY IT IS A STORED ANSWER RATHER THAN A CALCULATION. Issue #1133. The
	 * figure depends on which clip was chosen, and the player cycles through
	 * three of different lengths, so only `PlayAttackAnimation` knows. It works
	 * that number out and leaves it here for `UCataclysmSkillTemplate` to read
	 * on the very next line.
	 *
	 * ZERO IS THE CORRECT DEFAULT AND MEANS "NOW". A character that played no
	 * animation has no swing to wait for, so its blow lands in the activation
	 * frame exactly as every blow in the game did before issue #1133. That
	 * covers every enemy, which animates from its own class and never sets
	 * this, and it covers a checkout with no animation assets, and it covers
	 * every automation test, which runs in a world that is never ticked.
	 *
	 * READ IT ONLY STRAIGHT AFTER `PlayAttackAnimation`. It is not cleared
	 * afterwards, so it holds whatever the last swing decided.
	 */
	float SecondsUntilTheSwingConnects() const
	{
		return SwingConnectsInSeconds;
	}

	/**
	 * What this character can do beyond its ordinary attack, in the order it
	 * would rather use them. Empty means it only has AttackTarget.
	 *
	 * ORDER IS PRIORITY, and the caller takes the first entry that is in range
	 * and off cooldown. Put the ability you would rather use first.
	 *
	 * THE ORDINARY ATTACK IS NOT IN HERE, deliberately. It has no cooldown --
	 * the attack interval is a rate limit, not a cooldown -- so it is always
	 * available and is what happens when nothing in this list qualifies. That
	 * is the property that stops an enemy standing in reach doing nothing
	 * because everything it owns is cooling down, and keeping the fallback out
	 * of the list is what makes it structural rather than a rule to remember.
	 */
	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const { return {}; }

	/**
	 * Do the ability at the given index of EnemyAbilities.
	 *
	 * @param Index    into EnemyAbilities, already checked by the caller
	 * @param Target   what it was aimed at, which may since have moved
	 * @param AimedAt  where it was aimed when the wind-up started. A telegraphed
	 *   attack lands where it was marked, not where the target is now, which is
	 *   what makes walking out of one work.
	 */
	virtual void UseEnemyAbility(int32 Index, AActor* Target,
								 const FVector& AimedAt) {}

	/**
	 * Called when an ability's wind-up begins, so the character can start the
	 * animation that goes with it. Does nothing by default.
	 */
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) {}

	// ----------------------------------------------------------------------
	// Health, mana and energy shield coming back over time. Issue #653.
	//
	// ON THE SHARED BASE BECAUSE ALL THREE CHARACTERS CARRY THE SAME POOLS and
	// none of them is special: a player, a monster and a summoned minion each
	// have a vital attribute set with the same three regeneration attributes on
	// it. The arithmetic and the design's rules live in UCataclysmRegeneration.
	// What is here is only the clock that drives them and the record of when
	// this character was last hurt.
	// ----------------------------------------------------------------------

	/**
	 * Records that this character has just taken damage, for the energy
	 * shield's refill delay.
	 *
	 * CALLED FOR EVERY HIT THAT TOOK SOMETHING, including one an energy shield
	 * absorbed entirely and including a damage over time tick. The design
	 * requires both: the shield "refills 3 seconds after the character last took
	 * damage", taking damage again inside that window restarts the wait, and
	 * damage over time restarts it as well.
	 *
	 * A hit that was evaded, or that armour and resistance stopped completely,
	 * is not damage taken and does not restart the wait.
	 */
	void NoteDamageTaken();

	/**
	 * Seconds since this character last took damage.
	 *
	 * A LARGE NUMBER RATHER THAN ZERO WHEN IT HAS NEVER BEEN HURT, because zero
	 * would read as "hurt this instant" and would stop the shield ever filling
	 * on a character nothing has touched.
	 */
	float SecondsSinceLastDamage() const;

	/**
	 * Whether the regeneration clock is running on this character.
	 *
	 * EXISTS FOR A TEST, AND THE TEST IS WORTH THE ACCESSOR. Every other check
	 * on this feature calls UCataclysmRegeneration::ApplyStep directly, which is
	 * what the timer calls -- so all of them would still pass if the timer were
	 * never started and nothing regenerated in the running game at all. That is
	 * precisely the failure this feature exists to fix, so it needs its own
	 * guard. A world built by UWorld::CreateWorld is never ticked, so a test can
	 * ask whether the timer is running but can never watch it fire.
	 */
	bool IsRegenerating() const;

	/**
	 * The point this character was frightened away from. Written by
	 * `UCataclysmFear::ApplyFear` and read while `State.Feared` lasts; meaningless
	 * without the tag.
	 */
	FVector FearSource = FVector::ZeroVector;

	/**
	 * The Fervour this character holds back from its commander's pool while it
	 * is commanded, or zero. Issues #1160 and #1934.
	 *
	 * FIXED WHEN IT IS MADE OR TAKEN, by whatever made it, each after Crowned
	 * through `UCataclysmCommand::ReserveFor`: Summon Imp's `FervourReserve` for a
	 * summoned or risen imp, Subjugate's for a thrall, and since issue #2787 its
	 * kind's own figure for a necrosis imp, a Warhound, a Tyrant's Minion, a skill
	 * gadget and a skill trap. A Second Self's chosen one counts twice when the
	 * reserve is totalled, in `UCataclysmCommand::ReservedFervourOf`, and not here.
	 *
	 * ZERO FOR A MINION SPAWNED DIRECTLY, which nothing in play does, and for a
	 * War gadget, which the owner's no-War mandate leaves to issue #1934.
	 */
	float ReservedFervour = 0.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Put the model at `BodyMeshPath` on this character's mesh component, if it
	 * has none yet. Issue #1542.
	 *
	 * FOR `PreRegisterAllComponents`, WHICH RUNS BEFORE ANY OF THE ACTOR'S
	 * COMPONENTS REGISTER. A component attached to a bone or socket of the mesh
	 * -- the Brute's held rock, the player's two weapons -- works out where it is
	 * when it registers, by asking the mesh where that bone or socket is. A mesh
	 * with no model cannot answer, and the engine logs "GetSocketInfoByName(...):
	 * No SkeletalMesh for Component" each time it is asked. Counted on
	 * 2026-09-10, while the model arrived only in BeginPlay: 5,260 of those for
	 * 1,728 Brutes in one run, and four for each of the player's two sockets in
	 * each of the two runs counted.
	 *
	 * ONLY THE MODEL. Everything else a character does with its body -- where the
	 * mesh sits on the capsule, its animation, its clips -- stays in BeginPlay.
	 * BeginPlay still calls SetSkeletalMesh with the same model, and
	 * USkeletalMeshComponent::SetSkeletalMesh returns at once for the mesh it
	 * already holds.
	 *
	 * ONLY IN A GAME WORLD -- a game, a Play-In-Editor session, a test world. An
	 * editor level is left alone, so that a character placed in one is not saved
	 * with a hard reference to its art.
	 *
	 * NOTHING HAPPENS WHEN THE MODEL IS NOT THERE, as on any checkout without the
	 * art pack. BeginPlay's own load says so in the log.
	 */
	void WearBodyBeforeComponentsRegister(const TCHAR* BodyMeshPath);

	/**
	 * Set by `PlayAttackAnimation` in whichever class overrides it, and read
	 * back through `SecondsUntilTheSwingConnects`. Issue #1133.
	 *
	 * ZERO UNTIL SOMETHING SETS IT, which is what every enemy leaves it at.
	 */
	float SwingConnectsInSeconds = 0.0f;

	/** One step of regeneration. Driven by RegenerationTimer. */
	void RegenerationStep();

	/**
	 * Called at the end of every `RegenerationStep`. Issue #1821.
	 *
	 * EMPTY HERE. `ACataclysmPlayerCharacter` asks for its movement speed again
	 * from it, because a speed row can depend on time passing or on where other
	 * bodies are, and neither announces itself. A quarter of a second late is
	 * the same allowance the health debt on this step takes.
	 */
	virtual void AfterRegenerationStep() {}

	/** Fires every UCataclysmRegeneration::StepSeconds while alive. */
	FTimerHandle RegenerationTimer;

	/**
	 * World time when this character last took damage, in seconds. Negative
	 * means it never has, which is what SecondsSinceLastDamage turns into a
	 * large number rather than into a small one.
	 */
	float LastDamagedAtSeconds = -1.0f;

	/**
	 * Which side this character is on. Set in the subclass constructor.
	 *
	 * NOT REPLICATED, AND IT DOES NOT NEED TO BE. Both subclasses set it in
	 * their constructor, so it is part of the class default object and a client
	 * spawning the class already has the right value before any property could
	 * arrive over the network. A summon does have its side assigned at spawn
	 * time rather than in a constructor, and it stays correct on a client for a
	 * different reason: `UCataclysmTeams::TeamOf` follows the owner chain, and
	 * ownership is replicated. Anything that changes a character's side at
	 * runtime would need this replicated, and nothing does.
	 */
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;

	/**
	 * Points the ability system at this pawn and grants the starting kit.
	 * Must run on both server and client; where it is called from differs, which
	 * is the single most common source of bugs in this area.
	 */
	virtual void InitAbilityActorInfo();

	/** Granted on the server once the ability system is initialised. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySet> StartingAbilitySet;
};
