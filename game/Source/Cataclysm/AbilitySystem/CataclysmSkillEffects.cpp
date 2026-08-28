// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For how long a lasting harmful effect really runs on its target, which is
// the target's own stat rather than the attacker's. Issue #1033.
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
// For the boss check in ApplyStun: boss-ness lives on the enemy as its rarity
// step, so rule three has to ask the enemy class. Issue #395.
#include "Character/CataclysmEnemyCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTagsManager.h"

namespace
{
	/**
	 * Make an effect refresh rather than stack, and grant a tag while it lasts.
	 *
	 * WHY THE DEPRECATION IS SUPPRESSED HERE AND NOT WORKED AROUND. Unreal 5.7
	 * deprecated writing UGameplayEffect::StackingType directly and offers
	 * SetStackingType instead -- but that setter is inside WITH_EDITOR, so it
	 * does not exist in a packaged build. Assigning the field is the only route
	 * available at runtime until the engine provides one. Kept in one function
	 * so there is a single place to change when it does.
	 *
	 * ONE STACK ONLY is the design's rule for every effect a player can apply,
	 * and it is aggregated by TARGET rather than by source so that two
	 * characters burning the same enemy still produce one burn.
	 */
	void MakeSingleStackTagged(UGameplayEffect* Effect, const FGameplayTag& EffectTag)
	{
		if (!Effect || !EffectTag.IsValid())
		{
			return;
		}

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Effect->StackingType = EGameplayEffectStackingType::AggregateByTarget;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		Effect->StackLimitCount = 1;
		Effect->StackDurationRefreshPolicy =
			EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;

		UTargetTagsGameplayEffectComponent& TagsComponent =
			Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
		FInheritedTagContainer Granted;
		Granted.Added.AddTag(EffectTag);
		TagsComponent.SetAndApplyTargetTagChanges(Granted);
	}
}

const TCHAR* UCataclysmSkillEffects::BurnRowName = TEXT("DoT_Burn");
const TCHAR* UCataclysmSkillEffects::BleedRowName = TEXT("DoT_Bleed");

const TCHAR* UCataclysmSkillEffects::StatusEffectTableAssetPath =
	TEXT("/Game/Data/DT_StatusEffects.DT_StatusEffects");

const UDataTable* UCataclysmSkillEffects::LoadStatusEffectTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, StatusEffectTableAssetPath);
	if (!Table)
	{
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/StatusEffects.csv."), StatusEffectTableAssetPath);
	}
	return Table;
}

float UCataclysmSkillEffects::WeaponDamageOf(const UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}
	return AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute());
}

bool UCataclysmSkillEffects::IsSpell(const FGameplayTagContainer& SkillTags)
{
	const FGameplayTag Spell = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(SpellTagName), /*ErrorIfNotFound=*/false);
	return Spell.IsValid() && SkillTags.HasTag(Spell);
}

float UCataclysmSkillEffects::SpellDamageOf(const UAbilitySystemComponent* Source,
										   const FGameplayTagContainer& SkillTags,
										   float SkillHealthCostPercent)
{
	const FGameplayAttribute Spell =
		UCataclysmCombatAttributeSet::GetSpellDamageAttribute();
	if (!Source || !Source->HasAttributeSetForAttribute(Spell))
	{
		return 0.0f;
	}

	const float FromAttribute = Source->GetNumericAttribute(Spell);

	// ASKED FOR RATHER THAN READ, so a bonus the attribute could not carry
	// reaches the spell. Issue #958. The attribute was worked out with no skill
	// in hand and nothing known about the character, so a modifier requiring a
	// tag and a modifier requiring a state were both left out of it.
	// `StatForSkill` runs the same pipeline again with both in hand, and
	// answers the attribute's own figure when nothing was recorded.
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(Source);
	const float Value = Cataclysm
		? Cataclysm->StatForSkill(FName(TEXT("spell_damage")), SkillTags,
								  FromAttribute, SkillHealthCostPercent)
		: FromAttribute;

	return FMath::Max(0.0f, Value);
}

float UCataclysmSkillEffects::IncreasesBehindAttackDamage(
	const UAbilitySystemComponent* Source)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(Source);

	// AN ABILITY SYSTEM THIS PROJECT DID NOT MAKE REMEMBERS NOTHING, and zero is
	// the right answer for it: with no increases the bracket below is one and
	// the arithmetic is what it was before this existed.
	return Cataclysm ? FMath::Max(0.0f, Cataclysm->GetAttackDamageIncreases())
					 : 0.0f;
}

float UCataclysmSkillEffects::IncreasesForSkill(
	const UAbilitySystemComponent* Source,
	const FGameplayTagContainer& SkillTags, float SkillHealthCostPercent)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(Source);
	if (!Cataclysm)
	{
		return IncreasesBehindAttackDamage(Source);
	}

	// NEGATIVE IS CLAMPED AWAY FOR THE SAME REASON THE OTHER ONE CLAMPS IT.
	// This multiplies the whole hit, and a sum of increases below -100% would
	// turn a blow into healing rather than into a very small blow.
	return FMath::Max(
		0.0f, Cataclysm->AttackDamageIncreasesForSkill(
				  SkillTags, SkillHealthCostPercent));
}

