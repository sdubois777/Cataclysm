// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "NiagaraEffectType.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * NS_Impact_Ground: what a patch of burning ground looks like.
 *
 * WHAT THESE ARE GUARDING AGAINST is the return of nothing. Before 2026-08-22
 * `ACataclysmGroundZone` had one scene component and no visuals whatsoever, so
 * every patch of burning ground in the game was invisible and its damage was the
 * only evidence it existed. Eight of the sixteen designed Demonic skills leave
 * one. The project owner's words on 2026-08-22 were "the big stomps and ring
 * aoes are still nothing", and they were literally right. Issue #811.
 *
 * NOTHING HERE SPAWNS THE EFFECT. Niagara's `CreateNiagaraSystem` checks
 * `FApp::CanEverRender()` and the automation command in `tools/unreal_build.py`
 * passes `-nullrhi`, so no component can be observed. Issue #559. What can be
 * checked is the asset, the arithmetic that feeds it, and -- through
 * `UCataclysmGroundEffect::TimesAsked` -- whether a zone still asks for one.
 */

namespace CataclysmGroundEffectTest
{
	UNiagaraSystem* LoadGroundSystem()
	{
		return LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmGroundEffect::SystemAssetPath);
	}

	/** Metres, so these read the way the design document does. */
	constexpr float M = 100.0f;
}

