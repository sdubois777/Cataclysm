// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmStrikeEffect.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEffectType.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"
#include "Tests/CataclysmTestSkip.h"

/**
 * NS_Strike_Arc: what a melee swing looks like.
 *
 * WHAT THESE ARE GUARDING AGAINST. Until issue #811 the `Strike` shape drew
 * nothing at all, and it is the most common shape in the game: 16 of the 51
 * designed Demonic skills against `Projectile`'s 10. So the failure these
 * protect against is not a wrong-looking effect, it is the return of no effect,
 * and it is silent in every direction -- Niagara ignores a parameter a system
 * does not expose without a word, a mesh renderer whose material lacks the
 * matching usage flag falls back to the engine default without a word, and a
 * system with no effect type is culled by nothing without a word.
 *
 * NOTHING HERE SPAWNS THE EFFECT. Niagara's `CreateNiagaraSystem` checks
 * `FApp::CanEverRender()` before doing anything at all and the automation
 * command in `tools/unreal_build.py` passes `-nullrhi`, so a spawned component
 * cannot be observed by any test in this project. Issue #559. What can be
 * checked is the asset, the arithmetic that feeds it, and -- through
 * `UCataclysmStrikeEffect::TimesAsked` -- whether a swing still asks for one.
 *
 * THE MESH AND ITS MATERIAL COME OUT OF A GITIGNORED PACK, so on a fresh clone
 * they resolve to null. Those two assertions report a skipped half rather than
 * failing, which is the same arrangement the fifteen Paragon-dependent tests
 * use. Everything else here is checked either way.
 */

namespace CataclysmStrikeEffectTest
{
	UNiagaraSystem* LoadStrikeArcSystem()
	{
		return LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmStrikeEffect::SystemAssetPath);
	}

	/** The named emitter's handle, or null. */
	const FNiagaraEmitterHandle* EmitterNamed(const UNiagaraSystem* System,
											  const TCHAR* Name)
	{
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (Handle.GetName() == FName(Name))
			{
				return &Handle;
			}
		}
		return nullptr;
	}
}