float UCataclysmSkillEffects::DamageAgainstTypeOf(
	const UAbilitySystemComponent* Source, const AActor* Target)
{
	// ONLY AN ENEMY CARRIES A DAMAGE TYPE, so a hit on anything else finds no
	// type to match and the affix correctly does nothing. That is the design's
	// own rule: "They read the target, not the weapon."
	const FName Type = DamageTypeOf(Target);
	if (Type.IsNone() || !Source)
	{
		return 0.0f;
	}

	// BUILT ONCE ON FIRST USE RATHER THAN AS A FILE-SCOPE STATIC, for the reason
	// UCataclysmPlayerClassStats::StatToAttribute gives: an FGameplayAttribute
	// wraps an FProperty found by reflection, and that data is not ready during
	// static initialisation.
	static const TMap<FName, FGameplayAttribute> Against = []
	{
		using Combat = UCataclysmCombatAttributeSet;
		return TMap<FName, FGameplayAttribute>{
			{TEXT("War"), Combat::GetDamageVsWarAttribute()},
			{TEXT("Demonic"), Combat::GetDamageVsDemonicAttribute()},
			{TEXT("Death"), Combat::GetDamageVsDeathAttribute()},
			{TEXT("Pestilence"), Combat::GetDamageVsPestilenceAttribute()},
			{TEXT("Famine"), Combat::GetDamageVsFamineAttribute()},
			{TEXT("Celestial"), Combat::GetDamageVsCelestialAttribute()},
			{TEXT("Chaos"), Combat::GetDamageVsChaosAttribute()},
			{TEXT("Void"), Combat::GetDamageVsVoidAttribute()},
		};
	}();

	const FGameplayAttribute* Attribute = Against.Find(Type);
	if (!Attribute || !Source->HasAttributeSetForAttribute(*Attribute))
	{
		return 0.0f;
	}

	// A FRACTION, because it joins the increases bucket, which is a sum of
	// fractions. The attribute holds a percentage because that is what an affix
	// grants.
	return FMath::Max(0.0f, Source->GetNumericAttribute(*Attribute)) / 100.0f;
}

float UCataclysmSkillEffects::ModifiedDamage(const UAbilitySystemComponent* Source,
											 float BaseDamage,
											 const FGameplayTagContainer& SkillTags)
{
	// An ability system component this project did not make carries no modifier
	// list, which is not a fault: an enemy's plain melee attack goes through
	// here too and has nothing to scale it.
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(Source);
	if (!Cataclysm || BaseDamage <= 0.0f)
	{
		return BaseDamage;
	}

	const TArray<FCataclysmStatModifier>& Modifiers = Cataclysm->GetStatModifiers();
	if (Modifiers.IsEmpty())
	{
		return BaseDamage;
	}

	// Base is the skill's damage, not the weapon's. A skill buff's increase is
	// written against what the skill deals -- Burning Wrath reads "4% increased
	// fire damage", not "4% increased weapon damage" -- so the skill's own
	// percentage has already been applied by the time this runs.
	return UCataclysmStatPipeline::Evaluate(BaseDamage, Modifiers, SkillTags).Final;
}

float UCataclysmSkillEffects::ApplyHit(AActor* Instigator, AActor* Target,
									   float DamagePercent,
									   const FGameplayTagContainer& SkillTags,
									   const FCataclysmHitDelivery& Delivery)
{
	if (DamagePercent <= 0.0f)
	{
		// A Support skill's slot damage is zero by design -- the Skill Slots
		// sheet says so -- so this is the ordinary case for a buff, not a fault.
		return 0.0f;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return 0.0f;
	}

	// THE THREE-BUCKET PIPELINE, REOPENED. Issue #895.
	//
	// `AttackDamage` is already `(base + flat) x (1 + increases)`, and two
	// things have to join those buckets rather than sitting outside them:
	//
	//   FLAT SPELL DAMAGE joins the flat bucket, for a skill carrying the
	//   `Type.Spell` tag. The project owner chose on 2026-08-24 that a spell
	//   keeps the weapon's damage as its base and spell damage is added on top,
	//   rather than replacing it as Path of Exile does. That keeps a Wand's own
	//   38 flat damage worth something to the caster holding it.
	//
	//   INCREASED DAMAGE AGAINST THE TARGET'S TYPE joins the increases bucket.
	//   The design: "a conditional increase joins the increases bracket rather
	//   than becoming a third multiplier. That is what Diablo 4 and Last Epoch
	//   both do." The difference is large -- a character at +125% increased
	//   damage with a top-tier +400% conditional affix deals 6.25 times its base
	//   if the two add and 11.25 times if they multiply.
	//
	// SO THE BRACKET IS UNDONE AND REDONE. Dividing by (1 + increases) recovers
	// what the flat bucket held, the spell damage is added to it, and the whole
	// thing is multiplied by (1 + increases + conditional) once.
	//
	// A CHARACTER WITH NEITHER GETS EXACTLY WHAT IT GOT BEFORE. With no
	// increases and no conditional bonus both figures are zero, the division and
	// the multiplication are both by one, and this is the line it replaces.
	//
	// THE TWO BRACKETS ARE NOT THE SAME SUM, since issue #958, and using one
	// figure for both is what stopped a passive node saying "while at or below
	// 35% health, +2% increased damage per point" from reaching anything.
	//
	//   `Folded` is what was put INTO the attribute, worked out with no skill in
	//   hand and nothing known about the character, so it is what has to come
	//   back out to recover the flat bucket.
	//
	//   `Applying` is what should be multiplied back IN: the same sum worked out
	//   again for the skill being used and the state the character is in, so a
	//   modifier scoped by tag or by health is counted this time.
	//
	// THEY ARE EQUAL FOR A CHARACTER CARRYING NEITHER KIND, which is every
	// character before this issue, so nothing else changes.
	// AND WHAT THIS SKILL COST TRAVELS INTO BOTH OF THEM. Issue #983. Grand
	// Tithe reads "a skill whose health cost is above 10% of your maximum
	// health deals 4% increased damage per point", which no state built from
	// the character alone can judge: the same character using two skills an
	// instant apart answers it differently for each.
	//
	// NOT INTO `Folded`, DELIBERATELY, and that is the same split issue #958
	// drew for a health condition. `Folded` is what was put INTO the
	// attribute, worked out with no skill in hand, so it must stay worked out
	// that way or the bonus would be divided straight back out again.
	const float Folded = IncreasesBehindAttackDamage(Source);
	const float Applying =
		IncreasesForSkill(Source, SkillTags, Delivery.SkillHealthCostPercent);
	const float Conditional = DamageAgainstTypeOf(Source, Target);

	const float BeforeIncreases =
		WeaponDamageOf(Source) / FMath::Max(1.0f + Folded, UE_KINDA_SMALL_NUMBER);
	const float Flat = IsSpell(SkillTags)
		? SpellDamageOf(Source, SkillTags, Delivery.SkillHealthCostPercent)
		: 0.0f;

	const float Damage = ModifiedDamage(
		Source,
		(BeforeIncreases * DamagePercent / 100.0f + Flat)
			* (1.0f + Applying + Conditional),
		SkillTags);
	if (Damage <= 0.0f)
	{
		// A character with no weapon damage. Expected before a weapon is
		// equipped, and worth saying once rather than silently dealing nothing.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s hit %s for %.0f%% of a weapon damage of zero."),
			*GetNameSafe(Instigator), *GetNameSafe(Target), DamagePercent);
		return 0.0f;
	}

	// THE SKILL'S OWN TAGS DECIDE WHETHER THIS IS AREA DAMAGE, and a caller may
	// also say so outright. The two are combined rather than one overriding the
	// other, because a skill row and an enemy's C++ ability answer the same
	// question by different routes and neither should be able to cancel the other.
	FCataclysmHitDelivery Arrived = Delivery;
	Arrived.bIsArea = Arrived.bIsArea || IsAreaDamage(SkillTags);

	// AND WHETHER IT WAS STRUCK IN MELEE, read the same way and combined the
	// same way. Issue #1032. `Type.Melee` is on both sides' skills already: six
	// of the seven enemy abilities and 27 rows of the weapon skill sheet.
	//
	// HERE RATHER THAN AT EACH CALLER, because this is the one place every
	// damaging blow passes through holding its own tags -- which is the reason
	// the damage type is set here too, three lines down.
	{
		const FGameplayTag Melee = UCataclysmDamageCalculation::MeleeTag();
		Arrived.bIsMelee =
			Arrived.bIsMelee || (Melee.IsValid() && SkillTags.HasTag(Melee));
	}

	// AND THE SKILL'S OWN DAMAGE TYPE GOES WITH IT, so the bolt and the burst
	// can be drawn in it. Only for colour: see the field's declaration. Set here
	// and not by each caller because this is the one place every damaging skill
	// passes through holding its own tags. Issue #803.
	//
	// ROUND-TRIPPED THROUGH THE ONE DECODER rather than by looking for a tag
	// under `Element` here. DamageTypeFromTags and ElementTagFor are the only
	// encoding and decoding of a damage type in the project and their comments
	// say why they live beside each other. A third place that picked the tag out
	// itself is how the two would come to disagree.
	if (!Arrived.SkillElement.IsValid())
	{
		Arrived.SkillElement = UCataclysmDamageCalculation::ElementTagFor(
			UCataclysmDamageCalculation::DamageTypeFromTags(SkillTags));
	}

	return ApplyDirectDamage(Instigator, Target, Damage, Arrived) ? Damage : 0.0f;
}

