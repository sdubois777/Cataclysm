// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmHUD.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For asking whether this character can move Fervour at all. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// ---------------------------------------------------------------------------
// The colours, as six-digit sRGB hex so they can be read straight against
// section XIII of docs/Cataclysm_GDD_v2.md, which lists every colour in the
// game the same way. The leading hash is dropped because FColor::FromHex is
// what parses these, matching ACataclysmTelegraphMarker::DesignedRingHex and
// the rest of that class's constants. The reasoning for each colour is on its
// declaration in the header.
// ---------------------------------------------------------------------------

const TCHAR* UCataclysmCombatOverlay::BarBackingHex = TEXT("0A0F12");
const TCHAR* UCataclysmCombatOverlay::HealthFillHex = TEXT("C0392B");
const TCHAR* UCataclysmCombatOverlay::ShieldFillHex = TEXT("4FA3E3");
const TCHAR* UCataclysmCombatOverlay::ManaFillHex = TEXT("2E4FC0");
const TCHAR* UCataclysmCombatOverlay::FervourFillHex = TEXT("C7398D");
const TCHAR* UCataclysmCombatOverlay::ReachedHealthHex = TEXT("F5F0EA");
const TCHAR* UCataclysmCombatOverlay::AbsorbedHex = TEXT("4FA3E3");
const TCHAR* UCataclysmCombatOverlay::NothingThroughHex = TEXT("8C9196");
const TCHAR* UCataclysmCombatOverlay::CriticalStrikeHex = TEXT("FFA31F");
const TCHAR* UCataclysmCombatOverlay::RarityNameHex = TEXT("F5F0EA");

namespace
{
	/**
	 * Whether floating damage numbers are drawn.
	 *
	 * ON BY DEFAULT, which is a decision rather than a convenience. Diablo 4 and
	 * Last Epoch both ship damage numbers with a switch; Path of Exile 1 and 2
	 * have never had them at all. This project follows the majority and keeps
	 * the switch, because the whole reason the feature exists is to settle
	 * combat figures by playing them, and that is easier when they are there
	 * without being asked for.
	 */
	TAutoConsoleVariable<int32> CVarShowDamageNumbers(
		TEXT("Cataclysm.Overlay.DamageNumbers"),
		1,
		TEXT("Whether a floating number is drawn where a blow lands. 1 draws "
			 "them, 0 does not. A number appears for an evaded hit and for one "
			 "armour and resistance took to nothing, which the impact particle "
			 "deliberately does not."),
		ECVF_Default);

	/**
	 * Whether a bar is drawn over a damaged creature.
	 *
	 * IT ONLY EVER DRAWS OVER SOMETHING ALREADY HURT, whatever this is set to.
	 * See UCataclysmCombatOverlay::ShouldShowBarFor.
	 */
	TAutoConsoleVariable<int32> CVarShowOverheadBars(
		TEXT("Cataclysm.Overlay.OverheadBars"),
		1,
		TEXT("Whether a health bar is drawn over creatures. 1 draws them, 0 "
			 "does not. Nothing is ever drawn over an undamaged creature or a "
			 "dead one, whatever this is set to."),
		ECVF_Default);

	/**
	 * Whether an enemy's rarity is said over its head.
	 *
	 * SEPARATE FROM THE BAR'S TOGGLE, because the two answer different
	 * questions. A bar says how a fight is going; a rarity says whether to
	 * start one, which is why it appears before anything has been hit.
	 */
	TAutoConsoleVariable<int32> CVarShowRarityNames(
		TEXT("Cataclysm.Overlay.RarityNames"),
		1,
		TEXT("Whether an enemy's rarity is drawn over it. 1 draws it, 0 does "
			 "not. Nothing is ever drawn over a Common enemy or a dead one, "
			 "whatever this is set to."),
		ECVF_Default);

	/** Whether the player's own health is drawn on the frame. */
	TAutoConsoleVariable<int32> CVarShowPlayerVitals(
		TEXT("Cataclysm.Overlay.PlayerVitals"),
		1,
		TEXT("Whether the player's own health, and its energy shield when it "
			 "has one, are drawn in the corner of the screen. 1 draws them, 0 "
			 "does not."),
		ECVF_Default);

