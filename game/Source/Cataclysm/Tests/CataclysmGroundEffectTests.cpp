// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Dungeon/CataclysmFloorContents.h"
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

// --------------------------------------------------------------------------
// A patch that lasts until the floor ends
// --------------------------------------------------------------------------

/**
 * A floor-lasting patch is drawn for a real length of time, not for nothing.
 *
 * THIS IS THE TEST THE WHOLE FEATURE DEPENDS ON. A patch that lasts the floor
 * has no life span, so `GetLifeSpan()` is zero, and drawing it for zero seconds
 * would leave it sweeping, damaging and cursing correctly while being
 * invisible. Every other assertion about it would pass. Issue #1153 was that
 * exact failure for every patch in the game and nothing caught it for weeks.
 *
 * IT READS WHAT WAS ASKED FOR, because nothing else is visible: the automation
 * run passes -nullrhi and Niagara makes no component at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorPatchIsDrawnForARealTime,
	"Cataclysm.Effects.APatchThatLastsTheFloorIsDrawnForARealLengthOfTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorPatchIsDrawnForARealTime::RunTest(const FString&)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Source = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something to own it"), Source))
	{
		return false;
	}

	const int32 Before = UCataclysmGroundEffect::TimesAsked;

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnForTheFloor(
		Source, FVector::ZeroVector, FVector::ZeroVector, /*HalfWidthCm=*/4.0f * M,
		/*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a floor-lasting patch was left in the world"), Patch))
	{
		return false;
	}

	TestTrue(TEXT("it says it lasts the floor"), Patch->bLastsTheFloor);

	// NO LIFE SPAN AT ALL, which is what makes the floor the thing that ends it.
	TestEqual(TEXT("and it has no life span of its own"),
		Patch->GetLifeSpan(), 0.0f, 0.001f);

	TestEqual(TEXT("it asked to be drawn"),
		UCataclysmGroundEffect::TimesAsked, Before + 1);

	// THE ASSERTION THAT MATTERS. Zero here is an invisible hazard.
	TestEqual(TEXT("and it asked for a real length of time, not its zero life span"),
		UCataclysmGroundEffect::LastDuration,
		ACataclysmGroundZone::FloorDrawSeconds, 0.001f);

	TestTrue(TEXT("which is more than nothing"),
		UCataclysmGroundEffect::LastDuration > 0.0f);

	return true;
}

/**
 * A floor-lasting patch keeps asking to be drawn while it lives.
 *
 * ONE DRAWING IS NOT ENOUGH AND THAT IS THE POINT. The components a patch asks
 * for are not attached to it and destroy themselves after the time they were
 * given, so a patch drawn once for three seconds is invisible from the fourth
 * second onward while still burning whatever stands in it.
 *
 * THE PERIOD IS SHORTER THAN THE DRAWING ON PURPOSE, so the drawings overlap
 * rather than meet. This test would still pass if the two were equal, which is
 * why the reason lives on the constants where someone tidying them will read
 * it -- a flicker of one frame every three seconds is not something an
 * automation test can see.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorPatchKeepsAskingToBeDrawn,
	"Cataclysm.Effects.APatchThatLastsTheFloorKeepsAskingToBeDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorPatchKeepsAskingToBeDrawn::RunTest(const FString&)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Source = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something to own it"), Source))
	{
		return false;
	}

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnForTheFloor(
		Source, FVector::ZeroVector, FVector::ZeroVector, /*HalfWidthCm=*/4.0f * M,
		/*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a floor-lasting patch was left in the world"), Patch))
	{
		return false;
	}

	TestEqual(TEXT("it has asked for no redrawings yet"), Patch->RedrawsAsked, 0);

	const int32 AskedAfterTheFirstDrawing = UCataclysmGroundEffect::TimesAsked;

	// LONG ENOUGH FOR SEVERAL PERIODS, so this measures a repeating timer rather
	// than one that happened to fire once.
	CataclysmTestWorld::RunClock(World, 8.0f);

	TestTrue(FString::Printf(
			TEXT("it asked to be drawn again while it lived (%d times)"),
			Patch->RedrawsAsked),
		Patch->RedrawsAsked >= 2);

	TestTrue(TEXT("and those reached the drawing system"),
		UCataclysmGroundEffect::TimesAsked > AskedAfterTheFirstDrawing);

	// STILL THERE. Eight seconds is longer than the longest stated ground
	// duration in the sheet, which is ten, and far longer than the three a
	// single drawing lasts. A patch that lasts the floor outlives both.
	TestTrue(TEXT("and the patch itself is still in the world"), IsValid(Patch));

	return true;
}

/**
 * Leaving the floor is what ends a floor-lasting patch.
 *
 * WITHOUT THIS THE FEATURE IS A LEAK. A patch with no life span that nothing
 * destroys would burn on every later floor of the run. The thing that ends it
 * is `UCataclysmFloorContents::ClearTheFloor`, which is why no duration had to
 * be invented and why the spawn function states none.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorPatchEndsWithTheFloor,
	"Cataclysm.Effects.LeavingTheFloorIsWhatEndsAPatchThatLastsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorPatchEndsWithTheFloor::RunTest(const FString&)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Source = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something to own it"), Source))
	{
		return false;
	}

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnForTheFloor(
		Source, FVector::ZeroVector, FVector::ZeroVector, /*HalfWidthCm=*/4.0f * M,
		/*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a floor-lasting patch was left in the world"), Patch))
	{
		return false;
	}

	// ASSERTED BEFORE CLEARING, so "gone afterwards" cannot be true because it
	// was never there.
	if (!TestTrue(TEXT("it is in the world to begin with"), IsValid(Patch)))
	{
		return false;
	}

	UCataclysmFloorContents::ClearTheFloor(*World);

	TestFalse(TEXT("and leaving the floor takes it"), IsValid(Patch));

	return true;
}

/**
 * The two spawn functions that take a duration still refuse a bad one.
 *
 * THE NEW FUNCTION WAS ADDED BESIDE THEM RATHER THAN BY CHANGING THEM, and this
 * is what says so. A patch with a stated duration of zero is a row whose data
 * is wrong, and it is refused; a patch with no duration at all is a deliberate
 * thing a different function makes. Removing either refusal would make the
 * first indistinguishable from the second.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTimedPatchesStillRefuseNoDuration,
	"Cataclysm.Effects.APatchWithAStatedDurationOfNothingIsStillRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTimedPatchesStillRefuseNoDuration::RunTest(const FString&)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Source = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something to own it"), Source))
	{
		return false;
	}

	TestNull(TEXT("a round patch stating no duration is refused"),
		ACataclysmGroundZone::Spawn(Source, FVector::ZeroVector,
									/*RadiusCm=*/4.0f * M, /*Duration=*/0.0f,
									/*DamagePerTick=*/10.0f));

	TestNull(TEXT("and so is a long one"),
		ACataclysmGroundZone::SpawnAlong(Source, FVector::ZeroVector,
										 FVector(5.0f * M, 0.0f, 0.0f),
										 /*HalfWidthCm=*/1.0f * M,
										 /*Duration=*/0.0f,
										 /*DamagePerTick=*/10.0f));

	// AND THE NEW ONE MAKES A PATCH FROM THE SAME ARGUMENTS MINUS THE DURATION,
	// which is what makes the two refusals above a decision rather than an
	// inability.
	TestNotNull(TEXT("while a patch that lasts the floor needs no duration"),
		ACataclysmGroundZone::SpawnForTheFloor(
			Source, FVector::ZeroVector, FVector::ZeroVector,
			/*HalfWidthCm=*/4.0f * M, /*DamagePerTick=*/10.0f));

	// A WIDTH OF NOTHING IS STILL REFUSED BY THE NEW ONE. It drops the duration
	// check and keeps every other.
	TestNull(TEXT("but a patch with no width is still refused"),
		ACataclysmGroundZone::SpawnForTheFloor(
			Source, FVector::ZeroVector, FVector::ZeroVector,
			/*HalfWidthCm=*/0.0f, /*DamagePerTick=*/10.0f));

	return true;
}

