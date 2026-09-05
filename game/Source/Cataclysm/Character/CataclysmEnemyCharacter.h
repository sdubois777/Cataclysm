// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmEnemyCharacter.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UCataclysmCombatAttributeSet;
class UCataclysmAllResistanceAttributeSet;
class UStaticMeshComponent;

/**
 * Base for every enemy.
 *
 * Owns its ability system component directly, unlike the player. An enemy that
 * dies is destroyed and nothing about it needs to survive, so there is no reason
 * to separate the component from the pawn.
 */
UCLASS()
class CATACLYSM_API ACataclysmEnemyCharacter : public ACataclysmCharacterBase
{
	GENERATED_BODY()

public:
	ACataclysmEnemyCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/**
	 * Stop acting and leave the level.
	 *
	 * WHAT DYING IS FOR AN ENEMY, and until issue #517 it was nothing at all: a
	 * creature at zero health kept chasing, kept swinging and could not be
	 * removed, so a fight had no outcome.
	 *
	 * THREE THINGS, IN THIS ORDER. It is marked dead, which is what
	 * ACataclysmEnemyController asks before driving it and what stops a second
	 * call doing any of this twice. Anything it was in the middle of is stopped:
	 * a charge is cancelled and its movement is halted, so it does not slide on
	 * after it dies. Then it is destroyed on the next tick.
	 *
	 * ON THE NEXT TICK RATHER THAN NOW, because this runs inside the gameplay
	 * effect callback that dealt the killing blow. Destroying the actor there
	 * would tear down the ability system component that is still running.
	 *
	 * IT PLAYS NO DEATH ANIMATION, which is a gap rather than a decision. The
	 * Paragon packs ship Death_A and Death_B, and playing one means measuring its
	 * length and holding the creature for it -- per creature, because the Abyssal
	 * Warden has no animation Blueprint and queues clips in C++ (issue #387). See
	 * the issue filed alongside #517.
	 */
	virtual void HandleDeath() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ----------------------------------------------------------------------
	// A charge: the Movement shape, executed
	// ----------------------------------------------------------------------
	//
	// WHY IT IS HERE AND NOT ON ONE CREATURE. Two of the seven vertical slice
	// enemies are designed with a charge -- the Abyssal Warden's Stampede and
	// the Hellhound's Hellrush -- so the second one would otherwise copy the
	// first. Issue #491.
	//
	// WHAT THE DESIGN ALREADY FIXES, so that none of it is decided here. All of
	// it is from section X of `docs/Cataclysm_GDD_v2.md`:
	//
	//   - "A charge hits everything on the way", where a leap hits only where it
	//     lands. So this damages along the path rather than at the end.
	//   - The lane is fixed when the wind-up starts and does not follow the
	//     player, which is the general telegraph rule.
	//   - The creature is committed and RUNS THE FULL DISTANCE whether or not
	//     anything is still there. "It ends up ten metres past the player,
	//     facing away, and covering that ground again ... is the window the
	//     telegraph buys." So arriving at the target is not a stopping
	//     condition, and passing through it is the designed outcome.
	//   - "The player leaves the lane when their centre leaves it", which is how
	//     UCataclysmTargeting::IsInLine already measures one.

	/**
	 * Set off along a straight lane, hitting what it passes.
	 *
	 * @param ToPoint            where the lane ends. The controller captures
	 *   this at the moment the wind-up starts and draws the marker to the SAME
	 *   point, so the lane shown and the lane run cannot disagree.
	 * @param SpeedCmPerSecond   how fast it travels. See the Abyssal Warden's
	 *   header for how a charge speed is arrived at; it is not a general rule.
	 * @param HalfWidthCm        the lane's half-width, which is the ability's
	 *   own Radius and the same figure the marker was drawn with.
	 * @param DamagePercent      what one pass is worth, as a percent of the
	 *   creature's attack damage.
	 * @param KnockbackCm        how far each thing it runs through is shoved
	 *   away from it, in centimetres. Zero for a charge that does not displace.
	 *   Issue #625. It is a parameter rather than a constant here because it
	 *   belongs to the ability, and the two designed charges are on different
	 *   creatures.
	 */
	void BeginCharge(const FVector& ToPoint, float SpeedCmPerSecond,
					 float HalfWidthCm, float DamagePercent,
					 float KnockbackCm = 0.0f);

	/** Whether a charge is running now. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Enemy")
	bool IsCharging() const { return bCharging; }

	/**
	 * Stop a charge where it is.
	 *
	 * THE ONLY CALLER IS A STUN, and that is the decision rather than an
	 * incidental capability. `ACataclysmEnemyController::Think` treats a stun as
	 * outranking everything, including a committed wind-up, because the design
	 * says a stunned target cannot act at all. A creature that kept travelling
	 * while stunned would be acting.
	 *
	 * IT IS NOT WHAT "COMMITTED" MEANS. The design's commitment rule says a
	 * charge runs its full distance whether or not the TARGET is still there --
	 * it is what stops the attack tracking the player, and it is the reason a
	 * miss costs the creature the walk back. It says nothing about the creature
	 * being immune to crowd control, and reading it that way would make a charge
	 * the one attack in the game that interrupting cannot answer.
	 *
	 * WHAT IT COSTS THE CREATURE: the charge is spent. `AbilityLastUsedAt` is
	 * stamped when an ability LANDS, and a charge lands at the moment it sets
	 * off, so a charge stopped half way is on cooldown. That is deliberately
	 * harsher than an interrupted wind-up, which is not spent at all, because
	 * this one did happen -- it simply did not finish.
	 */
	void CancelCharge();

	/**
	 * Move one frame's worth along the lane, and hit whatever that step passed.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT WITHOUT TICKING A WORLD, which is the same
	 * reason ACataclysmAbyssalWardenCharacter::UpdateLoopingAnimation is public.
	 * Every automation test in this project builds its world with
	 * UWorld::CreateWorld and advances the clock by hand rather than letting the
	 * engine tick, so a charge that could only be advanced by Tick could not be
	 * checked at all.
	 *
	 * PER FRAME RATHER THAN PER THINKING PASS, and that is not a preference. The
	 * brain thinks four times a second; a charge at the Warden's speed covers
	 * 2.86 metres in one of those, so a charge advanced by the brain would move
	 * in visible jumps and could step straight over the player without the lane
	 * ever containing them.
	 */
	void AdvanceCharge(float DeltaSeconds);