bool UCataclysmSkillEffects::ReduceHealthDirectly(AActor* Instigator,
												 AActor* Target, float Amount)
{
	if (Amount <= 0.0f)
	{
		return false;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Struck = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Struck
		|| !Struck->GetSet<UCataclysmVitalAttributeSet>())
	{
		return false;
	}

	// THE HEALTH ATTRIBUTE AND NOT THE DAMAGE META ATTRIBUTE. That is what keeps
	// this from being a hit: the vital attribute set only runs the mitigation
	// order when the Damage attribute changes, and handles the Health attribute
	// changing with a clamp and a death check.
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(), FName(TEXT("CataclysmHealthLoss")));
	Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

	const int32 Index = Effect->Modifiers.Num();
	Effect->Modifiers.SetNum(Index + 1);
	FGameplayModifierInfo& Modifier = Effect->Modifiers[Index];
	Modifier.Attribute = UCataclysmVitalAttributeSet::GetHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FScalableFloat(-Amount);

	FGameplayEffectContextHandle Context = Source->MakeEffectContext();
	Context.AddInstigator(Instigator, Instigator);
	Struck->ApplyGameplayEffectToSelf(Effect, /*Level=*/1.0f, Context);
	return true;
}

bool UCataclysmSkillEffects::ApplyDirectDamage(AActor* Instigator, AActor* Target,
											   float Damage,
											   const FCataclysmHitDelivery& Delivery)
{
	if (Damage <= 0.0f)
	{
		return false;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return false;
	}

	// Written into the Damage META attribute, which the defender's vital
	// attribute set intercepts in PostGameplayEffectExecute and runs through
	// UCataclysmDamageCalculation::Resolve -- evasion, block, armor, resistance,
	// flat reduction, mana, energy shield, health, in that order. Nothing here
	// decides how much of it lands.
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(), FName(TEXT("CataclysmSkillHit")));
	Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

	const int32 Index = Effect->Modifiers.Num();
	Effect->Modifiers.SetNum(Index + 1);
	FGameplayModifierInfo& Modifier = Effect->Modifiers[Index];
	Modifier.Attribute = UCataclysmVitalAttributeSet::GetDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FScalableFloat(Damage);

	FGameplayEffectContextHandle Context = Source->MakeEffectContext();
	Context.AddInstigator(Instigator, Instigator);
	ApplyTypedSpec(Effect, Context, Defender, Instigator, Delivery);

	return true;
}

const TCHAR* UCataclysmSkillEffects::PointBlankAreaTagName =
	TEXT("Type.AOE.PointBlank");
const TCHAR* UCataclysmSkillEffects::SpellTagName = TEXT("Type.Spell");

const TCHAR* UCataclysmSkillEffects::AuraAreaTagName = TEXT("Type.AOE.Aura");

bool UCataclysmSkillEffects::IsAreaDamage(const FGameplayTagContainer& SkillTags)
{
	if (SkillTags.IsEmpty())
	{
		return false;
	}

	UGameplayTagsManager& Tags = UGameplayTagsManager::Get();
	for (const TCHAR* Name : { PointBlankAreaTagName, AuraAreaTagName })
	{
		const FGameplayTag Tag =
			Tags.RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid() && SkillTags.HasTag(Tag))
		{
			return true;
		}
	}
	return false;
}