// --------------------------------------------------------------------------
// Ending the drawings when a patch is cut short
// --------------------------------------------------------------------------

/**
 * A patch that finishes its own life leaves its drawings to finish theirs.
 *
 * THIS IS HALF OF A PAIR AND NEITHER HALF MEANS ANYTHING ALONE. The drawings
 * are spawned unattached on purpose so that a timed patch's fire burns out
 * rather than vanishing at the instant the actor goes. This asserts that
 * decision still holds. The test below asserts the opposite case.
 *
 * WHAT IS OBSERVABLE IS THE ASK, NOT THE DRAWINGS. No test here can see a
 * Niagara component -- the run passes -nullrhi and the engine creates none --
 * so `TimesEndAsked` records that a patch decided to end its drawings. The
 * decision is what was missing before issue #1660: nothing ever made one.
 *
 * THE PATCH IS ASSERTED GONE, not assumed. If the life span had not run out,
 * "no ask was made" would be true for a reason that has nothing to do with
 * the rule under test, and this would pass having measured nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmExpiredPatchLeavesItsDrawings,
	"Cataclysm.Effects.APatchThatFinishesItsOwnLifeLeavesItsDrawingsAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmExpiredPatchLeavesItsDrawings::RunTest(const FString&)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Source = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something to own it"), Source))
	{
		return false;
	}

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::Spawn(
		Source, FVector::ZeroVector, /*RadiusCm=*/4.0f * M, /*Duration=*/2.0f,
		/*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a timed patch was left in the world"), Patch))
	{
		return false;
	}

	const int32 AsksBefore = UCataclysmGroundEffect::TimesEndAsked;

	// PAST ITS TWO SECONDS, so the life span timer runs out and the engine
	// destroys it through LifeSpanExpired.
	CataclysmTestWorld::RunClock(World, 3.0f);

	if (!TestFalse(TEXT("its life span ran out and it is gone"), IsValid(Patch)))
	{
		return false;
	}

	TestEqual(TEXT("and it did not ask for its drawings to end"),
		UCataclysmGroundEffect::TimesEndAsked, AsksBefore);

	return true;
}

/**
 * A patch cut short by a floor change ends its drawings.
 *
 * THE OTHER HALF OF THE PAIR, AND THE DEFECT ITSELF. Before issue #1660 the
 * drawings kept playing after the patch was destroyed, in a place with nothing
 * in it -- every floor is built at the world origin and the floor actor is
 * reused, so floor 5 occupies the coordinates floor 1 did.
 *
 * THE DURATION IS LONG ENOUGH THAT IT CANNOT EXPIRE during this test. If it
 * could, the ask might come from the expiry path and the test would prove the
 * opposite of what it says.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCutShortPatchEndsItsDrawings,
	"Cataclysm.Effects.APatchCutShortByAFloorChangeEndsItsDrawings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCutShortPatchEndsItsDrawings::RunTest(const FString&)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Source = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something to own it"), Source))
	{
		return false;
	}

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::Spawn(
		Source, FVector::ZeroVector, /*RadiusCm=*/4.0f * M, /*Duration=*/60.0f,
		/*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a timed patch was left in the world"), Patch))
	{
		return false;
	}

	const int32 AsksBefore = UCataclysmGroundEffect::TimesEndAsked;

	UCataclysmFloorContents::ClearTheFloor(*World);

	if (!TestFalse(TEXT("leaving the floor took it"), IsValid(Patch)))
	{
		return false;
	}

	TestEqual(TEXT("and it asked for its drawings to end"),
		UCataclysmGroundEffect::TimesEndAsked, AsksBefore + 1);

	return true;
}

/**
 * A patch that lasts the floor can only ever end the second way.
 *
 * IT HAS NO LIFE SPAN, so there is no expiry for it to take. That makes it the
 * case where the rule matters most: every ending it can have is one where the
 * drawings must go with it.
 *
 * THE FIRST HALF IS WHAT MAKES THE SECOND MEAN SOMETHING. Running the clock
 * well past the longest stated ground duration and finding it still there,
 * with no ask made, is what shows the ask below came from the floor change
 * rather than from time passing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorPatchAlwaysEndsItsDrawings,
	"Cataclysm.Effects.APatchThatLastsTheFloorCanOnlyEndTheSecondWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorPatchAlwaysEndsItsDrawings::RunTest(const FString&)
{
	using namespace CataclysmGroundEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Source = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something to own it"), Source))
	{
		return false;
	}

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnForTheFloor(
		Source, FVector::ZeroVector, FVector::ZeroVector, /*HalfWidthCm=*/4.0f * M,
		/*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a floor-lasting patch was left in the world"), Patch))
	{
		return false;
	}

	const int32 AsksBefore = UCataclysmGroundEffect::TimesEndAsked;

	// FIFTEEN SECONDS IS PAST THE LONGEST STATED GROUND DURATION, which is ten.
	// A timed patch would have gone by now; this one has no life span to run
	// out, so nothing has cut it short and nothing should have been asked.
	CataclysmTestWorld::RunClock(World, 15.0f);

	if (!TestTrue(TEXT("time alone does not end it"), IsValid(Patch)))
	{
		return false;
	}

	TestEqual(TEXT("and nothing has asked for its drawings to end"),
		UCataclysmGroundEffect::TimesEndAsked, AsksBefore);

	UCataclysmFloorContents::ClearTheFloor(*World);

	TestFalse(TEXT("leaving the floor takes it"), IsValid(Patch));

	TestEqual(TEXT("and that is when it asks for its drawings to end"),
		UCataclysmGroundEffect::TimesEndAsked, AsksBefore + 1);

	return true;
}

// --------------------------------------------------------------------------
// A patch that travels through the level. Issue #1649
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPatchCarriesItsWholeSelf,
	"Cataclysm.Effects.ATravellingPatchCarriesItsFarEndAndKeepsItsLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPatchCarriesItsWholeSelf::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	// THE FAR END IS A WORLD POSITION, WHICH IS THE TRAP THIS EXISTS FOR.
	// Moving the actor alone drags the near end away and leaves the far end
	// where it was, so the shape stretches instead of travelling. Nothing else
	// in the project would have said so: the patch would still damage, still
	// draw, and simply be the wrong shape.
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

	// A LONG PATCH, because a round one cannot show a far end being left
	// behind -- its two ends are the same point.
	ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnAlong(
		Caster, FVector::ZeroVector, FVector(6.0f * M, 0.0f, 0.0f),
		/*HalfWidthCm=*/2.0f * M, /*Duration=*/30.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a long patch"), Patch))
	{
		return false;
	}

	const FVector StartedAt = Patch->GetActorLocation();
	const FVector FarEndWas = Patch->FarEnd;
	const float LengthWas = (FarEndWas - StartedAt).Size();

	// THE CONTROL FIRST. A patch that was never told to travel must not move
	// when stepped, or "it moved" below means nothing.
	Patch->TravelStep(1.0f);
	if (!TestEqual(TEXT("a patch that does not travel does not move"),
				   Patch->GetActorLocation(), StartedAt))
	{
		return false;
	}
	TestEqual(TEXT("and it has travelled nothing"), Patch->TravelledCm, 0.0f);

	// NOW SEND IT SIDEWAYS, across its own length rather than along it, so a
	// far end that failed to follow would change the length rather than just
	// the position.
	Patch->TravelAt(FVector(0.0f, 1.0f * M, 0.0f));
	Patch->TravelStep(2.0f);

	const FVector Moved = FVector(0.0f, 2.0f * M, 0.0f);

	TestEqual(TEXT("the patch moved by two seconds of travel"),
			  Patch->GetActorLocation(), StartedAt + Moved);
	TestEqual(TEXT("and its far end moved with it"),
			  Patch->FarEnd, FarEndWas + Moved);
	// CAST BECAUSE A VECTOR LENGTH IS DOUBLE PRECISION IN THIS ENGINE and the
	// recorded length is single, which leaves the comparison ambiguous between
	// two overloads rather than simply narrowing.
	TestEqual(TEXT("so its length is unchanged"),
			  static_cast<float>(
				  (Patch->FarEnd - Patch->GetActorLocation()).Size()),
			  LengthWas, 0.01f);
	TestEqual(TEXT("and it recorded how far it has come"),
			  Patch->TravelledCm, 2.0f * M, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRoundPatchStaysRound,
	"Cataclysm.Effects.ARoundPatchThatTravelsDoesNotBecomeALongOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRoundPatchStaysRound::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	// WHETHER A PATCH IS LONG IS DECIDED BY COMPARING ITS TWO ENDS, every time
	// it is asked. So a round patch whose far end was left behind would begin
	// answering that it is long, and would then be drawn and swept as a line.
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

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::Spawn(
		Caster, FVector::ZeroVector, /*RadiusCm=*/3.0f * M,
		/*Duration=*/30.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a round patch"), Patch))
	{
		return false;
	}

	if (!TestFalse(TEXT("it starts round"), Patch->IsLong()))
	{
		return false;
	}

	Patch->TravelAt(FVector(2.0f * M, 0.0f, 0.0f));
	Patch->TravelStep(3.0f);

	TestTrue(TEXT("it really moved"), Patch->TravelledCm > 0.0f);
	TestFalse(TEXT("and it is still round"), Patch->IsLong());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOnlyATravellingPatchTicks,
	"Cataclysm.Effects.OnlyAPatchThatTravelsCostsAFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOnlyATravellingPatchTicks::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	// EVERY PATCH IN THE GAME BEFORE THIS DID NO PER-FRAME WORK, and the
	// constructor gives the reason: it finds who is standing in it on a timer a
	// second apart, so ticking would ask sixty times as often for the same
	// answer. Travelling needs a per-frame step, and this pins that the cost
	// falls only on a patch that asked for it.
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

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::Spawn(
		Caster, FVector::ZeroVector, /*RadiusCm=*/3.0f * M,
		/*Duration=*/30.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a patch"), Patch))
	{
		return false;
	}

	TestFalse(TEXT("an ordinary patch does not tick"),
			  Patch->IsActorTickEnabled());

	Patch->TravelAt(FVector(1.0f * M, 0.0f, 0.0f));
	TestTrue(TEXT("one that travels does"), Patch->IsActorTickEnabled());

	// AND STOPPING COSTS NOTHING AGAIN.
	Patch->TravelAt(FVector::ZeroVector);
	TestFalse(TEXT("and one that stops travelling stops ticking"),
			  Patch->IsActorTickEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPatchThatOnlyCursesStillLooks,
	"Cataclysm.Effects.APatchWithNoDamageStillFindsWhoIsStandingInIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPatchThatOnlyCursesStillLooks::RunTest(const FString& Parameters)
{
	using namespace CataclysmGroundEffectTest;

	// UNTIL ISSUE #1649 THE SWEEP ASKED ONLY WHETHER THERE WAS DAMAGE, so a
	// patch carrying a curse and no damage returned without ever looking. It
	// then reported nobody inside, which reads as an empty patch rather than a
	// patch that never checked.
	//
	// SINGULARITY WELLS IS THE ROW THAT WOULD HAVE HIT IT: it slows and pulls,
	// and a well authored without damage would have done nothing at all while
	// looking finished.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A SOURCE AND SOMEBODY IT COUNTS AS AN ENEMY, because the sweep searches
	// for the source's enemies and a patch with nothing to find cannot tell
	// "it looked and found nobody" from "it never looked".
	ACataclysmEnemyCharacter* Source =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Standing =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(1.0f * M, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a source"), Source)
		|| !TestNotNull(TEXT("somebody standing in it"), Standing))
	{
		return false;
	}
	Source->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Source->SetHealth(500.0f);
	Standing->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Standing->SetHealth(500.0f);

	// ONE: A PATCH THAT DAMAGES. The control that says the search works at
	// these positions at all.
	ACataclysmGroundZone* Burning = ACataclysmGroundZone::Spawn(
		Source, FVector::ZeroVector, /*RadiusCm=*/4.0f * M,
		/*Duration=*/30.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("a damaging patch"), Burning))
	{
		return false;
	}
	Burning->Sweep();
	if (!TestEqual(TEXT("a damaging patch finds the one standing in it"),
				   Burning->LastSweepCount, 1))
	{
		return false;
	}

	// TWO: A PATCH THAT DOES NOTHING AT ALL. The control that says the skip is
	// still there and this change did not simply delete it.
	ACataclysmGroundZone* Inert = ACataclysmGroundZone::Spawn(
		Source, FVector::ZeroVector, /*RadiusCm=*/4.0f * M,
		/*Duration=*/30.0f, /*DamagePerTick=*/0.0f);
	if (!TestNotNull(TEXT("a patch that does nothing"), Inert))
	{
		return false;
	}
	Inert->Sweep();
	TestEqual(TEXT("a patch that neither damages nor curses does not look"),
			  Inert->LastSweepCount, 0);

	// THREE: NO DAMAGE, BUT A CURSE. This is the case that did nothing before.
	ACataclysmGroundZone* Cursing = ACataclysmGroundZone::Spawn(
		Source, FVector::ZeroVector, /*RadiusCm=*/4.0f * M,
		/*Duration=*/30.0f, /*DamagePerTick=*/0.0f);
	if (!TestNotNull(TEXT("a cursing patch"), Cursing))
	{
		return false;
	}
	Cursing->AlsoApply(UCataclysmSkillShapes::StatusTagFor(TEXT("Cripple")),
					   /*Seconds=*/4.0f, /*Magnitude=*/0.0f, NAME_None);
	Cursing->Sweep();
	TestEqual(TEXT("a patch that only curses still finds who is standing in it"),
			  Cursing->LastSweepCount, 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