// --------------------------------------------------------------------------
// The asset itself
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeArcSetsItsEffectType,
	"Cataclysm.Effects.StrikeArcSetsTheEnemyEffectType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeArcSetsItsEffectType::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmStrikeEffectTest::LoadStrikeArcSystem();
	if (!System)
	{
		AddError(FString::Printf(TEXT("NS_Strike_Arc does not exist at %s."),
			UCataclysmStrikeEffect::SystemAssetPath));
		return false;
	}

	// A system with no effect type is culled by nothing whatsoever. Niagara
	// ships no default effect type and no project-wide one, so "unset" means a
	// player spinning through a group of creatures produces uncapped,
	// undistanced systems. docs/Niagara_Conventions.md section 4 says such a
	// system "is not reviewable and should not be committed".
	const UNiagaraEffectType* Type = System->GetEffectType();
	if (!Type)
	{
		AddError(TEXT("NS_Strike_Arc has no effect type, so nothing culls it by "
					  "distance or by instance count."));
		return false;
	}

	// FXT_Enemy AND NOT FXT_PlayerSkill, for the reason the projectile body
	// gives: this one asset serves both sides. The player's 16 designed Strike
	// skills use it, and so does every creature whose ability has that shape.
	// FXT_Enemy is the tighter of the two on distance (4000 cm against 6000) and
	// the looser on instance count (60 against 40), which is the safer pair for
	// something that can be numerous. A repeating strike such as Pyroclasm asks
	// for one of these every 0.2 seconds for three seconds, which is the most
	// numerous any effect in the project gets from a single cast.
	// NEITHER NUMBER HAS A MEASUREMENT BEHIND IT -- issue #547.
	TestEqual(TEXT("NS_Strike_Arc uses FXT_Enemy"),
		Type->GetName(), FString(TEXT("FXT_Enemy")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeArcExposesTheStandardBlock,
	"Cataclysm.Effects.StrikeArcExposesTheStandardParameterBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeArcExposesTheStandardBlock::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmStrikeEffectTest::LoadStrikeArcSystem();
	if (!System)
	{
		AddError(TEXT("NS_Strike_Arc does not exist."));
		return false;
	}

	// Collected as full names, which is how the store holds them: Niagara
	// prefixes every user parameter with `User.` itself.
	TSet<FString> Exposed;
	for (const FNiagaraVariableWithOffset& Variable :
			System->GetExposedParameters().ReadParameterVariables())
	{
		Exposed.Add(Variable.GetName().ToString());
	}

	// THE NAMES ARE IDENTICAL ACROSS EVERY SYSTEM ON PURPOSE. A skill row sets
	// them without knowing which system it spawned, so the moment one system
	// calls it Colour and another calls it Tint the data-driven path is dead.
	// docs/Niagara_Conventions.md section 2 fixes this list, and a system
	// exposes all of it whether or not it uses every entry.
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
			TEXT("NS_Strike_Arc exposes %s. Without it, setting that parameter "
				 "does nothing at all and reports nothing."), Name),
			Exposed.Contains(FString(Name)));
	}

	// The three the code actually writes, checked against the constants the code
	// writes them with rather than against a second copy of the strings. A
	// rename in one place and not the other is exactly the silent break.
	const TPair<const TCHAR*, FName> Written[] = {
		{ TEXT("the primary colour"),
		  UCataclysmStrikeEffect::ElementColourParameter },
		{ TEXT("the dark colour"),
		  UCataclysmStrikeEffect::ElementColourDarkParameter },
		{ TEXT("the size"), UCataclysmStrikeEffect::ScaleParameter },
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeArcDrawsAMeshAndLightsTheFloor,
	"Cataclysm.Effects.StrikeArcDrawsASlashMeshAndLightsTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeArcDrawsAMeshAndLightsTheFloor::RunTest(const FString& Parameters)
{
	using namespace CataclysmStrikeEffectTest;

	UNiagaraSystem* System = LoadStrikeArcSystem();
	if (!System)
	{
		AddError(TEXT("NS_Strike_Arc does not exist."));
		return false;
	}

	// ONE AND EXACTLY ONE. Counted rather than merely looked up by name, because
	// a system created from an engine template arrives carrying that template's
	// own emitter. This system did: `DefaultSystem` brought a `Fountain` along,
	// and the Niagara stack reported no error, no warning and a clean compile
	// with it still in there. Counting is the only thing that found it.
	TestEqual(TEXT("NS_Strike_Arc has one emitter and no template leftovers"),
		System->GetEmitterHandles().Num(), 1);

	const FNiagaraEmitterHandle* Arc = EmitterNamed(System, TEXT("Arc"));
	if (!Arc)
	{
		AddError(TEXT("NS_Strike_Arc must have an emitter named Arc."));
		return false;
	}

	TestTrue(TEXT("the arc emitter is enabled"), Arc->GetIsEnabled());

	const FVersionedNiagaraEmitterData* ArcData = Arc->GetEmitterData();
	if (!ArcData)
	{
		AddError(TEXT("the Arc emitter of NS_Strike_Arc carries no data."));
		return false;
	}

	// A MESH AND NOT A SPRITE, WHICH IS THE WHOLE POINT OF THIS SHAPE. Every
	// other effect in the project draws flat camera-facing sprites, and the
	// project owner's judgement on 2026-08-21 was that they read as a placeholder
	// -- "just a basic orb shape". A sprite renderer here would satisfy "the
	// swing draws something" and lose the reason it was built, so the class of
	// the renderer is asserted rather than merely that one exists.
	const UNiagaraMeshRendererProperties* Mesh = nullptr;
	bool bFoundALight = false;
	for (const UNiagaraRendererProperties* Renderer : ArcData->GetRenderers())
	{
		if (const auto* AsMesh = Cast<UNiagaraMeshRendererProperties>(Renderer))
		{
			Mesh = AsMesh;
		}
		if (Cast<UNiagaraLightRendererProperties>(Renderer))
		{
			bFoundALight = true;
		}
	}

	if (!Mesh)
	{
		AddError(TEXT("the Arc emitter has no mesh renderer, so the swing draws "
					  "a flat sprite or nothing at all."));
		return false;
	}

	// A LIGHT, which is what makes a swing light the floor around the caster
	// rather than being a bright shape pasted on top of an unlit room. The
	// projectile body carries one for the same reason.
	TestTrue(TEXT("the arc carries a light renderer, so a swing lights the "
				  "ground around the caster"),
		bFoundALight);

	// THE MESH AND MATERIAL COME OUT OF A GITIGNORED PACK. On a fresh clone they
	// resolve to null and the arc draws with the engine's default material,
	// which is the same state the enemy Blueprints are in without the Paragon
	// packs. That is reported rather than failed, and the rest of this test
	// still ran.
	const bool bHasMesh = Mesh->Meshes.Num() > 0 && Mesh->Meshes[0].Mesh != nullptr;
	const bool bHasMaterial = Mesh->bOverrideMaterials
		&& Mesh->OverrideMaterials.Num() > 0
		&& Mesh->OverrideMaterials[0].ExplicitMat != nullptr;

	if (!bHasMesh || !bHasMaterial)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("which mesh and material the arc draws with is not checked; "
				 "the renderer class, the light, the emitter count, the effect "
				 "type and the parameter block are. The Knife_light pack is not "
				 "installed, so the arc draws with the engine default."));
		return true;
	}

	TestEqual(TEXT("the arc draws the pack's slash mesh"),
		Mesh->Meshes[0].Mesh->GetName(), FString(TEXT("SM_slash")));
	TestEqual(TEXT("the arc draws with the pack's slash material"),
		Mesh->OverrideMaterials[0].ExplicitMat->GetName(),
		FString(TEXT("MI_mid01")));

	return true;
}