	/** How far this charge has travelled so far, in centimetres. Read by tests,
	 *  which cannot otherwise tell a charge that moved from one that did not. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float ChargeTravelledCm = 0.0f;

	/** Where the running charge ends. Read by tests. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	FVector ChargeEndPoint = FVector::ZeroVector;

	/** How many separate actors this charge has hit. Read by tests, and what
	 *  proves a charge hits each target once rather than once per frame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 ChargeHitCount = 0;

	/**
	 * Sets both maximum health and current health, before or after BeginPlay.
	 *
	 * WHY A SETTER RATHER THAN A PROPERTY ON THE CLASS. An enemy's real health
	 * comes from its rarity tier and the run's difficulty, neither of which
	 * exists yet, so nothing that ships should carry a hard-coded figure. This
	 * is for whoever is placing an enemy to say what they want, and today that
	 * is the sandbox training dummy spawner. Issue #39 replaces it.
	 *
	 * Both together, because setting the maximum alone leaves an enemy at its
	 * old current value, and setting the current alone is clamped to the old
	 * maximum -- so either one on its own quietly does nothing useful.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetHealth(float NewMaxHealth);

	/**
	 * Sets what one of its attacks is worth, before or after BeginPlay.
	 *
	 * Deferred the same way SetHealth is and for the same reason: writing to an
	 * attribute before the ability system has registered its attribute sets
	 * raises an engine ensure rather than failing quietly.
	 *
	 * WHY AN ENEMY HAS "WEAPON DAMAGE" AT ALL. It carries no weapon. The damage
	 * pipeline reads one number, the AttackDamage attribute, whether the attacker
	 * is a character holding a greataxe or a monster with claws, so this is what
	 * an enemy's claws are worth. Issue #39 replaces the setter with a figure
	 * derived from tier, floor and rarity.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetAttackDamage(float NewAttackDamage);

	/**
	 * Sets how much armour it has, before or after BeginPlay.
	 *
	 * SUPPLIED RATHER THAN DECLARED, which is why this is a setter and not one
	 * of the per-enemy properties below. Issue #372. Armour is a SHARE in the
	 * design model, not an absolute: `stats_for` in
	 * `sim/cataclysm_sim/enemy_stats.py` computes it as
	 *
	 *     score * ARMOR_AT_COMMON * ARMOR_PER_STEP ** rarity_step * armor_share
	 *
	 * so the Brute's `armor_share` of 3.00 is a multiplier on a base that
	 * depends on what the encounter is worth. Nothing in the engine knows an
	 * enemy's score, so this class cannot compute the number and has to be told
	 * it -- exactly as it is told its health and its attack damage, which are
	 * shares for the same reason.
	 *
	 * Issue #355 publishes the archetype numbers as game data, after which the
	 * spawner reads a row and calls this rather than inventing a figure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetArmour(float NewArmour);

	/**
	 * Set how large an energy shield this creature carries, as a fraction of its
	 * maximum health, and rewrite the shield from it.
	 *
	 * A SETTER RATHER THAN ONLY A PROPERTY, so a spawner can supply the figure
	 * the way it supplies health, damage and armour. Issue #355 publishes the
	 * archetype numbers as game data, after which the spawner reads
	 * `EnergyShieldFraction` off a row and calls this.
	 *
	 * ORDER DOES NOT MATTER AGAINST `SetHealth`. Both end in
	 * `ApplyStartingAttributes`, which recomputes the shield from whatever the
	 * maximum health is at that moment, so setting the fraction before the health
	 * gives the same answer as setting it after.
	 *
	 * A negative fraction is refused, exactly as a negative armour is.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetEnergyShieldFraction(float NewFraction);

	/**
	 * Which rung of the rarity ladder this enemy was spawned at.
	 *
	 * THE STEP FROM `game/Data/EnemyRarities.csv`, whose rows are generated from
	 * `RARITY_ORDER` in `sim/cataclysm_sim/enemy_stats.py`: Common 0, Elite 1,
	 * Legendary 2, Herald 3, Boss 4, Cataclysm Boss 5. Supplied by whoever
	 * spawns the enemy, exactly as health, damage and armour are, because
	 * rarity is the encounter's business and not the class's -- the same Brute
	 * class is a Common in one room and an Elite in the next.
	 *
	 * WHY IT LIVES HERE AND NOT AS A TAG OR A STANDALONE BOSS FLAG. Decided by
	 * the project owner's steer on 2026-08-10 and recorded in
	 * `docs/DECISIONS.md`: the enemy generator has to assign each enemy a
	 * rarity from the pool weights anyway, so boss-ness DERIVES from the rarity
	 * it already sets -- see IsBoss below -- and there is no second thing to
	 * remember. A tag or a separate boolean could be forgotten; a Cataclysm
	 * Boss row cannot fail to be a boss.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetRarityStep(int32 NewStep);

	/**
	 * EditInstanceOnly, SO A RARITY CAN BE SET ON A CREATURE PLACED IN A LEVEL.
	 *
	 * WHY IT HAD TO CHANGE. This was VisibleAnywhere, which greys the field out
	 * in the Details panel, and SetRarityStep above has never had a caller
	 * outside the automation tests -- the thing meant to supply a rarity is the
	 * enemy generator, which is issue #508 and does not exist. So every creature
	 * in a play session was Common and nothing in the editor could change it.
	 * That made the whole rarity ladder unreachable by hand: the drop rate, the
	 * added magic find, and the boss stun rule all read this and all sat at rung
	 * zero. Found on 2026-08-19 when the project owner asked how to set it.
	 *
	 * INSTANCE ONLY, NOT EditAnywhere, WHICH WOULD ALSO ALLOW A BLUEPRINT
	 * DEFAULT. The comment above says why: rarity is the encounter's business
	 * and not the class's, and the same Brute is a Common in one room and an
	 * Elite in the next. A rarity baked into the Brute Blueprint would be a
	 * class-wide answer to a per-encounter question, and every Brute placed
	 * afterwards would silently inherit it.
	 *
	 * THE CLAMP IS WHAT SetRarityStep DOES, APPLIED TO THE PANEL. Typing into a
	 * Details field does not go through that function, so without these a
	 * negative step would make IsBoss's comparison meaningless and a step above
	 * the ladder would find no row in EnemyDrops.csv and drop nothing at all.
	 * The maximum is the last rung of the ladder and
	 * `tools/tests/test_enemy_tables_match_the_model.py` pins it to the model,
	 * because continuous integration builds no C++ and a ladder that grew a rung
	 * would otherwise leave the top one untypeable.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy",
			  meta = (ClampMin = "0", ClampMax = "5"))
	int32 RarityStep = 0;

	/**
	 * Which row of `game/Data/EnemyArchetypes.csv` this creature is.
	 *
	 * WHAT IT IS FOR TODAY, AND IT IS ONE THING: the panel that describes the
	 * creature under the cursor has to be able to say what the creature is
	 * called, and until this existed no enemy in the game had a name anywhere.
	 * `UCataclysmCreaturePanel::ArchetypeNameForRow` turns the row key
	 * `Abyssal_Warden` into "Abyssal Warden", so a creature renamed in the
	 * design workbook is renamed on screen without anybody editing C++. Issue
	 * #740.
	 *
	 * IT IS NOT WHERE THE CREATURE'S STATS COME FROM, and it deliberately does
	 * not become that here. Every enemy still carries its own copied figures,
	 * for the reason `ACataclysmGameMode` states: reading a share out of this
	 * table needs a Power Score to multiply it by, and that has no port at all.
	 * Issue #355 builds the transport and issue #39 wires the creatures onto it.
	 * When they do, this field is what they join on.
	 *
	 * EditDefaultsOnly, NOT EditInstanceOnly, WHICH IS THE OPPOSITE OF
	 * RarityStep ABOVE. Rarity is the encounter's business -- the same Brute is
	 * a Common in one room and an Elite in the next -- and an archetype is the
	 * class's: a Brute is a Brute wherever it is standing. So this is set in the
	 * subclass constructor and a placed creature cannot be typed into a
	 * different species.
	 *
	 * NONE BY DEFAULT, because the base class is what the sandbox spawns as a
	 * training dummy, and a practice target is not one of the designed
	 * archetypes. `UCataclysmCreaturePanel::UnnamedCreature` is what the panel
	 * says for those.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	FName ArchetypeRow;

	/**
	 * Which rows of `game/Data/EnemyModifiers.csv` this creature carries.
	 *
	 * THE DESIGN GIVES AN ENEMY ONE PER RUNG ABOVE COMMON, up to five for a
	 * Cataclysm Boss, and they are mechanical effects -- a burning aura, a charm
	 * on being hit -- that change how the creature has to be fought.
	 *
	 * **NOTHING GRANTS THE EFFECT YET, AND THIS DOES NOT.** It is a list of
	 * names that the panel reads and prints. A creature given Hellfire Aura here
	 * does not burn anybody, because the aura does not exist. Two issues cover
	 * the gap: issue #742 is that nothing ASSIGNS a modifier to any creature, so
	 * the design's one-per-rung-above-Common rule has never run, and issue #674
	 * is that five of the modifiers grant flat damage reduction and none of them
	 * DOES anything. Building the effects is part of issue #39.
	 *
	 * SO WHY IS IT HERE AT ALL. Because otherwise the one thing the hover panel
	 * exists for could never be seen by anybody. This is the same answer, for
	 * the same reason, that `RarityStep` above got when it was made typeable: an
	 * EditInstanceOnly field lets a creature placed in a level be given two
	 * modifiers by hand, so the panel's modifier lines can be looked at and
	 * judged before the generator that assigns them exists.
	 *
	 * EditInstanceOnly, LIKE RarityStep AND FOR ITS REASON. Which modifiers a
	 * creature carries is the encounter's business. A list baked into the Brute
	 * Blueprint would be a class-wide answer to a per-encounter question.
	 *
	 * **IT IS FILLED AUTOMATICALLY NOW.** `SetRarityStep` tops this list up to
	 * the count the creature's rung carries, drawing from its own Cataclysm's
	 * column and the Generic one. Issue #742. So a creature spawned by any
	 * spawner arrives carrying the right number without anybody typing one, and
	 * the field is left editable for the one thing a draw cannot do: put a
	 * NAMED modifier on a creature, which is how a specific encounter is built
	 * and how a test asks for one modifier in particular.
	 *
	 * ANYTHING TYPED IS KEPT AND THE DRAW TOPS UP AROUND IT, rather than being
	 * replaced. Two typed onto a Herald leaves one to draw. More typed than the
	 * rung carries are all kept, because deleting what somebody deliberately
	 * asked for is worse than a creature carrying one modifier too many.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<FName> ModifierRows;

	/**
	 * The first rung of the ladder that is a boss.
	 *
	 * 4 IS "Boss", AND THE TWO RUNGS FROM IT UP ARE THE BOSSES: Boss and
	 * Cataclysm Boss. Herald, at 3, is deliberately below the line -- the
	 * Abyssal Warden's reference rarity is Herald and it is a mini-boss the
	 * player may stun. `tools/tests/test_enemy_tables_match_the_model.py` pins
	 * this figure to `RARITY_ORDER.index("Boss")` in the model, because
	 * continuous integration builds no C++ and a drifted copy here would
	 * silently move the stun rule.
	 */
	static constexpr int32 FirstBossRarityStep = 4;