FName UCataclysmSkillEffects::DamageTypeOf(const AActor* Attacker)
{
	// ONLY AN ENEMY'S DAMAGE IS TYPED. The project owner settled this on
	// 2026-08-12: a player has eight resistances because eight Cataclysms attack
	// them, so an enemy's hit has to say which one applies. An enemy has ONE
	// generic resistance that meets every hit whatever it is, so a player's hit
	// has nothing to choose between and carries no type at all.
	//
	// A minion's or a projectile's hit is typed by whoever fired it, because the
	// instigator passed down the chain is that character rather than the thing it
	// sent. So an enemy's projectile is Demonic and a player's is untyped, which
	// is the same rule and not a special case.
	const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Attacker);
	return Enemy ? Enemy->DamageType : NAME_None;
}

void UCataclysmSkillEffects::ApplyTypedSpec(UGameplayEffect* Effect,
											 const FGameplayEffectContextHandle& Context,
											 UAbilitySystemComponent* Defender,
											 const AActor* Attacker,
											 const FCataclysmHitDelivery& Delivery)
{
	// BUILT AS A SPEC RATHER THAN HANDED TO ApplyGameplayEffectToSelf, purely so
	// a tag can be put on it. That overload builds the spec itself and gives no
	// chance to add one, and a dynamic asset tag is how the hit's damage type
	// reaches the defender: see UCataclysmDamageCalculation::ElementTagFor.
	FGameplayEffectSpec Spec(Effect, Context, /*Level=*/1.0f);

	// THE ATTACKER'S TYPE FIRST, AND IT IS THE ONE THAT DECIDES A RESISTANCE.
	// Unchanged: an enemy's hit says which of the player's eight resistances
	// meets it, and a player's hit says nothing, because an enemy holds one
	// generic resistance and has nothing to choose between.
	const FGameplayTag Element =
		UCataclysmDamageCalculation::ElementTagFor(DamageTypeOf(Attacker));
	if (Element.IsValid())
	{
		Spec.AddDynamicAssetTag(Element);
	}
	else if (Delivery.SkillElement.IsValid())
	{
		// A PLAYER'S HIT: THE SKILL'S TYPE, FOR COLOUR AND NOTHING ELSE. Without
		// this every effect a player skill produced drew in the authored
		// default, which is white, so a Demonic skill and a War skill looked the
		// same and both looked like the "nothing set this" case. Issue #803.
		//
		// THE MARKER IS WHAT KEEPS THE DAMAGE RULE INTACT. The defender reads
		// the element tag for what to draw either way, and reads it as a
		// resistance to apply only when this marker is absent. Stamped here,
		// where the attacker is known and alive, so an enemy's burn still
		// resolves correctly after the enemy is dead.
		const FGameplayTag ColourOnly =
			UCataclysmDamageCalculation::ElementIsForColourOnlyTag();
		if (ColourOnly.IsValid())
		{
			Spec.AddDynamicAssetTag(Delivery.SkillElement);
			Spec.AddDynamicAssetTag(ColourOnly);
		}
	}

	// HOW THE HIT ARRIVED, as two more tags on the same spec. Added only when
	// true, so an ordinary direct blow carries nothing extra.
	if (Delivery.bIsArea)
	{
		const FGameplayTag Area = UCataclysmDamageCalculation::AreaDamageTag();
		if (Area.IsValid())
		{
			Spec.AddDynamicAssetTag(Area);
		}
	}
	if (Delivery.bIsDamageOverTime)
	{
		const FGameplayTag OverTime =
			UCataclysmDamageCalculation::DamageOverTimeTag();
		if (OverTime.IsValid())
		{
			Spec.AddDynamicAssetTag(OverTime);
		}
	}
	if (Delivery.bIsMelee)
	{
		// Issue #1032. Without this the defender cannot tell a melee blow from
		// any other, because a skill's tags stop where the blow is built and
		// only what is added here reaches whoever it lands on.
		const FGameplayTag Melee = UCataclysmDamageCalculation::MeleeTag();
		if (Melee.IsValid())
		{
			Spec.AddDynamicAssetTag(Melee);
		}
	}
	if (Delivery.bCannotCriticallyStrike)
	{
		const FGameplayTag NoCrit =
			UCataclysmDamageCalculation::NoCriticalStrikeTag();
		if (NoCrit.IsValid())
		{
			Spec.AddDynamicAssetTag(NoCrit);
		}
	}
	if (Delivery.bCannotPenetrate)
	{
		const FGameplayTag NoPenetration =
			UCataclysmDamageCalculation::NoPenetrationTag();
		if (NoPenetration.IsValid())
		{
			Spec.AddDynamicAssetTag(NoPenetration);
		}
	}

	if (Delivery.bCarriesNoWeaponSubType)
	{
		const FGameplayTag NoSubType =
			UCataclysmDamageCalculation::NoWeaponSubTypeTag();
		if (NoSubType.IsValid())
		{
			Spec.AddDynamicAssetTag(NoSubType);
		}
	}

	if (Delivery.bCannotLeech)
	{
		const FGameplayTag NoLeech = UCataclysmDamageCalculation::NoLeechTag();
		if (NoLeech.IsValid())
		{
			Spec.AddDynamicAssetTag(NoLeech);
		}
	}

	if (Delivery.bCannotBeRetaliatedAgainst)
	{
		const FGameplayTag NoRetaliation =
			UCataclysmDamageCalculation::NoRetaliationTag();
		if (NoRetaliation.IsValid())
		{
			Spec.AddDynamicAssetTag(NoRetaliation);
		}
	}

	// THE SKILL'S OWN CRITICAL STRIKE CHANCE, AS A NUMBER RATHER THAN A TAG.
	// Everything above is a yes-or-no property and rides as a tag. This is a
	// figure, so it rides as a set-by-caller magnitude, which is Unreal's own way
	// to attach a number to one application of an effect. Added only when the
	// skill states one, so a hit that says nothing carries nothing and the
	// defender reads the attacker's attribute exactly as before. Issue #657.
	if (Delivery.CritChancePercent >= 0.0f)
	{
		const FGameplayTag CritChanceKey =
			UCataclysmDamageCalculation::SkillCritChanceDataTag();
		if (CritChanceKey.IsValid())
		{
			Spec.SetSetByCallerMagnitude(CritChanceKey, Delivery.CritChancePercent);
		}
	}

	Defender->ApplyGameplayEffectSpecToSelf(Spec);
}

