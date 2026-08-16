// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmHUD.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
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
const TCHAR* UCataclysmCombatOverlay::ReachedHealthHex = TEXT("F5F0EA");
const TCHAR* UCataclysmCombatOverlay::AbsorbedHex = TEXT("4FA3E3");
const TCHAR* UCataclysmCombatOverlay::NothingThroughHex = TEXT("8C9196");

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

float UCataclysmCombatOverlay::ScaleFor(const FCataclysmIncomingHit& Hit)
{
	// A TICK IS NOT A BLOW, and the size is what says so. The impact particle
	// solves the same problem by refusing to draw for a tick at all -- issue
	// #563 measured one attack producing seven bursts in five seconds, five of
	// them burn ticks. A number is cheap enough to draw for every tick, which is
	// what the genre does, but it should not shout as loudly as the strike that
	// started the burn.
	return Hit.bIsDamageOverTime ? DamageOverTimeScale : 1.0f;
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
	Number.Scale = ScaleFor(Hit);
	Number.StartedAt = World->GetTimeSeconds();
	Number.WorldAnchor = Struck->GetActorLocation()
		+ FVector(0.0f, 0.0f, AnchorHeightFor(Struck));

	HeadsUpDisplay->AddDamageNumber(Number);
}
