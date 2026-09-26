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
	 *
	 * THE NAME PROMISES LESS THAN IT DOES, AND THE NUMBER IS NOT THE ONE STORED.
	 * Issue #1668. This writes the MAXIMUM health and lets the current follow
	 * it, and the figure passed is scaled first: by the rarity's health
	 * multiplier and by the active dungeon modifiers' maximum-health
	 * multiplier, in `ApplyStartingAttributes`. `SetHealth(1000.0f)` on a
	 * Legendary does not leave it with 1000 maximum health, and a test that
	 * asserts it does is asserting something this never promised. Read the
	 * attribute back rather than trusting the argument.
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
	 * in the Details panel, and on the day it was found SetRarityStep above had
	 * no caller outside the automation tests -- the thing meant to supply a
	 * rarity is the enemy generator, which is issue #508 and does not exist. So
	 * every creature in a play session was Common and nothing in the editor
	 * could change it. That made the whole rarity ladder unreachable by hand:
	 * the drop rate, the added magic find, and the boss stun rule all read this
	 * and all sat at rung zero. Found on 2026-08-19 when the project owner asked
	 * how to set it.
	 *
	 * THAT SENTENCE STAYED IN THE PRESENT TENSE AFTER IT STOPPED BEING TRUE, and
	 * it is dated above rather than deleted because it is why the field is
	 * typeable. Measured on 2026-09-17: SetRarityStep has call sites in three
	 * files that are not tests -- `ACataclysmDungeonGameMode`'s floor spawners
	 * and `ACataclysmGameMode`'s sandbox spawners, both through
	 * `ACataclysmGameMode::RarityStepFor`, and
	 * `FCataclysmSaveApply::CreatureInto` restoring a saved creature. The dungeon
	 * rule `Chaos_Volatile_Evolution` is the fourth, and the first to call it in
	 * the middle of a fight rather than as a creature is placed.
	 *
	 * INSTANCE ONLY, NOT EditAnywhere, WHICH WOULD ALSO ALLOW A BLUEPRINT
	 * DEFAULT. The comment above says why: rarity is the encounter's business
	 * and not the class's, and the same Brute is a Common in one room and an
	 * Elite in the next. A rarity baked into the Brute Blueprint would be a
	 * class-wide answer to a per-encounter question, and every Brute placed
	 * afterwards would silently inherit it.
	 *
	 * THE CLAMP HERE IS WIDER THAN THE ONE SetRarityStep DOES, AND THIS LINE USED
	 * TO SAY THEY WERE THE SAME. That function clamps the bottom only --
	 * `RarityStep = FMath::Max(0, NewStep)` -- so these two figures are the only
	 * ceiling anywhere, and they only reach what somebody types into a Details
	 * field. Typing there does not go through that function, so without these a
	 * negative step would make IsBoss's comparison meaningless and a step above
	 * the ladder would find no row in EnemyDrops.csv and drop nothing at all.
	 *
	 * A CALLER THAT NEEDS A CEILING APPLIES ITS OWN, and one does: the dungeon
	 * rule `Chaos_Volatile_Evolution` raises a wounded creature's rung and holds
	 * it to `UCataclysmDungeonModifierEffects::VolatileEvolutionHighestRung`,
	 * Herald, so a floor rule cannot make a boss out of an ordinary creature.
	 * Giving this function a top clamp of its own was considered and not done:
	 * it would change what every existing caller may ask for, which is wider
	 * than that rule.
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

	/**
	 * Makes this creature an illusion, or stops it being one.
	 *
	 * SAFE BEFORE OR AFTER `BeginPlay`, like `SetAttackDamage` and for the same
	 * reason: it recomputes through `ApplyStartingAttributes`, which refuses to
	 * write an attribute whose set is not registered yet and is called again once
	 * it is.
	 *
	 * THE DAMAGE IS THE ONLY THING IT CHANGES WHEN IT IS CALLED AS THE CREATURE IS
	 * PLACED, which is where Illusory Enemies calls it. `ApplyStartingAttributes`
	 * also sets health and the energy shield to their maximums, so called on a
	 * wounded creature this would heal it; this comment said the damage was the
	 * only thing it changed, without that condition, until issues #1820 and #41.
	 * See `bIsAnIllusion`, and `SetPlacedDamageMultiplier` and
	 * `SetTimeAliveDamageMultiplier` for the routes that change the damage alone.
	 *
	 * NO ACCESS SPECIFIER ADDED AROUND THIS, DELIBERATELY. The section in force
	 * here is the `public:` opened at the top of the class, which
	 * `ApplyStartingAttributes` above is already in; a `public:` and a closing
	 * `private:` would have made every member declared after this point private,
	 * which compiles for a while and then fails somewhere unrelated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetIsAnIllusion(bool bNowAnIllusion);

	/**
	 * Whether this creature is an illusion.
	 *
	 * THE FIELD ITSELF IS PROTECTED, like `StartingAttackDamage` and every other
	 * designed figure beside it, and `BlueprintReadOnly` on it grants Blueprint
	 * access rather than C++ access. The automation tests read this instead.
	 *
	 * THE FIRST BUILD OF THIS CHANGE FAILED ON EXACTLY THAT. Three lines in
	 * `CataclysmDungeonModifierEffectsTests.cpp` read the field directly and got
	 * `error C2248: cannot access protected member`. The access section had been
	 * checked -- for `ApplyStartingAttributes`, which is public from the top of
	 * the class -- and not for the field, which sits in a protected section
	 * beginning hundreds of lines later. Checking one declaration's section says
	 * nothing about another's.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Enemy")
	bool IsAnIllusion() const { return bIsAnIllusion; }

	/**
	 * Multiplies this creature's attack damage for the rule that PLACED it, and changes
	 * nothing else about it. `Death_Grave_Tide`. Issues #1820 and #41.
	 *
	 * TWO MULTIPLIERS AND NOT ONE, because two rules can act on one creature and
	 * neither may overwrite the other. A wave sets this one as it places a creature and
	 * never touches it again; `SetTimeAliveDamageMultiplier` below is written every beat
	 * by a rule that counts how long the creature has lived. `WriteAttackDamage`
	 * multiplies by both.
	 *
	 * THE ATTACK DAMAGE ALONE IS REWRITTEN, THROUGH `WriteAttackDamage`, AND
	 * `ApplyStartingAttributes` IS NOT RE-RUN. That function sets health and the
	 * energy shield to their maximums, which is right for a creature being placed and
	 * wrong for one in a fight: a rule growing a wounded creature's damage through it
	 * would heal the creature every time.
	 *
	 * A MULTIPLIER ON THE DESIGNED FIGURE, NEVER ON THE ATTRIBUTE, for the reason the
	 * maximum-health write in `ApplyStartingAttributes` gives: the attribute is
	 * rewritten from the designed figure on every recompute, so a multiplier applied
	 * to the attribute would compound. It multiplies the rarity's damage scale, an
	 * illusion still deals nothing, and a later recompute keeps it, because
	 * `ApplyStartingAttributes` writes the damage through the same helper.
	 *
	 * NOT SAVED. A creature restored from a save starts again at 1.0: the save keeps
	 * its rarity, its modifiers and its health, and not this.
	 *
	 * @param NewMultiplier  1.0 for the creature's own damage; below zero is read as zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetPlacedDamageMultiplier(float NewMultiplier);

	/**
	 * Multiplies this creature's attack damage for a rule that counts how long it has
	 * been alive. `Famine_Ravenous_Hoard`. Issues #1820 and #41.
	 *
	 * NAMED FOR WHERE IT COMES FROM, like `SetPlacedDamageMultiplier` above, and it was
	 * called `SetFloorRuleDamageMultiplier` until Grave Tide needed a second source. A
	 * general name holding one of two sources is what misleads the next reader.
	 *
	 * Everything the setter above says about the route, the designed figure, the
	 * illusion and the save applies here too.
	 *
	 * @param NewMultiplier  1.0 for the creature's own damage; below zero is read as zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetTimeAliveDamageMultiplier(float NewMultiplier);

	/**
	 * Multiplies this creature's attack damage for a rule that reads how deep the floor
	 * is. `War_March_of_Progress`. Issues #1820 and #41.
	 *
	 * NAMED FOR WHERE IT COMES FROM, like the two above, and for the reason the middle
	 * one records: a general name holding one of several sources is what misleads the
	 * next reader. This one comes from the floor's depth and from nothing else.
	 *
	 * THREE MULTIPLIERS NOW, AND THE REASON IS THE ONE THE FIRST OF THEM GIVES. Three
	 * rules can act on one creature and none may overwrite another: a Horde wave sets
	 * the placed one, Ravenous Hoard writes the time-alive one every beat, and March of
	 * Progress writes this one every beat. `WriteAttackDamage` multiplies by all three.
	 *
	 * ITS VALUE IS THE SAME FOR EVERY CREATURE ON THE FLOOR, unlike the other two, which
	 * differ per creature. It is still held per creature rather than once on the game
	 * mode, because that is what makes it survive a recompute: `ApplyStartingAttributes`
	 * rewrites the damage from the designed figure and would drop a multiplier the
	 * creature did not carry.
	 *
	 * Everything the two setters above say about the route, the designed figure, the
	 * illusion and the save applies here too.
	 *
	 * @param NewMultiplier  1.0 for the creature's own damage; below zero is read as zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetFloorDepthDamageMultiplier(float NewMultiplier);

	/**
	 * Multiplies this creature's attack damage while it stands near a living Golden Spire.
	 * `Celestial_Golden_Spires`. Issues #1820 and #41.
	 *
	 * THE FOURTH SOURCE, AND THE ONE THAT BUILT THE MAP: every source's figure is an entry of
	 * `DamageMultipliersBySource` under its own key, and `WriteAttackDamage` multiplies them
	 * all, as ruled on 2026-09-17. Everything the three setters above say about the route,
	 * the designed figure, the illusion and the save applies here too.
	 *
	 * @param NewMultiplier  1.0 for the creature's own damage; below zero is read as zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetSpireDamageMultiplier(float NewMultiplier);

	/**
	 * Multiplies this creature's attack damage by what the plague beacons left standing on earlier
	 * floors of its dungeon add. `Pestilence_Pestilent_Empowerment`. Issues #1820 and #41.
	 *
	 * A FIFTH KEY OF `DamageMultipliersBySource`. Everything the setters above say about the route,
	 * the designed figure, the illusion and the save applies here too.
	 *
	 * @param NewMultiplier  1.0 for the creature's own damage; below zero is read as zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetPlagueBeaconsDamageMultiplier(float NewMultiplier);

	/**
	 * Multiplies this creature's attack damage once its floor's Trial of Endurance has run out.
	 * `Celestial_Trial_of_Endurance`. Issues #1820 and #41.
	 *
	 * A SIXTH KEY OF `DamageMultipliersBySource`. Everything the setters above say about the route,
	 * the designed figure, the illusion and the save applies here too.
	 *
	 * @param NewMultiplier  1.0 for the creature's own damage; below zero is read as zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetTrialOfEnduranceDamageMultiplier(float NewMultiplier);

	/**
	 * Multiplies this creature's attack damage while it stands within reach of an obsidian sarcophagus.
	 * `Death_Obsidian_Sarcophagi`. Issues #1820 and #41.
	 *
	 * A SEVENTH KEY OF `DamageMultipliersBySource`. Everything the setters above say about the route,
	 * the designed figure, the illusion and the save applies here too.
	 *
	 * @param NewMultiplier  1.0 for the creature's own damage; below zero is read as zero
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetObsidianSarcophagiDamageMultiplier(float NewMultiplier);

	/** The keys of `DamageMultipliersBySource`, one per rule that changes a creature's damage. */
	static constexpr const TCHAR* PlacedDamageSource = TEXT("Placed");
	static constexpr const TCHAR* TimeAliveDamageSource = TEXT("TimeAlive");
	static constexpr const TCHAR* FloorDepthDamageSource = TEXT("FloorDepth");
	static constexpr const TCHAR* SpireDamageSource = TEXT("GoldenSpires");
	static constexpr const TCHAR* PlagueBeaconsDamageSource = TEXT("PlagueBeacons");
	static constexpr const TCHAR* TrialOfEnduranceDamageSource = TEXT("TrialOfEndurance");
	static constexpr const TCHAR* ObsidianSarcophagiDamageSource = TEXT("ObsidianSarcophagi");

	/** What the source named `Source` multiplies this creature's attack damage by; 1.0 when none. */
	float DamageMultiplierFrom(const TCHAR* Source) const;

	/** Every source's multiplier multiplied together: what `WriteAttackDamage` applies. */
	float DamageMultiplierProduct() const;

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
	 * True when the floor's Field Medic rule chose this creature.
	 *
	 * WHAT SETS IT. `ACataclysmDungeonGameMode::PopulateFloor` marks one
	 * creature per floor when the floor carries `War_Field_Medic`. Nothing
	 * else writes it, and a creature spawned anywhere else is never a medic.
	 *
	 * A FLAG RATHER THAN A MODIFIER ROW, AND THAT IS DELIBERATE. Being a
	 * medic is a **dungeon** rule, from `game/Data/DungeonModifiers.csv`,
	 * while `ModifierRows` above holds keys from
	 * `game/Data/EnemyModifiers.csv`. Putting a dungeon key into that array
	 * would leave a reader looking it up in the wrong table and finding
	 * nothing. `SightRadiusMultiplier` on the shared character base is the
	 * same shape for the same reason: a per-floor decision written onto the
	 * creature at spawn.
	 *
	 * NOT SAVED. A floor restored from a save re-runs its rules.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bHealsAlliesForTheFloorRule = false;

	/**
	 * How long since a creature carrying Phasewalker last teleported.
	 *
	 * PUBLIC FOR THE REASON `SecondsSinceAuraPulse` ABOVE IS: the rule that
	 * reads it is a static on `UCataclysmEnemyModifiers`, so that the
	 * arithmetic can be tested by passing numbers in.
	 */
	float SecondsSincePhase = 0.0f;

	/**
	 * The timers and the sigil the remaining Demonic modifiers keep.
	 *
	 * PUBLIC FOR THE REASON THE TWO ABOVE ARE: the rules that read them are
	 * statics on `UCataclysmEnemyModifiers`, so the arithmetic can be tested
	 * by passing numbers in rather than by building a fight.
	 *
	 * A SIGIL IS A POINT AND A COUNTDOWN RATHER THAN AN ACTOR, because it has
	 * no art and nothing else in the game needs to find one. Whoever is about
	 * to die asks whether a friendly caster's sigil covers it.
	 *
	 * THE CHARM STAMP IS NEGATIVE UNTIL THE FIRST ONE, so a creature can
	 * charm immediately rather than waiting out a cooldown it never started.
	 */
	float SecondsSinceSacrifice = 0.0f;
	float SecondsSinceSigil = 0.0f;
	float SecondsSinceInfernoCharge = 0.0f;
	float SigilSecondsLeft = 0.0f;
	FVector SigilCentre = FVector::ZeroVector;
	float CharmLastAppliedAt = -1.0f;

	/**
	 * Whether this creature's Infernal Brand is exploding at this moment.
	 * Issue #1534.
	 *
	 * TRUE ONLY WHILE `UCataclysmEnemyModifiers::BrandOnHit` DEALS THE
	 * EXPLOSION, which is one function call: the explosion is an instant blow,
	 * resolved inside the call that applies it. What it stops is the explosion
	 * branding its own target. The explosion is an ordinary blow from this
	 * creature, so the target's attribute set hands it straight back to
	 * `BrandOnHit`, and without this it would add a stack to the count it had
	 * just spent.
	 *
	 * ON THE CREATURE RATHER THAN ON THE BLOW. Retaliation solves the same kind
	 * of loop with a flag in `FCataclysmHitDelivery`, but that flag has to cross
	 * to the defender as a gameplay tag and be read back there. Only this
	 * creature's own `BrandOnHit` ever needs to know, and it is on the stack
	 * while the answer matters.
	 */
	bool bInfernalBrandExploding = false;

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

	/**
	 * A creature the floor made its medic starts nothing hostile.
	 *
	 * THE ROW SAYS SO TWICE. `War_Field_Medic` reads "It does not attack",
	 * and states its purpose as forcing the player "to prioritize a
	 * **non-threatening** enemy". A creature that burns everything standing
	 * near it is threatening even if it never swings, so the reading that
	 * satisfies the row covers the modifier auras and not only the swing.
	 * Issue #1680.
	 *
	 * ONE MARK, TWO CONSEQUENCES. The same flag makes this creature heal its
	 * allies and stops it harming anybody. They are not separable: the row
	 * describes one creature.
	 */
	virtual bool TakesNoHostileAction() const override
	{
		return bHealsAlliesForTheFloorRule;
	}
	//~ End

	// ----------------------------------------------------------------------
	// Commander, the only thing in the game that makes a creature better
	//
	// GRANTED BY FOUR THINGS, AND THIS COMMENT USED TO SAY ONE. It read "GRANTED
	// BY THE SUCCUBUS'S AURA and by nothing else today", and that was already
	// wrong when it was written. Counted on 2026-09-14:
	//
	//   ACataclysmSuccubusCharacter::PulseDominion   every ally within 8 metres
	//   UCataclysmEnemyModifiers::RallyAlliesOnDeath Horde Leader's rally
	//   UCataclysmEnemyModifiers::TimedStep          the buff a sacrifice grants
	//   the dungeon rule Celestial_Hallowed_Groundfall, every creature standing
	//                                                in one of its craters
	//
	// THE SENTENCE ABOVE THIS ONE IS STILL TRUE and is why the dungeon rule uses
	// this tag rather than inventing a buff: Commander remains the only thing in
	// the game that makes a creature better, so "empower enemies" has exactly one
	// honest reading.
	//
	// The Succubus takes the tag off again when an ally leaves its aura or when
	// it dies. The other three grant it for a stated time and let it expire.
	// This is what the tag DOES.
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
	 * ITS StrengthCap OF 80 IS REACHED AND CHECKED SINCE ISSUE #1256, and this
	 * comment said the opposite until then: "no magnitude survives the path
	 * that applies this curse... so every Cripple in the game is the designed
	 * 30%." That was true and is no longer.
	 *
	 * WHAT CHANGED IS WHERE THE FIGURE COMES FROM, not where the reduction is
	 * applied. The applier holds the strength to the row's cap and rolls the
	 * surplus into the duration, and states the capped figure on the effect;
	 * this reads that figure back and falls back to the row when an
	 * application states none.
	 *
	 * IT WAS NOT MOVED ONTO THE SHARED PATH, AND THAT IS DELIBERATE. Issue
	 * #1256 proposed filling the `MovesStat` column and deleting this
	 * function. Measured, that would have stopped the curse working:
	 * `ApplyNamedEffect` SUBTRACTS its figure from an attribute, which is
	 * right for Shred taking 10 off a resistance and wrong for a percentage;
	 * and no enemy attribute is read for speed, since walk speed is
	 * `DesignedWalkSpeedCmPerSecond * SpeedMultiplier()` and the attack
	 * interval divides by the same. The issue carries the measurement.
	 */
	float CrippleMultiplier() const;

	/**
	 * What Feasting multiplies this creature's attack RATE by, or 1.0 when it
	 * is not feasting. Issue #1720.
	 *
	 * ATTACK SPEED ONLY, WHICH IS WHY IT IS NOT IN `SpeedMultiplier` BELOW.
	 * Commander and Cripple each name BOTH movement and attack speed, so they
	 * share one function and cannot disagree. The `Buff_Feasting` row names one
	 * of the two -- "its attack speed is increased" -- and a feasting creature
	 * that also walked faster would be doing something its own row does not say.
	 *
	 * THE COUNT IS THE STACK SYSTEM'S AND THE PER-STACK FIGURE IS THE ROW'S.
	 * `UCataclysmStacks` counts Feast stacks and caps them at five; the 4% each
	 * one is worth is the `Strength` column of that row, so re-tuning it is a
	 * data change. The same split `CrippleMultiplier` above makes.
	 *
	 * NEEDS NO REFRESH CALL, unlike the walk speed. An attack interval is asked
	 * for when a blow is about to land, so a stack gained or lapsed a moment ago
	 * is already counted. `RefreshWalkSpeed` exists because a movement component
	 * holds its speed as state and has to be told.
	 */
	float FeastingMultiplier() const;

	/**
	 * Everything acting on this creature's movement and attack speed at once.
	 *
	 * ONE FUNCTION SO THE TWO STATS CANNOT DISAGREE. Commander raises both
	 * and Cripple lowers both, so a creature that is inspired and crippled
	 * gets 1.2 x 0.7 either way rather than one stat seeing both and the
	 * other seeing one.
	 *
	 * IT IS NOT EVERYTHING ANY MORE, AND THAT IS DELIBERATE. `FeastingMultiplier`
	 * above moves the attack interval and not the walk speed, because its row
	 * names attack speed alone. The rule this function keeps is "anything naming
	 * BOTH stats belongs here", not "everything belongs here". A later effect
	 * naming both must go in this function rather than beside it.
	 */
	float SpeedMultiplier() const
	{
		return CommanderMultiplier() * CrippleMultiplier() * WraithMultiplier()
			* GroundDownMultiplier();
	}

	/**
	 * Ground Down, the Ravager's `Ravager_capstone_100` option 1. Issue #1515:
	 * "Enemies within 4 metres of you have 15% reduced Movement Speed and 15%
	 * reduced Attack Speed." It names BOTH stats, so it is a factor of
	 * `SpeedMultiplier` above, by that function's own rule.
	 *
	 * A SEPARATE SLOW, SO IT MULTIPLIES with Cripple and Commander: a crippled
	 * creature near the holder is at 0.7 x 0.85. That is the genre's shape --
	 * Path of Exile's Hinder and Maim both apply, and Temporal Chains multiplies
	 * with Chill. Ruled 2026-09-24.
	 *
	 * STATE ON THE CREATURE, NOT A TAG, so nothing counts it as a debuff.
	 * `UCataclysmDebuffs::GroundDownStep` notes it on every creature within a
	 * holder's radius each step, for three steps.
	 */
	float GroundDownMultiplier() const;

	/**
	 * Note that a holder of Ground Down has this creature within its radius, for
	 * `Percent` until the world's clock reaches `UntilSeconds`.
	 *
	 * SEVERAL HOLDERS DO NOT STACK: while one runs, the larger percent holds,
	 * as only the strongest Hinder takes effect in Path of Exile. Ruled
	 * 2026-09-24. The later end time is kept.
	 */
	void NoteGroundDown(float Percent, float UntilSeconds);

	/** The share Ground Down takes off both speeds right now, 0 to 100. */
	float GroundDownPercentNow() const;

	/**
	 * Nowhere to Run, the Ravager's `Ravager_capstone_200` option 1. Issue
	 * #1515: "Enemies within 8 metres of you cannot move away from you. They may
	 * move toward you or around you, but not further away."
	 *
	 * Note that `Holder` holds this creature until the world's clock reaches
	 * `UntilSeconds`, within `RadiusCm` of it. `UCataclysmDebuffs::
	 * NowhereToRunStep` notes it on every creature in a holder's radius each
	 * step. The place the hold measures from starts where the creature is now.
	 */
	void NoteHeldBy(const AActor* Holder, float UntilSeconds, float RadiusCm);

	/** Whether a holder of Nowhere to Run holds this creature now. */
	bool IsHeld() const;

	/**
	 * Displacement done TO this creature: a knockback, a pull, a tether's drag.
	 * The hold measures from where it landed rather than undoing it. Ruled
	 * 2026-09-25: the rule holds the creature's OWN movement, and the design
	 * document separates displacement from what the target does.
	 */
	void NoteDisplaced();

	/**
	 * Whether this creature may move itself to `Landing`: always when it is not
	 * held, and otherwise only when `Landing` is no farther from the holder than
	 * where it stands. Asked by a Phasewalker's step before it is made.
	 */
	bool MayMoveItselfTo(const FVector& Landing) const;

	/**
	 * Flee from `From` until the world's clock reaches `UntilSeconds`, WITHOUT
	 * being feared. The move is the one fear makes; no tag is applied, so no
	 * crowd-control rule, immunity or resistance touches it. For rules that
	 * make a creature run: The Plaguebearer and Morale Break. Called again, it
	 * replaces the point and the time.
	 */
	void FleeFrom(const FVector& From, float UntilSeconds);

	/** Stop fleeing a rule's point at once. */
	void StopFleeing();

	/**
	 * The point a rule told this creature to flee from, while it still is.
	 * @return false when no rule has it fleeing
	 */
	bool FleeSourceNow(FVector& OutFrom) const;

	/**
	 * Whether this creature stood back up under the Vengeful Wraiths floor rule.
	 *
	 * A FLAG AND NOT A TAG, WHICH IS THE OPPOSITE OF `CrippleMultiplier` BESIDE IT.
	 * Cripple is granted by an effect for a row's own duration and the ability system
	 * takes it away, so a tag is the single source of truth there and nothing has to be
	 * told. Being a wraith is permanent, given by a floor rule at the moment the creature
	 * is spawned: there is no effect to hang it on and nothing to take it away.
	 *
	 * IT SURVIVES A RUNG CHANGE BY ITSELF, which the wraith's other figures do not.
	 * `ApplyStartingAttributes` writes attributes and does not touch this, so a wraith
	 * raised a rung by Blood-Forged Champions or Volatile Evolution is still fast.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bIsVengefulWraith = false;

	/**
	 * Whether this creature is a voidling of the Void Parasite floor rule: an Imp that comes for the player
	 * and attaches on reaching them. Issues #1820 and #41. It puts "Voidling" under the health bar.
	 *
	 * A FLAG ON AN IMP AND NOT A CLASS OF ITS OWN. A class deriving from the Imp would name the Imp's
	 * archetype row, and `FCataclysmSaveApply::ClassForArchetype` gives a saved archetype to the first
	 * class that claims it, so a saved Imp could have come back as a voidling. The flag keeps the Imp's
	 * brain, attack and figures from `SpawnPlacedCreature` and leaves that map alone. Permanent, for the
	 * reason `bIsVengefulWraith` gives.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bIsAVoidling = false;

	/**
	 * Whether this creature is the Vampire Lord an obsidian sarcophagus let out, under the Obsidian
	 * Sarcophagi floor rule. Issues #1820 and #41. It puts "Vampire Lord" under the health bar.
	 *
	 * A FLAG ON A CREATURE OF THE FLOOR'S OWN KINDS, BECAUSE NO VAMPIRE LORD CREATURE EXISTS. It stands in for
	 * one, as Demon Prince's creature does for its prince, until the project owner names a creature for it.
	 * Permanent, for the reason `bIsVengefulWraith` gives.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bIsAVampireLord = false;

	/**
	 * Whether a portal of the Portal Unleashing floor rule sent this creature. Issues #1820 and #41. It puts
	 * "Abomination" under the health bar.
	 *
	 * A FLAG ON A CREATURE OF THE FLOOR'S OWN KINDS, BECAUSE NO ABOMINATION CREATURE EXISTS. It stands in for
	 * one until the project owner names a creature for it. Permanent, for the reason `bIsVengefulWraith` gives.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bIsAnAbomination = false;

	/**
	 * Whether this creature is one that already died and was brought back. Issues
	 * #1820 and #41.
	 *
	 * THE PROJECT OWNER'S DECISION OF 2026-09-17: "a creature that is revived or
	 * resurrected is marked, and its second death drops no loot and grants no
	 * experience." So one kill is paid for once. Set by the Celestial Divine
	 * Resurgence floor rule on every creature it raises, and by Vengeful Wraiths on
	 * every wraith: that row's own words are that the kill "stands back up", which
	 * was ruled under the owner's delegation on 2026-09-23 to be a revival.
	 *
	 * A GUARD, AN ARRIVING WAVE OR ANY OTHER NEW CREATURE IS NOT MARKED. It never
	 * died, so its first death pays in full -- the owner's own example.
	 *
	 * PERMANENT, FOR THE REASON `bIsVengefulWraith` GIVES: nothing takes it away, and
	 * a rung change does not touch it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bRisenFromTheDead = false;

	/**
	 * Whether a dungeon rule raised this creature mid-floor in answer to the player: the
	 * Unstable Portal's Warden or a Trick or Treat pair. Issues #1820 and #41.
	 *
	 * ITS DROPS ARE MARKED, and a marked drop rolls nothing for Trick or Treat when clicked,
	 * so a trick's pair cannot start another trick. Set by the dungeon game mode as it adds
	 * the creature to `CreaturesRaisedByARule`.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bRaisedByARule = false;

	/**
	 * Whether no damage reaches this creature's health, shield or mana: The Reaper, which
	 * cannot die. Issues #1820 and #41.
	 *
	 * ITS BLOWS STILL RESOLVE. Evasion, block and the announcement run as for any creature;
	 * `UCataclysmVitalAttributeSet` empties what the blow would have dealt, and puts a
	 * write straight to health back to the maximum. Set by the dungeon game mode as it
	 * raises the creature, and by nothing else.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bCannotBeHurt = false;

	/**
	 * Whether this creature's death is to pay nothing: a Blood Bond's elite, which dies because
	 * the player did, and a Plague Convergence creature, which a clock sent. Issues #1820 and
	 * #41. Set by the dungeon game mode -- on the elite immediately before its death, on a
	 * convergence creature as it spawns -- and by nothing else.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bDiesUnpaid = false;

	/** The point `FleeFrom` last named, and the clock time it lasts until. */
	FVector FleeingFrom = FVector::ZeroVector;
	float FleeUntilSeconds = -1.0f;

	/** `LastAttackUsed` before the creature has attacked at all. */
	static constexpr int32 NoAttackYet = -2;

	/** `LastAttackUsed` when the last attack was the ordinary one, which is not an ability. */
	static constexpr int32 OrdinaryAttackUsed = -1;

	/**
	 * The last attack this creature made: `NoAttackYet` (-2) before any, `OrdinaryAttackUsed`
	 * (-1) for its ordinary attack, or an index into `EnemyAbilities()` (0 or more) for an
	 * ability. Issues #1820 and #41, for Echoes of the Past.
	 *
	 * WRITTEN BY `ACataclysmEnemyController` AT THE THREE PLACES IT ATTACKS: the ordinary
	 * attack, an ability that lands after its wind-up, and one that lands at once. Only
	 * abilities are announced as skills used, so no listener could tell an ordinary attack
	 * from an ability's blow; this field is where the difference is kept.
	 */
	int32 LastAttackUsed = NoAttackYet;

	/**
	 * Whether Plague Harbingers chose this creature: it lays a disease trail as it walks, and
	 * killing it clears the trail and weakens the creatures near it. Issues #1820 and #41.
	 *
	 * WRITTEN BY `ACataclysmDungeonGameMode` AND READ UNDER THE HEALTH BAR, where
	 * `UCataclysmCombatOverlay::StatusLineFor` says "Harbinger", so the player can tell which
	 * creature to kill. Cleared when it dies and when the next floor or wave is placed.
	 */
	bool bPlagueHarbinger = false;

	/**
	 * Whether this creature's death pays the player loot and experience. False for a
	 * creature `bRisenFromTheDead` marks, and for one `bDiesUnpaid` marks. Asked by
	 * `HandleDeath`, and nothing else
	 * about a death changes: the notice is still sent and every rule still hears it.
	 */
	bool PaysForItsDeath() const { return !bRisenFromTheDead && !bDiesUnpaid; }

	/**
	 * What being a wraith does to this creature's movement and attack speed.
	 *
	 * HERE AND NOT WRITTEN ONTO TWO ATTRIBUTES, and that was measured rather than chosen.
	 * A creature's attack rate is `DesignedSecondsBetweenAttacks()` over
	 * `SpeedMultiplier()` and its walk speed is `DesignedWalkSpeedCmPerSecond` times the
	 * same, so NEITHER reads the `AttackSpeed` or `MovementSpeed` attribute. A floor rule
	 * writing those attributes on a creature does nothing at all, which is what the
	 * automation test for the row's three stats found. `SpeedMultiplier` above states the
	 * rule this follows: "anything naming BOTH stats belongs here".
	 */
	float WraithMultiplier() const;

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
		//
		// AND FEASTING, WHICH THE WALK SPEED DOES NOT GET. Its row names
		// attack speed alone. See `FeastingMultiplier`.
		return DesignedSecondsBetweenAttacks()
			/ (SpeedMultiplier() * FeastingMultiplier());
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

	/** Ground Down's share and when it ends, in world seconds. Issue #1515. */
	float GroundDownPercent = 0.0f;
	float GroundDownUntil = -1.0f;

	/**
	 * Nowhere to Run's hold. Issue #1515. Who holds this creature, until when,
	 * within what radius; where it stood when the hold last measured it; and,
	 * during a charge, how far from the holder the charge began, -1 for none.
	 */
	TWeakObjectPtr<const AActor> HeldBy;
	float HeldUntil = -1.0f;
	float HeldRadiusCm = 0.0f;
	FVector HeldLastLocation = FVector::ZeroVector;
	float HeldChargeStartCm = -1.0f;

	/**
	 * Take back whatever this creature's own movement since the last frame
	 * carried it farther from its holder. Run every frame after the charge
	 * advances. Walking and roaming are put back to the distance they had; a
	 * charge may pass through the holder but ends no farther than it began.
	 * The distance before is measured from where the holder stands NOW, so a
	 * holder walking away never drags a creature after it.
	 */
	void HoldAgainstMovingAway();

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
	 *
	 * VIRTUAL SINCE 2026-09-24, FOR THE BRUTE. It has a second designed speed
	 * for chasing and writes its walk itself; overriding this is what makes
	 * every caller -- Tick, the Vengeful Wraiths rule, a test -- reach that
	 * code rather than the base's, which the Brute's own write would then
	 * undo. Issue #1515, found while building Ground Down.
	 */
	virtual void RefreshWalkSpeed();

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
	 * A CREATURE AND A FLOOR HAZARD ARE THE ONLY TWO THINGS IN THE GAME WHOSE
	 * DAMAGE CARRIES A TYPE. This said "the only damage in the game" until a
	 * floor hazard gained one, and the hazard is the same rule rather than a
	 * second one: see the second paragraph below, and
	 * `FCataclysmHitDelivery::DamageType`, which is how a floor hazard's blow
	 * carries its type.
	 *
	 * THAT A PLAYER'S DAMAGE CARRIES NONE IS A RULING RATHER THAN AN OMISSION.
	 * The project owner settled it on 2026-08-12:
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

	/**
	 * What SetAttackDamage was last asked for. Zero means it deals nothing.
	 *
	 * THAT SECOND SENTENCE WAS FALSE UNTIL ISSUES #1820 AND #41. `ApplyStartingAttributes`
	 * guarded its write with `StartingAttackDamage > 0.0f`, so asking for zero
	 * recorded the zero and SKIPPED THE WRITE -- the attribute kept whatever it
	 * held, and a creature already given its designed damage went on dealing it
	 * in full with nothing reporting anything. The guard is now `>= 0.0f` and the
	 * sentence is true.
	 *
	 * FIVE TESTS ALREADY ASKED FOR ZERO through this setter and got what they
	 * wanted by accident, because their creatures had never been given damage, so
	 * the attribute was already zero and the call changed nothing either way.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float StartingAttackDamage = 0.0f;

	/**
	 * Whether this creature is an illusion, which deals nothing however it
	 * attacks. `Chaos_Illusory_Enemies`. Issues #1820 and #41.
	 *
	 * THE ROW: "Some enemies are illusions. They look and act like real enemies
	 * but do no damage." So an illusion is an ordinary creature in every respect
	 * this class already models -- it has health, it takes blows, it dies, its
	 * death is announced, it is drawn and animated like any other -- and one
	 * number about it is zero.
	 *
	 * IT IS HONOURED WHERE THE ATTACK DAMAGE IS WRITTEN, IN `WriteAttackDamage`,
	 * RATHER THAN WRITTEN ONCE BY WHOEVER MAKES THE ILLUSION, and that is the whole
	 * reason it is a field on the creature instead of a call at floor population.
	 * `ApplyStartingAttributes` is the one place a creature's designed numbers are
	 * decided and writes the damage through that helper, and SIX PUBLIC SETTERS
	 * re-run it -- `SetHealth`, `SetAttackDamage`, `SetArmour`, `SetRarityStep`,
	 * `SetEnergyShieldFraction` and `DrawModifiersForRarity` -- each recomputing
	 * the attack damage from `StartingAttackDamage`, the rarity's damage scale and every
	 * floor-rule multiplier. `SetPlacedDamageMultiplier`,
	 * `SetTimeAliveDamageMultiplier` and `SetFloorDepthDamageMultiplier` write it through
	 * the helper too. A zero written once is undone by any of them.
	 *
	 * TODAY NOTHING CALLS ONE AFTER A FLOOR IS POPULATED, measured 2026-09-14
	 * across `game/Source` outside the tests: the only caller of any of the six
	 * from outside this class is `ACataclysmDungeonGameMode::ApplyDesignedStats`,
	 * which is the population pass itself. **So writing the zero after that pass
	 * would also work today.** It was not done that way because that argument is
	 * a proof about the ABSENCE of a call, and it expires the first time anything
	 * raises a creature's armour mid-floor, promotes it, or re-draws its
	 * modifiers. This form cannot expire: the recompute honours the flag.
	 *
	 * IT DOES NOT MAKE THE CREATURE HARMLESS IN EVERY SENSE, and the row does not
	 * ask it to. Two things still reach the player from an illusion, both
	 * measured: the Abyssal Warden's aura strips two resistances through a status
	 * effect that deals no damage, and a status effect carrying a
	 * `FlatDamagePerTick` would not scale with attack damage at all. No creature
	 * applies one of those today -- the only status effect any creature applies
	 * is that resistance strip -- so the row's "do no damage" holds as written.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	bool bIsAnIllusion = false;

	/**
	 * What each rule that changes this creature's attack damage multiplies it by, under that
	 * rule's own key: `PlacedDamageSource` (a Grave Tide or Horde wave), `TimeAliveDamageSource`
	 * (Ravenous Hoard), `FloorDepthDamageSource` (March of Progress), `SpireDamageSource`
	 * (Golden Spires), `PlagueBeaconsDamageSource` (Pestilent Empowerment) and
	 * `TrialOfEnduranceDamageSource` (Trial of Endurance) and `ObsidianSarcophagiDamageSource` (Obsidian
	 * Sarcophagi). A source with no entry multiplies by 1.0. `WriteAttackDamage` multiplies
	 * by every entry. Issues #1820 and #41.
	 *
	 * ONE MAP RATHER THAN A FIELD PER SOURCE, as ruled by the coordinating session on
	 * 2026-09-17, so several rules can act on one creature and none may overwrite another,
	 * and a new source is a key rather than a field. The three sources before Golden Spires
	 * were three fields; their setters write their keys, so no caller moved.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	TMap<FName, float> DamageMultipliersBySource;

	/** Writes one source's entry, dropping it at 1.0, and rewrites the damage when it changed. */
	void SetDamageMultiplierFrom(const TCHAR* Source, float NewMultiplier);

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
	 * Writes the attack damage attribute from the designed figure, and nothing else.
	 * Issues #1820 and #41.
	 *
	 * ONE PLACE FOR THE FORMULA, called by `ApplyStartingAttributes` and by the two
	 * floor-rule setters: the designed figure, times the rarity's damage scale, times
	 * both floor-rule multipliers, or zero for an illusion. Two copies of it would give
	 * two answers the first time one of them changed.
	 *
	 * @param DamageScale  the rarity's damage multiplier, which the caller has read
	 */
	void WriteAttackDamage(float DamageScale);

	/**
	 * Reads the rarity's damage scale and writes the attack damage with it.
	 *
	 * WHAT BOTH FLOOR-RULE SETTERS DO AFTER STORING THEIR FIGURE, so the lookup and the
	 * write are not written twice.
	 */
	void RewriteAttackDamage();

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
