// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmBuriedWeapon.h"
// For the ailment a throwing row states, laid on each new host. Issue #2885.
#include "AbilitySystem/CataclysmAilments.h"
// For what a blow resolved to, so a burn is refused on an evaded one.
// Issue #1156.
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the Fervour the axe's next landing earns its thrower. Issue #1515.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
// For drawing the weapon in the creature it is buried in. Issue #2872.
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Items/CataclysmWeaponMeshes.h"
#include "Misc/ScopeExit.h"

const TCHAR* const UCataclysmBuriedWeapon::LogPrefix = TEXT("BURIED WEAPON: ");

namespace CataclysmBuriedWeaponLeap
{
	/**
	 * Every creature whose death is tearing a buried weapon free at this moment, outermost first. Issue #2872.
	 *
	 * NONE OF THEM IS RECORDED DEAD YET: `UCataclysmSkillEffects::MarkDead` tags a creature after its hooks, and this
	 * runs inside them. So each is left out of every search by name, and a death already here is not torn again. Raw
	 * pointers, because every entry is added and removed inside one call of `LeapFromDying` on the stack, in which
	 * nothing is collected.
	 */
	TArray<const AActor*>& TearingFree()
	{
		static TArray<const AActor*> Each;
		return Each;
	}

	/** What a weapon carries from one host to the next, copied out of its component before anything can change it. */
	struct FCarried
	{
		float RangeCm = 0.0f;
		float DamagePercent = 0.0f;
		bool bBurns = false;
		FGameplayTagContainer SkillTags;
		TWeakObjectPtr<AActor> Caster;
		TWeakObjectPtr<AActor> CastBy;
		FName SkillRow;
		FString SkillName;
		int32 Leaps = 0;
		/** What each host left dead pays, and the ailment each new host is given. Issue #2885, N6. */
		float KillFervour = 0.0f;
		FString StatedAilment;
	};

	/** The skill's name for a log line, or "A buried" weapon's when it was buried by no skill. */
	FString WhoseWeapon(const FString& SkillName)
	{
		return SkillName.IsEmpty() ? FString(TEXT("A buried")) : FString::Printf(TEXT("%s's"), *SkillName);
	}
}

UCataclysmBuriedWeapon* UCataclysmBuriedWeapon::BuryIn(
	AActor* Target, AActor* Caster, float RangeCm, float InDamagePercent,
	const FGameplayTagContainer& InSkillTags, bool bInBurns, const TWeakObjectPtr<AActor>& InCastBy, FName InSkillRow)
{
	if (!IsValid(Target) || RangeCm <= 0.0f)
	{
		return nullptr;
	}

	// REFRESHED RATHER THAN ADDED TO. Two Harrowers in one creature are one
	// buried axe, which matches the one-stack rule the design states for
	// ailments and damage over time.
	UCataclysmBuriedWeapon* Buried =
		Target->FindComponentByClass<UCataclysmBuriedWeapon>();
	if (!Buried)
	{
		Buried = NewObject<UCataclysmBuriedWeapon>(Target);
		if (!Buried)
		{
			return nullptr;
		}
		Buried->RegisterComponent();
	}

	Buried->RangeCm = RangeCm;
	Buried->DamagePercent = InDamagePercent;
	Buried->SkillTags = InSkillTags;
	Buried->bBurns = bInBurns;
	Buried->Caster = Caster;
	Buried->CastBy = InCastBy;
	Buried->SkillRow = InSkillRow;

	// AND NOTHING IT WOULD PAY OR LAY, until the caller says otherwise. Issue #2885, N6: a throw whose row states
	// `KillFervour` or `Ailment` writes them back after this call, and a leap carries them on; a refresh by any other
	// weapon -- a Harrower in a creature a Hungering Soul lodged in -- leaves a weapon that pays and lays nothing.
	Buried->KillFervour = 0.0f;
	Buried->StatedAilment.Reset();
	return Buried;
}

FName UCataclysmBuriedWeapon::WeaponBaseFor(const FGameplayTagContainer& InSkillTags)
{
	static const FString WeaponTagStem = TEXT("Item.Weapon.");
	for (const FGameplayTag& Tag : InSkillTags)
	{
		const FString Name = Tag.GetTagName().ToString();
		if (Name.StartsWith(WeaponTagStem) && Name.Len() > WeaponTagStem.Len())
		{
			return FName(*FString::Printf(TEXT("Weapon_%s"), *Name.RightChop(WeaponTagStem.Len())));
		}
	}
	return NAME_None;
}