	/**
	 * Whether the anti-stun-lock rule "a boss cannot be stunned at all"
	 * applies to this enemy.
	 *
	 * DERIVED, NEVER SET. Section VI of `docs/Cataclysm_GDD_v2.md` states the
	 * rule and `UCataclysmSkillEffects::ApplyStun` is its one caller. The
	 * Python model's copy is `is_boss_rarity` in
	 * `sim/cataclysm_sim/enemy_stats.py`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Enemy")
	bool IsBoss() const { return RarityStep >= FirstBossRarityStep; }

	/**
	 * Writes the whole designed stat block onto the attributes, if they are
	 * ready for it. Safe to call repeatedly and safe to call too early.
	 *
	 * Called from every setter above and from InitAbilityActorInfo, so it does
	 * not matter which happens first.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, which is the same reason
	 * ACataclysmBruteCharacter::ResolveBody is public. Whether InitAbilityActorInfo
	 * runs at all depends on how the world was built, and it does not run for an
	 * actor spawned into a world from UWorld::CreateWorld -- so a test that
	 * spawned an enemy and read its crit chance would read the attribute set's
	 * own default and could not tell that apart from the values never being
	 * applied. Measured: before this was public, two of the three tests in
	 * CataclysmEnemyAttributeTests.cpp failed for exactly that reason.
	 */
	void ApplyStartingAttributes();

	//~ Dying. Issue #522.