FCataclysmStatusEffectNumbers UCataclysmSkillEffects::BurnNumbers()
{
	return StatusEffectNumbers(BurnRowName, TEXT("Burn"));
}

FCataclysmStatusEffectNumbers UCataclysmSkillEffects::BleedNumbers()
{
	return StatusEffectNumbers(BleedRowName, TEXT("Bleed"));
}

FCataclysmStatusEffectNumbers UCataclysmSkillEffects::StatusEffectNumbers(
	const TCHAR* RowName, const TCHAR* HumanName)
{
	FCataclysmStatusEffectNumbers Numbers;

	const UDataTable* Table = LoadStatusEffectTable();
	if (!Table)
	{
		return Numbers;
	}

	const FCataclysmStatusEffectRow* Row = Table->FindRow<FCataclysmStatusEffectRow>(
		FName(RowName), TEXT("UCataclysmSkillEffects::StatusEffectNumbers"),
		/*bWarnIfRowMissing=*/false);
	if (!Row)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("The status effect table has no %s row, so nothing can apply "
				 "%s at all."), RowName, HumanName);
		return Numbers;
	}

	Numbers.DurationSeconds = Row->DurationSeconds;
	Numbers.FlatDamagePerTick = Row->FlatDamagePerTick;
	Numbers.PercentOfHit = Row->PercentOfHit;
	Numbers.PercentOfCurrentHealth = Row->PercentOfCurrentHealth;

	// BOTH HALVES ARE NEEDED AND EITHER ONE MISSING IS THE SAME FAULT. Burn had
	// neither until issue #895, and an effect lasting zero seconds or worth zero
	// damage is indistinguishable from an effect nobody wrote -- which is exactly
	// how the missing cooldown in issue #155 stayed hidden.
	//
	// EITHER BASE SATISFIES THE SECOND HALF. Burn states a flat amount since
	// 2026-08-24 and stated a percent of the hit before that, and Bleed states a
	// flat 20 a second; all are a per-tick amount this path can apply. A percent
	// of the target's current health is deliberately NOT accepted: it is a
	// different amount every tick, so it cannot be resolved to the one fixed
	// figure this path needs.
	const bool bStatesAnAmount = Numbers.FlatDamagePerTick > 0.0f
		|| Numbers.PercentOfHit > 0.0f;
	Numbers.bUsable = Numbers.DurationSeconds > 0.0f && bStatesAnAmount;
	if (!Numbers.bUsable)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s states a duration of %.1fs, a flat %.1f a tick and %.0f%% "
				 "of the hit. The duration and one of the two amounts must be "
				 "above zero or nothing is applied. They come from columns B, H "
				 "and C of the DoTs sheet."),
			HumanName, Numbers.DurationSeconds, Numbers.FlatDamagePerTick,
			Numbers.PercentOfHit);
	}

	return Numbers;
}

FName UCataclysmSkillEffects::StatusEffectRowForTag(const FGameplayTag& EffectTag)
{
	if (!EffectTag.IsValid())
	{
		return NAME_None;
	}

	const UDataTable* Table = LoadStatusEffectTable();
	if (!Table)
	{
		return NAME_None;
	}

	// THE LAST SEGMENT ONLY. `Status.VoidSplinter` and `Keyword.DoT.VoidSplinter`
	// are the same effect under two branches, and the branch says which VOCABULARY
	// the tag came from rather than which effect it is. See the header.
	FString Whole = EffectTag.ToString();
	FString Segment = Whole;
	int32 LastDot = INDEX_NONE;
	if (Whole.FindLastChar(TEXT('.'), LastDot))
	{
		Segment = Whole.RightChop(LastDot + 1);
	}
	if (Segment.IsEmpty())
	{
		return NAME_None;
	}

	FName Found = NAME_None;
	Table->ForeachRow<FCataclysmStatusEffectRow>(
		TEXT("UCataclysmSkillEffects::StatusEffectRowForTag"),
		[&Found, &Segment](const FName& RowName,
						   const FCataclysmStatusEffectRow& Row)
		{
			if (!Found.IsNone())
			{
				// ForeachRow HAS NO WAY TO STOP, so the first match is kept and
				// the rest of the walk does nothing. The table is 52 rows.
				return;
			}

			// THE SAME REDUCTION `tag_segment` IN
			// `tools/generate_gameplay_tags.py` DOES: letters and digits kept,
			// everything else dropped. "Void Splinter" becomes "VoidSplinter"
			// and "Touch of Nothing" becomes "TouchofNothing", which is what
			// that script produced and what the tag list holds.
			FString Reduced;
			Reduced.Reserve(Row.EffectName.Len());
			for (const TCHAR Letter : Row.EffectName)
			{
				if (FChar::IsAlnum(Letter))
				{
					Reduced.AppendChar(Letter);
				}
			}

			// CASE-SENSITIVE, BECAUSE THE TAG AND THE NAME ARE GENERATED FROM
			// THE SAME CELL. A difference in case would mean the tag list and
			// the effect table had drifted, and answering anyway would hide it.
			if (Reduced.Equals(Segment, ESearchCase::CaseSensitive))
			{
				Found = RowName;
			}
		});

	return Found;
}

float UCataclysmSkillEffects::AsMultiplier(const UAbilitySystemComponent* Source,
										  const FGameplayAttribute& Stat)
{
	// A BASELINE OF 100 MEANS UNCHANGED, which is what the design gives area of
	// effect and all three damage over time stats: "They are percentages of
	// whatever the skill or the effect itself does, so their baseline is 100%
	// rather than zero."
	//
	// AND A CHARACTER WITH NO SUCH ATTRIBUTE IS UNCHANGED TOO. Every enemy in
	// the game holds the combat attribute set, so this is the path an ability
	// system this project did not make would take.
	if (!Source || !Source->HasAttributeSetForAttribute(Stat))
	{
		return 1.0f;
	}

	// CLAMPED AT ZERO RATHER THAN ALLOWED NEGATIVE. Nothing in the design states
	// a negative one, and a negative duration or interval is not a smaller
	// effect, it is an effect that cannot be applied at all.
	return FMath::Max(0.0f, Source->GetNumericAttribute(Stat) / 100.0f);
}

