// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the aura that puts a debuff on whatever stands near. Issue #1057.
#include "AbilitySystem/CataclysmContagion.h"
// For holding the debuffs on a character still. Issue #1070.
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmLeech.h"
// For the nova a character at very low health releases. Issue #1050.
#include "AbilitySystem/CataclysmNova.h"
// For health owed falling due. Issue #991.
// For the Fervour that arrives from the passage of time. Issue #1008.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSiphon.h"
// For the Bulwark's and the Berserker's Fervour decay out of combat. Slice C of plan_death_and_war.md, 2026-11-24.
#include "AbilitySystem/CataclysmWarFervour.h"
#include "AbilitySystem/CataclysmBerserkerStates.h"
#include "AbilitySystem/CataclysmDelayedDamage.h"
// For Brute's Heart's Ground Pound, which fires by itself. Ruled 2026-10-17.
#include "AbilitySystem/CataclysmGroundPound.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
// For what a gadget does because time passed. Ruled 2026-10-13.
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSecondSelf.h"
#include "AbilitySystem/CataclysmSharedBuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
// For the side a body's pass-through bit is read from. Issue #2861.
#include "AbilitySystem/CataclysmTeams.h"
// For Brute's Heart's taunt aura, which beats by itself below half health. Ruled 2026-10-18.
#include "AbilitySystem/CataclysmTauntAura.h"
// For Chronomancer's Time-Lock's temporal rift, which appears by itself. Ruled 2026-10-19.
#include "AbilitySystem/CataclysmTimeLockRift.h"
#include "Character/CataclysmEnemyModifiers.h"
// For the capsule whose mask filter says which bodies pass through it. Issue #2861.
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPath.h"

ACataclysmCharacterBase::ACataclysmCharacterBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACataclysmCharacterBase::WearBodyBeforeComponentsRegister(const TCHAR* BodyMeshPath)
{
	// ONLY IN A GAME WORLD: a game, a Play-In-Editor session, a test world. An
	// editor level is left as it was. A character placed in one and given its
	// model here would carry the model into the level file whenever the level
	// was saved, and for the Brute that is a hard reference to art git does not
	// carry -- the thing its soft path exists to avoid.
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent || !BodyMeshPath || MeshComponent->GetSkeletalMeshAsset())
	{
		return;
	}

	// THE SAME LOAD BeginPlay DOES. With the art present, the first character of
	// each kind in a session now loads the asset here instead of in BeginPlay,
	// and every later call finds it already loaded. Without the art both calls
	// fail. The engine reports a missing asset only a couple of times a session
	// however often it is asked -- 2 "Failed to find object" lines for 8,848
	// Hellhounds spawned without their pack, counted on 2026-09-10 -- so the
	// second attempt should add nothing to such a checkout's log.
	if (USkeletalMesh* Body =
			Cast<USkeletalMesh>(FSoftObjectPath(BodyMeshPath).TryLoad()))
	{
		MeshComponent->SetSkeletalMesh(Body);
	}
}

void ACataclysmCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	// WHICH BODIES THIS ONE PASSES THROUGH, from the side its constructor gave it: the player's, and nought for a
	// creature. Issue #2861. A summoned minion is given its side after this, and `SetGenericTeamId` writes it again.
	RefreshPassThroughBody();

	// A REPEATING TIMER RATHER THAN A TICK. This is the only per-frame work any
	// of these characters would have and it does not need a frame. The base
	// disables ticking for every character deliberately, and the player pawn's
	// own Tick switches itself off the moment its camera glide settles, so
	// adding regeneration to either would undo that.
	//
	// NOT AN INFINITE PERIODIC GAMEPLAY EFFECT, which is the other shape this
	// could take. Its magnitude would have to be attribute-based, because the
	// amount comes from the character's own HealthRegen, ManaRegen and
	// EnergyShieldRegen, and an effect built at runtime with three
	// attribute-based modifiers is both harder to read and impossible to test
	// without an ability system -- whereas UCataclysmRegeneration::GainPerStep
	// and ShieldMayRefill can be checked with plain numbers.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RegenerationTimer, this,
			&ACataclysmCharacterBase::RegenerationStep,
			UCataclysmRegeneration::StepSeconds, /*bLoop=*/true,
			/*InFirstDelay=*/UCataclysmRegeneration::StepSeconds);
	}
}

void ACataclysmCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RegenerationTimer);
	}

	// AND NO COPY OF THIS CHARACTER'S BUFFS OUTLIVES IT. Issue #1833 group E part
	// 4a. Here, on the character whose step gives them, and not on its ability
	// system: a player's ability system belongs to its player state, which
	// outlives the character, so an ending there never came for a player.
	UCataclysmSharedBuffs::TakeBackAll(Cast<UCataclysmAbilitySystemComponent>(
		UCataclysmTargeting::AbilitySystemOf(this)));

	Super::EndPlay(EndPlayReason);
}

void ACataclysmCharacterBase::RegenerationStep()
{
	UCataclysmRegeneration::ApplyStep(this, UCataclysmRegeneration::StepSeconds,
									  SecondsSinceLastDamage());

	// LEECH ARRIVES ON THE SAME TIMER, AND IS NOT REGENERATION. Issue #895. The
	// design pays a hit's leech out over time rather than at once, across the
	// next three seconds by default and longer under `leech_payout_rate`, which
	// `UCataclysmLeech::PayoutSecondsFor` reads where a payment is made (#1618).
	// This is the only per-character step that already runs often enough to do
	// it smoothly. It is a separate call rather than another job
	// inside ApplyStep because the two obey different rules: an energy shield
	// waits three seconds after damage before it regenerates, and leech must not
	// wait, or it would stop exactly when it is meant to work.
	UCataclysmLeech::PayOutStep(this, UCataclysmRegeneration::StepSeconds);

	// AND HEALTH OWED FALLS DUE ON THE SAME TIMER. Issue #991. The Masochist's
	// Deferred Payment node takes part of a skill's health cost three seconds
	// after the skill is used, and this is the only per-character step that
	// already runs often enough to notice.
	//
	// A THIRD JOB HERE RATHER THAN A TIMER PER DEBT. A debt falling due a
	// fraction of a second late is not something a player can perceive, and a
	// timer per debt would be one more thing to cancel when a character dies.
	//
	// AND IT DRAINS OUT OVER FIVE SECONDS RATHER THAN ARRIVING IN ONE WRITE,
	// since issue #1120. The step length is passed in for that reason: this
	// timer is the clock the drain runs on, the same way leech above uses it.
	//
	// ITS RETURN VALUE IS DROPPED. Zero is the ordinary answer -- nothing owed,
	// or owed and not yet due -- and it is returned for tests rather than for
	// this caller.
	UCataclysmHealthDebt::DrainIfDue(this, UCataclysmRegeneration::StepSeconds);

	// AND A DEBT THAT NEVER FALLS DUE BLEEDS ITS OWNER OUT INSTEAD, ON THE SAME
	// TIMER. Issue #997, changed from an instant death to a drain by issue
	// #1120. The Masochist's The Reckoning keystone reads "If your debt ever
	// exceeds your current health, you die", and "ever" is what a step that runs
	// several times a second is for.
	//
	// AFTER THE DRAIN RATHER THAN BEFORE IT, so an ordinary debt draining this
	// step is taken first and this sees the health it left behind. The two never
	// both act on one character -- a Reckoning debt returns early from the drain
	// -- so the order changes nothing today, and it is the order that stays
	// right if that ever stops being true.
	//
	// ITS RETURN VALUE IS DROPPED, the same as the drain above and for the same
	// reason: zero is the ordinary answer and it is returned for tests.
	UCataclysmHealthDebt::DrainWhileDebtExceedsHealth(
		this, UCataclysmRegeneration::StepSeconds);

	// AND THE DELAYED SHARE OF BLOWS ALREADY TAKEN DRAINS ON THE SAME TIMER. Ruled 2026-10-12, for "25%-50% of
	// damage taken is dealt over 4-8 seconds". A worn row takes a share off each blow where it reaches health and
	// holds it; this takes what the held parcels owe for the time since the step before.
	//
	// IT IS NOT HANDED THE STEP'S LENGTH. Each parcel is counted by the world's clock from the moment it was
	// taken, so a step that arrives late takes what the late time owes.
	//
	// AFTER THE HEALTH DEBT'S TWO AND BEFORE THE GUARD BELOW, because this can kill and a character it has just
	// killed should gain nothing on this step. It skips a corpse itself.
	//
	// ITS RETURN VALUE IS DROPPED. Nought is the answer for every character holding no parcel, and it is returned
	// for tests.
	UCataclysmDelayedDamage::DrainStep(this);

	// AND A DEATH SENTENCE FALLS DUE ON THE SAME TIMER. Ruled 2026-10-17 and 2026-10-18, for "Killing a Rare or higher
	// enemy instanly heals you to full, failing to results in your death": a reprieve's seconds ended with no kill
	// that lifted it. Here for the reason the health debt gives above, so the death lands up to a quarter of a second
	// after the window ends. BEFORE THE GUARD BELOW, as the drain above is, because it kills. It refuses a corpse
	// itself, and does nothing for a character with no sentence running. Its return value is for tests.
	if (UCataclysmAbilitySystemComponent* Sentenced =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(this)))
	{
		Sentenced->CarryOutDueDeathSentence();
	}

	// AND A GADGET DOES WHAT ITS SUMMONER'S ROWS MAKE IT DO BECAUSE TIME PASSED, ON THE SAME TIMER. Ruled
	// 2026-10-13, for "Gadgets fire at all enemies within range simultaneously once every 5-8 seconds": the clock
	// of a gadget's volley is looked at here, so a volley can fire up to a quarter of a second late.
	//
	// EVERY CHARACTER COMES THROUGH HERE AND ALL BUT A GADGET ARE ANSWERED AT ONCE. The call does nothing for a
	// character that is not a minion, for a minion that is not a gadget, and for a gadget whose summoner holds no
	// such row.
	//
	// A JOB ON THIS STEP RATHER THAN A TIMER ON EACH GADGET, for the reason the health debt gives above: this
	// already runs four times a second, and a timer for each gadget is one more thing to cancel when one is
	// destroyed. A test moves the world's clock by hand and calls this job directly.
	//
	// BEFORE THE GUARD BELOW, as the drain above is, because what a gadget's step does may end the gadget. It
	// refuses a corpse itself.
	ACataclysmMinion::GadgetStep(this);

	// AND CAT'S CRADLE'S WIRES, ON THE SAME STEP (V5b2, ruled C1 and C2: "at the player's quarter-second step"). Nothing
	// for a character whose rows state no reach, which is every character but a Saboteur holding the option; a dead
	// one's wires are taken down. BEFORE THE GUARD BELOW, as the gadget's step is, so a death takes the wires down.
	if (UCataclysmAbilitySystemComponent* Cradle =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(this)))
	{
		Cradle->StepCatsCradle();
	}

	// AND FERVOUR MAY ARRIVE FROM THE PASSAGE OF TIME ALONE. Issue #1008. The
	// Masochist's Low Life keystone reads "While at or below 35% health you gain
	// 10 Fervour per second", and it is the first thing that fills the pool
	// without health having moved at all.
	//
	// A FIFTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// the debt gives above: this already runs several times a second, and a
	// timer per character is one more thing to cancel when one dies.
	//
	// AFTER THE LETHAL CHECK, so a character that just died gains nothing. The
	// step's own guards would catch it anyway -- the grant refuses a component
	// with no class resource set and this one refuses a corpse -- but the order
	// is what makes that true rather than incidental.
	//
	// ITS RETURN VALUE IS DROPPED, the same as the two calls above. Zero is the
	// ordinary answer for every character in the game.
	if (!UCataclysmSkillEffects::IsDead(this))
	{
		// AND WORN ROWS THAT GRANT ON A CLOCK, while in combat. Issue #1833,
		// timed grants. Here for the reason the Fervour step below gives: this
		// already runs several times a second, and the grant counts periods of
		// the combat rather than steps.
		if (UCataclysmAbilitySystemComponent* Cataclysm =
				Cast<UCataclysmAbilitySystemComponent>(
					UCataclysmTargeting::AbilitySystemOf(this)))
		{
			Cataclysm->StepTimedGrants();
		}

		UCataclysmFervour::GainPerSecondStep(
			UCataclysmTargeting::AbilitySystemOf(this),
			UCataclysmRegeneration::StepSeconds);

		// AND FERVOUR MAY LEAVE THE SAME WAY, FOR A CHARACTER THAT HAS LOST
		// CONTACT. Issue #1515. `Ravager_basic_spine_000` reads "Fervour decays
		// at 5 per second after 3 seconds with no enemy within 4 metres, so
		// losing contact is what empties it rather than a timer", and this is
		// the first thing in the game that takes Fervour away on a clock rather
		// than because something happened.
		//
		// AFTER THE GAIN AND INSIDE THE SAME GUARD. After, so a character
		// standing in a crowd earns and keeps rather than earning and then
		// being judged out of contact on stale state -- the gain's rate and
		// this refusal both read the world at the moment they are called, so
		// the order decides which reading each gets. Inside, because a corpse
		// should no more lose Fervour than gain it, and one guard saying so is
		// clearer than two.
		//
		// ITS RETURN VALUE IS DROPPED, the same as every call above it. Zero is
		// the ordinary answer for every character in the game: only a Ravager
		// holding the starting node, with Fervour in hand, out of contact for
		// three seconds, gets anything else.
		UCataclysmFervour::DecayStep(this, UCataclysmRegeneration::StepSeconds);

		// AND THE DEATH CLASSES' DECAY AFTER THE LAST KILL, 2026-11-24: the Soul Collector's 5 a second and the
		// Shadow's 10, once the grace after the last kill has run out. A rule of its own beside the Ravager's, for
		// the reason `UCataclysmFervour::KillDecayPerSecondStat` gives. Nought for every character without one of
		// those starting nodes, and its return value is dropped as every call above it is.
		UCataclysmFervour::KillDecayStep(UCataclysmTargeting::AbilitySystemOf(this), UCataclysmRegeneration::StepSeconds);

		// AND THE SHARES OF EARLIER SPENDS THAT CINDERS KEPT RETURNS, once due (the Death trees' slice C3c, 2026-11-25).
		// Nothing for a character with none waiting. Its return value is dropped as every call above it is.
		if (UCataclysmAbilitySystemComponent* Returning =
				Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(this)))
		{
			Returning->ReturnFervourDue();

			// AND THE FERVOUR THRESHOLDS' STEP: Long Shadow's record of what is held now, and the fourth vow's faster
			// recovery of running cooldowns (the Death trees' slice C3d). Its return value is dropped as every call
			// above it is.
			Returning->FervourThresholdStep(UCataclysmRegeneration::StepSeconds);
		}

		// AND THE SOUL COLLECTOR'S SIPHON (the Death trees' slice C2, 2026-11-25): who is in it, what each member
		// carries, and once a second what each second pays. Nothing for a character without the keystone, whose
		// record is cleared. Its return value is dropped as every call above it is.
		FCataclysmSiphon::Step(this, UCataclysmRegeneration::StepSeconds);

		// AND THE WAR CLASSES' DECAY, OUT OF COMBAT RATHER THAN OUT OF CONTACT. Slice C, 2026-11-24: the Bulwark's
		// "your Fervour decays slowly out of combat" (5 a second, the owner's Q21) and the Berserker's "decays at 10 per
		// second after 3 seconds out of combat". A separate stat from the Ravager's, so a character holding both rows
		// meets both rules. After the gain and inside the same guard, for the reasons given above. Its return value is
		// dropped too.
		FCataclysmWarFervour::OutOfCombatDecayStep(this, UCataclysmRegeneration::StepSeconds);

		// AND A DRAIN, WHICH WAITS FOR NOTHING, then the Berserker's state judged again. The Berserker's states,
		// 2026-11-25: "Fervour drains at 5 per second" while in Wrath, 8 in Frenzy, each a row under its state. After both
		// decays and inside the same guard; its return value is dropped too.
		FCataclysmBerserkerStates::DrainStep(this, UCataclysmRegeneration::StepSeconds);

		// AND VENGEFUL DISCHARGE'S OWN DRAIN, whose emptying of the pool sets off its retaliation pulse. Its own stat, so
		// no other drain or spend sets the pulse off. Its return value is dropped too. 2026-11-25.
		FCataclysmWarFervour::RetaliationPulseDrainStep(this, UCataclysmRegeneration::StepSeconds);
	}

	// AND A CHARACTER LOW ENOUGH ON HEALTH MAY RELEASE A NOVA. Issue #1050.
	// The Masochist's Unstable Aura reads "While at or below 10% health, you
	// release a nova every 5 seconds dealing damage equal to 1% of your
	// missing health per point to enemies within 6 metres", and it is the
	// only thing in the game that DEALS damage without the character acting.
	//
	// A SIXTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// the debt and the Fervour above both give: this already runs several
	// times a second, and a timer per character is one more thing to cancel
	// when one dies. The five second interval is kept by a timestamp on the
	// ability system component rather than by how often this is called.
	//
	// IT REFUSES A CORPSE ITSELF rather than sitting inside the branch above.
	// The two questions differ: that branch guards a grant to the character
	// itself, and this deals damage to other actors, so its own refusal is
	// where a reader looking for it will be.
	//
	// ITS RETURN VALUE IS DROPPED, the same as the three calls above. Zero is
	// the ordinary answer for every character in the game.
	UCataclysmNova::Step(this);

	// AND A CHARACTER MAY RADIATE AN AURA THAT PUTS A DEBUFF ON WHATEVER IS
	// STANDING NEAR IT. Issue #1057. The Masochist's Beacon of Despair reads
	// "You radiate an aura that applies a random debuff to enemies within 6
	// metres every 3 seconds", and it is the second thing in the game that
	// reaches another character without this one acting.
	//
	// A SEVENTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// every one above gives. Its three second interval is kept by a timestamp on
	// the ability system component, separate from the nova's five, so a
	// character holding both nodes runs each at its own rate.
	//
	// AFTER THE NOVA AND NOT BEFORE IT, though nothing today depends on the
	// order: they touch different things, one dealing damage and one applying a
	// lasting effect, and neither reads what the other wrote.
	//
	// IT REFUSES A CORPSE ITSELF, the same as the nova and for the same reason.
	UCataclysmContagion::AuraStep(this);

	// AND THE DEBUFFS ON A CHARACTER MAY BE HELD STILL RATHER THAN COUNTING
	// DOWN. Issue #1070. The Masochist's Ceaseless Penance reads "Debuffs on you
	// no longer expire while you are above 50% health", and something has to
	// keep pushing their end times out for as long as that stays true.
	//
	// AN EIGHTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// every one above gives, and with one more of its own: the condition can
	// stop being true between one step and the next, so what this needs is
	// exactly a thing that runs often and asks again each time.
	//
	// IT IS PASSED THE LENGTH OF THE STEP, WHICH NOTHING ELSE HERE NEEDS IN THE
	// SAME WAY. The others use it as a rate; this pushes each effect's end out
	// by precisely the time that passed, so the time left on it does not move.
	//
	// IT REFUSES A CORPSE ITSELF, the same as the two above.
	//
	// ITS RETURN VALUE IS DROPPED. Zero is the answer for every character in the
	// game without that capstone option, and it is returned for tests.
	UCataclysmDebuffs::HoldStep(this, UCataclysmRegeneration::StepSeconds);

	// AND THE CRIPPLES AND WEAKENS THIS CHARACTER APPLIED, ON ENEMIES NEAR IT.
	// Issue #1515, No Second Wind. The same step for the same reason: whether an
	// enemy is still within the radius is asked again each time.
	UCataclysmDebuffs::HoldAppliedNearbyStep(this, UCataclysmRegeneration::StepSeconds);

	// AND GROUND DOWN SLOWS THE CREATURES NEAR IT. Issue #1515. The same step,
	// so a creature that leaves the radius stops being renewed and recovers
	// three steps later.
	UCataclysmDebuffs::GroundDownStep(this, UCataclysmRegeneration::StepSeconds);

	// AND NOWHERE TO RUN HOLDS THE CREATURES NEAR IT. Issue #1515. The same step
	// and the same three-step hold as Ground Down; the creature enforces it every
	// frame between steps.
	UCataclysmDebuffs::NowhereToRunStep(this, UCataclysmRegeneration::StepSeconds);

	// AND A CHARACTER A ROW MAKES IMMUNE TO DEBUFFS LOSES THE ONES IT ALREADY
	// CARRIES. Ruled 2026-10-13, "You are immune to debuffs but also cannot apply
	// them"; that what was carried when the row was put on is removed is a
	// judgement recorded in `docs/DECISIONS.md`. On this step because nothing
	// announces a row being put on. It refuses a corpse itself, and its return
	// value, zero for every character without the row, is for tests.
	UCataclysmDebuffs::ImmunityStep(this);

	// AND A SECOND SELF IS CHOSEN AND GIVEN THIS CHARACTER'S MAXIMUM HEALTH AND
	// SPELL DAMAGE. Issue #1515. The same step, so the maximum follows live and
	// the next longest-held is chosen within one step of the last one's end.
	UCataclysmSecondSelf::Step(this);

	// AND THE ALLIES NEAR THIS CHARACTER ARE GIVEN THE MORE DAMAGE ITS BUFFS
	// GRANT IT, AND IT IS TAKEN BACK FROM ANY THAT HAVE LEFT. Issue #1833 group E
	// part 4a, ruled 2026-10-02 onto this step: a body walking in or out of
	// reach is a quarter of a second late at worst.
	UCataclysmSharedBuffs::Step(this);

	// AND A WEARER OF BRUTE'S HEART'S TEN PIECES POUNDS THE GROUND ONCE IT IS
	// READY AND AN ENEMY IS NEAR. Ruled 2026-10-17 by the owner (#5): it fires by
	// itself. On this step, so an enemy walking in is struck a quarter of a second
	// late at worst; the call answers nought for every character holding no such
	// row and refuses a corpse itself. Its return value is for tests.
	UCataclysmGroundPound::Step(this);

	// AND A WEARER OF BRUTE'S HEART'S SIX PIECES BELOW HALF HEALTH TAUNTS EVERY
	// ENEMY NEAR, ONCE A SECOND. Ruled 2026-10-18 (Q2). On this step, so the
	// beat is a quarter of a second late at worst and needs no timer of its own;
	// the call answers nought for every character holding no such row and
	// refuses a corpse itself. Its return value is for tests.
	UCataclysmTauntAura::Step(this);

	// AND A WEARER OF TYRANT'S CHAINS' TEN PIECES HAS ITS TYRANT'S MINION CAST ITS
	// ULTIMATE ON A CLOCK. Ruled 2026-10-17 (#7) and 2026-10-18 (T6): the keyed
	// skill gadget's clock, looked at here as `GadgetStep` looks at a gadget's, so
	// a cast is a quarter of a second late at worst. The call answers false for
	// every character that is not a Tyrant's Minion of a commander holding the
	// row, and refuses a corpse itself. Its return value is for tests.
	ACataclysmMinion::TyrantsMinionStep(this);

	// AND A CREATURE MAY CARRY AN ENEMY MODIFIER THAT RADIATES AN AURA. Issue
	// #742 gives a creature its modifiers; the Demonic Hellfire Aura is the
	// first of them whose effect reaches out on its own. "Emits a burning aura
	// that deals constant fire damage to nearby players."
	//
	// A NINTH JOB ON THIS STEP RATHER THAN A TIMER OF ITS OWN, for the reason
	// every one above gives.
	//
	// THE PLAYER COMES THROUGH HERE TOO, because this step is on the shared
	// character base, and the call answers zero for anything that is not a
	// creature carrying an aura modifier. That is the same arrangement the two
	// Masochist jobs above have in reverse: they answer zero for every creature.
	//
	// ITS INTERVAL IS KEPT ON THE CREATURE rather than on the ability system
	// component, unlike the nova's and the Beacon's. Those two belong to a
	// character's passive tree and there is one of each; a creature's aura
	// belongs to the creature.
	//
	// IT REFUSES A CORPSE ITSELF, the same as the three above.
	UCataclysmEnemyModifiers::AuraStep(this, UCataclysmRegeneration::StepSeconds);

	// AND A CREATURE CARRYING PHASEWALKER TELEPORTS SHORT DISTANCES. "Teleport
	// short distances every few seconds."
	//
	// A TENTH JOB ON THIS STEP, for the reason every one above gives, and it
	// answers false for anything that is not a creature carrying the
	// modifier -- which is the player, every Common creature, and every other
	// creature above Common that drew something else.
	//
	// ITS OWN INTERVAL RATHER THAN THE AURA'S, kept on the creature. A
	// creature carrying both an aura and Phasewalker runs each at its own
	// rate.
	UCataclysmEnemyModifiers::PhaseStep(this, UCataclysmRegeneration::StepSeconds);

	// AND THE THREE DEMONIC MODIFIERS THAT ACT ON A TIMER OF THEIR OWN:
	// Infernal Sacrifice eating an ally, Unholy Sigils laying a sigil, and
	// Inferno Charge setting off. Issue #742.
	//
	// AN ELEVENTH JOB ON THIS STEP, for the reason every one above gives, and
	// one call rather than three because all three are the same shape: a
	// countdown on the creature and something that happens when it reaches
	// zero. A creature carrying two of them runs each at its own rate.
	//
	// IT REFUSES A CORPSE ITSELF, the same as the four above.
	UCataclysmEnemyModifiers::TimedStep(this, UCataclysmRegeneration::StepSeconds);

	// AND WHERE THIS CHARACTER IS, SO THAT ITS MOVEMENT CAN BE READ. Issue #41,
	// slice 2. Rows ask whether a character is moving, how long it has stood
	// still, and how far it has walked since its own last attack, and nothing
	// could answer before this.
	//
	// A TWELFTH JOB ON THIS STEP, for the reason every one above gives, and it
	// suits this one especially: the step already runs for the player, every
	// creature and every minion, so all three get the same readings.
	//
	// IT KEEPS NO CLOCK OF ITS OWN. The sample only says what it saw; the
	// character's ability system component stamps the clocks from the world.
	UCataclysmMovement::SampleStep(this);

	// AND A WEARER OF CHRONOMANCER'S TIME-LOCK'S TEN PIECES PLACES ITS TEMPORAL RIFT ONCE IT IS READY AND AN ENEMY
	// IS NEAR, AND THE RIFT STANDING CATCHES WHOEVER HAS COME INTO IT. Ruled 2026-10-19 (K4, K5). On this step, as
	// the Ground Pound is, so an enemy walking in is caught a quarter of a second late at worst; the call answers
	// nought for every character holding no such row and refuses a corpse itself. Its return value is for tests.
	UCataclysmTimeLockRift::Step(this);

	// AND WHATEVER A SUBCLASS KEEPS THAT MUST BE ASKED FOR AGAIN AS TIME PASSES.
	// Issue #1821. LAST, so it sees this step's movement sample and every write
	// the jobs above made.
	AfterRegenerationStep();
}