// --------------------------------------------------------------------------
// The arithmetic
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeArcSizeFollowsTheSwing,
	"Cataclysm.Effects.StrikeArcIsAsWideAsTheSwingItDraws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeArcSizeFollowsTheSwing::RunTest(const FString& Parameters)
{
	// THE DESIGNED STRIKES, IN CENTIMETRES, taken from the ShapeParams column of
	// game/Data/WeaponSkills.csv where they are written in metres. Emberpierce
	// and Searing Hook are the narrowest at 2.5 metres; Rain of Cinders and
	// Firestorm of Lashes the widest at 7.
	TestEqual(TEXT("a 2.5 metre swing draws a 2.5 metre arc"),
		UCataclysmStrikeEffect::ScaleFor(250.0f), 2.5f);
	TestEqual(TEXT("a 4 metre swing draws a 4 metre arc"),
		UCataclysmStrikeEffect::ScaleFor(400.0f), 4.0f);
	TestEqual(TEXT("a 7 metre swing draws a 7 metre arc"),
		UCataclysmStrikeEffect::ScaleFor(700.0f), 7.0f);

	// EVERY DESIGNED SWING SITS INSIDE THE CLAMPS, which is what makes them
	// guards rather than design. If a designed radius were clamped, the arc would
	// silently stop matching the cone that decides who was hit.
	TestTrue(TEXT("the narrowest designed swing is above the lower clamp"),
		UCataclysmStrikeEffect::ScaleFor(250.0f)
			> UCataclysmStrikeEffect::MinimumScale);
	TestTrue(TEXT("the widest designed swing is below the upper clamp"),
		UCataclysmStrikeEffect::ScaleFor(700.0f)
			< UCataclysmStrikeEffect::MaximumScale);

	// AND THE CLAMPS HOLD AT BOTH ENDS. A row asking for zero would draw an
	// invisible swing; one asking for a kilometre would fill the level.
	TestEqual(TEXT("a swing of no radius still draws something"),
		UCataclysmStrikeEffect::ScaleFor(0.0f),
		UCataclysmStrikeEffect::MinimumScale);
	TestEqual(TEXT("a negative radius is clamped rather than mirrored"),
		UCataclysmStrikeEffect::ScaleFor(-400.0f),
		UCataclysmStrikeEffect::MinimumScale);
	TestEqual(TEXT("a kilometre swing is clamped"),
		UCataclysmStrikeEffect::ScaleFor(100000.0f),
		UCataclysmStrikeEffect::MaximumScale);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeArcPointsWhereItWasAimed,
	"Cataclysm.Effects.StrikeArcPointsWhereTheSwingWasAimed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeArcPointsWhereItWasAimed::RunTest(const FString& Parameters)
{
	// THE ARC POINTS ALONG THE AIM. The mesh is laid out along its own +X, so
	// the rotation's yaw is the whole answer for a top-down game.
	TestEqual(TEXT("aiming along +X gives no yaw"),
		UCataclysmStrikeEffect::FacingFor(FVector(1, 0, 0)).Yaw, 0.0);
	TestEqual(TEXT("aiming along +Y gives a quarter turn"),
		UCataclysmStrikeEffect::FacingFor(FVector(0, 1, 0)).Yaw, 90.0);
	TestEqual(TEXT("aiming along -X gives a half turn"),
		FMath::Abs(UCataclysmStrikeEffect::FacingFor(FVector(-1, 0, 0)).Yaw),
		180.0);

	// A LONGER VECTOR IS THE SAME DIRECTION. AimDirection is not documented to
	// return a unit vector, so a swing aimed at a distant cursor must not come
	// out differently from one aimed at a near cursor.
	TestEqual(TEXT("the length of the aim vector does not change the facing"),
		UCataclysmStrikeEffect::FacingFor(FVector(0, 5000, 0)).Yaw,
		UCataclysmStrikeEffect::FacingFor(FVector(0, 1, 0)).Yaw);

	// THE ARC STAYS FLAT WHATEVER THE CURSOR'S HEIGHT. A swing sweeps across the
	// ground; a cursor above or below the caster would otherwise stand the arc
	// on its edge, where a flat mesh is nearly invisible from a camera looking
	// down at 60 degrees.
	const FRotator Steep = UCataclysmStrikeEffect::FacingFor(FVector(1, 0, 10));
	TestEqual(TEXT("aiming steeply upward does not tilt the arc"),
		Steep.Pitch, 0.0);
	TestEqual(TEXT("and it still points along the aim"), Steep.Yaw, 0.0);

	const FRotator Down = UCataclysmStrikeEffect::FacingFor(FVector(0, -1, -10));
	TestEqual(TEXT("aiming steeply downward does not tilt the arc either"),
		Down.Pitch, 0.0);
	TestEqual(TEXT("and it still points along the aim"), Down.Yaw, -90.0);

	// A ZERO AIM IS A REAL CASE, not a fault: AimDirection returns zero when a
	// player controller has no cursor position yet. FVector::Rotation on a zero
	// vector says nothing useful, so this answers with no rotation rather than
	// letting a NaN travel into a spawn transform.
	TestTrue(TEXT("an aim of nowhere gives a finite rotation"),
		UCataclysmStrikeEffect::FacingFor(FVector::ZeroVector)
			== FRotator::ZeroRotator);
	TestTrue(TEXT("and so does an aim straight up, which flattens to nothing"),
		UCataclysmStrikeEffect::FacingFor(FVector(0, 0, 1))
			== FRotator::ZeroRotator);

	return true;
}

// --------------------------------------------------------------------------
// The colour, which is where every other effect in the project goes wrong
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeArcTakesItsColourFromTheSkill,
	"Cataclysm.Effects.StrikeArcTakesItsColourFromTheSkillWhenTheCasterIsUntyped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeArcTakesItsColourFromTheSkill::RunTest(const FString& Parameters)
{
	// THIS IS THE HALF OF ISSUE #803 THAT THIS SHAPE FIXES. Every other effect
	// in the project asks UCataclysmSkillEffects::DamageTypeOf and nothing else,
	// and that returns NAME_None for a player by design -- so every player
	// projectile and every player hit burst draws the authored white. A white
	// slash arc would have shipped the same fault in a new asset.
	//
	// NO CASTER AT ALL IS THE PLAYER'S CASE HERE, and it is honest rather than a
	// dodge: DamageTypeOf casts its argument to ACataclysmEnemyCharacter and
	// answers NAME_None for anything else, so a player pawn and a null pointer
	// take exactly the same branch. Building a real player pawn would exercise
	// the same line.
	const FGameplayTag Demonic =
		UCataclysmDamageCalculation::ElementTagFor(FName(TEXT("Demonic")));
	if (!Demonic.IsValid())
	{
		AddError(TEXT("Element.Demonic is not in the tag vocabulary, so this "
					  "test cannot say anything."));
		return false;
	}

	TestEqual(TEXT("an untyped caster swinging a Demonic skill draws Demonic"),
		UCataclysmStrikeEffect::DamageTypeFor(nullptr, Demonic),
		FName(TEXT("Demonic")));

	const FGameplayTag Void =
		UCataclysmDamageCalculation::ElementTagFor(FName(TEXT("Void")));
	TestEqual(TEXT("and a Void skill draws Void, so this is the tag being read "
				   "rather than one damage type being hard-coded"),
		UCataclysmStrikeEffect::DamageTypeFor(nullptr, Void),
		FName(TEXT("Void")));

	// A SKILL THAT SAYS NOTHING STILL DRAWS THE AUTHORED DEFAULT. NAME_None
	// means "leave the asset's own white and black in place", not "something
	// went wrong", and UCataclysmElementVisuals::ColoursFor returns false for it
	// so nothing is written at all.
	TestEqual(TEXT("a skill with no element tag leaves the authored default"),
		UCataclysmStrikeEffect::DamageTypeFor(nullptr, FGameplayTag()),
		FName(NAME_None));

	// AND A TAG THAT IS NOT AN ELEMENT IS NOT MISTAKEN FOR ONE. DamageTypeFromTags
	// is the project's only decoder and it answers NAME_None for anything
	// outside the Element.* branch; a leaf taken by splitting on a dot here
	// would have answered "Melee".
	const FGameplayTag NotAnElement = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Type.Melee")), /*ErrorIfNotFound=*/false);
	if (NotAnElement.IsValid())
	{
		TestEqual(TEXT("a tag that is not a damage type is not read as one"),
			UCataclysmStrikeEffect::DamageTypeFor(nullptr, NotAnElement),
			FName(NAME_None));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