	/**
	 * The clips this creature may die with, in no particular order.
	 *
	 * FILLED BY THE SUBCLASS THAT OWNS THE ART, in its ResolveBody, beside
	 * every other clip it loads. It is held on the base rather than on each
	 * creature because what is done WITH it is the same for all of them --
	 * play one, wait for it, remove the body -- and that lives in HandleDeath
	 * below.
	 *
	 * EMPTY IS THE ORDINARY CASE AND NOT A FAULT. Five of the seven vertical
	 * slice creatures have no art yet and die wearing a placeholder cylinder,
	 * which has nothing to play. Those are removed on the next tick, which is
	 * what every creature did before issue #522.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<TObjectPtr<class UAnimSequence>> DeathAnimations;

	/**
	 * Where one animation in a folder lives, in full.
	 *
	 * An Unreal asset path repeats the asset's name after the package path, so
	 * a clip called `Death_A` in a folder `X` is at `X/Death_A.Death_A`. Every
	 * creature that loads a list of clips writes that out.
	 *
	 * **IT IS HERE BECAUSE TWO CREATURES HAVING THEIR OWN COPY BROKE THE
	 * BUILD.** The Imp and the Corrupted Sentinel each carried an identical
	 * `ClipPathIn` in an anonymous namespace in their own `.cpp`, which is the
	 * ordinary way to keep a helper private to one file. That works for as long
	 * as the two files are compiled separately, and Unreal merges a module's
	 * `.cpp` files into one translation unit -- so the moment both landed in the
	 * same unity blob the definitions collided with "function already has a
	 * body". It reached `development` on 2026-08-20, because UnrealBuildTool
	 * uses `git status` to decide which files to compile on their own: while
	 * either file was modified it was kept out of the blob and the collision
	 * could not happen. **The build passed for a reason that went away when the
	 * work was committed.**
	 *
	 * `tools/tests/test_no_two_files_share_an_anonymous_helper.py` is the guard
	 * that catches it on a pull request, since continuous integration never
	 * builds the C++ and so cannot catch it the way the compiler did.
	 *
	 * THE FORMAT STRING IS A LITERAL AND HAS TO BE. Unreal 5.8's
	 * `FString::Printf` takes a `TCheckedFormatString`, which cannot be built
	 * from a `const TCHAR*` variable, so the folder and the name are arguments
	 * and the shape is fixed inside this function.
	 */
	static FString ClipPathIn(const TCHAR* Folder, const TCHAR* Name);

	/**
	 * What the death clip's draw is salted with, so it is not the draw the
	 * drops came from.
	 *
	 * BOTH ARE SEEDED FROM THE SAME TWO FACTS -- this creature and the moment
	 * it died -- because those are the only two facts available, and two
	 * streams from one seed answer identically. Without a salt, which clip a
	 * creature fell with would move in step with what it dropped.
	 *
	 * THE VALUE ITSELF MEANS NOTHING. Any constant that is not zero separates
	 * the two streams; this one is arbitrary and is written down rather than
	 * typed inline so that nobody reads it as a designed number.
	 */
	static constexpr int32 DeathDrawSalt = 0x5EAD;

	/**
	 * Keeps the modifier draw off the drop and death streams.
	 *
	 * THE SAME REASON THE DEATH SALT ABOVE EXISTS. All three seed from this
	 * creature's identity and the world clock, so without a salt each two draws
	 * made in the same frame would run the same stream and the modifiers a
	 * creature carried would be decided by the same numbers as the way it fell
	 * over. The value itself means nothing.
	 */
	static constexpr int32 ModifierDrawSalt = 0x30D1;

	/**
	 * Tops `ModifierRows` up to the count this creature's rung carries.
	 *
	 * CALLED FROM `SetRarityStep`, WHICH IS THE ONLY PLACE A RUNG IS SET and is
	 * already the place the stat block is re-applied for the same reason: a
	 * spawner sets these on the lines after `SpawnActor` in whatever order suits
	 * it, and the order must not matter.
	 *
	 * SAFE TO CALL TWICE, and it has to be, because a spawner may set the rung
	 * more than once. It only ever adds up to the shortfall, so a second call
	 * with the same rung adds nothing and a call raising the rung adds the
	 * difference.
	 */
	/**
	 * Draws the modifiers this creature's rung carries, into `ModifierRows`.
	 *
	 * **CALLED BY A SPAWNER AND NOT BY `SetRarityStep`, AND THAT WAS A BUG
	 * ONCE.** It was inside the setter until 2026-09-05, which was tidy and
	 * wrong: a draw is random, so any test that spawned two creatures, set the
	 * same rung on both and compared them could fail depending on what each
	 * drew. `Cataclysm.Enemy.RarityScalesWhicheverOrderTheSpawnerSetsItIn` did
	 * exactly that -- one creature drew Titanic Resolve and came out with half
	 * again the health of the other -- and it failed only sometimes, which is
	 * worse than failing always.
	 *
	 * SO IT IS THE SAME SHAPE AS THE RARITY ROLL ITSELF.
	 * `ACataclysmGameMode::RarityStepFor` rolls a rung and the spawners call it;
	 * `SetRarityStep` only sets what it is given. This is the modifier half of
	 * that split.
	 *
	 * SAFE TO CALL TWICE. It only ever adds up to the shortfall, so a second
	 * call at the same rung adds nothing and a call after the rung was raised
	 * adds the difference.
	 */
	void DrawModifiersForRarity();

	/**
	 * How long since an aura modifier on this creature last pulsed, in seconds.
	 *
	 * PUBLIC BECAUSE `UCataclysmEnemyModifiers::AuraStep` KEEPS IT, and that is
	 * a static on a separate class for the reason every rule about modifiers is:
	 * the arithmetic can then be tested by passing numbers in. The alternative
	 * was making the helper a friend of this class to reach one float.
	 *
	 * NOT SAVED AND NOT REPLICATED. It is at most one second of drift in when a
	 * burn is refreshed.
	 */
	float SecondsSinceAuraPulse = 0.0f;

	/**
	 * Writes the attribute changes this creature's modifiers ask for.
	 *
	 * CALLED LAST BY `ApplyStartingAttributes`, SO THEY WIN. Every write in that
	 * function sets a base from the archetype and the rarity, and a modifier is
	 * a change to this creature in particular. One written before the
	 * archetype's own figure would be overwritten by it on the next call, which
	 * happens every time a spawner sets anything.
	 *
	 * MAXIMUM HEALTH IS NOT DONE HERE and is the exception. It has to be applied
	 * where the health is first written, because the energy shield is computed
	 * as a share of it further down the same function.
	 */
	void ApplyModifierAttributes();

	/**
	 * The seed to draw modifiers from, for a test that needs the same draw
	 * twice.
	 *
	 * WHY A SEAM AT ALL. The ordinary seed mixes this creature's identity with
	 * the world clock, so a test cannot ask for a particular draw and cannot
	 * repeat one. Zero means nothing was said and the ordinary seed is used.
	 *
	 * THE SAME TRAP THE RARITY ROLL HAS. Seeding from an object identity makes
	 * a result depend on how many objects were made before it, so a test that
	 * pins nothing can pass alone and fail in a full run.
	 */
	void SetModifierSeedForTests(int32 Seed) { ModifierSeedForTests = Seed; }