float UCataclysmSkillEffects::AsMultiplierForSkill(
	const UAbilitySystemComponent* Source, const FGameplayAttribute& Stat,
	FName StatName, const FGameplayTagContainer& SkillTags)
{
	// THE SAME TWO GUARDS AS AsMultiplier, AND THEY COME FIRST FOR THE SAME
	// REASON. A blow dealt in someone else's name has no source, and an ability
	// system without the attribute has nothing to scale.
	if (!Source || !Source->HasAttributeSetForAttribute(Stat))
	{
		return 1.0f;
	}

	const float FromAttribute = Source->GetNumericAttribute(Stat);

	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(Source);
	const float Value = Cataclysm
		? Cataclysm->StatForSkill(StatName, SkillTags, FromAttribute)
		: FromAttribute;

	return FMath::Max(0.0f, Value / 100.0f);
}

FCataclysmDamageOverTimeNumbers UCataclysmSkillEffects::DamageOverTimeNumbers(
	const UAbilitySystemComponent* Source, float DamagePerTick,
	float DurationSeconds)
{
	FCataclysmDamageOverTimeNumbers Numbers;
	if (DamagePerTick <= 0.0f || DurationSeconds <= 0.0f)
	{
		return Numbers;
	}

	const float FrequencyScale = AsMultiplier(
		Source, UCataclysmCombatAttributeSet::GetDotFrequencyAttribute());

	// A FREQUENCY OF ZERO WOULD BE A DIVISION BY ZERO, and is refused rather
	// than clamped to something invented. Nothing in the game can produce one:
	// the class line gives 100 and every source is an increase.
	if (FrequencyScale <= 0.0f)
	{
		return Numbers;
	}

	Numbers.DamagePerTick = DamagePerTick * AsMultiplier(
		Source, UCataclysmCombatAttributeSet::GetDotDamageAttribute());
	Numbers.DurationSeconds = DurationSeconds * AsMultiplier(
		Source, UCataclysmCombatAttributeSet::GetDotDurationAttribute());

	// FREQUENCY DIVIDES THE GAP BETWEEN TICKS. More of it is a shorter gap and
	// so more ticks in the same time, which is what "More ticks in the same
	// time | Rises" means in the design's own table.
	Numbers.SecondsPerTick = BaseSecondsPerTick / FrequencyScale;

	Numbers.Ticks = Numbers.SecondsPerTick > 0.0f
		? Numbers.DurationSeconds / Numbers.SecondsPerTick
		: 0.0f;
	Numbers.TotalDamage = Numbers.DamagePerTick * Numbers.Ticks;

	Numbers.bUsable = Numbers.DamagePerTick > 0.0f
		&& Numbers.DurationSeconds > 0.0f && Numbers.SecondsPerTick > 0.0f;
	return Numbers;
}

bool UCataclysmSkillEffects::ApplyDamageOverTime(
	AActor* Instigator, AActor* Target, float DamagePerTick,
	float DurationSeconds, const FGameplayTag& EffectTag,
	bool bScalesWithInstigator)
{
	if (DamagePerTick <= 0.0f || DurationSeconds <= 0.0f)
	{
		return false;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return false;
	}

	// THE ATTACKER'S THREE STATS, AND UNTIL ISSUE #895 NONE OF THEM WAS READ.
	// All three attributes existed, were clamped and were replicated, and the
	// comment that used to stand here said the frequency stat "is not wired in
	// yet". The three affixes granting them were worth nothing.
	//
	// NULL FOR A BLOW DEALT IN SOMEONE ELSE'S NAME, which is how a minion's burn
	// declines the summoner's three stats without this function needing to know
	// what a minion is.
	const FCataclysmDamageOverTimeNumbers Numbers = DamageOverTimeNumbers(
		bScalesWithInstigator ? Source : nullptr, DamagePerTick,
		DurationSeconds);
	if (!Numbers.bUsable)
	{
		return false;
	}

	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(), FName(TEXT("CataclysmDamageOverTime")));
	Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	// AND THE TARGET'S OWN STAT DECIDES HOW LONG IT REALLY LASTS, after the
	// attacker's three stats above have decided what it deals and how often.
	// Issue #1033. The other path is `ApplyTagForDuration`; both call this so
	// that a stun and a burn cannot disagree.
	//
	// THE TICK LENGTH IS NOT SCALED WITH IT, so a longer burn is more ticks of
	// the same size rather than the same number of slower ones. That is what
	// makes a node lengthening an effect on the character worth something to a
	// class paid per effect carried, and it matches how the attacker's own
	// duration stat already behaves in `DamageOverTimeNumbers`.
	Effect->DurationMagnitude = FGameplayEffectModifierMagnitude(
		FScalableFloat(UCataclysmDebuffs::DurationOn(
			Defender, Numbers.DurationSeconds)));
	Effect->Period = FScalableFloat(Numbers.SecondsPerTick);

	// False, so the first tick lands a second in rather than at once. Applying
	// on the instant would mean a skill that reapplies burn faster than once a
	// second dealt its whole burn on every application.
	Effect->bExecutePeriodicEffectOnApplication = false;

	const int32 Index = Effect->Modifiers.Num();
	Effect->Modifiers.SetNum(Index + 1);
	FGameplayModifierInfo& Modifier = Effect->Modifiers[Index];
	Modifier.Attribute = UCataclysmVitalAttributeSet::GetDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FScalableFloat(Numbers.DamagePerTick);

	MakeSingleStackTagged(Effect, EffectTag);

	FGameplayEffectContextHandle Context = Source->MakeEffectContext();
	Context.AddInstigator(Instigator, Instigator);
	// Typed the same way a direct hit is. A burn left by an enemy is that
	// enemy's damage type, so the player's resistance to it applies to every
	// tick and not only to the blow that started it.
	//
	// AND MARKED AS DAMAGE OVER TIME, which is what stops an energy shield
	// absorbing it. A shield that soaked burn would be a second health bar rather
	// than a distinct defence, and damage over time is the design's answer to
	// shield stacking. Issue #513.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsDamageOverTime = true;
	ApplyTypedSpec(Effect, Context, Defender, Instigator, Delivery);

	return true;
}

