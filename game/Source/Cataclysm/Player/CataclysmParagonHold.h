// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "CataclysmParagonHold.generated.h"

/**
 * Keeps every loaded object of the Paragon art packs alive for the rest of the session. Issue #2968.
 *
 * WHY. In the packaged game, garbage collection freed objects of the Paragon packs that a Paragon creature's material
 * still needed when it was drawn, and the next draw read freed memory and crashed (`SetSamplerTable` under
 * `RHISetShaderParameters`). It never happened in the editor or an uncooked standalone game, which keep those objects
 * through references the cook removes. A bisect on 2026-11-27 proved it: with a collection every frame, every run
 * drawing Paragon creatures in their own materials crashed within 38 seconds of walking, and the same run with every
 * loaded Paragon object held survived. See docs/DECISIONS.md.
 *
 * HOW. Before every garbage collection it adds every live object whose package lies under the Paragon packs' folders
 * to `Held`, a property the collector follows. Nothing of those packs is freed after it has been loaded once; that is
 * the cost, and it is the scope the bisect proved.
 */
UCLASS()
class CATACLYSM_API UCataclysmParagonHold : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * The start of every Paragon pack's package path. BUILT AT RUN TIME, never written as one string:
	 * tools/package_game.py reads every content path written in the source as one the cook must cover, and this is a
	 * prefix, not a path.
	 */
	static const FString& ParagonPrefix();

	/**
	 * Add every live object whose package name starts with `Prefix` to `Into`, and say how many were new. Skips an
	 * object already unreachable or marked as garbage, which must not be brought back.
	 */
	static int32 HoldObjectsUnder(const FString& Prefix, TSet<TObjectPtr<UObject>>& Into);

	/** How many objects this holds now. */
	int32 NumHeld() const { return Held.Num(); }

	/** Hold every live object under `Prefix` now, as a collection is about to. For tests, which hold under a package
	 *  of their own rather than the Paragon packs. */
	int32 HoldNowForTests(const FString& Prefix) { return HoldObjectsUnder(Prefix, Held); }

private:
	void HoldBeforeCollecting();

	/** Everything held, followed by the garbage collector because it is a property. */
	UPROPERTY(Transient)
	TSet<TObjectPtr<UObject>> Held;

	FDelegateHandle BeforeCollectingHandle;
};