	/**
	 * The clip this creature actually died with, once it has.
	 *
	 * READ BY TESTS, WHICH CANNOT OTHERWISE SEE WHAT WAS PLAYED. It is the
	 * same reason ACataclysmAbyssalWardenCharacter::LastPlayedAnimation
	 * exists: nothing reaches a screen under -nullrhi, so the only evidence a
	 * clip was chosen is the choice being recorded.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TObjectPtr<class UAnimSequence> DiedWith;

	/**
	 * How long this creature's body is being kept, in seconds.
	 *
	 * ZERO MEANS THE NEXT TICK, which is what a creature with no death clip
	 * gets. Recorded for the same reason DiedWith is: a timer is not visible
	 * to a test, and the difference between waiting for a clip and not waiting
	 * at all is the whole of issue #522.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float CorpseSeconds = 0.0f;

	/** A stand-in body, so an enemy is visible before there is any art. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	//~ Driven by ACataclysmEnemyController
	virtual float AttackReachCm() const override { return MeleeReachCm; }
	virtual float SightRadiusCm() const override { return NoticeRadiusCm; }
	virtual void AttackTarget(AActor* Target) override;
	//~ End

	// ----------------------------------------------------------------------
	// Commander, the only thing in the game that makes a creature better
	//
	// GRANTED BY THE SUCCUBUS'S AURA and by nothing else today.
	// `ACataclysmSuccubusCharacter::PulseDominion` puts the `Status.Buff.Commander`
	// tag on every ally within 8 metres and takes it off again when they leave
	// or when it dies. This is what the tag DOES.
	// ----------------------------------------------------------------------

	/**
	 * How much a creature holding Commander gains, as a percentage.
	 *
	 * TWENTY, FROM THE DESIGN. `game/Data/StatusEffects.csv` and the Buffs sheet
	 * of `docs/All_Things_Cataclysm.xlsx` both state it.
	 */
	static constexpr float CommanderIncreasePercent = 20.0f;

	/**
	 * What Commander multiplies, or 1.0 when this creature does not hold it.
	 *
	 * **IT COVERS TWO STATS AND NOT EVERY STAT**, decided by the project owner
	 * on 2026-08-20. The design's own words were "20% increased stats", which
	 * does not say which; the answer is **movement speed and attack speed**.
	 * `docs/DECISIONS.md` records why the other candidates were left out, and
	 * maximum health in particular: an enemy's attributes are BASE values, and
	 * current health does not rise with the maximum, so an ally walking in and
	 * out of the field would lose health permanently from an effect meant to
	 * help it.
	 */
	float CommanderMultiplier() const;

	/**
	 * What Cripple multiplies, or 1.0 when this creature is not crippled.
	 *
	 * THE CURSE THAT COULD NOT REACH ANYTHING UNTIL 2026-09-04. Its row in
	 * game/Data/StatusEffects.csv reads "Reduces the affected enemy's
	 * movement and attack speed by 30% for 4 seconds", and nothing in the
	 * project could change either. A player character follows the movement
	 * speed attribute; an enemy's speed is its own designed figure and the
	 * attribute reaches it nowhere. Issue #1152.
	 *
	 * THE SAME SHAPE AS Commander ABOVE, AND THAT IS THE POINT. Both are a
	 * tag on the creature and a percentage in the data, both cover movement
	 * speed and attack speed, and both are read here so a creature carrying
	 * one of each is multiplied by both rather than by whichever was checked
	 * last.
	 *
	 * THE REDUCTION IS THE ROW'S OWN Strength, read out of the Status Effects
	 * sheet rather than written here, so re-tuning the curse needs no code.
	 *
	 * ITS StrengthCap OF 80 IS NOT REACHED AND IS NOT CHECKED. The row says a
	 * magnitude raises the reduction to that cap and then extends the
	 * duration instead, and no magnitude survives the path that applies this
	 * curse: `UCataclysmSkillEffects::ApplyNamedEffect` grants the tag and
	 * keeps no number, so every Cripple in the game is the designed 30%.
	 * Issue #1144 is the column that would change that.
	 */
	float CrippleMultiplier() const;

	/**
	 * Everything acting on this creature's movement and attack speed at once.
	 *
	 * ONE FUNCTION SO THE TWO STATS CANNOT DISAGREE. Commander raises both
	 * and Cripple lowers both, so a creature that is inspired and crippled
	 * gets 1.2 x 0.7 either way rather than one stat seeing both and the
	 * other seeing one.
	 */
	float SpeedMultiplier() const
	{
		return CommanderMultiplier() * CrippleMultiplier();
	}

	/**
	 * Seconds between this creature's attacks BEFORE any buff.
	 *
	 * **THIS IS THE ONE A CREATURE OVERRIDES**, not `SecondsBetweenAttacks`
	 * below, which is `final` so that the mistake is a compile error rather than
	 * a creature that silently ignores every buff. Five creatures overrode the
	 * other one before Commander had a magnitude, and every one of them would
	 * have opted itself out without a word.
	 */
	virtual float DesignedSecondsBetweenAttacks() const
	{
		return AttackIntervalSeconds;
	}

	/**
	 * Seconds between this creature's attacks, buffs included.
	 *
	 * DIVIDED RATHER THAN MULTIPLIED, because this is an INTERVAL and the buff
	 * is a speed. 20% more attack speed is 2.6 seconds becoming 2.167, not 3.12.
	 *
	 * `final`. See `DesignedSecondsBetweenAttacks` above.
	 */
	virtual float SecondsBetweenAttacks() const override final
	{
		// DIVIDED BY EVERYTHING AT ONCE. Commander's 1.2 shortens the
		// interval and Cripple's 0.7 lengthens it, which is what a reduction
		// in attack SPEED means for an INTERVAL.
		return DesignedSecondsBetweenAttacks() / SpeedMultiplier();
	}

	/**
	 * How fast this creature walks BEFORE any buff, in centimetres per second.
	 *
	 * READ OFF THE MOVEMENT COMPONENT IN BeginPlay rather than declared per
	 * creature, because every creature already sets `MaxWalkSpeed` in its own
	 * constructor and a second copy of that number would be one that could
	 * disagree. Zero until BeginPlay has run.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float DesignedWalkSpeedCmPerSecond = 0.0f;

	/**
	 * Bring this creature's walk speed back into line with the buffs it holds.
	 *
	 * WHY WALK SPEED NEEDS THIS AND ATTACK SPEED DOES NOT. An interval is asked
	 * for each time the brain thinks, so it can be computed on demand. Walk
	 * speed is a STORED number the movement component reads every frame, so
	 * something has to write it when the buff lands and when it lapses.
	 *
	 * CALLED FROM Tick, so it is self-correcting. The Succubus takes the tag
	 * away explicitly in every path it controls, but an effect that simply
	 * expired would otherwise leave a creature walking fast for ever. One tag
	 * lookup a frame is a hash lookup and a comparison.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT WITHOUT TICKING A WORLD.
	 *
	 * NAMED FOR WHAT IT WRITES RATHER THAN FOR ONE OF ITS INPUTS, since
	 * 2026-09-04. It was `RefreshCommanderBuff` and it now applies the
	 * Cripple curse as well, so a name saying "commander" would send the
	 * next reader looking in the wrong place. Issue #1152.
	 */
	void RefreshWalkSpeed();