void ACataclysmCharacterBase::NoteDamageTaken()
{
	const UWorld* World = GetWorld();
	LastDamagedAtSeconds = World ? World->GetTimeSeconds() : 0.0f;
}

bool ACataclysmCharacterBase::IsRegenerating() const
{
	const UWorld* World = GetWorld();
	return World
		&& World->GetTimerManager().IsTimerActive(RegenerationTimer);
}

float ACataclysmCharacterBase::SecondsSinceLastDamage() const
{
	// NEVER HURT ANSWERS WITH A LARGE NUMBER RATHER THAN WITH ZERO. Zero would
	// read as "hurt this instant" and would stop an energy shield ever filling
	// on a character nothing has touched.
	if (LastDamagedAtSeconds < 0.0f)
	{
		return TNumericLimits<float>::Max();
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return TNumericLimits<float>::Max();
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastDamagedAtSeconds);
}

UAbilitySystemComponent* ACataclysmCharacterBase::GetAbilitySystemComponent() const
{
	// Subclasses decide where the component lives.
	return nullptr;
}

void ACataclysmCharacterBase::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;

	// AND THE BODY GOES WITH THE SIDE. Issue #2861: "A thrall changes sides when taken, so its collision must change
	// with it." `UCataclysmCommand::Subjugate`, `ACataclysmMinion::Spawn` and a load's thrall all give a side through
	// here, so none of them can leave a body on the side it came from.
	RefreshPassThroughBody();
}