FGameplayTag UCataclysmSkillEffects::BurnTag()
{
	// Requested by name rather than declared as a native tag, for the same
	// reason CataclysmAbilitySlots requests the slot tags by name: a native
	// declaration would create the tag whether or not the workbook still lists
	// it, hiding exactly the disagreement that matters.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Burn")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmSkillEffects::ApplyBurn(AActor* Instigator, AActor* Target,
									   float HitDamage,
									   bool bScalesWithInstigator)
{
	const FCataclysmStatusEffectNumbers Burn = BurnNumbers();

	// THE ZERO-DAMAGE REFUSAL USED TO BE REDUNDANT AND IS NOW LOAD-BEARING.
	// While Burn was a percent of the hit, a hit dealing nothing produced a burn
	// worth nothing and ApplyDamageOverTime refused it anyway. Burn is a flat
	// amount since 2026-08-24, so without this a fully mitigated hit would apply
	// a burn at full strength. Keeping it preserves the behaviour the project
	// already had; whether a hit that dealt nothing should apply an ailment at
	// all is a design question and is issue #917.
	if (!Burn.bUsable || HitDamage <= 0.0f)
	{
		return false;
	}

	// WHAT THE TABLE STATES IS WHAT ONE TICK DEALS, AND IT WAS READ AS A TOTAL
	// UNTIL ISSUE #895. Under that reading raising the tick rate divided one
	// total into more, smaller ticks, so Damage over Time Frequency could not be
	// worth anything -- and the design's reason for having three separate stats
	// is that all three multiply.
	//
	// THE AMOUNT IS FLAT RATHER THAN A SHARE OF THE HIT since 2026-08-24, which
	// the project owner chose because a share of the hit multiplies twice: the
	// hit grows about fifteenfold across the eight difficulty tiers and the
	// three stats multiply on top. Burn is now 25 a second for four seconds,
	// which is 100 before those stats. DamagePerTickAgainst still reads the
	// percent-of-hit column, so a row stating one would work unchanged.
	return ApplyDamageOverTime(Instigator, Target,
							   Burn.DamagePerTickAgainst(HitDamage),
							   Burn.DurationSeconds, BurnTag(),
							   bScalesWithInstigator);
}

FGameplayTag UCataclysmSkillEffects::StunnedTag()
{
	// Requested by name rather than declared natively, for the reason BurnTag
	// gives: a native declaration would create the tag whether or not the
	// workbook still lists it, hiding exactly the disagreement that matters.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Stunned")), /*ErrorIfNotFound=*/false);
}

FGameplayTag UCataclysmSkillEffects::StunImmuneTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.StunImmune")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmSkillEffects::IsStunned(const AActor* Actor)
{
	return HasTag(Actor, StunnedTag());
}

bool UCataclysmSkillEffects::ApplyKnockback(AActor* Instigator, AActor* Target,
											float DistanceCm)
{
	if (DistanceCm <= 0.0f || !IsValid(Instigator) || !IsValid(Target))
	{
		return false;
	}

	// Away from whatever hit it, along the ground.
	FVector Away = Target->GetActorLocation() - Instigator->GetActorLocation();
	Away.Z = 0.0f;
	if (Away.IsNearlyZero())
	{
		// Standing exactly on the instigator. There is no direction to push in,
		// and picking one arbitrarily would shove a target somewhere nobody could
		// have predicted.
		return false;
	}

	// HALVED FOR EACH DISPLACEMENT THE TARGET HAS ALREADY TAKEN INSIDE THE
	// WINDOW: the full distance, then half, then a quarter, resetting once 5
	// seconds pass with no displacement at all. Three shoves inside the window
	// move a target seven metres in total rather than twelve, which is what stops
	// it being held at the far end of a room. Issues #302 and #628.
	//
	// ASKED OF THE TARGET, because the count belongs to the target rather than to
	// whatever is shoving: the previous shove was usually a different attack and
	// often a different actor. It is kept on the ability system component because
	// that is what everything hittable has.
	float Share = 1.0f;
	if (UCataclysmAbilitySystemComponent* TargetAbilities =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Target)))
	{
		Share = TargetAbilities->TakeNextDisplacementShare();
	}

	// A DISPLACEMENT RATHER THAN AN IMPULSE, because most of what this hits has
	// no physics body and a knockback that silently did nothing would look
	// exactly like a knockback. Swept, so a shove into a wall stops at the wall.
	Target->AddActorWorldOffset(Away.GetSafeNormal() * DistanceCm * Share,
								/*bSweep=*/true);
	return true;
}

FGameplayTag UCataclysmSkillEffects::DeadTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Dead")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmSkillEffects::IsDead(const AActor* Actor)
{
	return HasTag(Actor, DeadTag());
}

bool UCataclysmSkillEffects::MarkDead(AActor* Actor)
{
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
	const FGameplayTag Dead = DeadTag();
	if (!System || !Dead.IsValid() || System->HasMatchingGameplayTag(Dead))
	{
		return false;
	}

	// LOOSELY RATHER THAN THROUGH AN EFFECT, because every other tag here is
	// granted for a duration and this one must never expire on its own. A
	// duration effect with an infinite duration would do the same thing with
	// more moving parts and one more way to get the duration wrong. What takes
	// it off again is ClearDead below, called deliberately.
	System->AddLooseGameplayTag(Dead);
	return true;
}

bool UCataclysmSkillEffects::ClearDead(AActor* Actor)
{
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
	const FGameplayTag Dead = DeadTag();
	if (!System || !Dead.IsValid() || !System->HasMatchingGameplayTag(Dead))
	{
		return false;
	}

	System->RemoveLooseGameplayTag(Dead);
	return true;
}