// --------------------------------------------------------------------------
// The asset
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneDrawsARing,
	"Cataclysm.Effects.GroundZoneDrawsARingMeshAndNotASprite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundZoneDrawsARing::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmGroundEffectTest::LoadGroundSystem();
	if (!System)
	{
		AddError(FString::Printf(TEXT("NS_Impact_Ground does not exist at %s."),
			UCataclysmGroundEffect::SystemAssetPath));
		return false;
	}

	// A system with no effect type is culled by nothing whatsoever. This one
	// takes FXT_Ambient rather than one of the two Kill variants because a
	// burning patch lasts up to ten seconds and a player walks away from it and
	// back: docs/Niagara_Conventions.md section 4 says anything looping needs a
	// reaction that brings it back, "or it never comes back when the player
	// walks back into range". A zone that vanished for good would leave damage
	// on invisible ground, which is worse than the state before this existed.
	const UNiagaraEffectType* Type = System->GetEffectType();
	if (!Type)
	{
		AddError(TEXT("NS_Impact_Ground has no effect type, so nothing culls it "
					  "by distance or by instance count."));
		return false;
	}
	TestEqual(TEXT("NS_Impact_Ground uses FXT_Ambient, which comes back when the "
				   "player walks away and returns"),
		Type->GetName(), FString(TEXT("FXT_Ambient")));

	// ONE AND EXACTLY ONE EMITTER. Counted rather than looked up by name,
	// because a system created from an engine template arrives carrying that
	// template's own emitter and the Niagara stack reports a clean compile with
	// the stray still in it.
	TestEqual(TEXT("NS_Impact_Ground has one emitter and no template leftovers"),
		System->GetEmitterHandles().Num(), 1);

	const FNiagaraEmitterHandle* Ring = nullptr;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName() == FName(TEXT("Ring")))
		{
			Ring = &Handle;
		}
	}
	if (!Ring)
	{
		AddError(TEXT("NS_Impact_Ground must have an emitter named Ring."));
		return false;
	}

	TestTrue(TEXT("the ring emitter is enabled"), Ring->GetIsEnabled());

	const FVersionedNiagaraEmitterData* RingData = Ring->GetEmitterData();
	if (!RingData)
	{
		AddError(TEXT("the Ring emitter carries no data."));
		return false;
	}

	// A MESH AND NOT A SPRITE. A camera-facing sprite cannot lie flat on the
	// ground, and the whole job of this effect is to say where the ground is
	// dangerous. A sprite renderer would satisfy "the zone draws something" and
	// lose the reason it was built.
	const UNiagaraMeshRendererProperties* Mesh = nullptr;
	for (const UNiagaraRendererProperties* Renderer : RingData->GetRenderers())
	{
		if (const auto* AsMesh = Cast<UNiagaraMeshRendererProperties>(Renderer))
		{
			Mesh = AsMesh;
		}
	}
	if (!Mesh)
	{
		AddError(TEXT("the Ring emitter has no mesh renderer, so the zone is a "
					  "flat sprite or nothing at all."));
		return false;
	}

	TestFalse(TEXT("the ring casts no shadow"), Mesh->bCastShadows);

	// THE MESH AND MATERIAL COME OUT OF A GITIGNORED PACK, so on a fresh clone
	// they resolve to null and the ring draws with the engine default. Reported
	// rather than failed; everything above was checked either way.
	const bool bHasMesh = Mesh->Meshes.Num() > 0 && Mesh->Meshes[0].Mesh != nullptr;
	const bool bHasMaterial = Mesh->bOverrideMaterials
		&& Mesh->OverrideMaterials.Num() > 0
		&& Mesh->OverrideMaterials[0].ExplicitMat != nullptr;

	if (!bHasMesh || !bHasMaterial)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("which mesh and material the ring draws with is not checked; "
				 "the effect type, the emitter count, the renderer class and "
				 "the shadow setting are. The Easy Shockwaves VFX pack is not "
				 "installed."));
		return true;
	}

	TestEqual(TEXT("the ring draws the pack's floor ring mesh"),
		Mesh->Meshes[0].Mesh->GetName(),
		FString(TEXT("SM_VFX_Cyl_In_Out_Floor_01")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneExposesTheStandardBlock,
	"Cataclysm.Effects.GroundZoneExposesTheStandardParameterBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundZoneExposesTheStandardBlock::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmGroundEffectTest::LoadGroundSystem();
	if (!System)
	{
		AddError(TEXT("NS_Impact_Ground does not exist."));
		return false;
	}

	TSet<FString> Exposed;
	for (const FNiagaraVariableWithOffset& Variable :
			System->GetExposedParameters().ReadParameterVariables())
	{
		Exposed.Add(Variable.GetName().ToString());
	}

	// THE NAMES ARE IDENTICAL ACROSS EVERY SYSTEM ON PURPOSE, so a skill row can
	// set them without knowing which system it spawned.
	// docs/Niagara_Conventions.md section 2 fixes this list.
	const TCHAR* Required[] = {
		TEXT("User.ElementColour"),
		TEXT("User.ElementColourDark"),
		TEXT("User.Intensity"),
		TEXT("User.Scale"),
		TEXT("User.Duration"),
		TEXT("User.ImpactNormal"),
		TEXT("User.TargetPosition"),
	};

	for (const TCHAR* Name : Required)
	{
		TestTrue(FString::Printf(
			TEXT("NS_Impact_Ground exposes %s. Without it, setting that "
				 "parameter does nothing at all and reports nothing."), Name),
			Exposed.Contains(FString(Name)));
	}

	// The four the code writes, checked against the constants the code writes
	// them with rather than against a second copy of the strings.
	const TPair<const TCHAR*, FName> Written[] = {
		{ TEXT("the primary colour"),
		  UCataclysmGroundEffect::ElementColourParameter },
		{ TEXT("the dark colour"),
		  UCataclysmGroundEffect::ElementColourDarkParameter },
		{ TEXT("the size"), UCataclysmGroundEffect::ScaleParameter },
		{ TEXT("how long it burns"), UCataclysmGroundEffect::DurationParameter },
	};

	for (const TPair<const TCHAR*, FName>& Pair : Written)
	{
		TestTrue(FString::Printf(
			TEXT("the parameter the code writes for %s is one the asset "
				 "exposes"), Pair.Key),
			Exposed.Contains(FString(TEXT("User.")) + Pair.Value.ToString()));
	}

	return true;
}

// --------------------------------------------------------------------------
// The arithmetic
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneRingStopsWhereDamageStops,
	"Cataclysm.Effects.GroundZoneRingStopsWhereTheDamageStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundZoneRingStopsWhereDamageStops::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	// THE DESIGNED GROUND ZONES, from the GroundRadius entries in the
	// ShapeParams column of game/Data/WeaponSkills.csv, which are written in
	// metres. Molten Cleave and Reap the Ashes are 4, Pyroclasm 5, Rain of
	// Cinders 7, Scorching Arc 4.5.
	TestEqual(TEXT("a 4 metre zone draws a 4 metre ring"),
		UCataclysmGroundEffect::ScaleFor(4.0f * M), 4.0f);
	TestEqual(TEXT("a 7 metre zone draws a 7 metre ring"),
		UCataclysmGroundEffect::ScaleFor(7.0f * M), 7.0f);

	// EVERY DESIGNED ZONE SITS INSIDE THE CLAMPS, which is what makes them
	// guards. A clamped designed radius would draw a ring that lies about where
	// the damage is, which is the one thing this effect must not do.
	TestTrue(TEXT("the smallest designed zone is above the lower clamp"),
		UCataclysmGroundEffect::ScaleFor(3.0f * M)
			> UCataclysmGroundEffect::MinimumScale);
	TestTrue(TEXT("the largest designed zone is below the upper clamp"),
		UCataclysmGroundEffect::ScaleFor(7.0f * M)
			< UCataclysmGroundEffect::MaximumScale);

	TestEqual(TEXT("a zone of no radius still draws something"),
		UCataclysmGroundEffect::ScaleFor(0.0f),
		UCataclysmGroundEffect::MinimumScale);
	TestEqual(TEXT("a kilometre zone is clamped"),
		UCataclysmGroundEffect::ScaleFor(1000.0f * M),
		UCataclysmGroundEffect::MaximumScale);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneCoversItsWholeLength,
	"Cataclysm.Effects.GroundZoneCoversItsWholeLengthWithoutGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundZoneCoversItsWholeLength::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	// A ROUND ZONE IS ONE COPY. This is the common case: a stomp, a pool of
	// lava, a patch under the caster's feet.
	TestEqual(TEXT("a round zone draws one copy"),
		UCataclysmGroundEffect::HowManyAlong(0.0f, 4.0f * M), 1);

	// AND SO IS NONSENSE. A negative length or a zero radius must not produce a
	// loop that never ends.
	TestEqual(TEXT("a negative length draws one copy rather than looping"),
		UCataclysmGroundEffect::HowManyAlong(-500.0f, 4.0f * M), 1);
	TestEqual(TEXT("a zone of no width draws one copy rather than looping"),
		UCataclysmGroundEffect::HowManyAlong(1200.0f, 0.0f), 1);

	// A LONG ZONE IS COVERED END TO END WITH NO GAP. Copies are spread evenly,
	// so Count - 1 gaps span the length and each gap must be no wider than a
	// diameter. This is asserted as the property rather than as a table of
	// numbers, so it still holds if the spacing rule is rewritten.
	const float Widths[] = { 1.0f * M, 2.0f * M, 3.5f * M };
	const float Lengths[] = { 1.0f * M, 4.0f * M, 12.0f * M };
	int32 Checked = 0;

	for (const float Radius : Widths)
	{
		for (const float Length : Lengths)
		{
			const int32 Count =
				UCataclysmGroundEffect::HowManyAlong(Length, Radius);
			if (Count >= UCataclysmGroundEffect::MostCopies)
			{
				// At the cap the zone is knowingly drawn with gaps. The cap is
				// what the next assertion is about.
				continue;
			}

			++Checked;
			const float Gap = Length / static_cast<float>(Count - 1 > 0 ? Count - 1 : 1);
			TestTrue(FString::Printf(
				TEXT("a %.0f cm zone %.0f cm wide draws %d copies, and the %.1f "
					 "cm between them is no wider than the %.0f cm they each "
					 "cover"), Length, Radius * 2.0f, Count, Gap, Radius * 2.0f),
				Gap <= Radius * 2.0f + KINDA_SMALL_NUMBER);
		}
	}

	TestTrue(TEXT("the sweep actually checked some zones"), Checked > 0);

	// THE CAP HOLDS. A very long, very narrow zone must not ask for dozens of
	// Niagara components; nothing has measured what they cost -- issue #547.
	TestEqual(TEXT("an absurdly long thin zone is capped"),
		UCataclysmGroundEffect::HowManyAlong(100.0f * M, 0.1f * M),
		UCataclysmGroundEffect::MostCopies);

	// AND NO DESIGNED ZONE REACHES IT. The longest is a twelve metre line two
	// metres wide, which is Infernal Lance's reach.
	TestTrue(TEXT("the longest designed zone is drawn without gaps"),
		UCataclysmGroundEffect::HowManyAlong(12.0f * M, 1.0f * M)
			< UCataclysmGroundEffect::MostCopies);

	return true;
}