	/** The vital attribute set on any actor that has one. Null when it has not. */
	const UCataclysmVitalAttributeSet* VitalsSetOf(const AActor* Actor)
	{
		// THROUGH THE INTERFACE RATHER THAN THROUGH A CAST, so this works for a
		// player, whose ability system lives on its player state, and for an
		// enemy, whose lives on its pawn, without knowing which it is holding.
		const UAbilitySystemComponent* AbilitySystem =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		return AbilitySystem
			? AbilitySystem->GetSet<UCataclysmVitalAttributeSet>()
			: nullptr;
	}
}

FLinearColor UCataclysmCombatOverlay::ColourFromHex(const TCHAR* Hex)
{
	// FColor::FromHex DOES NOT REPORT BAD INPUT, it returns something, so these
	// are compile-time constants in this file rather than anything a person can
	// type. The telegraph's live colour overrides are the place that has to
	// defend against a typo, and it does; nothing here reads a console variable.
	return FLinearColor(FColor::FromHex(FString(Hex)));
}

bool UCataclysmCombatOverlay::DamageNumbersEnabled()
{
	return CVarShowDamageNumbers.GetValueOnAnyThread() != 0;
}

bool UCataclysmCombatOverlay::OverheadBarsEnabled()
{
	return CVarShowOverheadBars.GetValueOnAnyThread() != 0;
}

bool UCataclysmCombatOverlay::PlayerVitalsEnabled()
{
	return CVarShowPlayerVitals.GetValueOnAnyThread() != 0;
}

bool UCataclysmCombatOverlay::ShouldShowNumberFor(
	const FCataclysmIncomingHit& Hit, const FCataclysmDamageResult& Outcome,
	float HealthRemaining)
{
	// A KILLING BLOW LEAVES HEALTH AT ZERO AND MUST STILL BE DRAWN, which is
	// why this asks both questions rather than only the first. What is refused
	// is a hit that arrived at something already dead and therefore did
	// nothing: a burn still ticking on a corpse, or a second blow landing in
	// the same second as the one that killed it.
	const bool bTargetWasAlreadyDown = HealthRemaining <= 0.0f
		&& Outcome.DealtToHealth <= 0.0f;

	return !bTargetWasAlreadyDown;
}

int32 UCataclysmCombatOverlay::FigureFor(float Amount)
{
	if (Amount <= 0.0f)
	{
		return 0;
	}

	// FMath::RoundToInt IS FloorToInt(F + 0.5f), so it answers 0 for anything
	// below half a point of damage. The floor of 1 is what stops that reading as
	// "the defence stopped everything" when the truth is the opposite.
	return FMath::Max(1, FMath::RoundToInt(Amount));
}

FString UCataclysmCombatOverlay::TextFor(const FCataclysmDamageResult& Outcome)
{
	if (Outcome.bEvaded)
	{
		return TEXT("Evaded");
	}

	// ROUNDED, BUT NEVER ROUNDED AWAY. See FigureFor: any damage at all prints
	// at least 1, because "0" already means the defence stopped the whole blow.
	const int32 ToHealth = FigureFor(Outcome.DealtToHealth);
	const int32 ToShield = FigureFor(Outcome.AbsorbedByShield);
	const int32 ToMana = FigureFor(Outcome.AbsorbedByMana);

	if (ToHealth > 0)
	{
		// BOTH FIGURES WHEN A SHIELD TOOK SOME OF IT, health first, because
		// health is the one that matters and a shield stripping is secondary.
		//
		// THIS PAIR IS ALSO WHAT PAYS FOR THE CRITICAL STRIKE COLOUR. Colour no
		// longer separates a hit that reached health from one a shield absorbed
		// when the hit was a critical strike, because the colour says it was
		// one. These two figures still separate them.
		return ToShield > 0
			? FString::Printf(TEXT("%d (+%d)"), ToHealth, ToShield)
			: FString::Printf(TEXT("%d"), ToHealth);
	}

	if (ToShield > 0)
	{
		return FString::Printf(TEXT("%d"), ToShield);
	}

	if (ToMana > 0)
	{
		return FString::Printf(TEXT("%d"), ToMana);
	}

	// A BLOCK THAT LEFT NOTHING SAYS SO IN A WORD. A block removes half the
	// hit, so a blocked blow usually still lands something and takes the branch
	// above; this is the case where half of a small hit was already nothing.
	if (Outcome.bBlocked)
	{
		return TEXT("Blocked");
	}

	// NOTHING GOT THROUGH AND IT WAS NOT DODGED OR BLOCKED, so armour,
	// resistance and flat reduction between them took the whole hit. That is
	// exactly the case issues #483 and #644 are about, and a zero on screen is
	// the only way to see it happening while playing.
	return TEXT("0");
}