bool UCataclysmSkillEffects::ApplyStun(AActor* Instigator, AActor* Target,
									   float DurationSeconds, float DamageDealt,
									   bool bStunIsDesigned)
{
	if (DurationSeconds <= 0.0f)
	{
		return false;
	}

	// RULE TWO: A STUNNED TARGET CANNOT BE STUNNED AGAIN FOR FIVE SECONDS.
	// Checked first because it is the cheapest and because it applies to every
	// stun, designed or incidental. A designed stun does not escape it: that is
	// the whole reason the Brute's Stomp has a five second cooldown rather than
	// a cooldown from the Heavy slot's one-to-four second band.
	if (HasTag(Target, StunImmuneTag()))
	{
		return false;
	}

	// RULE ONE: A HIT MUST TAKE AT LEAST A TENTH OF MAXIMUM HEALTH TO STUN.
	//
	// A DESIGNED STUN SKIPS THIS AND ONLY THIS. An attack whose entire purpose
	// is to stun should not fail to when it lands, so the Stomp does not have to
	// clear the bar. It still obeys the window above. The distinction is
	// stun_is_designed in sim/cataclysm_sim/damage.py.
	if (!bStunIsDesigned)
	{
		const UAbilitySystemComponent* Defender =
			UCataclysmTargeting::AbilitySystemOf(Target);
		if (!Defender)
		{
			return false;
		}

		// A target already dead is not stunned, matching can_be_stunned in the
		// model. Without this a killing blow would stun a corpse.
		const float Health = Defender->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
		if (Health <= 0.0f)
		{
			return false;
		}

		const float MaxHealth = Defender->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		if (MaxHealth <= 0.0f)
		{
			return false;
		}

		if (DamageDealt < MaxHealth * StunDamageThresholdPercent / 100.0f)
		{
			return false;
		}
	}

	// RULE THREE: A BOSS CANNOT BE STUNNED AT ALL. Section VI of the design
	// document, and the last of its three anti-stun-lock rules to arrive --
	// issue #395. Boss-ness derives from the rarity the spawner set, steps 4
	// and 5 of the ladder: Boss and Cataclysm Boss. See
	// ACataclysmEnemyCharacter::IsBoss for why it is rarity rather than a flag
	// or a tag.
	//
	// UNCONDITIONALLY, INCLUDING FOR A DESIGNED STUN. bStunIsDesigned exists to
	// skip the damage THRESHOLD above, because an attack built to stun should
	// not fail to when it lands; it does not skip the immunity window and it
	// does not skip this. "At all" means at all.
	if (const ACataclysmEnemyCharacter* Enemy =
			Cast<ACataclysmEnemyCharacter>(Target))
	{
		if (Enemy->IsBoss())
		{
			return false;
		}
	}

	if (!ApplyTagForDuration(Instigator, Target, StunnedTag(), DurationSeconds))
	{
		return false;
	}

	// THE WINDOW STARTS WHEN THE STUN DOES, not when it ends, which is why five
	// seconds of immunity and 1.5 seconds of stun leave a 3.5 second gap the
	// target can act in before it can be stunned again.
	ApplyTagForDuration(Instigator, Target, StunImmuneTag(),
						StunImmunityWindowSeconds);

	return true;
}

bool UCataclysmSkillEffects::HasTag(const AActor* Actor, const FGameplayTag& Tag)
{
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Actor);
	return AbilitySystem && Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag);
}

bool UCataclysmSkillEffects::ApplyTagForDuration(
	AActor* Instigator, AActor* Target, const FGameplayTag& EffectTag,
	float DurationSeconds)
{
	if (!EffectTag.IsValid() || DurationSeconds <= 0.0f)
	{
		return false;
	}

	UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Instigator);
	UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!Source || !Defender)
	{
		return false;
	}

	// AND THE TARGET'S OWN STAT DECIDES HOW LONG IT REALLY LASTS. Issue
	// #1033. Two Masochist nodes lengthen the harmful effects on the
	// character itself, because eleven nodes in that branch pay it for each
	// one it carries. Unchanged for every other character in the game.
	//
	// ONE OF THE TWO PATHS THAT APPLY A LASTING EFFECT, and the other is
	// `ApplyDamageOverTime`. Honouring one and not the other would lengthen a
	// stun and not a burn, or the reverse, with nothing reporting it.
	const float OnTarget =
		UCataclysmDebuffs::DurationOn(Defender, DurationSeconds);
	if (OnTarget <= 0.0f)
	{
		// A DURATION THE TARGET'S STAT TOOK TO NOTHING APPLIES NOTHING, rather
		// than applying an effect with no duration, which the engine would
		// treat as lasting for ever. Unreachable today: nothing lowers the stat.
		return false;
	}

	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(),
		FName(*FString::Printf(TEXT("CataclysmStatus_%s"), *EffectTag.ToString())));
	Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	Effect->DurationMagnitude =
		FGameplayEffectModifierMagnitude(FScalableFloat(OnTarget));

	MakeSingleStackTagged(Effect, EffectTag);

	FGameplayEffectContextHandle Context = Source->MakeEffectContext();
	Context.AddInstigator(Instigator, Instigator);
	Defender->ApplyGameplayEffectToSelf(Effect, /*Level=*/1.0f, Context);

	return true;
}

int32 UCataclysmSkillEffects::RemoveEffectsGranting(
	AActor* Target, const FGameplayTag& EffectTag)
{
	if (!EffectTag.IsValid())
	{
		return 0;
	}

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Target);
	if (!AbilitySystem)
	{
		return 0;
	}

	// ASKED BEFORE AND AFTER, because RemoveActiveEffectsWithGrantedTags returns
	// void. A caller that cannot tell whether anything was taken away cannot be
	// tested, and the aura this exists for needs exactly that: a test has to be
	// able to say that killing the Succubus took the buff OFF, rather than that
	// it called something.
	const bool bHadIt = AbilitySystem->HasMatchingGameplayTag(EffectTag);

	FGameplayTagContainer Granted;
	Granted.AddTag(EffectTag);
	AbilitySystem->RemoveActiveEffectsWithGrantedTags(Granted);

	return (bHadIt && !AbilitySystem->HasMatchingGameplayTag(EffectTag)) ? 1 : 0;
}