// --------------------------------------------------------------------------
// That the zone is still drawn at all
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryGroundZoneAsksToBeDrawn,
	"Cataclysm.Effects.EveryGroundZoneAsksToBeDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryGroundZoneAsksToBeDrawn::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	// THIS IS THE TEST THE WHOLE THING DEPENDS ON. NS_Impact_Ground could be
	// authored perfectly, every asset test above could pass, and a patch of
	// burning ground could still be invisible -- which is exactly the state it
	// was in until 2026-08-22. Removing the UCataclysmGroundEffect::PlayFor call
	// from ACataclysmGroundZone::BeginPlay fails this and nothing else.
	//
	// IT COUNTS ASKS RATHER THAN EFFECTS, because no test in this project can
	// observe a Niagara component. Issue #559.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	const int32 Before = UCataclysmGroundEffect::TimesAsked;

	ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
		Caster, FVector(3.0f * M, 0.0f, 0.0f), /*RadiusCm=*/4.0f * M,
		/*Duration=*/6.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a ground zone was left in the world"), Zone))
	{
		return false;
	}

	TestEqual(TEXT("and it asked to be drawn"),
		UCataclysmGroundEffect::TimesAsked, Before + 1);

	// A LONG ZONE ASKS ONCE TOO. The row of copies is decided inside PlayFor, so
	// one call covers a path as well as a point; a second call here would mean
	// the shape had leaked into the caller.
	ACataclysmGroundZone* Line = ACataclysmGroundZone::SpawnAlong(
		Caster, FVector::ZeroVector, FVector(8.0f * M, 0.0f, 0.0f),
		/*HalfWidthCm=*/1.0f * M, /*Duration=*/5.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a long ground zone was left in the world"), Line))
	{
		return false;
	}

	TestEqual(TEXT("and a long one asks once as well"),
		UCataclysmGroundEffect::TimesAsked, Before + 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneIsDrawnWithItsOwnSize,
	"Cataclysm.Effects.AGroundZoneIsDrawnWithItsOwnSizeAndDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A zone asks to be drawn at the size, reach and duration it was spawned with.
 * Issue #1153.
 *
 * WHAT WENT WRONG. `ACataclysmGroundZone::SpawnAlong` set the radius, the far end
 * and the life span on the lines AFTER `UWorld::SpawnActor`. That function runs
 * `BeginPlay` before it returns, in any world that has already begun play, and
 * `BeginPlay` is where the zone asks to be drawn. So every patch of burning
 * ground in the game asked for a radius of zero, a far end at the world origin
 * and a duration of nothing. Twenty-two rows of `game/Data/WeaponSkills.csv`
 * leave burning ground, so that was all of them.
 *
 * THE DAMAGE WAS NEVER AFFECTED. `Sweep` reads those members when its timer
 * fires, long after the spawn returned, so standing in a patch always burned for
 * the right amount over the right area. It was a drawing fault only, which is
 * part of why it lasted.
 *
 * WHY THE TEST ABOVE COULD NOT CATCH IT.
 * `Cataclysm.Effects.EveryGroundZoneAsksToBeDrawn` reads
 * `UCataclysmGroundEffect::TimesAsked`, which counts that `PlayFor` was called
 * and says nothing about what it was called with. The counter went up every time
 * throughout. Nothing downstream can be looked at either: the automation command
 * passes `-nullrhi` and Niagara creates no component, which is issue #559.
 *
 * SO THE ARGUMENTS ARE RECORDED, and this reads them back.
 *
 * THE RECORD IS POISONED FIRST, and that is not decoration. These are static and
 * outlive one test, so a zone that asked for nothing at all would otherwise leave
 * whatever the previous test wrote and every assertion below would pass on it.
 *
 * EVERY NUMBER DIFFERS FROM EVERY OTHER AND FROM ZERO, and neither zone sits on
 * the world origin, because the broken values were zero and the origin. A round
 * zone spawned at the origin would have had a correct far end by accident.
 */
bool FCataclysmGroundZoneIsDrawnWithItsOwnSize::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("caster"), Caster))
	{
		return false;
	}

	const auto Poison = []()
	{
		UCataclysmGroundEffect::LastStart = FVector(-1.0f, -1.0f, -1.0f);
		UCataclysmGroundEffect::LastFarEnd = FVector(-1.0f, -1.0f, -1.0f);
		UCataclysmGroundEffect::LastRadiusCm = -1.0f;
		UCataclysmGroundEffect::LastDuration = -1.0f;
	};

	// ---------------------------------------------------------------
	// A long zone, which is the shape a dragged line of fire leaves
	// ---------------------------------------------------------------

	const FVector Start(2.0f * M, 1.0f * M, 0.0f);
	const FVector End(11.0f * M, 1.0f * M, 0.0f);
	const float HalfWidthCm = 1.5f * M;
	const float LongDuration = 6.0f;

	Poison();
	const int32 BeforeLong = UCataclysmGroundEffect::TimesAsked;

	ACataclysmGroundZone* Line = ACataclysmGroundZone::SpawnAlong(
		Caster, Start, End, HalfWidthCm, LongDuration, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a long ground zone was left in the world"), Line))
	{
		return false;
	}

	// ASKED AT ALL, FIRST. Without this the four assertions below would be
	// reading the poison rather than an answer, and would say so, but this names
	// the reason directly.
	if (!TestEqual(TEXT("the long zone asked to be drawn"),
				   UCataclysmGroundEffect::TimesAsked, BeforeLong + 1))
	{
		return false;
	}

	TestEqual(TEXT("the long zone asked at its own near end"),
			  UCataclysmGroundEffect::LastStart, Start, 1.0f);
	TestEqual(TEXT("reaching its own far end rather than the world origin"),
			  UCataclysmGroundEffect::LastFarEnd, End, 1.0f);
	TestEqual(TEXT("as wide as it burns rather than nothing"),
			  UCataclysmGroundEffect::LastRadiusCm, HalfWidthCm, 0.01f);
	TestEqual(TEXT("and for as long as it burns rather than no time at all"),
			  UCataclysmGroundEffect::LastDuration, LongDuration, 0.5f);

	// ---------------------------------------------------------------
	// A round zone, which is a stomp and is the other spawn entry point
	// ---------------------------------------------------------------

	// `Spawn` HAS ITS OWN TURN because it is a separate function, even though it
	// hands straight over to `SpawnAlong` today. Every skill that leaves a pool
	// rather than a line comes through it.
	const FVector Middle(3.0f * M, 4.0f * M, 0.0f);
	const float RadiusCm = 4.0f * M;
	const float RoundDuration = 5.0f;

	Poison();
	const int32 BeforeRound = UCataclysmGroundEffect::TimesAsked;

	ACataclysmGroundZone* Pool = ACataclysmGroundZone::Spawn(
		Caster, Middle, RadiusCm, RoundDuration, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a round ground zone was left in the world"), Pool))
	{
		return false;
	}

	if (!TestEqual(TEXT("the round zone asked to be drawn"),
				   UCataclysmGroundEffect::TimesAsked, BeforeRound + 1))
	{
		return false;
	}

	TestEqual(TEXT("the round zone asked at its own middle"),
			  UCataclysmGroundEffect::LastStart, Middle, 1.0f);

	// A CIRCLE IS A PATH WHOSE TWO ENDS ARE THE SAME POINT, which is what
	// `Spawn` says in its own comment. The far end being the middle is the right
	// answer here and the world origin is not, which is why this zone is not
	// spawned at the origin.
	TestEqual(TEXT("with both ends at that middle rather than one at the origin"),
			  UCataclysmGroundEffect::LastFarEnd, Middle, 1.0f);
	TestEqual(TEXT("at its own radius"),
			  UCataclysmGroundEffect::LastRadiusCm, RadiusCm, 0.01f);
	TestEqual(TEXT("and for its own duration"),
			  UCataclysmGroundEffect::LastDuration, RoundDuration, 0.5f);

	// AND THE ZONE STILL HOLDS WHAT IT WAS GIVEN AFTERWARDS, so a repair that
	// somehow reached the drawing without reaching the actor is caught. `Sweep`
	// reads these when its timer fires and burns whoever is standing there.
	TestEqual(TEXT("the round zone kept its radius"), Pool->RadiusCm, RadiusCm,
			  0.01f);
	TestEqual(TEXT("and the long one kept its far end"), Line->FarEnd, End, 1.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