FLinearColor UCataclysmCombatOverlay::ColourFor(
	const FCataclysmDamageResult& Outcome)
{
	// A CRITICAL STRIKE FIRST, AHEAD OF WHERE THE DAMAGE WENT. Colour said only
	// where the damage went until issue #668, and a critical strike was marked
	// by size and an exclamation mark instead. The project owner played that and
	// could not tell one from an ordinary hit, so colour took the job.
	//
	// IT ASKS ShowsCriticalStrike RATHER THAN Outcome.bWasCritical, which is what
	// keeps this branch from swallowing the grey. A critical strike is rolled
	// before block, armour and resistance, so a well-defended target can stop one
	// dead -- and an orange "0" would say the opposite of what happened. The size
	// asks the same question, so the two cannot disagree.
	if (ShowsCriticalStrike(Outcome))
	{
		return ColourFromHex(CriticalStrikeHex);
	}

	if (Outcome.DealtToHealth > 0.0f)
	{
		return ColourFromHex(ReachedHealthHex);
	}

	if (Outcome.AbsorbedByShield > 0.0f || Outcome.AbsorbedByMana > 0.0f)
	{
		return ColourFromHex(AbsorbedHex);
	}

	// Evaded, blocked to nothing, or wholly mitigated. The text says which.
	return ColourFromHex(NothingThroughHex);
}

bool UCataclysmCombatOverlay::ShowsCriticalStrike(
	const FCataclysmDamageResult& Outcome)
{
	// THE SAME THREE FIGURES ColourFor ASKS ABOUT, in the same form, so a number
	// cannot be drawn large and marked while its colour says nothing arrived.
	return Outcome.bWasCritical
		&& (Outcome.DealtToHealth > 0.0f || Outcome.AbsorbedByShield > 0.0f
			|| Outcome.AbsorbedByMana > 0.0f);
}

float UCataclysmCombatOverlay::ScaleFor(const FCataclysmIncomingHit& Hit,
										const FCataclysmDamageResult& Outcome)
{
	// A TICK IS NOT A BLOW, and the size is what says so. The impact particle
	// solves the same problem by refusing to draw for a tick at all -- issue
	// #563 measured one attack producing seven bursts in five seconds, five of
	// them burn ticks. A number is cheap enough to draw for every tick, which is
	// what the genre does, but it should not shout as loudly as the strike that
	// started the burn.
	float Scale = Hit.bIsDamageOverTime ? DamageOverTimeScale : 1.0f;

	// AND A CRITICAL STRIKE IS LOUDER THAN A BLOW, which is the whole point of
	// marking one. MULTIPLIED RATHER THAN ASSIGNED, so the tick rule above still
	// holds if the two ever meet. They cannot today: a damage over time tick can
	// never critically strike.
	if (ShowsCriticalStrike(Outcome))
	{
		Scale *= CriticalStrikeScale;
	}

	return Scale;
}

bool UCataclysmCombatOverlay::ShouldShowBarFor(float Health, float MaxHealth)
{
	if (MaxHealth <= 0.0f)
	{
		return false;
	}

	// A CORPSE GETS NOTHING. An enemy destroys itself on the tick AFTER it
	// dies, because ACataclysmEnemyCharacter::HandleDeath runs inside the
	// gameplay effect callback that dealt the killing blow, so without this an
	// empty bar flashes for one frame at the end of every fight.
	if (Health <= 0.0f)
	{
		return false;
	}

	// AND NOTHING OVER SOMETHING UNHURT. The project owner's decision, and what
	// Path of Exile does even with its own enemy life bar setting enabled.
	return Health < MaxHealth;
}

bool UCataclysmCombatOverlay::RarityNamesEnabled()
{
	return CVarShowRarityNames.GetValueOnAnyThread() != 0;
}