	// ----------------------------------------------------------------------
	// Phases
	//
	// ONE CREATURE HAS THEM AND EVERY CREATURE CAN. The Gatekeeper is the only
	// enemy the design gives phases to, and putting the machinery here rather
	// than on that one class costs nothing -- a creature that leaves
	// `PhaseHealthFractions` empty is in phase 1 for ever, which is what every
	// ability's default phase of 1 already assumes.
	// ----------------------------------------------------------------------

	/**
	 * The health fractions at which each later phase begins, highest first.
	 *
	 * `PHASE_TRANSITIONS["Gatekeeper"]` in
	 * `sim/cataclysm_sim/enemy_abilities.py` is `(0.60, 0.30)`: N entries make
	 * N+1 phases, so two entries mean three phases, beginning at full health,
	 * at 60% and at 30%.
	 *
	 * **HEALTH AND NOTHING ELSE.** The research recorded with issue #354 in
	 * `docs/DECISIONS.md` found no shipped ARPG boss whose phases are triggered
	 * by a timer; a timer appears only as a fail-window inside a transition.
	 *
	 * EMPTY FOR EVERY CREATURE BUT THE BOSS, which is what keeps them all in
	 * phase 1.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TArray<float> PhaseHealthFractions;

	virtual int32 CurrentPhase() const override { return PhaseReached; }

	virtual void HealthChanged() override;

	/**
	 * Work out which phase this creature's health puts it in, and move to it.
	 *
	 * **IT ONLY EVER GOES FORWARD.** A creature healed back above a threshold
	 * keeps the phase it reached. Nothing heals a creature today -- see the note
	 * in `ApplyStartingAttributes` about why a creature has no regeneration --
	 * so this is a guard against a future healer rather than a live case. It is
	 * written that way because a boss that un-learned an ability mid-fight is
	 * exactly what "phases add, they do not take away" forbids, and because a
	 * fight that oscillated across a threshold would flicker its whole rotation.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT WITHOUT DEALING DAMAGE.
	 *
	 * @return whether the phase changed
	 */
	bool RefreshPhase();

	/** The highest phase this creature has reached. One until it loses health. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	int32 PhaseReached = 1;

	// ----------------------------------------------------------------------
	// Lining an attack animation up with the moment it deals damage
	//
	// **THE PROBLEM THESE TWO SOLVE.** A telegraphed attack lands exactly when
	// its wind-up ends: `ACataclysmEnemyController::ContinueWindUp` dismisses
	// the marker and then calls `UseEnemyAbility`. For the animation to keep
	// that promise, the frame in which the blow actually connects has to be at
	// the end of the wind-up too. Fitting the clip to the window by its whole
	// LENGTH does not do that -- it lines up the clip's end and leaves the
	// strike wherever the arithmetic puts it.
	//
	// Measured on 2026-08-21 under issue #526, that left the Gatekeeper's
	// hammer connecting 0.73 seconds before its damage and the Succubus's cast
	// releasing 1.14 seconds before its bolt. Issue #784.
	//
	// **THE RULE IS THE BRUTE'S, FROM 2026-08-08, AND THIS IS ITS ONE COPY.**
	// `ACataclysmBruteCharacter::MontageRateFor` and `MontageDelaySecondsFor`
	// have used it for that creature's two abilities since then and now call
	// these. A second copy of an arithmetic rule in this repository is how the
	// power model drifted twice.
	// ----------------------------------------------------------------------

	/**
	 * The rate a clip must play at for its strike to arrive when the blow lands.
	 *
	 * **NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE.**
	 * Where the clip reaches its strike sooner than the attack lands this
	 * answers 1 and `StrikeAlignedDelaySeconds` waits instead. Slowing a clip
	 * down to fill the gap was tried on the Brute and reported from a play
	 * session as slow motion.
	 *
	 * COMPRESSION IS STILL NEEDED THE OTHER WAY, for a clip whose strike falls
	 * after the blow does. Where even the ceiling is not enough the clip cannot
	 * fit and the caller has a design question rather than an arithmetic one --
	 * that is issue #416 on the Brute's rock throw.
	 *
	 * @param StrikeSeconds   when the clip strikes, at its authored speed
	 * @param LandsAtSeconds  when the attack deals its damage
	 */
	static float StrikeAlignedPlayRate(float StrikeSeconds, float LandsAtSeconds,
									   float MinimumRate, float MaximumRate);

	/**
	 * Seconds after the wind-up begins that the clip should start, so its strike
	 * arrives exactly when the blow lands.
	 *
	 * **A DELAY RATHER THAN A HELD POSE.** Holding the clip on its first frame
	 * for the difference was tried on the Brute and read as the creature seizing
	 * up. Waiting before starting means the creature stands in its ordinary idle
	 * -- which moves -- and then performs the whole attack as one continuous
	 * movement.
	 *
	 * ZERO WHERE THE CLIP HAS TO BE COMPRESSED INSTEAD, because the rate above
	 * has already brought the strike forward to the moment the blow lands.
	 */
	static float StrikeAlignedDelaySeconds(float StrikeSeconds,
										   float LandsAtSeconds,
										   float MinimumRate,
										   float MaximumRate);