uint8 ACataclysmCharacterBase::PassThroughBodyMaskBit() const
{
	// ON THE PLAYER'S SIDE AND NOT A PLAYER: a summoned minion, a deployed gadget, a thrall. The player answers for
	// itself, in `ACataclysmPlayerCharacter`.
	return TeamId == UCataclysmTeams::IdFor(ECataclysmTeam::Players) ? CommandedBodyMaskBit : 0;
}

uint8 ACataclysmCharacterBase::MovesThroughMaskFor(uint8 OwnBit)
{
	// THE OTHER TWO AND NOT ITS OWN, so two commanded bodies still block each other, as they did before issue #2861.
	return OwnBit != 0 ? static_cast<uint8>(PassThroughBodyMaskBits & ~OwnBit) : static_cast<uint8>(0);
}

void ACataclysmCharacterBase::RefreshPassThroughBody()
{
	// THE ENGINE HAS SIX BITS FOR THIS (`NumExtraFilterBits`), AND THREE ARE USED.
	static_assert(PassThroughBodyMaskBits < (1 << NumExtraFilterBits),
				  "A pass-through bit above the engine's extra filter bits would be cut off the physics filter.");

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	// BOTH HALVES: what this body is when another moves into it, and what it moves through itself. A body whose bit is
	// nought ignores nothing, which is every creature; and a creature on the monsters' side carries `HostileBodyMaskBit`
	// as what it is, which only a character with Hollow Step ignores, and only while its movement skill moves it (the
	// Death trees' slice C6e, 2026-11-25). Before that, no creature's filter carried a bit at all.
	static_assert((HostileBodyMaskBit & PassThroughBodyMaskBits) == 0 && HostileBodyMaskBit < (1 << NumExtraFilterBits),
				  "The hostile body bit must be its own, and inside the engine's extra filter bits.");
	const uint8 OwnBit = PassThroughBodyMaskBit();
	const bool bHostile = OwnBit == 0 && TeamId == UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);
	Capsule->SetMaskFilterOnBodyInstance(bHostile ? HostileBodyMaskBit : OwnBit);
	Capsule->SetMoveIgnoreMask(MovesThroughMaskFor(OwnBit));
}

void ACataclysmCharacterBase::SetMovesThroughHostiles(bool bThrough)
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetMoveIgnoreMask(
			MovesThroughMaskFor(PassThroughBodyMaskBit()) | (bThrough ? HostileBodyMaskBit : static_cast<uint8>(0)));
	}
}

bool ACataclysmCharacterBase::MovesThroughHostiles() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	return Capsule && (Capsule->GetMoveIgnoreMask() & HostileBodyMaskBit) != 0;
}

FGenericTeamId ACataclysmCharacterBase::GetGenericTeamId() const
{
	return TeamId;
}

void ACataclysmCharacterBase::InitAbilityActorInfo()
{
	// Subclasses supply the owner and avatar.
}