bool UCataclysmBuriedWeapon::DrawIn(const FVector& CameFrom)
{
	AActor* Host = GetOwner();
	if (!IsValid(Host) || !Host->GetRootComponent())
	{
		return false;
	}

	// THE MESH THE HAND DRAWS FOR THE THROWING SKILL'S WEAPON, through the same table and lookup the player character
	// uses. A BASE WHOSE MESH DID NOT LOAD IS NOT ASKED AGAIN IN THIS RUN: `MeshFor` warns each time, which is each
	// time in a worktree without the art pack, and a throw buries a weapon every few seconds; once says it.
	static TSet<FName> BasesWithNoMesh;
	const FName Base = WeaponBaseFor(SkillTags);
	if (Base.IsNone() || BasesWithNoMesh.Contains(Base))
	{
		return false;
	}
	float Scale = 1.0f;
	UStaticMesh* Mesh = UCataclysmWeaponMeshes::MeshFor(UCataclysmWeaponMeshes::LoadTable(), Base, Scale);
	if (!Mesh)
	{
		BasesWithNoMesh.Add(Base);
		return false;
	}

	if (!DrawnWeapon)
	{
		DrawnWeapon = NewObject<UStaticMeshComponent>(Host, NAME_None);
		if (!DrawnWeapon)
		{
			return false;
		}
		// NO COLLISION. It is a picture of an axe: a static mesh blocks the visibility channel by default, which would
		// catch the cursor's aim and a shot's line of sight on the creature it is buried in.
		DrawnWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DrawnWeapon->SetGenerateOverlapEvents(false);
		DrawnWeapon->SetCanEverAffectNavigation(false);
		DrawnWeapon->SetStaticMesh(Mesh);
		DrawnWeapon->SetupAttachment(Host->GetRootComponent());
		DrawnWeapon->RegisterComponent();
	}

	// IN THE SIDE IT CAME FROM, two thirds of the body's radius out and a third of the way up from its middle, its
	// mesh's forward axis pointing from where it came from into the body. Where it sits is a judgement of the writing
	// session, made without seeing the mesh; the owner is asked to look.
	const FVector Middle = Host->GetActorLocation();
	FVector Toward = Middle - CameFrom;
	Toward.Z = 0.0f;
	if (!Toward.Normalize())
	{
		Toward = Host->GetActorForwardVector() * -1.0f;
	}
	float BodyRadius = 0.0f;
	float BodyHalfHeight = 0.0f;
	Host->GetSimpleCollisionCylinder(BodyRadius, BodyHalfHeight);
	const FVector Where = Middle - Toward * BodyRadius * (2.0f / 3.0f) + FVector(0.0f, 0.0f, BodyHalfHeight / 3.0f);
	DrawnWeapon->SetWorldLocationAndRotation(Where, Toward.Rotation());
	DrawnWeapon->SetWorldScale3D(FVector(Scale > 0.0f ? Scale : 1.0f));
	return true;
}

