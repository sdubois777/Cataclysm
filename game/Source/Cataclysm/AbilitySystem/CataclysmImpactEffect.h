// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmImpactEffect.generated.h"

class UDataTable;
struct FHitResult;

/**
 * Forward declared rather than included, so this header pulls in no Niagara
 * type. The Niagara module is a PRIVATE dependency of this module and should
 * stay one -- see Cataclysm.Build.cs. A pointer to an incomplete class needs
 * nothing from that module, so anything may include this header; only code that
 * dereferences the result has to include NiagaraComponent.h itself.
 */
class UNiagaraComponent;

/**
 * The hit effect that plays where a blow lands.
 *
 * ONE SYSTEM SERVES ALL EIGHT DAMAGE TYPES. That is the whole point of the
 * arrangement and it is why this class exists rather than eight assets:
 * NS_Impact_Point reads two colours from user parameters, and this class fills
 * them from the damage type's row in DT_ElementVisuals at the moment of the hit.
 * A Demonic hit and a Void hit are the same asset with different rows, so
 * changing a colour is a data edit and touches no asset.
 * docs/Niagara_Conventions.md section 5 sets it out.
 *
 * THE PARAMETER NAMES BELOW MUST MATCH THE ASSET EXACTLY. Niagara silently
 * ignores a parameter a system does not expose -- no warning, no error, no
 * effect -- so a typo here does not fail, it just quietly stops the colours
 * arriving. Cataclysm.Effects.ImpactPointReadsItsColoursFromTheTable is what
 * notices.
 */
UCLASS()
class CATACLYSM_API UCataclysmImpactEffect : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The system built in issue #558. */
	static const TCHAR* SystemAssetPath;

	/** The eight damage types' colours, built in issue #549. */
	static const TCHAR* ElementVisualsAssetPath;

	/**
	 * The user parameters this class writes. Bare names without the `User.`
	 * prefix, which Niagara adds itself.
	 */
	static const FName ElementColourParameter;
	static const FName ElementColourDarkParameter;
	static const FName TargetPositionParameter;
	static const FName ImpactNormalParameter;

	/** Null when the table asset is missing. Loaded once and kept. */
	static const UDataTable* LoadElementVisuals();

	/**
	 * Where the effect plays, and which way up it faces.
	 *
	 * A HIT RESULT BEING PRESENT IS NOT THE SAME AS IT DESCRIBING A HIT, and
	 * assuming otherwise is issue #562: every blow an enemy landed on the player
	 * drew its effect in the middle of the level rather than on the player. A
	 * hit result that never blocked anything carries a zero impact point, and
	 * the zero impact point is the world origin. A player's own attack sweeps
	 * the world and produces a real one, which is why that direction looked
	 * correct and hid the fault.
	 *
	 * So the damaged actor's own location is used unless the hit result actually
	 * blocked something.
	 *
	 * SEPARATE AND STATIC SO A TEST CAN REACH IT. Nothing here needs a world, a
	 * component or a rendering device, so the automation harness can exercise it
	 * -- which issue #559 records it cannot do for the spawn itself.
	 */
	static FVector ImpactLocationFor(const FHitResult* Landed,
									 const AActor* Struck,
									 FVector& OutNormal);

	/**
	 * The two colours for a damage type, looked up by the leaf of its
	 * `Element.*` tag -- `Demonic`, `Void` and so on, which is exactly what
	 * FCataclysmIncomingHit::DamageType holds.
	 *
	 * Returns false and writes nothing when the type has no row, which includes
	 * every untyped hit. Player damage carries no damage type by design, so
	 * that is a normal case and not a fault.
	 */
	static bool ColoursFor(FName DamageType, FLinearColor& OutPrimary,
						   FLinearColor& OutSecondary);

	/**
	 * Plays the impact where a blow landed.
	 *
	 * A hit with no damage type still plays, with the system's own authored
	 * defaults: a white spark and a black core. White is the deliberate "nothing
	 * set this" value -- no designed row is white -- so an untyped hit looks
	 * exactly like what it is.
	 *
	 * Returns the component so a caller can inspect what it was given. Null when
	 * there is no world, the system asset is missing, or the effect type's
	 * scalability rejected the spawn.
	 *
	 * THAT LAST ONE IS NOT A FAILURE. It is FXT_Enemy working: past 4000 cm, or
	 * outside the view frustum, or beyond twenty live instances of this system,
	 * the effect is refused before it starts and the blow lands unseen. That is
	 * the whole reason the effect type exists.
	 *
	 * IT ALSO RETURNS NULL IN EVERY AUTOMATION TEST, and that is a property of
	 * the harness rather than of this code. Niagara's CreateNiagaraSystem checks
	 * FApp::CanEverRender() before it does anything at all, and the test command
	 * in tools/unreal_build.py passes -nullrhi. So no automation test can
	 * observe a spawned component, and none of them try. Issue #559 is what
	 * would be needed to cover the spawn itself.
	 */
	static UNiagaraComponent* SpawnAt(const UObject* WorldContextObject,
									  const FVector& Location,
									  const FVector& ImpactNormal,
									  FName DamageType);
};