	/**
	 * How close it must be to hit, in centimetres.
	 *
	 * A JUDGEMENT, NOT A DESIGN FIGURE, and so are the two below. Nothing in the
	 * design states any of them. Two metres is a little over twice the capsule
	 * radius, which is about the distance at which two of these placeholder
	 * cylinders look like they are touching.
	 *
	 * Per enemy rather than one constant for all of them, which is the shape
	 * Diablo II uses: its monstats.txt gives every monster type its own vision
	 * distance. Issue #39's seven enemies are the reason -- a Hellhound that
	 * charges and a Corrupted Sentinel that never moves cannot share one number.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float MeleeReachCm = 200.0f;

	/** How far it notices something to attack, in centimetres. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float NoticeRadiusCm = 1500.0f;

	/** Seconds between its attacks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.01"))
	float AttackIntervalSeconds = 1.5f;

	/**
	 * The four designed figures that are the same at every rarity. Issue #372.
	 *
	 * DECLARED HERE RATHER THAN SUPPLIED, unlike health, damage and armour. The
	 * design model splits its enemy statistics in two: `stats_for` in
	 * `sim/cataclysm_sim/enemy_stats.py` scales health, damage and armour by the
	 * encounter's score and the enemy's rarity, and takes these four "unchanged
	 * from the archetype". A creature's crit chance does not depend on which
	 * floor it is standing on, so it belongs to the class the way its attack
	 * interval and its reach do.
	 *
	 * THE DEFAULTS ARE THE MODEL'S BASELINE ARCHETYPE, so an enemy that has not
	 * had its own figures decided carries the same ones the model gives an
	 * undesigned creature. `tools/tests/test_enemy_profile_defaults.py` holds
	 * the two together.
	 *
	 * ONE RESISTANCE FIGURE, NOT EIGHT. The model states it plainly: "percent of
	 * all incoming damage resisted, whatever its type. One figure, not eight."
	 * An enemy holds `UCataclysmAllResistanceAttributeSet`, which is that one
	 * figure and nothing else, where a player holds
	 * `UCataclysmResistanceAttributeSet`, which is eight and nothing else. No
	 * character holds both. It used to be one figure written onto all eight typed
	 * resistances, which meant it was never met at all: see issue #486 and the
	 * comment at the write.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float ResistancePercent = 0.0f;

	/**
	 * Which of the player's eight resistances this creature's attacks are met by.
	 *
	 * DEMONIC, BECAUSE THE WHOLE VERTICAL SLICE IS. The design gives each
	 * Cataclysm one damage type and says its enemies deal it; all seven Demonic
	 * creatures therefore share this. `Archetype.cataclysm` in
	 * `sim/cataclysm_sim/enemy_stats.py` is the same field with the same default,
	 * and `EnemyArchetypes.csv` carries it per archetype for when this is read
	 * from data rather than declared.
	 *
	 * THIS IS THE ONLY DAMAGE IN THE GAME THAT CARRIES A TYPE, and that is a
	 * ruling rather than an omission. The project owner settled it on 2026-08-12:
	 * "the only damage that should actually be typed is enemy damage so the
	 * player's resistances can take effect". A player has eight resistances
	 * because eight Cataclysms attack them, so an enemy's hit has to say which one
	 * applies. An enemy has one generic resistance, so a player's hit has nothing
	 * to select and stays untyped.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	FName DamageType = FName(TEXT("Demonic"));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float CritChancePercent = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float CritMultiplierPercent = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float EvasionPercent = 0.0f;

	/**
	 * How large an energy shield this creature carries, as a fraction of its
	 * maximum health.
	 *
	 * A FRACTION RATHER THAN AN ABSOLUTE FIGURE, because that is what the design
	 * holds. `Archetype.energy_shield_fraction` in
	 * `sim/cataclysm_sim/enemy_stats.py` is the same field, and that model
	 * computes the shield as `health x energy_shield_fraction`, so
	 * `ApplyStartingAttributes` does the same arithmetic from the same two
	 * inputs. Storing a second absolute number would be a figure that could
	 * disagree with the health beside it.
	 *
	 * NOTHING COULD EXPRESS ONE AT ALL UNTIL ISSUE #485. The fraction reached
	 * `game/Data/EnemyArchetypes.csv` and reached `FCataclysmEnemyArchetypeRow`
	 * in `game/Source/Cataclysm/Data/CataclysmDataRows.h`, and then stopped:
	 * there was no property here and `ApplyStartingAttributes` never wrote
	 * `MaxEnergyShield`, so every enemy in the editor had a shield of zero
	 * whatever the design said.
	 *
	 * TWO OF THE SEVEN SLICE ENEMIES NEED IT AND NEITHER IS BUILT. The Succubus
	 * is designed at 0.50 and the Corrupted Sentinel at 0.35; the other five,
	 * including both creatures that do have a class, are designed at 0.00. So
	 * this changes no creature in the game today and is what lets the two that
	 * need it have one the moment they exist. Issue #39 builds them, and
	 * `tools/tests/test_enemy_energy_shield_reaches_the_engine.py` fails the
	 * moment either class appears without setting this.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float EnergyShieldFraction = 0.0f;

	/**
	 * What one attack deals, as a percent of its own attack damage.
	 *
	 * 100, because the Skill Slots sheet of the design workbook gives the basic
	 * attack 100% on the grounds that it IS weapon damage and every other slot
	 * is a percentage of it. An enemy has only this until issue #39 gives each
	 * one designed abilities.
	 */
	static constexpr float AttackPercentOfOwnDamage = 100.0f;

	/**
	 * What an enemy's ordinary attack IS, as gameplay tags.
	 *
	 * WHY AN ENEMY ABILITY HAS TAGS AT ALL. A player's skill carries a tag list
	 * from the Tags column of the weapon skill matrix and an enemy's ability
	 * carried none, so nothing could ask what an enemy attack is. That already
	 * cost one workaround: whether a hit is area damage is normally read off the
	 * skill's own tags, and the two enemy abilities that sweep a volume had to
	 * say so a second way, at the call site. Issue #519, asked for by the project
	 * owner on 2026-08-12.
	 *
	 * WRITTEN IN THE SAME FORMAT AS A SKILL ROW'S Tags CELL, and parsed by the
	 * same `UCataclysmSkillShapes::TagsFromCell`, so there is one format and one
	 * parser rather than a second of each. It warns about a name the vocabulary
	 * does not have, and `Cataclysm.EnemyTags.*` fails if any of these carries
	 * one.
	 *
	 * DECLARED IN C++ BESIDE THE ABILITY'S OTHER CONSTANTS rather than generated
	 * from a table. Issue #519 weighed both and preferred this: a generated enemy
	 * ability table is the shape issue #355 proposes for enemy archetype numbers
	 * and the two should go together, while the tags themselves have to be chosen
	 * either way.
	 *
	 * THE TAGS FOLLOW WHAT THE DESIGNED PLAYER SKILLS ALREADY DO, rather than
	 * being invented. A single melee swing is what Cinderslash carries.
	 *
	 * NO `Element.*` TAG, on purpose. An enemy's damage type is a field on this
	 * class and already reaches the hit as an `Element.*` tag through
	 * `UCataclysmSkillEffects::DamageTypeOf`. Naming it here as well would put
	 * the damage type in two places that could disagree.
	 *
	 * NO `Slot.*` TAG either. Those exist so a player affix scoped to, say, heavy
	 * attack damage can find the player's heavy skill. An enemy carries no stat
	 * modifiers, so a slot tag on an enemy ability would scope nothing.
	 */
	static const TCHAR* BasicAttackTags;

	/**
	 * What a charge IS, as gameplay tags.
	 *
	 * `Keyword.Charge` is what Furnace Charge and Flamedart carry, and
	 * `Keyword.Stagger` is here because this one shoves aside what it runs
	 * through, which is the rule the design settled on issue #310.
	 */
	static const TCHAR* ChargeTags;

protected:
	virtual void InitAbilityActorInfo() override;