bool UCataclysmCombatOverlay::ShouldShowRarityNameFor(int32 RarityStep,
													  float Health,
													  float MaxHealth)
{
	// A COMMON CREATURE IS NOT MARKED. See the header: marking every creature
	// would mark none of them.
	if (RarityStep < LowestMarkedRarityStep)
	{
		return false;
	}

	if (MaxHealth <= 0.0f)
	{
		return false;
	}

	// A CORPSE GETS NOTHING, the same rule and the same reason ShouldShowBarFor
	// gives: an enemy destroys itself on the tick AFTER it dies.
	if (Health <= 0.0f)
	{
		return false;
	}

	// AND NOTHING ELSE IS ASKED. Unlike a health bar, this does NOT wait for the
	// creature to be hurt. A rarity that only appeared once the fight had
	// started would tell the player what they had already found out.
	return true;
}

FString UCataclysmCombatOverlay::PoolTextFor(float Current, float Maximum)
{
	if (Maximum <= 0.0f)
	{
		return FString();
	}

	// A POOL THAT IS NOT EMPTY NEVER READS ZERO. See the header: a character
	// on 0.3 health is alive, and rounding alone would print "0" for them.
	const int32 Left = Current > 0.0f
		? FMath::Max(1, FMath::RoundToInt(Current))
		: 0;

	return FString::Printf(TEXT("%d / %d"), Left,
						   FMath::RoundToInt(Maximum));
}

float UCataclysmCombatOverlay::BarFractionFor(float Current, float Maximum)
{
	if (Maximum <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Clamp(Current / Maximum, 0.0f, 1.0f);
}

bool UCataclysmCombatOverlay::IsOverheadBarCandidate(
	const AActor* Actor, const AActor* LocalPlayerPawn)
{
	if (!Actor)
	{
		return false;
	}

	// THE PLAYER'S OWN PAWN IS EXCLUDED BY IDENTITY, not by class, so this stays
	// right if a second pawn class is ever playable and wrong for nobody if one
	// never is. The player's health is on the frame instead, where it can be
	// read without looking away from what is attacking.
	if (Actor == LocalPlayerPawn)
	{
		return false;
	}

	return !UCataclysmSkillEffects::IsDead(Actor);
}

float UCataclysmCombatOverlay::AnchorHeightFor(const AActor* Actor)
{
	if (!Actor)
	{
		return AnchorMarginCm;
	}

	// bOnlyCollidingComponents IS FALSE ON PURPOSE. The placeholder body every
	// character carries is a /Engine/BasicShapes mesh attached to the root, and
	// asking only for colliding components answers with the capsule, which is
	// not always the tallest thing on the actor.
	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);

	// MEASURED FROM THE ACTOR'S OWN LOCATION rather than from the bounds
	// centre, because the caller adds this to GetActorLocation. An actor with no
	// components has no bounds at all and gets the margin alone, which is the
	// honest answer for something with no body rather than a guess at one.
	const float TopOfBody = (Origin.Z + Extent.Z) - Actor->GetActorLocation().Z;
	return FMath::Max(TopOfBody, 0.0f) + AnchorMarginCm;
}

float UCataclysmCombatOverlay::RisePixelsFor(float Age)
{
	const float Fraction =
		FMath::Clamp(Age / NumberLifetimeSeconds, 0.0f, 1.0f);
	return NumberRisePixels * Fraction;
}

float UCataclysmCombatOverlay::FadeFor(float Age)
{
	if (Age <= 0.0f)
	{
		return 1.0f;
	}

	if (Age >= NumberLifetimeSeconds)
	{
		return 0.0f;
	}

	// FULLY OPAQUE FIRST, THEN FADING. A number that starts fading immediately
	// is hardest to read at the moment it is most wanted, which is the instant
	// the blow lands.
	const float OpaqueUntil = NumberLifetimeSeconds * NumberOpaqueShare;
	if (Age <= OpaqueUntil)
	{
		return 1.0f;
	}

	const float FadingOver = NumberLifetimeSeconds - OpaqueUntil;
	return FMath::Clamp(1.0f - (Age - OpaqueUntil) / FadingOver, 0.0f, 1.0f);
}