bool UCataclysmBuriedWeapon::LeapFromDying(AActor* Dying)
{
	using namespace CataclysmBuriedWeaponLeap;

	if (!IsValid(Dying))
	{
		return false;
	}

	UCataclysmBuriedWeapon* Buried =
		Dying->FindComponentByClass<UCataclysmBuriedWeapon>();
	if (!Buried || Buried->RangeCm <= 0.0f)
	{
		// The ordinary case. Nothing but Harrower buries a weapon.
		return false;
	}

	// A DEATH ALREADY TEARING A WEAPON FREE IS NOT TORN AGAIN. Issue #2872: a second axe in a host this one's leap
	// killed leaps from inside that blow, and the creature this one left is still dying further up the stack, not yet
	// recorded dead; struck again at nought health it would come back here.
	TArray<const AActor*>& Tearing = TearingFree();
	if (Tearing.Contains(Dying))
	{
		return false;
	}
	const int32 TearingBefore = Tearing.Num();
	Tearing.Add(Dying);
	ON_SCOPE_EXIT
	{
		TearingFree().SetNum(TearingBefore);
	};

	// EVERYTHING IT CARRIES, copied before any blow, because a blow can reach this component again.
	FCarried Carried;
	Carried.RangeCm = Buried->RangeCm;
	Carried.DamagePercent = Buried->DamagePercent;
	Carried.bBurns = Buried->bBurns;
	Carried.SkillTags = Buried->SkillTags;
	Carried.Caster = Buried->Caster;
	Carried.CastBy = Buried->CastBy;
	Carried.SkillRow = Buried->SkillRow;
	Carried.SkillName = Buried->SkillName;
	Carried.Leaps = Buried->Leaps;
	Carried.KillFervour = Buried->KillFervour;
	Carried.StatedAilment = Buried->StatedAilment;
	const FString Whose = WhoseWeapon(Carried.SkillName);

	// THE HOST IT TEARS FREE OF IS A HOST IT LEAVES DEAD, WHOEVER KILLED IT. Issue #2885, the Death draft's N6, slice
	// DS3: Hungering Soul, "every host it leaves dead gives you 2 Fervour". Paid before the leap, and whether or not a
	// living enemy is in reach to leap to. ONLY WHILE THE THROWER IS HERE: a thrower gone has no pool to fill.
	if (Carried.KillFervour > 0.0f && Carried.Caster.IsValid())
	{
		UCataclysmFervour::GainStated(UCataclysmTargeting::AbilitySystemOf(Carried.Caster.Get()), Carried.KillFervour,
										 /*bForAKill=*/true);
	}

	AActor* From = Dying;
	// WHERE THE WEAPON CAME INTO `From` FROM, once it has leapt: the side it is drawn in, if it stops there.
	FVector CameIntoFromWhere = FVector::ZeroVector;
	int32 LeapsMade = 0;
	for (;;)
	{
		// THE CASTER IS CREDITED, AND THE DYING CREATURE STANDS IN FOR IT WHEN IT IS
		// GONE. A player who threw the axe and then left, or died, should not stop
		// the axe already in the field from moving on. `FindEnemiesInSphere` needs
		// somebody to decide sides from, and the dying creature knows its own.
		const bool bThrowerIsHere = Carried.Caster.IsValid();
		AActor* Credited = bThrowerIsHere ? Carried.Caster.Get() : From;
		const FVector FromWhere = From->GetActorLocation();

		TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
			From->GetWorld(), Credited, FromWhere, Carried.RangeCm);

		// NOT BACK INTO THE CORPSE, NOR INTO ANY BODY THIS CHAIN HAS LEFT. None of them is recorded dead yet, so the
		// search still finds them, and each is dropped by name.
		Nearby.RemoveAll([&Tearing](const AActor* One)
		{
			return Tearing.Contains(One);
		});
		if (Nearby.IsEmpty())
		{
			// "Until nothing is left in reach." The axe stops here, in the body it
			// was already in, which is destroyed with it. Issue #2872: a body this
			// chain's own blow killed holds it too, drawn there.
			if (From != Dying)
			{
				if (UCataclysmBuriedWeapon* Rests = BuryIn(From, Carried.Caster.Get(), Carried.RangeCm,
														   Carried.DamagePercent, Carried.SkillTags, Carried.bBurns,
														   Carried.CastBy, Carried.SkillRow))
				{
					Rests->Leaps = Carried.Leaps;
					Rests->SkillName = Carried.SkillName;
					Rests->KillFervour = Carried.KillFervour;
					Rests->StatedAilment = Carried.StatedAilment;
					Rests->DrawIn(CameIntoFromWhere);
				}
			}
			UE_LOG(LogCataclysm, Display,
				TEXT("%s%s weapon stays in %s: no living enemy within %.1f m of it. %d leap%s in all."),
				LogPrefix, *Whose, *GetNameSafe(From), Carried.RangeCm / 100.0f, Carried.Leaps,
				Carried.Leaps == 1 ? TEXT("") : TEXT("s"));
			break;
		}

		// THE NEAREST ONE, which is what the row says and which
		// `FindEnemiesInSphere` answers first.
		AActor* NextHost = Nearby[0];
		const FVector NextWhere = NextHost->GetActorLocation();

		// WHICH SIDE, ASKED BEFORE THE HIT as a fired contact asks it. Issue #1938.
		const bool bFromBehind = UCataclysmSkillEffects::IsBehind(Credited, NextHost);

		// AND WHO THE THROWING USE WAS CAST FROM, so a skill gadget's axe is still priced as the gadget's while the gadget
		// stands. Empty for every axe a player threw by hand, which is the delivery this always sent. Stack 2 (P5). AND
		// ONLY WHILE THE THROWER IS HERE: when it has gone the dying creature is credited, and its blow is no gadget's.
		// AND AS IT WAS KEPT, NOT THROUGH `Get`, so a skill trap used up before this leap is still named by its identity and
		// the leap is priced and announced as the trap's (ruled 2026-10-21, #2462). A gadget gone still answers null to
		// `SkillGadgetCasting`, so its axe is the player's then, as before (P5).
		FCataclysmHitDelivery Delivery;
		Delivery.CastBy = bThrowerIsHere ? Carried.CastBy : TWeakObjectPtr<AActor>();
		// AND THE THROWING SKILL'S TAGS, AS THE BLOW OF THAT SKILL IT IS, so a kill this leap makes carries them as a cast's
		// blow's kill carries its skill's, and a `kill` row requiring a tag answers it. Ruled 2026-10-22 (#2465, N1 (a),
		// N2 (a)). The skill object may be long gone, so the tags kept here are named rather than the skill. A player's own
		// axe and a trap's alike; melee or ranged was already read from these tags by `ApplyHit` (N3 (a)).
		Delivery.SkillTagsWithNoSkill = &Carried.SkillTags;
		// AND THE THROWING SKILL'S ROW, so the leap scales by that skill's level and credits it. Issue #2764, slice 2a.
		Delivery.SkillRowWithNoSkill = Carried.SkillRow;

		FCataclysmDamageResult Resolved;
		const float Dealt = UCataclysmSkillEffects::ApplyHit(
			Credited, NextHost, Carried.DamagePercent, Carried.SkillTags,
			Delivery, &Resolved);

		// A DESIGNED BURN, because `bBurns` was copied from the throwing row's own
		// `Burn=1`. Issue #917: a skill that states an ailment applies it whether or
		// not the blow hurt.
		//
		// THE COMMENT THAT STOOD HERE SAID A BURN IS A SHARE OF THE HIT. That
		// stopped being true on 2026-08-24, when burn became a flat 25 a second.
		//
		// AN EVADED BLOW SETS NOTHING ALIGHT. Issue #1156, decided on 2026-09-04:
		// evasion says the blow never connected, so nothing it carries arrives. One
		// armour stopped dead still burns, because armour answers whether the blow
		// hurt rather than whether it landed. `Dealt` cannot be used to ask this --
		// it is the figure that was SENT, and it is positive for an evaded blow.
		//
		// AND THE THROWING SKILL'S TAGS, AS THE LEAP'S HIT CARRIES THEM, so a kill by a tick of this burn carries them as
		// the leap's own kill does, and no skill name. Issue #2683, under #2465's ruling N1 (a). Identity only: the burn is
		// priced as it was, with no skill (ruled 2026-11-01, J1).
		//
		// AND NOT ON A HOST THE BLOW ALREADY KILLED, issue #2872: a corpse is not set alight, and the weapon goes on.
		const bool bKilledIt = UCataclysmSkillEffects::IsDead(NextHost);
		const bool bSetAlight = Carried.bBurns && !Resolved.bEvaded && !bKilledIt;
		if (bSetAlight)
		{
			UCataclysmSkillEffects::ApplyBurn(Credited, NextHost, Dealt,
											  /*bScalesWithInstigator=*/true,
											  /*bBurnIsDesigned=*/true,
											  /*DealtBy=*/nullptr,
											  /*Skill=*/nullptr,
											  /*SkillTagsWithNoSkill=*/&Carried.SkillTags,
											  /*SkillRowWithNoSkill=*/Carried.SkillRow);
		}

		// AND THE AILMENT THE THROWING ROW STATES, BY THE BURN'S OWN RULE ABOVE: on a blow that landed, carrying damage,
		// on a host that lives. Issue #2885, slice DS3: Hungering Soul's soul "lodges in it, giving it Necrosis", in each
		// host in turn. At magnitude one, naming the throwing skill's tags and row as the burn does, the skill object
		// being long gone. Nothing for a weapon whose row states none, which is Harrower's.
		if (!Carried.StatedAilment.IsEmpty() && Dealt > 0.0f && !Resolved.bEvaded && !bKilledIt)
		{
			if (const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindNamedInASkillRow(Carried.StatedAilment))
			{
				UCataclysmAilments::Apply(Credited, NextHost, *Kind, /*Magnitude=*/1.0f, /*Skill=*/nullptr,
					/*DamageType=*/NAME_None, /*Seconds=*/0.0f, /*SkillTagsWithNoSkill=*/&Carried.SkillTags,
					/*SkillRowWithNoSkill=*/Carried.SkillRow);
			}
		}

		// AND A HOST THIS LEAP'S OWN BLOW KILLED IS A HOST IT LEAVES DEAD TOO, paid now: its death was recorded inside
		// the blow, before the weapon was in it, so nothing else pays for it. Issue #2885, N6.
		if (bKilledIt && bThrowerIsHere && Carried.KillFervour > 0.0f)
		{
			UCataclysmFervour::GainStated(UCataclysmTargeting::AbilitySystemOf(Credited), Carried.KillFervour,
											 /*bForAKill=*/true);
		}

		// AND THE FERVOUR THE LANDING EARNS WHOEVER THREW THE AXE. Issue #1515, the
		// Ravager's starting node: "1 for each enemy your attacks hit". This blow
		// never passes through `UCataclysmSkillTemplate::HitTargets`, so it pays
		// here, by the same test: it landed and it carried damage. ONLY WHILE THE
		// THROWER IS STILL HERE: when it has gone, `Credited` is the dying creature,
		// and a creature's blows earn nothing.
		if (bThrowerIsHere && Dealt > 0.0f && !Resolved.bEvaded)
		{
			UCataclysmFervour::GainForEnemiesHit(
				UCataclysmTargeting::AbilitySystemOf(Credited), Carried.SkillTags,
				/*EnemiesHit=*/1);

			// AND THE THROWER'S RUNNING BUFFS ARE TOLD, BY THE SAME TEST AND ONLY
			// WHILE THE THROWER IS HERE. Issue #1938, ruled 2026-09-24: this blow
			// never passes through `HitTargets`. When the thrower has gone the
			// credited attacker is the dying creature, whose blows tell nothing.
			UCataclysmSkillTemplate::NoteBlowLanded(
				Credited, NextHost, NextWhere, bFromBehind);
		}

		// THE WEAPON IS NOW THE NEXT HOST'S, credited to whoever this blow was, which is what makes it go on. Copied
		// from what it carried rather than from the skill, because the skill ended long ago: a Harrower thrown ten
		// seconds back is not running any more.
		Carried.Caster = Credited;
		++Carried.Leaps;
		++LeapsMade;

		UE_LOG(LogCataclysm, Display,
			TEXT("%s%s weapon tore free of %s and struck %s, %.1f m away and the nearest living enemy within %.1f m: "
				 "sent %.0f%s%s. Leap %d."),
			LogPrefix, *Whose, *GetNameSafe(From), *GetNameSafe(NextHost),
			FVector::Dist2D(FromWhere, NextWhere) / 100.0f, Carried.RangeCm / 100.0f, Dealt,
			Resolved.bEvaded ? TEXT(", evaded") : (bSetAlight ? TEXT(", setting it alight") : TEXT("")),
			bKilledIt ? TEXT("; the blow killed it, so the weapon tears free again") : TEXT(""),
			Carried.Leaps);

		if (!bKilledIt)
		{
			// IT LIVES, SO THE WEAPON STAYS IN IT until it dies, drawn there.
			if (UCataclysmBuriedWeapon* Moved = BuryIn(NextHost, Credited, Carried.RangeCm, Carried.DamagePercent,
													   Carried.SkillTags, Carried.bBurns, Carried.CastBy,
													   Carried.SkillRow))
			{
				Moved->Leaps = Carried.Leaps;
				Moved->SkillName = Carried.SkillName;
				Moved->KillFervour = Carried.KillFervour;
				Moved->StatedAilment = Carried.StatedAilment;
				Moved->DrawIn(FromWhere);
			}
			break;
		}

		// THE LEAP'S OWN BLOW KILLED IT. Its death was recorded inside that blow, before the weapon was in it, so it
		// tore nothing free; the weapon goes on from it now, and it joins the bodies every later search leaves out.
		Tearing.Add(NextHost);
		CameIntoFromWhere = FromWhere;
		From = NextHost;
	}

	// AND IT IS OUT OF THE BODY IT TORE FREE OF, picture and all, when it went anywhere. Where it found nobody it
	// stays, which is what "the axe stays where it lands" is until something kills that body.
	if (LeapsMade > 0)
	{
		if (Buried->DrawnWeapon)
		{
			Buried->DrawnWeapon->DestroyComponent();
			Buried->DrawnWeapon = nullptr;
		}
		Buried->DestroyComponent();
	}
	return LeapsMade > 0;
}