	/**
	 * Writes one attribute, if the ability system is holding it yet.
	 *
	 * Ten attributes are written on spawn and every one needs the same guard, so
	 * it is a helper rather than ten copies of the same `if`.
	 *
	 * IT SAID THIRTEEN UNTIL 2026-08-16, and that had been true: eight of them
	 * were the eight typed resistances, which issue #486 collapsed into one
	 * all-damage figure, and issue #485 then added the two energy shield writes.
	 */
	void ApplyIfHeld(const struct FGameplayAttribute& Attribute, float Value);

	/** What SetHealth was last asked for. Zero means the attribute set's own default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float StartingMaxHealth = 0.0f;

	/** What SetAttackDamage was last asked for. Zero means it deals nothing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float StartingAttackDamage = 0.0f;

	/**
	 * What SetArmour was last asked for. Zero means no armour.
	 *
	 * ZERO IS A REAL ANSWER HERE, unlike for health. The Imp's `armor_share` is
	 * 0.0 in the design model, so an unarmoured enemy is designed rather than
	 * unconfigured, and nothing should treat it as missing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float StartingArmour = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	/** Three sets, not the player's five. See the constructor for why. */
	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmCombatAttributeSet> CombatAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmAllResistanceAttributeSet> ResistanceAttributes;

	/**
	 * How far a charge may step in one frame before it is split into several,
	 * in centimetres.
	 *
	 * WHY A CHARGE IS STEPPED AT ALL. Damage is applied to whatever lies within
	 * the lane's half-width of the segment travelled this frame. On a slow frame
	 * that segment is long, and at a low frame rate a single step could jump
	 * from one side of a target to the other -- but the segment still contains
	 * it, so that alone is safe. What is NOT safe is the geometry sweep: a very
	 * long step swept in one go can tunnel a thin wall. Splitting the frame into
	 * steps no longer than this bounds both.
	 *
	 * HALF THE NARROWEST DESIGNED LANE. The narrowest Charge-mode radius any
	 * ability uses is 1.5 metres, so a 75 cm step cannot skip past a lane's
	 * width. It costs at most two sweeps a frame at 60 frames a second and the
	 * Warden's speed.
	 */
	static constexpr float LongestChargeStepCm = 75.0f;

private:
	/**
	 * Plays one of this creature's death clips, if it has any.
	 *
	 * ONTO THE COMPONENT IN SINGLE-NODE MODE, WHICH TAKES THE MESH OFF ITS
	 * ANIMATION BLUEPRINT. That is what is wanted here and nowhere else: a
	 * living creature needs its graph, because the graph is what blends its
	 * locomotion, and a dead one has nothing left to blend into. The Brute has
	 * an animation Blueprint and the Abyssal Warden does not, and this is the
	 * one path that works for both -- which is why the death clip is handled
	 * here rather than three times over in the subclasses.
	 *
	 * A SINGLE-NODE ONE-SHOT HOLDS ITS LAST FRAME FOREVER, which is a fault
	 * everywhere else in this project -- the project owner reported it on
	 * 2026-08-09 for the Abyssal Warden's attack -- and is exactly right for a
	 * death: the body keeps the pose it fell into until it is removed.
	 *
	 * ITS OWN RANDOM STREAM, NOT THE ONE THE DROPS CAME FROM. Sharing one
	 * would make which clip a creature died with change what it dropped, which
	 * is a coupling nobody would look for and which no test would catch.
	 *
	 * @return how long the body should be kept, or 0 when nothing was played
	 */
	float PlayDeathAnimation();

	FCataclysmAbilitySetHandles GrantedHandles;

	/** Whether a charge is running. See BeginCharge. */
	bool bCharging = false;

	/** How fast the running charge travels, in centimetres per second. */
	float ChargeSpeedCmPerSecond = 0.0f;

	/** The running charge's lane half-width, in centimetres. */
	float ChargeHalfWidthCm = 0.0f;

	/** What one pass of the running charge is worth, as a percent. */
	float ChargeDamagePercent = 0.0f;

	/** See `SetModifierSeedForTests`. Zero means nothing was said. */
	int32 ModifierSeedForTests = 0;

	/** How far the running charge shoves what it hits, in centimetres. */
	float ChargeKnockbackCm = 0.0f;

	/**
	 * Who this charge has already hit.
	 *
	 * ONCE PER CHARGE, NOT ONCE PER FRAME, and this is what enforces it. The
	 * lane is re-tested every step, so a target standing still inside it would
	 * otherwise be hit sixty times a second. "A charge hits everything on the
	 * way" is one hit each.
	 *
	 * WEAK POINTERS BECAUSE A TARGET CAN DIE MID-CHARGE, and a raw pointer to a
	 * destroyed actor would be compared against a later allocation at the same
	 * address.
	 */
	TArray<TWeakObjectPtr<AActor>> ChargeAlreadyHit;

	/** One step of a charge: move, then hit what the step passed. Returns false
	 *  when the charge ended, either by arriving or by meeting geometry. */
	bool StepCharge(float StepCm);

	/**
	 * Put one step of a charge on the floor rather than at the height the charge
	 * set off from.
	 *
	 * WHY A CHARGE HAS TO DO THIS ITSELF. Issue #497. A charge moves the creature
	 * with SetActorLocation rather than through the movement component, so
	 * nothing else in the frame is finding the floor for it. Before this, every
	 * step kept the height of the one before, which is right only on level
	 * ground: up a ramp the capsule ended up inside the geometry, and off a ledge
	 * the creature ran out into the air. `docs/Cataclysm_GDD_v2.md` settles what
	 * it should do instead -- a charge "runs along the ground and meets whatever
	 * is in the way", and "it is stopped by the level, not by bodies".
	 *
	 * WHAT IT WILL AND WILL NOT CLIMB. Both allowances come from the movement
	 * component, so a charge and a walk agree about the ground by construction
	 * rather than by a second number kept in step by hand: the walkable floor
	 * angle gives the steepest slope it will follow, and MaxStepHeight the
	 * tallest single lip it will mount. Ground higher than both ends the charge,
	 * which is the same rule a wall is already stopped by.
	 *
	 * @param From         where the step starts: the creature's capsule centre.
	 * @param RemainingCm  how much of the lane is left, in the floor plane. Only
	 *   sets how far down it is worth looking; see the definition.
	 * @param StepCm       how far this step travels, in the floor plane.
	 * @param Step         the step's destination. Its height is replaced.
	 * @return false when the ground ahead is higher than the creature could get
	 *   onto in one step, which ends the charge where it stands.
	 */
	bool SetChargeStepHeight(const FVector& From, float RemainingCm,
							 float StepCm, FVector& Step) const;
};