bool UCataclysmCombatOverlay::VitalsOf(const AActor* Actor, float& OutHealth,
									   float& OutMaxHealth)
{
	const UCataclysmVitalAttributeSet* Vitals = VitalsSetOf(Actor);
	if (!Vitals)
	{
		return false;
	}

	OutHealth = Vitals->GetHealth();
	OutMaxHealth = Vitals->GetMaxHealth();
	return true;
}

bool UCataclysmCombatOverlay::ShieldOf(const AActor* Actor, float& OutShield,
									   float& OutMaxShield)
{
	const UCataclysmVitalAttributeSet* Vitals = VitalsSetOf(Actor);
	if (!Vitals)
	{
		return false;
	}

	OutShield = Vitals->GetEnergyShield();
	OutMaxShield = Vitals->GetMaxEnergyShield();
	return true;
}

bool UCataclysmCombatOverlay::ManaOf(const AActor* Actor, float& OutMana,
									 float& OutMaxMana)
{
	const UCataclysmVitalAttributeSet* Vitals = VitalsSetOf(Actor);
	if (!Vitals)
	{
		return false;
	}

	OutMana = Vitals->GetMana();
	OutMaxMana = Vitals->GetMaxMana();
	return true;
}

bool UCataclysmCombatOverlay::FervourOf(const AActor* Actor, float& OutFervour,
										float& OutMaxFervour)
{
	// THROUGH THE INTERFACE, the same way `VitalsSetOf` above reaches the vital
	// set, so this works for a player whose ability system lives on its player
	// state without knowing that it does.
	const UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem
			? AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>()
			: nullptr;
	if (!Resource)
	{
		return false;
	}

	// A GENERATOR AND NOT MERELY A POOL. Every class has a maximum of 100, so
	// asking about the maximum would answer true for every character in the game
	// and draw a bar that could only ever read zero. Asking whether anything can
	// move it makes the bar's appearance mean "the generator node you spent on
	// is working", which is what the project owner needs to see. Issue #954.
	if (!UCataclysmFervour::HasAGenerator(AbilitySystem))
	{
		return false;
	}

	OutFervour = Resource->GetClassResource();
	OutMaxFervour = Resource->GetMaxClassResource();
	return true;
}

void UCataclysmCombatOverlay::Record(const AActor* Struck,
									 const FCataclysmIncomingHit& Hit,
									 const FCataclysmDamageResult& Outcome)
{
	if (!Struck || !DamageNumbersEnabled())
	{
		return;
	}

	const UWorld* World = Struck->GetWorld();
	if (!World)
	{
		return;
	}

	// ASKED OF THE STRUCK ACTOR'S OWN WORLD, so a hit landing in a world with no
	// local player -- a dedicated server, and every automation test -- takes the
	// null path and draws nothing rather than reaching for whatever world
	// happens to be current.
	//
	// THIS RUNS ON THE AUTHORITY, WHICH IS WHY A REMOTE CLIENT WOULD SEE NO
	// NUMBERS. A gameplay effect only executes on the authority, so in a session
	// with a remote player every number is made in the server's process. Nothing
	// is broken today, because the sandbox runs standalone and authority and the
	// local player are the same process. Issue #651 records what a gameplay cue
	// would take. The BARS are not affected: they read replicated Health and
	// MaxHealth every frame rather than reacting to an event.
	const APlayerController* Controller = World->GetFirstPlayerController();
	ACataclysmHUD* HeadsUpDisplay =
		Controller ? Cast<ACataclysmHUD>(Controller->GetHUD()) : nullptr;
	if (!HeadsUpDisplay)
	{
		return;
	}

	float HealthRemaining = 0.0f;
	float MaxHealth = 0.0f;
	VitalsOf(Struck, HealthRemaining, MaxHealth);

	if (!ShouldShowNumberFor(Hit, Outcome, HealthRemaining))
	{
		return;
	}

	FCataclysmDamageNumber Number;
	Number.Text = TextFor(Outcome);
	Number.Colour = ColourFor(Outcome);
	Number.Scale = ScaleFor(Hit, Outcome);
	Number.StartedAt = World->GetTimeSeconds();
	Number.WorldAnchor = Struck->GetActorLocation()
		+ FVector(0.0f, 0.0f, AnchorHeightFor(Struck));

	HeadsUpDisplay->AddDamageNumber(Number);
}
