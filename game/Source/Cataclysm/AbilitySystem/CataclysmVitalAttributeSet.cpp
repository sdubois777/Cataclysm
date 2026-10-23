// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmVitalAttributeSet.h"
// For asking the attacker what a stat is worth with this hit's tags and the
// attacker's own state in hand, rather than reading the attribute. Issue #959.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For a debuff on this character spreading to what stands near it. Issue #1058.
#include "AbilitySystem/CataclysmContagion.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "AbilitySystem/CataclysmDamageConversion.h"
// For the share of a blow that is taken later instead of now. Ruled 2026-10-12.
#include "AbilitySystem/CataclysmDelayedDamage.h"
// For the share of a blow on an ally that each wearer beside it takes. Ruled 2026-10-15.
#include "AbilitySystem/CataclysmAllyDamageShare.h"
// For the share of a point-blank skill's blow struck at an ally inside its area. Ruled 2026-10-15.
#include "AbilitySystem/CataclysmAreaShareOnAllies.h"
// For the Bleeding a melee critical strike may apply. Issue #1032.
#include "AbilitySystem/CataclysmDebuffs.h"
// For that Bleeding's roll, drawn below 100. Issue #2201.
#include "AbilitySystem/CataclysmPercentDraw.h"
// For every chance to apply an ailment a blow carried, a blunt weapon's stun
// among them. Issue #899.
#include "AbilitySystem/CataclysmAilments.h"
// For asking who was commanding a dying creature. Issue #1518.
#include "AbilitySystem/CataclysmCommand.h"
// For announcing each blow, and keeping it as the target's last, so a death
// can say who dealt it. Issue #41, slice 4.
#include "AbilitySystem/CataclysmCombatEvents.h"
// For turning health lost to damage into Fervour. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
// For the character's own Cataclysm type, so a hit of another one can be told
// apart from its own. Issue #975.
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "AbilitySystem/CataclysmLeech.h"
// For the debt cleared and the Fervour granted by dropping to low health.
// Issue #1069.
#include "AbilitySystem/CataclysmLowHealthRelief.h"
// For how much a defender strikes back for, at whom, and what it leeches from
// it. Issues #1047 and #1048.
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmMinion.h"
// For the Soul a minion's death leaves, Reaper's Embrace's six-piece.
#include "AbilitySystem/CataclysmReaperSoul.h"
// For the stack that taking damage builds. Issue #1003.
#include "AbilitySystem/CataclysmStacks.h"
#include "Character/CataclysmCharacterBase.h"
// For the difficulty tier a hit resolves at. It lives on the game mode because
// nothing smaller holds one and the design's own home for it, the dungeon, does
// not exist yet. Issue #514.
#include "Player/CataclysmGameMode.h"
// For the weapon sub-type a hit carries, which is a property of what the
// attacker is holding rather than a number on its attribute set. Issue #639.
#include "Items/CataclysmWeaponSlotsComponent.h"
// For the floating number that says what the blow did. Issue #518.
#include "Interface/CataclysmCombatOverlay.h"
#include "Cataclysm.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

/**
 * Forces the critical strike roll to a fixed number, so it can be watched.
 *
 * WHY THIS EXISTS. A critical strike is the first random roll in the project
 * that fires on an ordinary hit. Evasion and block are rolled too, but a
 * defender's evasion and block chance are both zero unless something sets them,
 * so those rolls never fire by accident. An attacker's critical strike chance is
 * never zero: a player holding any weapon has 5% and every enemy archetype has
 * between 5% and 15%. Without a way to pin it, every automation test that asserts
 * an exact damage figure through a real gameplay effect would pass most of the
 * time and fail the rest, which is worse than failing.
 *
 * `UCataclysmDamageCalculation::Resolve` already takes the roll as a parameter,
 * which serves a test calling it directly. This serves the other path, where the
 * hit arrives as a gameplay effect and no test is holding the arguments.
 *
 * -1, the default, rolls normally. 0 always critically strikes, because every
 * chance above zero beats it. 100 never does, because the comparison is strictly
 * less than. Anything between behaves as that roll.
 *
 * IT IS ALSO USEFUL AT THE KEYBOARD. `Cataclysm.CritRoll 0` makes every blow a
 * critical strike, which is how the display is judged without waiting for one.
 */
static TAutoConsoleVariable<float> CVarCritRoll(
	TEXT("Cataclysm.CritRoll"),
	-1.0f,
	TEXT("Pins the critical strike roll, 0-100. -1 rolls normally. 0 always "
		 "critically strikes; 100 never does."),
	ECVF_Default);

/**
 * Pins the block roll, 0-100, for tests, the same shape as `Cataclysm.CritRoll`
 * above. -1, the default, rolls normally. 0 blocks whenever the defender has any
 * chance to; 100 never blocks. Issue #1833 group E part 2: a row that acts on a
 * block can only be tested through a real block if the block is certain, and a
 * chance of 100 can still miss on a roll of exactly 100.
 * SINCE #2201 a real draw is below 100 (`CataclysmPercentDraw::Draw`), so a
 * chance of 100 blocks on every drawn roll and only the pin can give a roll of
 * 100.
 */
static TAutoConsoleVariable<float> CVarBlockRoll(
	TEXT("Cataclysm.BlockRoll"),
	-1.0f,
	TEXT("Pins the block roll, 0-100. -1 rolls normally. 0 blocks whenever there "
		 "is any chance to; 100 never blocks."),
	ECVF_Default);

namespace
{
	/**
	 * The body a hit on this set landed on. Issue #1755.
	 *
	 * NOT `GetOwningActor()` FOR A PLAYER. A player's vital set is created on
	 * its player state (`ACataclysmPlayerState`), which stands at the world's
	 * origin, carries no weapon slots and is not the character. Anything asked
	 * of the defender's position or components has to be asked of the ability
	 * system's avatar, the rule `UCataclysmAbilitySystemComponent::
	 * WithEnemiesInReach` states. For an enemy or a minion the set lives on the
	 * character, and the avatar and the owner are the same actor.
	 *
	 * THE OWNER IS KEPT FOR A SET WITH NO AVATAR, which is what it answered
	 * before, so such a set reads exactly as it did.
	 */
	AActor* CataclysmDefendingBody(const UAttributeSet& Set)
	{
		const UAbilitySystemComponent* System = Set.GetOwningAbilitySystemComponent();
		AActor* Avatar = System ? System->GetAvatarActor() : nullptr;
		return Avatar ? Avatar : Set.GetOwningActor();
	}
}

UCataclysmVitalAttributeSet::UCataclysmVitalAttributeSet()
{
	// Placeholders only. Real starting values come from a class stat line
	// applied as a gameplay effect; the three Demonic classes are in the design
	// document. These exist so an attribute set constructed with no class
	// attached is still in a valid state rather than at zero health.
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitMana(50.0f);
	InitMaxMana(50.0f);
	InitEnergyShield(0.0f);
	InitMaxEnergyShield(0.0f);
	InitHealthRegen(1.0f);
	InitManaRegen(1.0f);
	InitEnergyShieldRegen(0.0f);
	InitLifeLeech(0.0f);
	InitManaLeech(0.0f);
	InitEnergyShieldLeech(0.0f);
	InitHealingCeilingReduction(0.0f);
	InitHealingReceivedReduction(0.0f);
	InitDamage(0.0f);
}

void UCataclysmVitalAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, Health);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxHealth);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, Mana);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxMana);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShield);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, MaxEnergyShield);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, HealthRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, ManaRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShieldRegen);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, LifeLeech);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, ManaLeech);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, EnergyShieldLeech);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, HealingCeilingReduction);
	CATACLYSM_REPLICATE(UCataclysmVitalAttributeSet, HealingReceivedReduction);
	// Damage is a meta attribute. It is never replicated.
}

float UCataclysmVitalAttributeSet::MaximumEnergyShieldAsked() const
{
	if (const UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(GetOwningAbilitySystemComponent()))
	{
		return Cataclysm->MaximumEnergyShield();
	}
	return GetMaxEnergyShield();
}

void UCataclysmVitalAttributeSet::PostAttributeChange(
	const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	// HEALTH IS LOWERED TO A LOWERED MAXIMUM. Issue #2190, ruled by the coordinating session on 2026-10-01 as a
	// labelled judgement: health above its maximum is a fault, because the next write of any size -- a hit of one
	// point -- clamps it in `PreAttributeChange` and reads as a large hit. Measured before this: a Pact of Wrath's or a
	// refresh's lowered maximum left health above it until the next write.
	//
	// HERE AND NOT IN `PreAttributeChange`, because that hook can change only the value being written, which is the
	// maximum. The engine calls this after every write to an attribute's current value, by a base write or by an
	// effect (`FGameplayAttribute::SetNumericValueChecked`), so no route that lowers the maximum misses it.
	//
	// THE MAXIMUM ONLY: not the healing ceiling and not what a reservation leaves, which are their own rules. RAISING
	// the maximum raises nothing. Health only: issue #1757's ruling that a lowered ceiling does not take shield or
	// mana away is a different case and stands.
	if (Attribute == GetMaxHealthAttribute() && NewValue < OldValue && GetHealth() > NewValue)
	{
		if (UAbilitySystemComponent* OwningAbilities = GetOwningAbilitySystemComponent())
		{
			OwningAbilities->SetNumericAttributeBase(GetHealthAttribute(), NewValue);
		}
	}
}

void UCataclysmVitalAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		// A CREATURE THAT CANNOT BE HURT IS HELD AT ITS MAXIMUM: The Reaper. Issues #1820 and
		// #41. HERE, BEFORE ANYTHING READS THE CURRENT VALUE: a write to the health base runs
		// this clamp and then `PostAttributeBaseChange`, whose death check would otherwise see
		// the lowered value before `PostGameplayEffectExecute` could put it back. Seen in the
		// whole-suite run of 2026-09-24, where a write straight to health left the Reaper dead
		// at full health.
		//
		// AND A SHROUDED ONE IS HELD WHERE IT IS: Shadowy Enemies. Issues #1820 and #41. Not at its
		// maximum, since a creature hurt while it stood in the light keeps that hurt when it leaves
		// it; its health may rise and may not fall.
		const ACataclysmEnemyCharacter* Unhurt =
			Cast<ACataclysmEnemyCharacter>(GetOwningActor());
		if (Unhurt && Unhurt->bCannotBeHurt)
		{
			NewValue = GetMaxHealth();
		}
		else if (Unhurt && Unhurt->bShrouded)
		{
			NewValue = FMath::Clamp(FMath::Max(NewValue, GetHealth()), 0.0f, GetMaxHealth());
		}
		else
		{
			NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());

			// AND A CREATURE IN A FRIENDLY UNHOLY SIGIL CANNOT BE KILLED. Issue
			// #1559, ruled 2026-09-30: "Allies in this sigil cannot be killed."
			// HERE, so every way health is lowered is held -- a blow, a direct
			// reduction, a write to the base -- the way the two holds above are.
			//
			// AT ONE HEALTH, A JUDGEMENT: the row gives no figure, and one is the
			// least that is still alive. Never raised: a creature already below
			// one keeps what it has.
			//
			// ASKED ONLY OF A WRITE THAT WOULD KILL, so an ordinary hit costs no
			// search for a sigil.
			if (Unhurt && NewValue <= 0.0f && GetHealth() > 0.0f
				&& UCataclysmEnemyModifiers::IsProtectedBySigil(Unhurt))
			{
				NewValue = FMath::Min(1.0f, GetHealth());
			}
		}
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxMana());
	}
	else if (Attribute == GetEnergyShieldAttribute())
	{
		// ASKED FOR RATHER THAN READ, since issue #1973. This is the clamp that
		// decides whether a shield a scaled row grants can ever be filled: the
		// attribute holds the unscaled figure, so clamping against it would
		// refuse every point Hollow Crown adds.
		NewValue = FMath::Clamp(NewValue, 0.0f, MaximumEnergyShieldAsked());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// The one maximum that cannot be zero. See the class comment.
		NewValue = FMath::Max(NewValue, 1.0f);
	}
	else if (Attribute == GetMaxManaAttribute()
		|| Attribute == GetMaxEnergyShieldAttribute()
		|| Attribute == GetHealthRegenAttribute()
		|| Attribute == GetManaRegenAttribute()
		|| Attribute == GetEnergyShieldRegenAttribute()
		|| Attribute == GetLifeLeechAttribute()
		|| Attribute == GetManaLeechAttribute()
		|| Attribute == GetEnergyShieldLeechAttribute())
	{
		// Zero is a legitimate value for all of these. A class with no energy
		// shield is a design position, not an error state.
		//
		// LOWERING A MAXIMUM LEAVES THE CURRENT VALUE WHERE IT WAS, ON PURPOSE.
		// Nothing here re-clamps the energy shield (or mana) when its maximum
		// falls, so a character can hold 100 shield against a maximum of 40
		// until something writes the shield attribute, and the shield branch
		// above clamps it then. Ruled deliberate on 2026-09-14 under the
		// owner's delegation, issue #1757: a shield already raised is not taken
		// away by a lowered ceiling, it is simply not refilled past it.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
	else if (Attribute == GetHealingCeilingReductionAttribute()
		|| Attribute == GetHealingReceivedReductionAttribute())
	{
		// BOTH ARE A SHARE OF A HUNDRED AND BOTH ARE HELD INSIDE IT, and they
		// are clamped together because the same two arguments apply to each.
		// Issue #988 added the first and issue #41's slice 5 the second. WHAT
		// THEY MEAN DIFFERS and the header says which is which: the ceiling caps
		// how HIGH healing may take a character, the reduction cuts how much of
		// each amount ARRIVES.
		//
		// BELOW ZERO EACH WOULD INVERT ITSELF. A negative ceiling reduction
		// would RAISE the ceiling above maximum health; a negative healing
		// reduction would make a curse heal more than no curse at all.
		//
		// PAST A HUNDRED EACH WOULD GO THROUGH ZERO. A ceiling reduction past
		// one hundred puts the ceiling below zero, which is not "cannot be
		// healed" but a negative amount of health to be healed to; a healing
		// reduction past one hundred would turn healing into damage by a route
		// nothing else in the game uses.
		//
		// A HUNDRED IS LEGITIMATE FOR BOTH and means healing restores nothing at
		// all. No node states either, and it is not a data error if one ever
		// does.
		NewValue = FMath::Clamp(NewValue, 0.0f, 100.0f);
	}
}

void UCataclysmVitalAttributeSet::PostAttributeBaseChange(
	const FGameplayAttribute& Attribute, float OldValue, float NewValue) const
{
	Super::PostAttributeBaseChange(Attribute, OldValue, NewValue);

	// EVERY WRITE TO HEALTH THAT NO GAMEPLAY EFFECT CAUSED ARRIVES HERE, which
	// is the whole reason this override exists. See the header. A health cost
	// and a debt falling due both write the attribute directly, and until issue
	// #1072 neither of them told anything at all.
	if (Attribute != GetHealthAttribute())
	{
		return;
	}

	// `NewValue` IS DELIBERATELY NOT READ, AND THAT IS NOT AN OVERSIGHT. The
	// engine passes the BASE value, while the clamp this set applies in
	// `PreAttributeChange` lands on the CURRENT value, so the two differ
	// whenever a write went past a limit: a health cost of 500 charged against
	// 200 health leaves the base at -300 and the current value at 0. Both
	// notifications below read `GetHealth()`, which is the current value, and
	// that is the number a character actually has.
	//
	// THE DEATH NOTICE FIRST, WHICH IS THE ORDER `PostGameplayEffectExecute`
	// USES. A character that has just been emptied should be dead before
	// anything watching a threshold looks at it.
	NotifyIfHealthReachedZero();
	NotifyHealthChanged();
}

void UCataclysmVitalAttributeSet::PostGameplayEffectExecute(
	const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		// A TICK OF VOID SPLINTER STATES A SHARE OF THIS CHARACTER'S HEALTH AND
		// NOT AN AMOUNT, and the amount is worked out here, from the health it
		// has now. Issue #915. The effect carries a damage of one only so that a
		// tick arrives at all; an effect carrying no share keeps its own damage.
		// `ShareOfHealthTick` holds the rule that a boss is never taken below
		// half its maximum health by it, applied before resistance.
		const float ShareOfCurrentHealth = Data.EffectSpec.GetSetByCallerMagnitude(
			FName(UCataclysmSkillEffects::ShareOfCurrentHealthDataName),
			/*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/-1.0f);
		const ACataclysmEnemyCharacter* AsEnemy =
			Cast<ACataclysmEnemyCharacter>(GetOwningActor());
		const float LocalDamage = ShareOfCurrentHealth >= 0.0f
			? UCataclysmSkillEffects::ShareOfHealthTick(ShareOfCurrentHealth,
				GetHealth(), GetMaxHealth(), AsEnemy && AsEnemy->IsBoss())
			: GetDamage();
		SetDamage(0.0f);

		if (LocalDamage > 0.0f)
		{
			// The whole order lives in UCataclysmDamageCalculation, so it can be
			// tested by passing numbers in rather than by building an effect
			// spec for every case.
			//
			// TWO OF THE HIT'S PROPERTIES NOW REACH IT, and they arrive by two
			// different routes because they are two different kinds of thing.
			// Issue #486.
			//
			//   the DAMAGE TYPE belongs to the hit, and rides on the effect as an
			//   `Element.*` tag, put there by UCataclysmSkillEffects
			//
			//   the RESISTANCE PENETRATION belongs to the attacker rather than to
			//   any one blow, so it is read off the attacker at the moment the
			//   blow lands, which is also the moment it is true
			//
			// HOW IT ARRIVED RIDES ON THE EFFECT TOO, as two more tags. Whether
			// the hit swept a volume decides the evasion step, and whether it is
			// damage over time decides evasion and, with whether it is a bleed,
			// whether an energy shield absorbs it. Issues #513 and #2014.
			//
			// ALL OF THEM REACH IT NOW. Armour penetration gained an attribute on
			// issue #520 and is read beside the resistance penetration below; the
			// weapon sub-type is read off the attacking actor on issue #639,
			// because it is a property of what is in its hand rather than a number
			// it carries.
			FCataclysmIncomingHit Hit;
			Hit.Damage = LocalDamage;

			FGameplayTagContainer AssetTags;
			Data.EffectSpec.GetAllAssetTags(AssetTags);

			// THE ELEMENT TAG ANSWERS TWO QUESTIONS AND THEY HAVE DIFFERENT
			// ANSWERS. What to draw the hit as is always the tag on the effect.
			// Which of the defender's resistances applies is the same tag ONLY
			// when the hit is really of that type -- a player's hit carries its
			// skill's element for colour and is marked so that it is not read as
			// a resistance, because an enemy holds one generic resistance and
			// has nothing to choose between. Issue #803.
			const FName ElementOnTheEffect =
				UCataclysmDamageCalculation::DamageTypeFromTags(AssetTags);
			const bool bColourOnly = AssetTags.HasTag(
				UCataclysmDamageCalculation::ElementIsForColourOnlyTag());

			Hit.EffectDamageType = ElementOnTheEffect;
			Hit.DamageType = bColourOnly ? NAME_None : ElementOnTheEffect;
			Hit.bIsArea = AssetTags.HasTag(
				UCataclysmDamageCalculation::AreaDamageTag());
			Hit.bIsDamageOverTime = AssetTags.HasTag(
				UCataclysmDamageCalculation::DamageOverTimeTag());

			// AND WHETHER THE TICK IS A BLEED, the one kind the energy shield
			// lets through. Issue #2014. From the GRANTED tags, where
			// `ApplyDamageOverTime` puts the ailment; the asset tags carry only
			// the bare `Keyword.DoT`. Gathered only for a tick, since a hit
			// cannot be a bleed.
			if (Hit.bIsDamageOverTime)
			{
				FGameplayTagContainer Granted;
				Data.EffectSpec.GetAllGrantedTags(Granted);
				Hit.bIsBleed = Granted.HasTag(UCataclysmDebuffs::BleedTag());

				// AND WHICH AILMENT IT IS, for a defender's row that names one.
				// Ruled 2026-10-06. Every granted tag under the damage over time
				// parent; a tick with no ailment grants none and carries none.
				const FGameplayTag DamageOverTime =
					UCataclysmDamageCalculation::DamageOverTimeTag();
				for (const FGameplayTag& One : Granted)
				{
					if (DamageOverTime.IsValid() && One.MatchesTag(DamageOverTime))
					{
						Hit.DamageOverTimeTags.AddTag(One);
					}
				}
			}

			// AND WHETHER THE ATTACKER IS CHARMED BY THIS CREATURE. The Beguiling
			// enemy modifier reads "Players cannot deal direct damage to you
			// while charmed", so the blow arrives and does nothing.
			//
			// DIRECT DAMAGE ONLY, WHICH THE ROW SAYS. Area damage still lands,
			// the same exception evasion already has, so a charmed player is
			// not helpless -- they have an answer, and it is the answer this
			// game already teaches.
			//
			// IT ASKS WHETHER THE DEFENDER CARRIES THE MODIFIER RATHER THAN
			// WHICH CREATURE APPLIED THE CHARM, and that is a simplification
			// worth stating: a player charmed by one Beguiling creature also
			// deals no direct damage to a second one standing beside it. The
			// alternative is remembering a source per debuff, which nothing in
			// this project does yet.
			if (!Hit.bIsArea)
			{
				const ACataclysmEnemyCharacter* Charmer =
					Cast<ACataclysmEnemyCharacter>(GetOwningActor());
				const FGameplayTag Charm =
					UCataclysmSkillShapes::StatusTagFor(TEXT("Beguiling"));

				// THE CAUSER RATHER THAN `Attacker`, which is not in scope until
				// further down this function.
				const UAbilitySystemComponent* Swinging =
					UCataclysmTargeting::AbilitySystemOf(
						Data.EffectSpec.GetContext().GetEffectCauser());

				if (Charmer && Charm.IsValid() && Swinging
					&& UCataclysmEnemyModifiers::Carries(
						Charmer->ModifierRows,
						UCataclysmEnemyModifiers::BeguilingRow)
					&& Swinging->HasMatchingGameplayTag(Charm))
				{
					Hit.Damage = 0.0f;
				}
			}

			// AND WHETHER THE ATTACKER'S MODIFIERS SAY IT CANNOT BE DODGED. The
			// Perfect Aim enemy modifier reads "Attacks cannot be dodged", and
			// evasion is rolled on the DEFENDER, so the only way an attacker can
			// refuse it is to say so on the blow it sends.
			//
			// A CAST TO THE CREATURE CLASS, like the boss-immunity check in
			// `UCataclysmSkillEffects::ApplyStun`. A modifier is a property of a
			// creature rather than an attribute, because it is a rule about the
			// blow rather than a number on either side.
			if (const ACataclysmEnemyCharacter* Striker =
					Cast<ACataclysmEnemyCharacter>(
						Data.EffectSpec.GetContext().GetEffectCauser()))
			{
				Hit.bCannotBeEvaded =
					UCataclysmEnemyModifiers::AttacksCannotBeDodged(
						Striker->ModifierRows);

				// AND WHETHER THAT CREATURE IS A BOSS, for "you take 20%-40% less
				// damage from Boss enemies". Issue #666. The same creature and the
				// same cast, so a blow from anything but an enemy is not a boss's.
				Hit.bFromBoss = Striker->IsBoss();
			}

			// AND WHETHER THE BLOW ITSELF SAYS IT CANNOT BE EVADED. Ruled 2026-10-07.
			// `FCataclysmHitDelivery::bCannotBeEvaded` is stamped on the effect by
			// `UCataclysmSkillEffects::ApplyTypedSpec`, and the one blow that sets it
			// is the direct hit the action `strike_target` deals.
			//
			// AFTER THE CAST ABOVE AND JOINED TO IT, because that block assigns the
			// flag and this must not be overwritten by it. The melee keystone
			// further down joins the flag the same way.
			Hit.bCannotBeEvaded = Hit.bCannotBeEvaded
				|| Data.EffectSpec.GetSetByCallerMagnitude(
					   FName(UCataclysmSkillEffects::CannotBeEvadedDataName),
					   /*WarnIfNotFound=*/false, 0.0f) > 0.0f;

			// AND WHETHER THE BLOW SAYS THIS CHARACTER CANNOT REDUCE OR MITIGATE IT.
			// Ruled 2026-10-12, for "Retaliation damage cannot be reduced or
			// mitigated by the attacker". `FCataclysmHitDelivery::bCannotBeMitigated`
			// is stamped on the effect by `UCataclysmSkillEffects::ApplyTypedSpec`,
			// and the one blow that sets it is retaliation from a character whose
			// rows hold `UCataclysmRetaliation::CannotBeMitigatedStat`.
			//
			// ONLY THE CALCULATION READS IT, for the six steps
			// `FCataclysmIncomingHit::bCannotBeMitigated` lists. NOTHING BELOW IN
			// THIS FUNCTION READS IT, so the blow is still emptied by the Sacrificial
			// Ward, by the no-damage window, by the window against its damage type
			// and by a creature that takes no damage, is still held to one point by
			// the two saves from a lethal blow, and is still turned into bleeding or
			// divided by the Sacrificial Bond, as any blow is. Those are not among
			// the six the ruling names.
			Hit.bCannotBeMitigated = Data.EffectSpec.GetSetByCallerMagnitude(
				FName(UCataclysmSkillEffects::CannotBeMitigatedDataName),
				/*WarnIfNotFound=*/false, 0.0f) > 0.0f;

			// AND WHETHER WHOEVER THREW IT IS STAGGERED, for "Staggered enemies
			// deal 15%-30% increased damage to you". Issue #45.
			//
			// OUTSIDE THE CAST ABOVE, DELIBERATELY. A boss is a creature rarity,
			// so that fact can only come from an enemy creature and is read
			// inside it. The Staggered state is left by a knockback, pull or
			// knockdown on anything it moves, the player included, so reading
			// this inside that cast would leave it false for every blow a player
			// throws -- silently, and the row would never fire.
			Hit.bFromStaggered = UCataclysmSkillEffects::IsStaggered(
				Data.EffectSpec.GetContext().GetEffectCauser());

			// AND WHETHER IT IS UNDER CROWD CONTROL, for "You take 15%-25% more
			// damage from enemies that are currently CC'd". Issue #1686. The same
			// causer, outside the creature cast for the same reason.
			Hit.bFromCrowdControlled = UCataclysmSkillEffects::IsCrowdControlled(
				Data.EffectSpec.GetContext().GetEffectCauser());

			// AND WHICH DEBUFFS IT IS CARRYING, for "+2% increased Damage
			// Reduction per point against enemies you have Weakened". Issue
			// #1515. The same causer the line above reads, and outside the
			// creature cast for the same reason: a debuff lands on anything a
			// skill applies one to, the player included.
			//
			// "ENEMIES YOU HAVE WEAKENED" IS READ AS "AN ENEMY CARRYING WEAKEN",
			// which is a ruling rather than a shortcut. Nothing in the game
			// records who applied a debuff: a tag has no applier, and
			// `UCataclysmAilments` reads the instigator at the moment of
			// application and passes it on rather than storing it. The bonus
			// already shipped for the same idea does not ask either --
			// `UCataclysmDebuffs::DamageAgainstSharedDebuff` compares two tag
			// lists. `docs/DECISIONS.md` carries it.
			Hit.AttackerDebuffs = UCataclysmDebuffs::TagsOnActor(
				Data.EffectSpec.GetContext().GetEffectCauser());

			// AND HOW LONG AGO THIS CHARACTER FIRST GOT A BLOW THROUGH TO IT, for
			// "Enemies that survive more than 5 seconds after first being hit gain
			// 20%-40% increased damage". Ruled 2026-10-12 under the owner's
			// delegation. The same causer the three lines above read, and outside
			// the creature cast for their reason: the record is kept by every
			// character.
			//
			// THE CAUSER'S RECORD, ASKED ABOUT THIS CHARACTER'S ABILITY SYSTEM,
			// which is the key `NoteStruckBy` writes for a blow of this
			// character's. A causer this character has never struck, or one that
			// is not a character of this project, leaves -1 and the condition
			// refuses. THE CAUSER OF A MINION'S BLOW IS THE MINION, so a summoner
			// struck ten seconds ago does not make its minion's blow one of these.
			if (const UCataclysmAbilitySystemComponent* Thrower =
					Cast<UCataclysmAbilitySystemComponent>(
						UCataclysmTargeting::AbilitySystemOf(
							Data.EffectSpec.GetContext().GetEffectCauser())))
			{
				Hit.SecondsSinceDefenderFirstStruckAttacker =
					Thrower->SecondsSinceFirstStruckBy(GetOwningAbilitySystemComponent());
			}

			// AND HOW FAR APART THE TWO CHARACTERS STOOD, for "You take 25% less
			// damage from enemies more than 6 metres away from you".
			//
			// HERE, BECAUSE THIS IS THE ONLY PLACE IN THE GAME THAT BUILDS ONE OF
			// THESE and it is the only place with both characters in hand.
			// `UCataclysmDamageCalculation::Resolve` is handed the defender's
			// ability system and nothing else, so a position cannot reach it any
			// other way.
			//
			// THE CAUSER RATHER THAN THE INSTIGATOR, which is the same actor the
			// two checks above read. For a minion's blow the causer is the minion
			// and the instigator is its summoner, and the distance a row means is
			// to the thing that struck.
			//
			// THE SHARED MEASUREMENT, NOT A SECOND COPY. `UCataclysmCombatEvents`
			// reports the same number to every listener using this same function,
			// so a passive row and the combat log cannot disagree about one
			// strike. It answers -1 when either actor is missing, and every
			// predicate reading a distance refuses a negative one.
			//
			// TO THE DEFENDER'S BODY, NOT TO THIS SET'S OWNER. Issue #1755. A
			// player's vital set lives on its player state, which stands at the
			// world's origin wherever the character goes, so measuring to
			// `GetOwningActor()` gave a player's rows the attacker's distance from
			// the origin: Standing Apart softened a blow from beside a player who
			// stood far out and not one from across the room near the origin, and
			// "Nearby enemies deal less" did the reverse. The same rule
			// `UCataclysmAbilitySystemComponent::WithEnemiesInReach` states,
			// "MEASURED FROM THE AVATAR AND NOT FROM THIS COMPONENT'S OWNER". The
			// owner is kept only for a set with no avatar, and for an enemy or a
			// minion the two are the same actor.
			//
			// AND FROM WHERE THE BLOW WAS STRUCK when its effect says it was
			// struck from a point: a gadget's copy of its summoner's skill.
			// Ruled 2026-10-14. See `MetresFromWhereItWasStruck`.
			Hit.OpponentDistanceMetres = UCataclysmSkillEffects::MetresFromWhereItWasStruck(
				Data.EffectSpec, Data.EffectSpec.GetContext().GetEffectCauser(),
				CataclysmDefendingBody(*this));

			// AND WHETHER IT WAS STRUCK IN MELEE, FROM RANGE, OR AS A SPELL.
			// Issues #1032 and #666. Read here beside the other two because they
			// come from the same place: tags the ability that threw the blow put
			// on the effect. Mutilation Mastery's chance to apply Bleeding further
			// down this function asks about melee, and `Resolve` hands all three
			// to the damage taken lookup, for a modifier that asks about the hit.
			Hit.bIsMelee =
				AssetTags.HasTag(UCataclysmDamageCalculation::MeleeTag());
			Hit.bIsRanged =
				AssetTags.HasTag(UCataclysmDamageCalculation::RangedTag());
			Hit.bIsSpell = UCataclysmSkillEffects::IsSpell(AssetTags);

			// AND WHETHER THE ATTACKER'S OWN KEYSTONE SAYS THIS BLOW CANNOT BE
			// DODGED. Issue #1515. `Ravager_keystone_spine_002` Every Swing Lands:
			// "Your melee attacks cannot be evaded."
			//
			// IT SETS THE FLAG THE EVASION STEP ALREADY HONOURS rather than adding
			// a second read there. Evasion is rolled on the DEFENDER, so the only
			// way an attacker can refuse it is to say so on the blow it sends, and
			// `bCannotBeEvaded` exists for exactly that.
			//
			// THE PERFECT AIM ENEMY MODIFIER IS THE OTHER SETTER, further down this
			// function, and the two are not two names for one thing: that one is a
			// rule on a creature, read off its modifier rows, and applies to every
			// attack it makes. This one is a stat on a player, read through the
			// pipeline, and applies only to melee. Either may set the flag and
			// neither reads the other.
			//
			// MELEE ONLY, WHICH IS THE ROW'S OWN LIMIT. A ranged attack or a spell
			// from the same character is still evadable.
			//
			// AND IT DOES NOT REACH A MINION'S BLOW. A minion's damage is dealt in
			// its summoner's name, so the attacker read here IS the player; what
			// keeps this off it is that a minion's blow carries no melee tag, being
			// delivered with an empty tag container. The design's rule is that "a
			// minion reaches its summoner through exactly three channels, and
			// nothing else crosses", and `MinionDelivery` already blocks four other
			// things by name. This one is blocked by the melee gate instead, which
			// is a property of how minion damage is delivered rather than a stated
			// rule -- so a test asserts it directly rather than trusting it.
			if (Hit.bIsMelee)
			{
				if (const UCataclysmAbilitySystemComponent* Swinging =
						Cast<UCataclysmAbilitySystemComponent>(
							Data.EffectSpec.GetContext()
								.GetInstigatorAbilitySystemComponent()))
				{
					const FGameplayAttribute Suppressed =
						UCataclysmCombatAttributeSet::GetMeleeEvasionSuppressedAttribute();

					// THE ATTRIBUTE-SET CHECK IS NOT OPTIONAL. Reading an attribute
					// whose set the component does not hold raises an engine ensure
					// rather than answering zero.
					if (Swinging->HasAttributeSetForAttribute(Suppressed))
					{
						Hit.bCannotBeEvaded = Hit.bCannotBeEvaded
							|| Swinging->StatForSkill(
								   FName(UCataclysmDamageCalculation::MeleeEvasionSuppressedStat),
								   AssetTags,
								   Swinging->GetNumericAttribute(Suppressed)) > 0.0f;
					}
				}
			}

			// WHETHER THIS BLOW MAY IGNORE ANY OF THE DEFENDER'S ARMOUR OR
			// RESISTANCE. Read up here rather than beside the first thing that
			// needs it, because two separate places below do: the attacker's two
			// penetration attributes, and the weapon sub-type, which arrives by a
			// different route and can ignore 20% of armour on its own. Issue #659.
			const bool bCanPenetrate = !AssetTags.HasTag(
				UCataclysmDamageCalculation::NoPenetrationTag());

			if (const UAbilitySystemComponent* Attacker =
					Data.EffectSpec.GetContext().GetInstigatorAbilitySystemComponent())
			{
				if (const UCataclysmCombatAttributeSet* Offence =
						Attacker->GetSet<UCataclysmCombatAttributeSet>())
				{
					// THE TWO PENETRATIONS, which are TWO stats rather than one
					// under two names. Resistance penetration cuts into the
					// target's resistance at step 4; armour penetration cuts
					// into its armour at step 3.
					// Nothing held an armour penetration value at all until
					// issue #520, so `Hit.ArmorPenetration` was applied
					// correctly by Resolve and never set, and the three
					// enchantments in EnchantmentsPositive.csv that grant it
					// could do nothing.
					//
					// READ OFF THE ATTACKER FOR THE SAME REASON: penetration of
					// either kind belongs to whoever is swinging rather than to
					// any one blow, so it is read at the moment the blow lands,
					// which is also the moment it is true.
					//
					// AND FOR THAT SAME REASON A MINION'S BLOW TAKES NEITHER.
					// Its damage is dealt in its summoner's name, so without
					// this the attacker read above is the player and a minion
					// cuts into a target's armour and resistance by whatever the
					// player's gear supplies. The design says a minion reaches
					// its summoner through exactly three channels and names
					// penetration among what does not cross. Issue #659, which
					// is the other half of the design sentence issue #649 built
					// the critical strike exclusion for.
					if (bCanPenetrate)
					{
						// BOTH ARE ASKED FOR RATHER THAN READ, for the reason the
						// critical strike chance below gives. Issue #947 lists
						// them among the stats a skill uses that still read the
						// attribute. That attribute is worked out with no skill
						// in hand, so a modifier requiring a tag is missing from
						// it, and two authored enchantments -- "Spells ignore
						// 15%-30% of enemy resistances" and "Your DoTs ignore
						// 20%-40% of enemy resistances" -- cannot be written
						// while that is true.
						//
						// ITS OWN CAST RATHER THAN A HOISTED ONE. The critical
						// strike block below makes the same cast for itself, and
						// keeping each block self-contained is what lets a stat
						// be moved to an ask one commit at a time.
						//
						// THE FALLBACK IS THE ATTRIBUTE, so a character with no
						// such modifier is unchanged, and an enemy -- which is
						// never given a character stat line -- takes the same
						// path it always did.
						const UCataclysmAbilitySystemComponent* AskingToPenetrate =
							Cast<const UCataclysmAbilitySystemComponent>(Attacker);

						// AND WITH THE CHARACTER STRUCK, SINCE ISSUE #1833'S SMALL
						// ENGINE HALVES, as the armour penetration ask below
						// already was. "Your first hit against each enemy in a
						// combat ignores all resistances" asks whether this
						// defender has been struck, and a target condition with
						// no target in hand refuses.
						Hit.ResistancePenetration = AskingToPenetrate
							? AskingToPenetrate->StatForSkill(
								  FName(TEXT("penetration")), AssetTags,
								  Offence->GetPenetration(),
								  /*SkillHealthCostPercent=*/-1.0f,
								  FCataclysmBlowContext(),
								  /*MetresMovedBeforeBlow=*/-1.0f,
								  Hit.OpponentDistanceMetres,
								  UCataclysmSkillEffects::IsStaggered(GetOwningActor()),
								  /*Target=*/GetOwningActor())
							: Offence->GetPenetration();

						// ARMOUR PENETRATION IS ASKED WITH THE ATTACK'S COUNT, so a row
						// conditioned on how many enemies the attack struck together
						// is judged against this blow. Issue #1515. Sundering: "Your
						// melee attacks ignore enemy Armor entirely when they hit three
						// or more enemies at once." The count travels on the effect; a
						// blow without one reads -1, which refuses, and every other
						// fact is passed as none, which is what the ask above passes.
						//
						// A DEFENDER THAT FORBIDS PENETRATION STILL WINS: the Ravager's
						// own keystone is judged after this, in the damage
						// calculation, and ignores this figure whatever it is.
						const int32 EnemiesStruckTogether = FMath::RoundToInt(
							Data.EffectSpec.GetSetByCallerMagnitude(
								FName(UCataclysmSkillEffects::EnemiesStruckTogetherDataName),
								/*WarnIfNotFound=*/false,
								/*DefaultIfNotFound=*/-1.0f));

						Hit.ArmorPenetration = AskingToPenetrate
							? AskingToPenetrate->StatForSkill(
								  FName(TEXT("armor_penetration")), AssetTags,
								  Offence->GetArmorPenetration(),
								  /*SkillHealthCostPercent=*/-1.0f,
								  FCataclysmBlowContext(),
								  /*MetresMovedBeforeBlow=*/-1.0f,
								  Hit.OpponentDistanceMetres,
								  UCataclysmSkillEffects::IsStaggered(GetOwningActor()),
								  /*Target=*/GetOwningActor(),
								  EnemiesStruckTogether)
							: Offence->GetArmorPenetration();

						// AND THE SHARE A CRITICAL STRIKE IGNORES ON TOP, asked with the
						// same facts and added in the damage calculation only if the
						// blow critically strikes, which is not known yet. Issue #1833,
						// ruled 2026-09-30: "Your critical strikes ignore 20%-40% of
						// enemy armor". Nothing without an ability system of ours.
						Hit.CriticalArmorPenetration = AskingToPenetrate
							? AskingToPenetrate->StatForSkill(
								  FName(UCataclysmDamageCalculation::CriticalArmorPenetrationStat),
								  AssetTags, 0.0f,
								  /*SkillHealthCostPercent=*/-1.0f,
								  FCataclysmBlowContext(),
								  /*MetresMovedBeforeBlow=*/-1.0f,
								  Hit.OpponentDistanceMetres,
								  UCataclysmSkillEffects::IsStaggered(GetOwningActor()),
								  /*Target=*/GetOwningActor(),
								  EnemiesStruckTogether)
							: 0.0f;
					}

					// AND THE CRITICAL STRIKE, read here for that same reason.
					// Both attributes existed, were replicated, were clamped and
					// were set on every enemy from its archetype row, and no code
					// in the project read either one, so no hit was ever
					// multiplied by a critical strike multiplier. Issue #649. It
					// is the same shape of defect as the attack speed that never
					// reached a character, which issue #647 fixed one session
					// earlier: an attribute initialised with a comment describing
					// an intention nobody built.
					//
					// TWO HITS MAY NEVER CRITICALLY STRIKE and both are excluded
					// here rather than inside the calculation, because both are
					// facts about how this blow was thrown rather than about the
					// arithmetic.
					//
					// A DAMAGE OVER TIME TICK CANNOT. The design gives damage over
					// time its own three scaling levers and calls the critical
					// strike attribute "the direct-hit damage attribute", and both
					// games in the genre that ship this layer agree outright: Last
					// Epoch's manual says a damage over time effect is not a hit
					// and so cannot be dodged and does not deal critical strikes,
					// and Path of Exile says damage over time cannot critically
					// hit. See docs/DECISIONS.md for the sources.
					//
					// A MINION'S BLOW CANNOT. It is dealt in its summoner's name,
					// so without this the attacker read above is the player and
					// every minion would inherit the player's critical strikes.
					// The design forbids it in as many words and set minion damage
					// at the top of its band because of it.
					const bool bCanCriticallyStrike =
						!Hit.bIsDamageOverTime
						&& !AssetTags.HasTag(
							UCataclysmDamageCalculation::NoCriticalStrikeTag());
					if (bCanCriticallyStrike)
					{
						// THE SKILL'S OWN CHANCE WINS OVER THE CHARACTER'S, when
						// the skill states one. Critical strike chance belongs to
						// the skill being used rather than to the character using
						// it -- the design's stat source table names "the skill
						// being used" and adds "A character has no critical strike
						// chance in the abstract" -- and a character holds six
						// skills at once against this one attribute. So a skill
						// that states its own sends it with the hit, as a
						// set-by-caller magnitude keyed by Data.SkillCritChance,
						// and the attribute holds the default for every skill that
						// states nothing. Issue #657.
						//
						// -1 MEANS NOTHING WAS SENT, which is every hit in the game
						// today: all 398 rows of the weapon skill matrix leave the
						// column blank, and an enemy's attack has no skill row at
						// all. Zero cannot mean it, because a skill designed never
						// to critically strike states zero and must get zero.
						const float Stated = Data.EffectSpec.GetSetByCallerMagnitude(
							UCataclysmDamageCalculation::SkillCritChanceDataTag(),
							/*WarnIfNotFound=*/false,
							/*DefaultIfNotFound=*/-1.0f);

						// THE CHARACTER'S OWN CHANCE IS ASKED FOR RATHER THAN READ.
						// Issue #959, and it is the first of the stats #947 lists
						// to be wired. A bonus that applies only in some state --
						// the Masochist's Last Stand gives +3% per point while at
						// or below 20% health -- is never written onto the
						// attribute, because it would be stale the moment health
						// moved. `StatForSkill` runs the same pipeline again with
						// the attacker's health in hand, and falls back to the
						// attribute for a character that has no such bonus, so
						// nothing without one is changed.
						//
						// THE ATTACKER'S STATE AND NOT THE DEFENDER'S. This is the
						// attacker's critical strike chance, so the condition on it
						// is about the attacker, and `StatForSkill` is asked of the
						// attacker's own ability system.
						//
						// AND THE CHARACTER BEING STRUCK IS HANDED OVER, which is not
						// the same thing and was missing. Issue #1982. A condition about
						// the TARGET -- "against Boss enemies", "against an enemy you
						// have Crippled" -- is still a condition on the attacker's own
						// row: it is the attacker asking about what it is hitting.
						// `Target` is the NINTH parameter and defaults to null, so this
						// call reached it only by passing the eight before it, and until
						// it did, `WithTargetState` had nothing to read. Every such row
						// on a critical strike stat was accepted by every check,
						// shipped, and granted nothing.
						//
						// THE STRUCK CHARACTER IS THIS ATTRIBUTE SET'S OWNER, because
						// this runs on the DEFENDER. It is the same pairing
						// `Hit.OpponentDistanceMetres` uses further up, where the causer
						// is the attacker and the owning actor is what it hit.
						//
						// SIX CONDITIONS, NOT TWO. `WithTargetState` fills the target's
						// ailments, its health share and whether it is a boss, so this
						// reaches `target_carries_cripple`,
						// `target_carries_cripple_and_weaken`,
						// `target_carries_void_splinter`, `target_health_below`,
						// `target_is_boss` and `target_is_not_boss`.
						//
						// AND THE DISTANCE AND THE TARGET'S STAGGER, since issue #1992.
						// They are separate parameters rather than part of the target
						// state, and until then a row on a critical strike stat asking
						// `target_within_metres` or `target_is_staggered` granted
						// nothing. The distance is `Hit.OpponentDistanceMetres`, the
						// same physical distance read from the other end; the stagger
						// is the struck character's own, where `Hit.bFromStaggered`
						// above is the attacker's.
						const UCataclysmAbilitySystemComponent* Asking =
							Cast<const UCataclysmAbilitySystemComponent>(Attacker);
						const float OwnCritChance = Asking
							? Asking->StatForSkill(FName(TEXT("crit_chance")),
												   AssetTags,
												   Offence->GetCritChance(),
												   /*SkillHealthCostPercent=*/-1.0f,
												   FCataclysmBlowContext(),
												   /*MetresMovedBeforeBlow=*/-1.0f,
												   Hit.OpponentDistanceMetres,
												   UCataclysmSkillEffects::IsStaggered(
													   GetOwningActor()),
												   /*Target=*/GetOwningActor())
							: Offence->GetCritChance();

						// HELD UNDER THE ATTACKER'S OWN CEILING, which matters only
						// for the stated route. The attribute was already clamped
						// when it was written, but a skill's stated chance never
						// passes through that clamp, so a skill stating 80% on a
						// character an enchantment has capped at 30% would
						// otherwise land at 80%. Issue #680.
						//
						// AND THE CEILING IS ASKED THROUGH THE PIPELINE, over the
						// attribute. Issue #1833, ruled 2026-09-30: "Your critical
						// strike chance cannot exceed 30%-50%" is `max_crit_chance`
						// flat -70 to -50 on the attribute's 100. Clamped to the
						// attribute, so a row can lower the ceiling and never raise it,
						// the owner's ruling of 2026-08-17.
						const float Ceiling = Asking
							? FMath::Clamp(Asking->StatAppliedTo(
											  FName(UCataclysmCombatAttributeSet::MaxCritChanceStat),
											  AssetTags, Offence->GetMaxCritChance()),
										  0.0f, Offence->GetMaxCritChance())
							: Offence->GetMaxCritChance();
						Hit.CritChance = FMath::Min(
							Stated >= 0.0f ? Stated : OwnCritChance, Ceiling);

						// THE MULTIPLIER IS ASKED FOR TOO, AND FOR THE SAME
						// REASON THE CHANCE IS. Issue #947 lists it among the
						// stats a skill uses that still read the attribute; the
						// chance four lines above was the first of that list to
						// be wired and this is the second. The attribute is
						// worked out with no skill in hand, so a modifier
						// requiring a tag or a state is missing from it, and a
						// row saying "Spell critical strikes deal 50%-100%
						// increased damage" would be dropped in silence.
						//
						// THE SAME `Asking` AND THE SAME `AssetTags`, so the two
						// halves of one critical strike are read on identical
						// terms. A tag that scopes the chance and not the
						// multiplier would be the worse outcome of the two.
						//
						// FALLS BACK TO THE ATTRIBUTE, which is what a character
						// with no such modifier has always had. `StatForSkill`
						// answers the fallback when nothing was recorded for the
						// stat -- every enemy is in that case, because an enemy
						// is never given a character stat line.
						Hit.CritMultiplier = Asking
							? Asking->StatForSkill(FName(TEXT("crit_multiplier")),
												   AssetTags,
												   Offence->GetCritMultiplier(),
												   /*SkillHealthCostPercent=*/-1.0f,
												   FCataclysmBlowContext(),
												   /*MetresMovedBeforeBlow=*/-1.0f,
												   Hit.OpponentDistanceMetres,
												   UCataclysmSkillEffects::IsStaggered(
													   GetOwningActor()),
												   /*Target=*/GetOwningActor())
							: Offence->GetCritMultiplier();

						// AND THE EDICT OF HUMILITY HOLDS IT AT 100: "a critical strike deals no extra damage".
						// 2026-10-23, G7c. HELD, NOT ZEROED AND NOT REMOVED: 100 keeps the roll's guard in
						// `UCataclysmDamageCalculation::Resolve` above nought, so a critical strike is still rolled,
						// reported and heard by every on-crit row, ruled 2026-10-23 on Path of Exile's Elemental
						// Overload. A MULTIPLIER ALREADY BELOW 100 STAYS BELOW IT: the Edict takes extra damage away
						// and adds none. With the same `Asking` and `AssetTags` as the multiplier; a creature has no
						// stat line and reads the fallback, nought, so no creature is held.
						if (Asking
							&& Asking->StatForSkill(
								   FName(UCataclysmDamageCalculation::CriticalStrikesDealNoExtraDamageStat), AssetTags,
								   0.0f) > 0.0f)
						{
							Hit.CritMultiplier = FMath::Min(Hit.CritMultiplier,
															UCataclysmDamageCalculation::NoExtraCriticalMultiplier);
						}

						// AND WHAT A HIT KEEPS IF ITS ROLL FAILS, ASKED ON THE SAME
						// TERMS AS THE MULTIPLIER. Issue #1686. Inside this block, so
						// only a blow that CAN critically strike is a non-critical
						// strike when it does not: a tick, a minion's blow and
						// retaliation keep 100, ruled under the owner's delegation
						// on 2026-09-23. A creature has no stat line and reads the
						// fallback, 100.
						Hit.NonCriticalDamagePercent = Asking
							? Asking->StatForSkill(
								FName(UCataclysmDamageCalculation::NonCriticalDamageStat),
								AssetTags,
								UCataclysmDamageCalculation::NormalNonCriticalDamage,
								-1.0f, FCataclysmBlowContext(), -1.0f,
								Hit.OpponentDistanceMetres,
								UCataclysmSkillEffects::IsStaggered(GetOwningActor()),
								GetOwningActor())
							: UCataclysmDamageCalculation::NormalNonCriticalDamage;
					}
				}
			}

			// AND THE WEAPON SUB-TYPE, WHICH IS READ OFF THE ATTACKING ACTOR rather
			// than off its attributes, because it is a property of what is in its
			// hand rather than a number it carries. Issue #639: all three of these
			// were applied correctly by Resolve and none was ever set, because
			// nothing joined the equipped weapon to a hit.
			//
			// AN ENEMY ANSWERS EMPTY and always will: it has no weapon slots
			// component, because it attacks from its own attack damage rather than
			// from a weapon. That is the same answer every hit gave before this
			// existed, so nothing about an enemy's hit changes.
			//
			// BLUNT'S EFFECT IS NOT APPLIED WHERE THE OTHER THREE ARE, though the
			// sub-type itself is read here with them. Slashing, magic and piercing
			// each change what a blow does to a pool or to armour, so `Resolve`
			// applies them along with the damage. Blunt's effect is a chance to
			// stun, which is something that happens to the defender after the
			// numbers land, so it is rolled further down this function once the
			// hit is known to have reached health. Search `bIsBlunt`.
			//
			// THE TEST THAT SAYS SO IS
			// `Cataclysm.DamageType.ABluntWeaponCanStunWhatItHits`, which swings a
			// Fist until it lands a stun. A named test is worth more here than
			// another sentence claiming the roll exists, given that the sentence
			// this replaced claimed the opposite for months.
			//
			// THIS COMMENT USED TO SAY THAT ROLL DID NOT EXIST, which is issue
			// #900. Issue #639 built it and the comment was never updated, so for
			// a while the file stated in one place that nothing in the project
			// could roll a stun on an ordinary hit and did exactly that in
			// another. That is not a harmless staleness: a comment saying a
			// capability is missing is read as a reason not to look for it, and
			// issue #899 was written recommending that the mechanism be built
			// when it was already there in this same function.
			//
			// NO LINE NUMBER OR DISTANCE IS QUOTED HERE ON PURPOSE. The first
			// draft of this correction said "a hundred and nineteen lines below";
			// leech and retaliation landed between the two points in the days
			// after and it was already wrong.
			// A BLOW CAN BE FORBIDDEN THE WHOLE SUB-TYPE, which is what a minion's
			// is. The causer a sub-type is read off is the summoner, exactly as
			// the attacker is, so without this a sword in the player's hand makes
			// its imps deal 10% more to health and a wand makes them strip 10%
			// more energy shield. The design's general rule blocks it: a minion
			// reaches its summoner through exactly three channels and a weapon
			// sub-type is not one of them. Issue #676.
			const bool bCarriesSubType = !AssetTags.HasTag(
				UCataclysmDamageCalculation::NoWeaponSubTypeTag());

			const FString SubType = bCarriesSubType
				? UCataclysmWeaponSlotsComponent::SubTypeOf(
					Data.EffectSpec.GetContext().GetEffectCauser())
				: FString();

			Hit.bIsSlashing =
				SubType.Equals(TEXT("Slashing"), ESearchCase::IgnoreCase);
			Hit.bIsMagic = SubType.Equals(TEXT("Magic"), ESearchCase::IgnoreCase);

			// PIERCING IS BLOCKED BY TWO INDEPENDENT THINGS AND NEEDS ONLY ONE.
			// It is a sub-type, so the rule above reaches it; and its whole effect
			// IS armour penetration -- `Resolve` adds a further 20% of the
			// defender's armour ignored on top of the attacker's own stat -- so a
			// blow forbidden to penetrate must not get it either. Issue #659 added
			// the second reason; issue #676 added the first. A minion carries both
			// and either alone would be enough.
			const bool bHoldsAPiercingWeapon =
				SubType.Equals(TEXT("Piercing"), ESearchCase::IgnoreCase);
			Hit.bIsPiercing = bCanPenetrate && bHoldsAPiercingWeapon;
			Hit.bIsBlunt = SubType.Equals(TEXT("Blunt"), ESearchCase::IgnoreCase);

			// AND THE SHARE OF ARMOUR THE BLOW ITSELF SAYS IT IGNORES. Ruled
			// 2026-10-07. `FCataclysmHitDelivery::NamedArmorPenetrationPercent` is
			// stamped on the effect by `UCataclysmSkillEffects::ApplyTypedSpec`, and
			// the one blow that carries it is a trap's, for its summoner's
			// `armor_penetration` rows that name `Type.Trap`.
			//
			// NOT INSIDE `bCanPenetrate`, AND THAT IS THE POINT. That flag stops the
			// ATTACKER'S figures reaching a blow, and a minion's blow carries it.
			// This figure was worked out by the caller from rows that name the
			// minion's kind, which is what the design allows to cross.
			//
			// ADDED, AFTER THE ATTACKER'S OWN FIGURE WAS READ ABOVE, so it cannot be
			// overwritten by that read. A blow with no stamp adds nought. The
			// damage calculation clamps the sum and still lets a defender whose
			// armour cannot be ignored refuse all of it.
			Hit.ArmorPenetration += FMath::Max(
				0.0f,
				Data.EffectSpec.GetSetByCallerMagnitude(
					FName(UCataclysmSkillEffects::NamedArmorPenetrationDataName),
					/*WarnIfNotFound=*/false, 0.0f));

			// THE DIFFICULTY TIER IS READ RATHER THAN ASSUMED, since issue #514.
			// This passed a literal 1 because nothing in the project held a tier
			// at all, and the tier decides what armour is worth: armour removes
			// `armor / (armor + 800 x tier)` of a hit, capped at 75%, so the
			// Abyssal Warden's designed 5,954 stopped 75% of every hit instead
			// of the 48.19% its design states. Every armoured thing in the game
			// was 2.07 times harder to hurt than the simulation said.
			//
			// ASKED OF THE DEFENDER'S WORLD, because that is where the fight is
			// happening. `ACataclysmGameMode::DifficultyTierIn` answers with the
			// console variable, then the game mode, then tier 1 -- and a world
			// with no game mode gets exactly the answer this line used to
			// hard-code, so nothing that does not care is changed by it.
			// WHETHER THIS BLOW IS A WEARER'S SHARE OF WHAT A BLOW DID TO AN ALLY BESIDE IT. Ruled 2026-10-15, for "You
			// take 5%-15% damage done to nearby allies" (R7): it meets this character's defences as any blow does, and
			// raises none of its rows. Each place below that a blow taken raises something reads this, and the share
			// is never passed on again from here. See `FCataclysmHitDelivery::bIsAShareOfDamageDoneToAnAlly`.
			const bool bShareOfAnAllysDamage = UCataclysmAllyDamageShare::IsAShare(Data.EffectSpec);
			// AND WHETHER IT IS THE SHARE OF A POINT-BLANK SKILL'S BLOW ITS ATTACKER STRUCK AT THIS CHARACTER, AN ALLY
			// INSIDE THE SKILL'S AREA. Ruled 2026-10-15, for "Your AOE skills deal 50%-75% of their damage to nearby
			// allies as well as enemies" (R5, R6): its damage meets this character as any blow's does, and it is not
			// recorded as the attacker's first blow on this character, nor brands or charms its attacker. See
			// `FCataclysmHitDelivery::AreaSkillShareOnAnAllyPercent`.
			const bool bAreaSkillShareOnAnAlly = UCataclysmAreaShareOnAllies::IsAShare(Data.EffectSpec);

			// WHETHER THIS IS THE Nth HIT OF A WORN "EVERY Nth HIT YOU TAKE" ROW,
			// asked BEFORE it is resolved, because the count advances after.
			// Issue #1833, phase 2. A tick is not a hit and is never the Nth.
			// NOR IS A SHARE OF AN ALLY'S BLOW, which the count does not count either. Ruled 2026-10-15.
			if (!Hit.bIsDamageOverTime && !bShareOfAnAllysDamage)
			{
				if (const UCataclysmAbilitySystemComponent* Taking =
						Cast<UCataclysmAbilitySystemComponent>(
							GetOwningAbilitySystemComponent()))
				{
					Hit.BonusDamagePercent = Taking->NthHitTakenBonusPercent();
				}
			}

			FCataclysmDamageResult Resolved =
				UCataclysmDamageCalculation::Resolve(
					Hit, GetOwningAbilitySystemComponent(),
					ACataclysmGameMode::DifficultyTierIn(GetOwningActor()),
					/*EvasionRoll=*/-1.0f, CVarBlockRoll.GetValueOnAnyThread(),
					CVarCritRoll.GetValueOnAnyThread());

			// A CONTAGIOUS TOUCH KEEPS ONLY WHETHER IT WAS EVADED. Issues #1820 and #41. The row: "Enemies no longer
			// deal damage on contact. Instead, they apply a stacking "Contagion" debuff." The blow came through the
			// whole pipeline so that the evasion step above answered it with every condition it reads, as ruled; the
			// rest is dropped here, before anything is dealt, leeched, retaliated or announced, and nothing is drawn but
			// an evaded touch's "Evaded". Recorded, so `ACataclysmEnemyCharacter::AttackTarget` learns the answer as
			// every blow's sender does.
			if (const ACataclysmEnemyCharacter* Toucher =
					Cast<ACataclysmEnemyCharacter>(Data.EffectSpec.GetContext().GetEffectCauser());
				Toucher && Toucher->bSwingIsAContagiousTouch)
			{
				FCataclysmDamageResult Touch;
				Touch.bEvaded = Resolved.bEvaded;
				if (UCataclysmAbilitySystemComponent* Touched =
						Cast<UCataclysmAbilitySystemComponent>(GetOwningAbilitySystemComponent()))
				{
					Touched->RecordResolvedHit(Touch);
				}
				// AN EVADED TOUCH STILL SAYS "Evaded", as ruled: the player sees that evasion worked, as with every
				// evaded blow. The one call `PlayImpactEffect` makes for the number, on the actor it draws on; no
				// particle and nothing else is drawn. A test world has no heads-up display, so no test sees it.
				if (Touch.bEvaded)
				{
					UCataclysmCombatOverlay::Record(
						UCataclysmImpactEffect::ActorToDrawOn(GetOwningAbilitySystemComponent()), Hit, Touch);
				}
				return;
			}

			// AND A BOSS'S FLOOR IS CHECKED A SECOND TIME, ON WHAT CAME OUT.
			// Issue #915. The first check is made before the target's defences,
			// in `ShareOfHealthTick` above, which is what keeps a boss's
			// resistance worth having against Void Splinter. A stat that makes
			// the boss take MORE damage than normal would otherwise carry the
			// one tick that reaches the line past it, so what finally reaches
			// health is held to the same line here.
			//
			// ONLY A SHARE TICK ON A BOSS IS TOUCHED. Every other blow arrives
			// exactly as the calculation resolved it, and `ShareOfHealthRoomLeft`
			// answers a target's whole health for anything that is not a boss.
			if (ShareOfCurrentHealth >= 0.0f)
			{
				Resolved.DealtToHealth = FMath::Min(Resolved.DealtToHealth,
					UCataclysmSkillEffects::ShareOfHealthRoomLeft(
						GetHealth(), GetMaxHealth(),
						AsEnemy && AsEnemy->IsBoss()));
			}

			// SACRIFICIAL WARD, BEFORE NOTHING STOPS IT BELOW, because a blow the
			// ward takes whole is lethal to nobody. Issue #1515: "Damage that
			// would break your Energy Shield instead destroys the minion with the
			// least health remaining, no more than once every 3 seconds."
			//
			// "WOULD BREAK" IS THE BLOW'S SHIELD SHARE REACHING WHAT THE SHIELD
			// HOLDS, while it holds something: a shield already at nothing is not
			// broken again. Damage over time counts, since the sentence says
			// "damage"; a bleed never reaches the shield (issue #2014) and so
			// never asks.
			//
			// JUDGEMENT: "INSTEAD" CANCELS THE WHOLE BLOW. The shield keeps what
			// it had, health takes nothing, and the minion dies in its place.
			// With nothing alive to spend, the shield breaks as it would have.
			//
			// THE MINION DIES AS ANY DEATH DOES, ruled on 2026-09-23 under the
			// owner's delegation: its health is written to nothing, so its own
			// death path runs, and Every One Bursts, Press-Ganged, Rekindled and
			// Shared Ruin each answer it as they would any other.
			if (UCataclysmAbilitySystemComponent* Warded =
					Cast<UCataclysmAbilitySystemComponent>(
						GetOwningAbilitySystemComponent()))
			{
				const float Shield = GetEnergyShield();
				if (Shield > 0.0f && Resolved.AbsorbedByShield >= Shield)
				{
					const FName WardStat(
						UCataclysmAbilitySystemComponent::ShieldBreakDestroysMinionEverySecondsStat);
					const float WardEvery =
						Warded->StatForSkill(WardStat, FGameplayTagContainer(), 0.0f);
					AActor* Spent = WardEvery > 0.0f && Warded->MaySpendMinionForShield()
						? UCataclysmCommand::LeastHealthCommandedBy(Warded->GetAvatarActor())
						: nullptr;
					if (UAbilitySystemComponent* SpentSystem =
							UCataclysmTargeting::AbilitySystemOf(Spent))
					{
						Resolved.DealtToHealth = 0.0f;
						Resolved.AbsorbedByShield = 0.0f;
						Resolved.AbsorbedByMana = 0.0f;
						Warded->NoteMinionSpentForShield(WardEvery);
						// AND THE TEMPORARY ABSORB KEEPS WHAT IT HELD: the whole blow is cancelled.
						Resolved.AbsorbedByTemporary = 0.0f;

						// KILLED BY NOBODY. A death written to health names as its
						// killer whoever last struck the dying creature, however long
						// ago, so a minion an enemy grazed a minute earlier would
						// credit that enemy for the ward. Its record is emptied first:
						// the ward's death is nobody's kill, neither the player's nor
						// the creature that struck the shield, which is never recorded
						// on the minion at all.
						if (UCataclysmAbilitySystemComponent* SpentCataclysm =
								Cast<UCataclysmAbilitySystemComponent>(SpentSystem))
						{
							SpentCataclysm->RecordLastBlow(FCataclysmLastBlow());
						}
						SpentSystem->SetNumericAttributeBase(
							UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
					}
				}
			}

			// AND NOTHING STOPS IT, ON WHAT CAME OUT AS WELL. Issue #1515: "You
			// cannot be brought below 1 health by a single hit. When a hit would
			// have done so you take no damage for 2 seconds, no more than once
			// every 20 seconds." Ruled on 2026-09-23 under the owner's
			// delegation: a damage over time tick is not "a single hit", so only
			// a hit is saved; the window after it stops ALL damage, ticks
			// included, because the sentence says "no damage".
			//
			// THE WHOLE RESULT IS EMPTIED IN THE WINDOW, NOT ONLY THE HEALTH, so
			// nothing downstream hears of a blow that did nothing: no shield
			// drawn, no refill wait restarted, no leech for the attacker.
			//
			// LETHAL MEANS THE RESOLVED FIGURE REACHES THE HEALTH LEFT. The
			// Masochist's conversion below may turn some of a blow into bleeding
			// first, but a Ravager has none to hold, so the order is stated
			// rather than felt.
			//
			// AND, FOR A WEARER OF "25%-50% of damage taken is dealt over 4-8 seconds", LESS THE SHARE THAT WILL BE
			// DELAYED. Ruled 2026-10-13 by the coordinating session under the owner's delegation: the two saves
			// below judge the blow after the delayed share is taken off, so that a save is never spent on a blow
			// that would not have killed. The share is taken far below, where the blow is written to health, so
			// each save asks `EmptiesHealthLessTheDelayedShare` what it will be.
			//
			// WHETHER ONE OF THE TWO SAVES CUT THIS BLOW TO LEAVE ONE POINT. Read where the share is taken:
			// NOTHING OF A SAVED BLOW IS DELAYED, a judgement of the writing session. A save promises its holder the
			// one point, and the heal of the second save fires on the fall to it. The option not taken: take the
			// share of the cut figure, as was done until 2026-10-13, which leaves a wearer of the second save well
			// above the share of health its heal watches and so not healed by the blow the row cut for it.
			bool bSavedFromALethalBlow = false;
			if (UCataclysmAbilitySystemComponent* Guarded =
					Cast<UCataclysmAbilitySystemComponent>(
						GetOwningAbilitySystemComponent()))
			{
				// WHETHER THE BLOW STILL EMPTIES HEALTH ONCE ITS DELAYED SHARE IS TAKEN OFF. Asked by each save below
				// as the LAST of its conditions, so only of a blow whose resolved figure reaches the health held and
				// only for a holder of a save that is ready. A character holding no save is asked nothing new.
				//
				// THE FIGURE THE SHARE IS LATER TAKEN OF: what the blow dealt to health and its overkill together,
				// which the lines far below call `WholeBeforeDelay`. The three pools were given theirs by the
				// calculation, so `Resolved.DealtToHealth` is already what is left after them. And the same
				// comparison those lines make before the write to health, so the two cannot disagree.
				//
				// YES WITHOUT THE COMPARISON WHEN NO SHARE WOULD BE DELAYED, so for a character with no such row each
				// save does exactly what it did. NO SHARE IS ASKED FOR WHILE THE BREAKING POINT'S WINDOW IS OPEN:
				// that conversion takes the whole blow first and nothing is then delayed, so the saves read the
				// whole blow, as they did.
				const auto EmptiesHealthLessTheDelayedShare = [this, Guarded, &Resolved, &Hit]() -> bool
				{
					const float WholeToHealth = Resolved.DealtToHealth + Resolved.Overkill;
					const float DelayedLater = Guarded->IsConvertingDamageToBleeding()
						? 0.0f
						: UCataclysmDelayedDamage::ShareThatWouldBeDelayed(
							Guarded, WholeToHealth, Hit.bIsDamageOverTime);
					return DelayedLater <= 0.0f || WholeToHealth - DelayedLater >= GetHealth();
				};

				// A WORN ROW'S CHANCE THAT THIS BLOW DOES NOTHING TO ITS WEARER. Ruled 2026-10-06. "Spells that
				// hit you have a 15%-30% chance to be absorbed dealing no damage", and "Melee attacks that hit you
				// have a 10%-20% chance to be reflected back as retaliation damage": the wearer takes none of a
				// reflected hit, and the whole of it before mitigation is paid back. ONE ROLL EACH FOR ONE BLOW.
				// Not for a blow that was evaded, which did not hit; not for a tick; and a reflected hit is not
				// reflected back, which the no-retaliation tag on it says. EMPTIED HERE, WHERE THE NO-DAMAGE
				// WINDOW EMPTIES A BLOW, so everything after reads it as that window's blow is read.
				//
				// AND THE WINDOW AGAINST ONE DAMAGE TYPE IS READ WHEREVER THE NO-DAMAGE WINDOW IS, at the three
				// places below. Ruled 2026-10-12, for "you have a 10% chance to become immune to the damage type
				// that hit you for 5 seconds". `Hit.DamageType` is the type that decides a resistance, none for
				// a blow whose element is for colour only, and `IsImmuneToDamageType` answers false for none.
				// So a blow or a tick of a type whose window is open is emptied as the no-damage window empties
				// one, and everything after reads it the same way. ASKED AT EACH PLACE AND NOT KEPT IN A LOCAL,
				// as `IsDamageImmune` is: the save below can open the no-damage window between the second read
				// and the third.
				const bool bMayBeTurned = !Resolved.bEvaded && Hit.Damage > 0.0f && !Guarded->IsDamageImmune()
					&& !Guarded->IsImmuneToDamageType(Hit.DamageType);
				const bool bAbsorbed = bMayBeTurned && UCataclysmDamageCalculation::SpellIsAbsorbed(Guarded, Hit);
				const bool bReflected = bMayBeTurned && !bAbsorbed
					&& !AssetTags.HasTag(UCataclysmDamageCalculation::NoRetaliationTag())
					&& UCataclysmDamageCalculation::MeleeIsReflected(Guarded, Hit);
				if (bReflected)
				{
					UCataclysmRetaliation::PayReflected(
						Guarded, CataclysmDefendingBody(*this), Data.EffectSpec.GetContext().GetEffectCauser(),
						Hit.Damage);
				}
				// AND AN ABSORBED SPELL IS STORED, for "Absorbed spell damage is
				// converted to bonus damage on your next attack". Ruled
				// 2026-10-07: what the spell would have dealt after the wearer's
				// defences, which is these three figures before the lines below
				// empty them. ONLY WHEN THE ABSORB TURNED IT ASIDE: `bAbsorbed`
				// is never true inside the no-damage window, and a reflected hit
				// was not absorbed.
				if (bAbsorbed)
				{
					Guarded->NoteSpellAbsorbedDamage(
						Resolved.DealtToHealth + Resolved.AbsorbedByShield
							+ Resolved.AbsorbedByMana + Resolved.AbsorbedByTemporary);
				}
				if (Guarded->IsDamageImmune() || Guarded->IsImmuneToDamageType(Hit.DamageType)
					|| bAbsorbed || bReflected)
				{
					Resolved.DealtToHealth = 0.0f;
					Resolved.AbsorbedByShield = 0.0f;
					Resolved.AbsorbedByMana = 0.0f;
					Resolved.AbsorbedByTemporary = 0.0f;
				}
				else if (!Hit.bIsDamageOverTime && GetHealth() > 0.0f
					&& Resolved.DealtToHealth >= GetHealth())
				{
					using UGuarded = UCataclysmAbilitySystemComponent;
					const FName SurvivedEveryStat(UGuarded::LethalHitSurvivedEverySecondsStat);
					const FName ImmuneForStat(UGuarded::ImmuneAfterLethalHitSecondsStat);
					const float Every = Guarded->StatForSkill(
						SurvivedEveryStat, FGameplayTagContainer(), 0.0f);
					if (Every > 0.0f && Guarded->MaySurviveLethalHit() && EmptiesHealthLessTheDelayedShare())
					{
						bSavedFromALethalBlow = true;
						Resolved.DealtToHealth = FMath::Max(0.0f, GetHealth() - 1.0f);
						Guarded->NoteLethalHitSurvived(Every, Guarded->StatForSkill(
							ImmuneForStat, FGameplayTagContainer(), 0.0f));
					}
				}

				// AND A REPRIEVE THAT ENDS IN DEATH UNLESS A KILL IS MADE, FOR A BLOW OR A TICK NOTHING STOPS IT DID
				// NOT SAVE. Ruled 2026-10-17 and 2026-10-18 for the Demon King's Regalia's ten-piece: the same one
				// point left, its own figures and clock (Q6), and the sentence and the critical strike rows begun by
				// `NoteLethalHitReprieved`. Judgements of the writing session: AFTER NOTHING STOPS IT, because that
				// save costs nothing, so the sentence is risked only when it is not ready; and A TICK TOO, because
				// "if you would be killed" is about dying and not about a hit, as Archon's Aegis's save below reads
				// its sentence. Only a blow that lands while the Rage's stack is already held is reprieved (Q7): a
				// blow that itself takes health below 25% grants that stack in the write below, after this is asked.
				if (!bSavedFromALethalBlow && !Guarded->IsDamageImmune()
					&& !Guarded->IsImmuneToDamageType(Hit.DamageType)
					&& GetHealth() > 0.0f && Resolved.DealtToHealth >= GetHealth()
					&& Guarded->MayReprieveLethalHit() && EmptiesHealthLessTheDelayedShare())
				{
					bSavedFromALethalBlow = true;
					Resolved.DealtToHealth = FMath::Max(0.0f, GetHealth() - 1.0f);
					Guarded->NoteLethalHitReprieved();
				}

				// AND A WORN ROW THAT HEALS AS HEALTH FALLS BELOW ITS SHARE, which
				// also saves a blow that would empty health: ruled 2026-09-30 for
				// Archon's Aegis, because 0 is below 10%. The blow leaves one point,
				// and the fall below the threshold then fires the heal through
				// `NotifyHealthChanged`. A damage over time tick is saved too: the
				// sentence is about health falling, not about a hit. After Nothing
				// Stops It, which saves first and spends nothing of this.
				if (!Guarded->IsDamageImmune() && !Guarded->IsImmuneToDamageType(Hit.DamageType)
					&& GetHealth() > 1.0f
					&& GetMaxHealth() > 0.0f && Resolved.DealtToHealth >= GetHealth()
					&& Guarded->SavesLethalBlowByCrossing(GetHealth() / GetMaxHealth() * 100.0f)
					&& EmptiesHealthLessTheDelayedShare())
				{
					bSavedFromALethalBlow = true;
					Resolved.DealtToHealth = GetHealth() - 1.0f;
				}
			}

			// AND A CREATURE NO DAMAGE REACHES: The Reaper. Issues #1820 and #41. The blow
			// has resolved -- evaded, blocked or not -- and deals nothing, the way the
			// immunity above deals nothing. After the save, so nothing is ever saved from a
			// blow this then empties. AND A SHROUDED ONE, Shadowy Enemies, the same way: the blow
			// still reaches the announcement below, which is how a fire hit on a shrouded
			// creature exposes it.
			if (AsEnemy && AsEnemy->TakesNoDamage())
			{
				Resolved.DealtToHealth = 0.0f;
				Resolved.AbsorbedByShield = 0.0f;
				Resolved.AbsorbedByMana = 0.0f;
				Resolved.AbsorbedByTemporary = 0.0f;
			}

			// AND A CHARACTER THAT HAS BECOME IMMUNE TO THE ONE ATTACKER THAT THREW THIS BLOW. Ruled 2026-10-12
			// under the owner's delegation, for "Enemies that have been alive for more than 15 seconds become
			// immune to your damage until you disengage for 3 seconds". THE ONE IMMUNITY HERE THAT IS KEYED ON
			// THE ATTACKER: every one above refuses all attackers alike. The blow is emptied as those empty one,
			// so everything after reads it the same way: no health, no shield or absorb drawn, no leech, no
			// record of a blow that got through, no retaliation in answer.
			//
			// EVERY KIND OF BLOW THAT REACHES THIS FUNCTION, WITH NO EXCEPTION BY KIND: a hit, a tick of damage
			// over time, retaliation, a blow that is a row's answer to an event, and a blow that is the
			// consequence of a death. A blow that carries the cannot-be-mitigated flag is emptied too: that flag
			// is read by the calculation alone, as it is for the immunities above.
			//
			// `AttackerOf`, THE CHARACTER `NoteStruckBy` BELOW RECORDS THE BLOW UNDER, so the blow that starts
			// an attacker's time and the blows refused when it runs out are the same attacker's. A minion's
			// blow is the minion's own and is not refused for its summoner's rule; while the summoner holds
			// Conduit it is the summoner's, and is.
			//
			// AFTER THE TWO SAVES AND BESIDE THE CREATURE NO DAMAGE REACHES, and before the overkill line
			// below, which then clears the overkill of a blow this emptied.
			//
			// AND THE RESULT SAYS WHY IT IS EMPTY, which the three other immunities above do not say of theirs.
			// Ruled 2026-10-12: an immune enemy feeds no on-hit row and shows no figure. The mark is read by the
			// hit's announcement, by a machine's `deployable_hit` and by the floating number; see
			// `FCataclysmDamageResult::bRefusedByImmunityToAttacker`. THE BLOW IS STILL A COMBAT EVENT FOR BOTH
			// CHARACTERS: the lines far below that record a hit taken and a hit dealt do not read the mark.
			//
			// AND A MAXIMUM AN AILMENT OF THAT ATTACKER'S WAS HOLDING DOWN RETURNS NOW. Ruled 2026-10-12: on an
			// enemy immune to it, the attacker's ailments and their riders take nothing. Nothing announces the
			// moment an enemy becomes immune, so the first blow it refuses is where that is acted on; for an
			// ailment that is its next tick. See `UCataclysmAbilitySystemComponent::NoteBlowRefusedAsImmune`.
			if (UCataclysmAbilitySystemComponent* Refusing =
					Cast<UCataclysmAbilitySystemComponent>(GetOwningAbilitySystemComponent()))
			{
				if (Refusing->IsImmuneToDamageOf(UCataclysmTargeting::AbilitySystemOf(
						UCataclysmCombatEvents::AttackerOf(Data.EffectSpec.GetContext()))))
				{
					Resolved.DealtToHealth = 0.0f;
					Resolved.AbsorbedByShield = 0.0f;
					Resolved.AbsorbedByMana = 0.0f;
					Resolved.AbsorbedByTemporary = 0.0f;
					Resolved.bRefusedByImmunityToAttacker = true;
					Refusing->NoteBlowRefusedAsImmune();
				}
			}

			// AND A BLOW THAT NO LONGER EMPTIES HEALTH HAS NO OVERKILL. Ruled 2026-10-07. Every step above that
			// lowers `Resolved.DealtToHealth` leaves it below the health held: the boss's floor on a share tick,
			// the Sacrificial Ward, an immune, absorbing or reflecting wearer, the two saves that leave one
			// point, and a creature no damage reaches. ONE LINE AFTER ALL OF THEM rather than one in each, so a
			// step added later is covered without having to know of this. A blow that deals nothing kills
			// nothing.
			if (Resolved.DealtToHealth <= 0.0f || Resolved.DealtToHealth < GetHealth())
			{
				Resolved.Overkill = 0.0f;
			}

			// EVERYTHING BELOW READS THIS, so the second check reaches the health
			// write, the floating number, the leech and everything else that asks
			// what the blow dealt.
			const FCataclysmDamageResult& Outcome = Resolved;
			// WHETHER THIS IS THE ATTACKER'S FIRST BLOW TO GET THROUGH, from the
			// record below, carried to the announcement. Issue #1833 group E part 1.
			bool bFirstFromAttacker = false;

			// RECORDED ON THE CHARACTER, SO THE BLOW'S SENDER CAN LEARN WHAT
			// BECAME OF IT. Issue #1156. Everything above this line happens
			// inside the DEFENDER, after the attacker has already handed over a
			// number and moved on, so without this the attacker knows only what
			// it sent. `UCataclysmSkillEffects::ApplyHit` reads it back the
			// instant `ApplyGameplayEffectToSelf` returns, which is inside this
			// same call because the effect carrying the damage is Instant.
			//
			// HERE RATHER THAN BESIDE THE DRAWING BELOW, because this must
			// happen for every resolved blow and the drawing refuses several
			// kinds. An evaded blow draws no particle and is exactly the case
			// the caller needs to hear about.
			if (UCataclysmAbilitySystemComponent* Cataclysm =
					Cast<UCataclysmAbilitySystemComponent>(
						GetOwningAbilitySystemComponent()))
			{
				Cataclysm->RecordResolvedHit(Outcome);
			}

			// THE ENERGY SHIELD'S REFILL WAIT STARTS HERE. The design gives the
			// shield a three second delay after the character last took damage,
			// restarted by taking damage again inside that window, and says
			// damage over time restarts it as well. That last part is
			// load-bearing: the shield does not absorb a bleed (issue #2014;
			// until then it absorbed no damage over time at all), so without it
			// a bleeding character would refill freely and the shield would be
			// strongest against the one thing it ignores.
			//
			// ANYTHING THAT GOT THROUGH COUNTS, including a blow a shield
			// swallowed whole and including a burn tick. A hit that was evaded,
			// or that armour and resistance stopped completely, took nothing
			// and does not restart the wait. Issue #653.
			//
			// THE AVATAR, NOT THE OWNER, for the same reason the hit effect and
			// the death path both need it: GetOwningActor answers with the
			// ability system's owner, and for the player that is the player
			// state, which is not a character at all. Issues #562 and #565.
			// AND A BLOW THE TEMPORARY ABSORB TOOK GOT THROUGH AS WELL, a judgement by
			// the writing session: the character was struck and something it held was
			// spent, as with a blow the energy shield swallowed whole. So the attacker
			// leeches from it, retaliation answers it and it is recorded as a blow of
			// its attacker's. THE REFILL WAIT DIRECTLY BELOW IS THE ONE EXCEPTION.
			if (Outcome.DealtToHealth > 0.0f || Outcome.AbsorbedByShield > 0.0f
				|| Outcome.AbsorbedByMana > 0.0f || Outcome.AbsorbedByTemporary > 0.0f)
			{
				if (ACataclysmCharacterBase* Hurt =
						GetOwningAbilitySystemComponent()
							? Cast<ACataclysmCharacterBase>(
								  GetOwningAbilitySystemComponent()
									  ->GetAvatarActor())
							: nullptr)
				{
					// AND FOR A BLOW THE TEMPORARY ABSORB TOOK WHOLE TOO. Ruled 2026-10-07:
					// such a blow got through, and the energy shield's refill wait
					// restarts for it as for any blow that lands. This was first written
					// the other way, on a reading of "it does not count as the energy
					// shield" that the ruling did not mean: that ruling is about rows
					// that name the energy shield, not about being hit.
					Hurt->NoteDamageTaken();
				}

				// AND THE ATTACKER LEECHES FROM WHAT GOT THROUGH. Issue #895:
				// the three leech attributes existed and no code anywhere read
				// one, so all three affixes granting them were worth nothing.
				//
				// ALL THREE FIGURES, NOT ONLY THE HEALTH. The design says leech
				// is "a percentage of the damage actually dealt... the damage
				// the target really took", and damage a shield or mana absorbed
				// is damage the target took.
				//
				// ALREADY CAPPED AT WHAT THE TARGET HAD, so the design's rule
				// that "overkill does not count" needs nothing here: Resolve
				// writes DealtToHealth as the smaller of the damage and the
				// target's remaining health.
				//
				// A MINION'S BLOW LEECHES NOTHING, which is the fourth of the
				// four exclusions its blow carries and is checked here rather
				// than inside the leech code, exactly as the critical strike
				// and the two penetrations are.
				//
				// A TICK ALSO SAYS WHICH AILMENT IT IS. Ruled 2026-10-07, for "Bleed
				// damage you deal also leeches 10%-20% of its value as HP". The
				// asset tags carry the skill's tags and the bare `Keyword.DoT`;
				// the ailment is a granted tag, gathered above for a tick alone.
				// Without it a leech row scoped to `Keyword.DoT.Bleed` was
				// discarded on every tick. A hit carries none, so a hit is asked
				// with the same tags as before.
				if (!AssetTags.HasTag(UCataclysmDamageCalculation::NoLeechTag()))
				{
					FGameplayTagContainer LeechTags = AssetTags;
					LeechTags.AppendTags(Hit.DamageOverTimeTags);
					UCataclysmLeech::NoteHit(
						const_cast<UAbilitySystemComponent*>(
							Data.EffectSpec.GetContext()
								.GetInstigatorAbilitySystemComponent()),
						Outcome.DealtToHealth + Outcome.AbsorbedByShield
							+ Outcome.AbsorbedByMana + Outcome.AbsorbedByTemporary,
						LeechTags);
				}

				// AND A BLOW THAT GOT THROUGH TO A BOSS OPENS THE ATTACKER'S
				// COOLDOWN WINDOW. "Your cooldowns reset 50%-100% faster when
				// fighting Boss enemies, for 4 seconds after you strike one" is
				// the row it exists for, and a cooldown counts down with nobody
				// being struck, so the per-blow condition `target_is_boss`
				// cannot reach it.
				//
				// IN THIS BLOCK, SO "STRUCK" MEANS WHAT GOT THROUGH. A blow
				// that was evaded, or that armour and resistance stopped
				// completely, never reaches here -- the same line the energy
				// shield's refill wait draws above.
				//
				// `AttackerOf` AND NOT THE INSTIGATOR, which is the whole
				// reason that function exists. A minion's blow opens the
				// MINION's window, and its summoner's only while the summoner
				// holds the Ritualist keystone Conduit. Reading the instigator
				// here would answer "the minion" always and the keystone would
				// apply to the kill credit and not to this.
				if (AsEnemy && AsEnemy->IsBoss())
				{
					if (UCataclysmAbilitySystemComponent* Striker =
							Cast<UCataclysmAbilitySystemComponent>(
								UCataclysmTargeting::AbilitySystemOf(
									UCataclysmCombatEvents::AttackerOf(
										Data.EffectSpec.GetContext()))))
					{
						Striker->NoteStruckABoss();
					}
				}

				// AND THIS CHARACTER REMEMBERS WHO GOT A BLOW THROUGH. Issue
				// #1815, "your first hit against each enemy". The same site and
				// the same `AttackerOf` as the Boss clock above, so "a hit" means
				// the same thing to both: health, shield or mana absorbed it.
				// Written after the blow's own stats were read, so the first blow
				// is judged against a record that does not hold it yet.
				//
				// NOT A BLOW THAT IS THE CONSEQUENCE OF A DEATH. Ruled 2026-10-07 under
				// the project owner's delegation: an overkill explosion is not a blow
				// its attacker struck, so it is not the attacker's first blow on this
				// character, and the attacker's next real hit still is. The stamp is
				// `UCataclysmSkillEffects::ConsequenceOfADeathDataName`. THE BOSS
				// CLOCK ABOVE IS LEFT AS IT WAS: damage dealt to a boss is engagement
				// with it, whatever dealt it.
				const bool bConsequenceOfADeath = Data.EffectSpec.GetSetByCallerMagnitude(
					FName(UCataclysmSkillEffects::ConsequenceOfADeathDataName),
					/*WarnIfNotFound=*/false, 0.0f) > 0.0f;
				// NOR A BLOW A WORN ROW STRUCK IN ANSWER TO AN EVENT. Ruled 2026-10-07: it is
				// dealt while its wearer is acting on an event, when the wearer's
				// "first hit against each enemy" row cannot fire, so recording it would
				// use that first hit up for nothing. The stamp is
				// `UCataclysmSkillEffects::RowsAnswerToAnEventDataName`.
				const bool bARowsAnswerToAnEvent = Data.EffectSpec.GetSetByCallerMagnitude(
					FName(UCataclysmSkillEffects::RowsAnswerToAnEventDataName),
					/*WarnIfNotFound=*/false, 0.0f) > 0.0f;
				// NOR THE EXPLOSION AN AILMENT MADE WHEN IT ENDED BY TIME. Ruled 2026-10-13: it is not a blow
				// its applier struck, as an overkill explosion is not, so the applier's next real hit on a
				// character it caught is still its first. The stamp is
				// `UCataclysmSkillEffects::ExplosionAtAnAilmentsEndDataName`.
				const bool bExplosionAtAnAilmentsEnd = Data.EffectSpec.GetSetByCallerMagnitude(
					FName(UCataclysmSkillEffects::ExplosionAtAnAilmentsEndDataName),
					/*WarnIfNotFound=*/false, 0.0f) > 0.0f;
				// NOR A SHARE OF AN ALLY'S BLOW: its attacker struck the ally and not this character. Ruled 2026-10-15.
				// NOR A POINT-BLANK SKILL'S SHARE STRUCK AT THIS ALLY, which raises no `first_hit_dealt` (R6) and so
				// must not use that first hit up. Ruled 2026-10-15.
				UCataclysmAbilitySystemComponent* Struck =
					bConsequenceOfADeath || bARowsAnswerToAnEvent || bExplosionAtAnAilmentsEnd || bShareOfAnAllysDamage
						|| bAreaSkillShareOnAnAlly
					? nullptr
					: Cast<UCataclysmAbilitySystemComponent>(GetOwningAbilitySystemComponent());
				if (Struck)
				{
					bFirstFromAttacker = Struck->NoteStruckBy(
						UCataclysmTargeting::AbilitySystemOf(
							UCataclysmCombatEvents::AttackerOf(
								Data.EffectSpec.GetContext())),
						Outcome.bWasCritical);
				}

				// AND THE DEFENDER DEALS ITS RETALIATION BACK. Issue #895: the
				// `Retaliation` attribute existed, was clamped, was replicated,
				// was given to the Masochist by its class line at 158, and no
				// code in the project read it.
				//
				// A FLAT AMOUNT, NOT A SHARE OF THE HIT. The class stat table
				// writes it as a bare 158 while writing damage reduction as "8%"
				// and life leech as "3%", so the table already says which of the
				// two it is. Diablo IV's Thorns is flat in the same way.
				//
				// ONLY WHEN SOMETHING GOT THROUGH, so a hit that was evaded, or
				// that armour and resistance stopped completely, provokes
				// nothing. That is the branch this sits in.
				//
				// NOT ON A DAMAGE OVER TIME TICK. Path of Exile, Diablo IV and
				// Last Epoch all agree that reflection answers a hit rather than
				// a tick, and a burn ticking once a second against a retaliating
				// target would otherwise be a second, silent source of damage.
				//
				// NOT ITSELF A HIT, which is what stops two retaliating
				// characters reflecting at one another without end.
				// ReduceHealthDirectly writes to the Health attribute rather
				// than to the Damage meta attribute, so none of the mitigation
				// order runs and nothing here is reached a second time.
				//
				// AND A MINION'S BLOW PROVOKES NONE. It is credited to its
				// summoner, so without the check a Ritualist standing at range
				// would take this every time one of its imps struck.
				if (!Hit.bIsDamageOverTime
					&& !AssetTags.HasTag(
						UCataclysmDamageCalculation::NoRetaliationTag()))
				{
					// HOW MUCH, AT WHOM, AND WHAT COMES BACK FOR IT ALL MOVED TO
					// `UCataclysmRetaliation`. Issues #1047 and #1048. This
					// function still decides WHETHER to retaliate, because it is
					// the only place that knows a hit got through; two Masochist
					// capstone options change the other three, and neither could
					// be checked by passing numbers in while all four lived
					// inside a six hundred line function.
					//
					// WHAT `Pay` STILL DOES UNCHANGED for every character in the
					// game today: it asks for the figure rather than reading the
					// attribute, because a bonus whose SIZE grows with a state --
					// Reciprocity gives "+1% for each point of Fervour you
					// currently hold" -- is never folded into a gameplay
					// attribute and would be dropped in silence (issue #980); it
					// asks the DEFENDER'S ability system, because this is the
					// defender's retaliation; it passes NO SKILL TAGS, because
					// `AssetTags` belongs to the blow that came IN and
					// retaliation is not a skill; and it falls back to the
					// attribute, so a character with no such bonus gets exactly
					// what it got before.
					//
					// THE CHARACTER DEALS IT, NOT THE SET'S OWNER. Issue #1755,
					// ruled 2026-09-25. This passed `GetOwningActor()` "which is
					// what it was before this moved", and for a player that is
					// the player state: the returned blow was dealt by it, the
					// kill was credited to it, and the creature measured the blow
					// from the world's origin. Every other blow a player deals
					// names the character, so this does too. `Pay` still finds
					// the avatar separately for the area search.
					// `Hit.Damage` AND NOT WHAT LANDED, since issue #1227. The
					// share is taken of what the blow was worth before this
					// character's own armour, resistance and reductions, so a
					// well-defended Masochist retaliates for as much as a poorly
					// defended one struck by the same blow. Using what landed
					// would punish the class for its own defences.
					UCataclysmRetaliation::Pay(
						GetOwningAbilitySystemComponent(), CataclysmDefendingBody(*this),
						Data.EffectSpec.GetContext().GetEffectCauser(),
						Hit.Damage);
				}

				// AND ANY RUNNING BUFF THAT REACTS TO A BLOW TAKEN IS TOLD, AND
				// BY WHOM. The Greataxe's Burning Wrath: "while it lasts, any
				// enemy that strikes you in melee is set alight." That sentence
				// had no hook at all until 2026-09-02, which is why the row had
				// carried `Burn=1` since it was written and had never set
				// anything alight. Issue #1157.
				//
				// HERE BECAUSE THIS IS THE ONE PLACE EVERY INCOMING BLOW IN THE
				// GAME IS RESOLVED, which is the same argument that put
				// retaliation directly above it. A strike, a projectile, a
				// minion's swing and an enemy ability all arrive at this
				// function.
				//
				// IN THE SAME BRANCH AS RETALIATION, so a blow that was evaded,
				// or that armour and resistance stopped completely, tells nobody
				// anything. "Strikes you" is not "swings at you", and it is the
				// rule the attacking side's `NoteBlowLanded` already follows.
				//
				// OUTSIDE THE RETALIATION TEST, THOUGH, AND THAT IS DELIBERATE.
				// A damage over time tick and a blow carrying the no-retaliation
				// tag both still happened to the defender; whether they count is
				// the buff's business, and it is handed what it needs to decide.
				//
				// AND HOW MUCH REACHED HEALTH GOES WITH IT, added 2026-09-02 for
				// the Fist's Living Pyre: "returns health equal to 25% of the
				// damage that hit dealt". Issue #1162.
				//
				// `Outcome.DealtToHealth` AND NOT WHAT WAS SENT, which is the
				// same figure the design defines leech against. It is read here
				// rather than by the skill because by the time a skill could
				// look, the health has already moved and the amount is gone.
				//
				// BEFORE THE ENERGY SHIELD IS SUBTRACTED BELOW AND THAT CHANGES
				// NOTHING: `DealtToHealth` is already what is left once the
				// shield has absorbed its share. `Outcome.AbsorbedByShield` is
				// the other part and is deliberately not counted -- a blow the
				// shield swallowed dealt nothing to health.
				//
				// NOT FOR A SHARE OF AN ALLY'S BLOW, which tells no running buff of this character's anything. Ruled
				// 2026-10-15.
				if (!bShareOfAnAllysDamage)
				{
					UCataclysmSkillTemplate::NoteBlowTaken(
						GetOwningActor(),
						Data.EffectSpec.GetContext().GetEffectCauser(),
						Hit.bIsMelee, Hit.bIsDamageOverTime,
						Outcome.DealtToHealth);
				}
			}

			// A BLOW THAT BREAKS THE ENERGY SHIELD, defined as the Sacrificial
			// Ward above defines "would break": the blow's shield share reaches
			// what the shield holds, while it holds something. Issue #1833 group D
			// part 3, ruled 2026-10-01: "When your energy shield is broken, you
			// smite all nearby enemies". Only a blow breaks it; a drain or a
			// reservation that empties the shield does not. Read before the write
			// below and raised further down, once the blow is wholly resolved.
			const float ShieldBeforeBlow = GetEnergyShield();
			const bool bBrokeShield = ShieldBeforeBlow > 0.0f
				&& Outcome.AbsorbedByShield >= ShieldBeforeBlow;

			// AND WHAT THE TEMPORARY ABSORB TOOK IS TAKEN FROM IT, first, as it was
			// decided first: step 6b of `UCataclysmDamageCalculation::Resolve`. The
			// project owner, 2026-10-07: it is separate from the energy shield. So the
			// break above does not read it, nor the Sacrificial Ward, nor the store
			// `NoteShieldAbsorbedDamage` fills further down.
			if (Outcome.AbsorbedByTemporary > 0.0f)
			{
				if (UCataclysmAbilitySystemComponent* AbsorbHolder =
						Cast<UCataclysmAbilitySystemComponent>(
							GetOwningAbilitySystemComponent()))
				{
					AbsorbHolder->SpendTemporaryAbsorb(Outcome.AbsorbedByTemporary);
				}
			}

			if (Outcome.AbsorbedByShield > 0.0f)
			{
				SetEnergyShield(FMath::Clamp(
					GetEnergyShield() - Outcome.AbsorbedByShield,
					0.0f, MaximumEnergyShieldAsked()));
			}
			// AND WHAT THE MANA TOOK IS TAKEN FROM THE MANA. Ruled 2026-10-06:
			// "DoTs deal damage to your mana pool first". Nought for every blow
			// but a damage over time tick on a character carrying
			// `damage_over_time_taken_from_mana_first`; see step 7 of
			// `UCataclysmDamageCalculation::Resolve`. This is the only damage a
			// mana pool takes. Regeneration is not touched, and a skill whose
			// cost the mana left cannot pay is refused as for any character
			// short of mana.
			if (Outcome.AbsorbedByMana > 0.0f)
			{
				SetMana(FMath::Clamp(GetMana() - Outcome.AbsorbedByMana,
									 0.0f, GetMaxMana()));
			}
			// AND SOME OF IT MAY NOT REACH HEALTH AT ALL, ARRIVING AS BLEEDING
			// INSTEAD. Issue #985, the Masochist's The Breaking Point. What is
			// converted is NOT lessened -- the same total arrives, spread over
			// five seconds -- so this subtracts rather than reduces, and a
			// character with no window open converts nothing and loses nothing.
			//
			// BEFORE THE SHIELD? NO, AFTER IT. `Outcome.DealtToHealth` is what
			// is left once the energy shield has absorbed its share, and the
			// node says it converts the damage you TAKE. A Masochist has no
			// energy shield by design -- `sim/cataclysm_sim/classes.py` says
			// zero deliberately, because "a shield absorbs the damage the class
			// needs to convert" -- so the order is stated rather than felt.
			const UAbilitySystemComponent* Defending =
				GetOwningAbilitySystemComponent();
			const float TurnedIntoBleeding =
				UCataclysmDamageConversion::ConvertIfActive(
					Defending ? Defending->GetAvatarActor() : nullptr,
					Outcome.DealtToHealth, Hit.bIsDamageOverTime);

			// A LOCAL RATHER THAN CHANGING `Outcome`, WHICH IS CONST ON PURPOSE.
			// What the damage calculation resolved is a record of the hit and
			// everything below reads it; the conversion is a later step that
			// decides how much of that record reaches health, not a correction
			// to it. The Fervour line further down deliberately still counts
			// what reached health, which is what this leaves behind.
			const float Arriving = Outcome.DealtToHealth - TurnedIntoBleeding;

			// SACRIFICIAL BOND: "All damage taken is redirected and divided among
			// nearby allies." Issue #1559, ruled 2026-09-30. The creature keeps
			// its share and the rest goes to the allies below, after its own
			// health is written. HERE ONLY, in the damage branch: the allies'
			// shares arrive as direct reductions, which never come back through
			// this branch, so an ally that is bonded too cannot pass it on again.
			//
			// `Outcome` IS NOT CHANGED, so whoever struck leeches and is told of
			// the whole blow; the blow happened in full, and the bond decides only
			// whose health pays for it.
			//
			// THE WHOLE BLOW IS DIVIDED, BEFORE IT IS CUT TO THE HEALTH LEFT. Issue #2289. The project owner,
			// 2026-10-08, asked "should a large enough blow kill a creature under Sacrificial Bond?": "yes". The
			// rest is a labelled judgement by the coordinating session under the owner's delegation, 2026-10-08.
			// `Outcome.DealtToHealth` is the blow already cut to this creature's health and `Outcome.Overkill` is
			// what was cut off, so the two add up to what reached the health step: `Whole`. Until this, the share
			// was taken of the cut figure, which is never more than the health held, so no blow killed a bonded
			// creature with an ally in reach. Now the creature keeps its share of the whole, and what reaches its
			// health is the smaller of that and its health. It dies when its share of the whole blow is at least
			// its remaining health.
			//
			// A BLOW UNDER ITS HEALTH HAS NO OVERKILL, so it is divided exactly as it was. A character with nobody
			// to share with keeps the whole, and the smaller of the whole and its health is `Arriving` again, so
			// nothing changes for it either. A tick of damage over time comes through this same branch.
			//
			// THE CONVERSION TO BLEEDING ABOVE IS NOUGHT FOR A CREATURE, as read: its window opens only for a
			// character whose stat line carries `damage_to_bleeding_on_low_health`, a creature is never given a
			// stat line, and the attribute behind it is on a set only the player's state holds. And it converts a
			// blow whole or not at all, when `Arriving` is nought and nothing is shared. So `Whole` takes nothing
			// off for it. A conversion of part of a blow, or one a creature could hold, would need a ruling on
			// what `Whole` is.
			//
			// AND A SHARE OF WHAT WOULD REACH HEALTH MAY BE TAKEN LATER INSTEAD. Ruled 2026-10-12 under the owner's
			// delegation, for "25%-50% of damage taken is dealt over 4-8 seconds". The share is taken off HERE, of
			// what is about to be written to health, so mana, the energy shield and the temporary absorb have had
			// theirs and none of what they took is delayed. It is held in a store on the character and drained by
			// `UCataclysmDelayedDamage::DrainStep`.
			//
			// OF THE BLOW BEFORE IT IS CUT TO THE HEALTH LEFT, which is `Arriving` and the overkill together, as
			// the bond below reads it. So the share is taken first, and a blow that would have emptied health can
			// leave the character alive, holding a store that then empties it. Ruled.
			//
			// AFTER THE CONVERSION TO BLEEDING, AND A BLOW THAT CONVERSION TOOK IS NOT DELAYED. While The Breaking
			// Point's window is open the whole blow has become a Bleed, `Arriving` is nought and nothing is asked.
			// The Bleed's ticks are ticks, which are never delayed. A judgement of the writing session. The option
			// not taken: take the share first and turn only the rest into bleeding. It was not taken because that
			// conversion turns a blow whole or not at all and every line below is written on that.
			//
			// AFTER THE TWO SAVES FROM A LETHAL BLOW, WHICH SIT ABOVE AND HAVE ALREADY ASKED WHAT THIS SHARE WILL BE.
			// Until 2026-10-13 they read the blow before any share was taken, so a blow whose whole figure reached
			// the health held was saved, and its save spent, though with the share taken off it would not have
			// killed. Ruled that day: a save judges the blow less this share. See
			// `EmptiesHealthLessTheDelayedShare` above. AND NOTHING OF A BLOW A SAVE DID CUT IS DELAYED, which is
			// what `bSavedFromALethalBlow` says: the save leaves its holder the one point and no store.
			//
			// `Outcome` IS NOT CHANGED, as for the bond: whoever struck leeches from the whole blow and is told of
			// the whole blow, and the number drawn over the character is the whole blow.
			//
			// NOUGHT FOR A TICK AND FOR A CHARACTER WITH NO SUCH ROW, so every line below reads what it read.
			const float WholeBeforeDelay = Arriving + Outcome.Overkill;
			const float Delayed = Arriving > 0.0f && !bSavedFromALethalBlow
				? UCataclysmDelayedDamage::DelayShare(
					GetOwningAbilitySystemComponent(), WholeBeforeDelay, Hit.bIsDamageOverTime)
				: 0.0f;

			// THE BOND DIVIDES WHAT IS LEFT ONCE THE SHARE IS TAKEN. No character holds both today: a creature
			// wears no row.
			TArray<AActor*> Sharing;
			const float Whole = WholeBeforeDelay - Delayed;
			const float KeptOfWhole = Arriving > 0.0f
				? Whole * UCataclysmEnemyModifiers::ShareOfDamageKept(GetOwningActor(), &Sharing)
				: Arriving;
			const float ToHealth = Arriving > 0.0f ? FMath::Min(KeptOfWhole, GetHealth()) : Arriving;

			// A BLOW THAT WOULD KILL A CREATURE IN AN UNHOLY SIGIL DOES NOT.
			// Issue #1559. `PreAttributeChange` holds it at one health; this is
			// what tells the announcement below and the base write further down.
			const bool bWouldKill = ToHealth > 0.0f && ToHealth >= GetHealth();
			const bool bHeldBySigil = bWouldKill
				&& UCataclysmEnemyModifiers::IsProtectedBySigil(GetOwningActor());

			// ANNOUNCED HERE, WHERE THE BLOW'S WHOLE OUTCOME IS KNOWN, AND BEFORE
			// HEALTH IS WRITTEN. Issue #41, slice 4. Every resolved blow reaches
			// this line, evaded and blocked ones included. The death the write
			// below may cause reads the record this leaves, so it must come
			// first. `NoteBlow` builds nothing when nothing listens.
			//
			// AND WHAT THE ANNOUNCEMENT ASKED OF THIS CHARACTER IS TAKEN WHEN IT RETURNS. Ruled 2026-10-12: a row
			// of the attacker's that executes the enemy a hit leaves below a share of its health hears the hit
			// inside `NoteBlow`, before the write below, and leaves its request on this character's ability system.
			// See `UCataclysmAbilitySystemComponent::AskToBeExecuted`. WHAT WAS ASKED BEFORE THIS HIT IS SET ASIDE
			// AND PUT BACK, so this hit acts only on its own announcement's request: a hit dealt to this character
			// from inside another hit's announcement reaches these lines while the outer hit's request is waiting.
			UCataclysmAbilitySystemComponent* Executable =
				Cast<UCataclysmAbilitySystemComponent>(GetOwningAbilitySystemComponent());
			const UCataclysmAbilitySystemComponent::FExecuteAsked AskedBeforeThisHit =
				Executable ? Executable->TakeExecuteAsked() : UCataclysmAbilitySystemComponent::FExecuteAsked();
			// AND A CULL'S REQUEST, THE SAME WAY, beside the execute's and not merged with it. Ruled 2026-10-19 (C1), for
			// Reaper's Embrace's ten-piece. See `UCataclysmAbilitySystemComponent::AskToBeCulled`.
			const UCataclysmAbilitySystemComponent::FExecuteAsked CullBeforeThisHit =
				Executable ? Executable->TakeCullAsked() : UCataclysmAbilitySystemComponent::FExecuteAsked();
			UCataclysmCombatEvents::NoteBlow(
				Data, Hit, Outcome, AssetTags,
				/*bLethal=*/bWouldKill && !bHeldBySigil, bFirstFromAttacker);
			const UCataclysmAbilitySystemComponent::FExecuteAsked AskedByThisHit =
				Executable ? Executable->TakeExecuteAsked() : UCataclysmAbilitySystemComponent::FExecuteAsked();
			const UCataclysmAbilitySystemComponent::FExecuteAsked CullByThisHit =
				Executable ? Executable->TakeCullAsked() : UCataclysmAbilitySystemComponent::FExecuteAsked();
			if (Executable)
			{
				Executable->PutBackExecuteAsked(AskedBeforeThisHit);
				Executable->PutBackCullAsked(CullBeforeThisHit);
			}

			// "YOU TAKE 5%-15% DAMAGE DONE TO NEARBY ALLIES": THE WEARERS BESIDE THIS CHARACTER ARE FOUND NOW, BEFORE THE
			// WRITE BELOW, and paid below the bond, once everything this blow does to this character is done. Ruled
			// 2026-10-15 (R2 to R5). Found first because the write may kill this character, and a minion that dies may
			// leave the world inside the write. NOBODY IS SEARCHED FOR when nothing is about to reach health, for a share
			// of an ally's blow, which is never shared again (R7), and by `WearersBeside` for a wearer struck (R4) or a
			// character not of the players' team. THE SHARE IS OF WHAT THE WRITE REALLY TAKES (R5): measured across it,
			// so an overkill, a sigil's hold and the delayed share are all left out, and nothing the execute below
			// takes afterwards is in it (R6).
			const TArray<AActor*> WearersToShareWith = ToHealth > 0.0f && !bShareOfAnAllysDamage
				? UCataclysmAllyDamageShare::WearersBeside(CataclysmDefendingBody(*this), GetOwningAbilitySystemComponent())
				: TArray<AActor*>();
			const float HealthBeforeTheWrite = GetHealth();
			float TakenFromHealth = 0.0f;

			if (ToHealth > 0.0f)
			{
				SetHealth(FMath::Clamp(GetHealth() - ToHealth,
									   0.0f, GetMaxHealth()));

				// AND THE BASE IS PUT BACK WHERE THE SIGIL HELD THE CURRENT VALUE.
				// `PreAttributeChange` clamps only the current value; the base is
				// stored as written, which here is zero, and the next gain would add
				// to that. The same write-back the health branch below makes.
				if (bHeldBySigil)
				{
					SetHealth(GetHealth());
				}
				TakenFromHealth = FMath::Max(0.0f, HealthBeforeTheWrite - GetHealth());
				NotifyIfHealthReachedZero();
				NotifyHealthChanged();

				// AND THE EXECUTE THIS HIT'S ANNOUNCEMENT ASKED FOR, NOW THAT THE HIT'S DAMAGE IS WRITTEN. Ruled
				// 2026-10-12. AFTER THE DEATH CHECK ABOVE, so a hit that killed leaves nothing to execute:
				// `ExecuteIfLeftBelow` refuses the dead. It judges the share on the health this write left, and
				// takes what is left as a direct loss, which comes back through the health branch at the end of
				// this function and not through this one. ONLY INSIDE THIS BLOCK: a hit that wrote nothing to
				// health executes nobody.
				if (Executable && AskedByThisHit.IsAsked())
				{
					Executable->ExecuteIfLeftBelow(AskedByThisHit);
				}
				// AND THE CULL THIS HIT ASKED FOR, AFTER THE EXECUTE, on the same health the write left (C4). Ruled
				// 2026-10-19, for Reaper's Embrace's ten-piece. An execute that killed leaves a dead character, which
				// `KillIfLeftBelow` refuses, so an execute row acts and starts its cooldown exactly as before (C1).
				if (Executable && CullByThisHit.IsAsked())
				{
					Executable->CullIfLeftBelow(CullByThisHit);
				}

				// AND HEALTH LOST TO DAMAGE FILLS FERVOUR. Issue #954. The
				// Masochist's starting node states the rule -- 1 Fervour per 1%
				// of maximum health lost -- and a character that has not spent a
				// point on a generator has a rate of zero, so this costs nothing
				// for everybody else.
				//
				// WHAT REACHED HEALTH, NOT WHAT THE HIT WAS WORTH, and the
				// difference is a design position rather than an accident.
				// `docs/Cataclysm_GDD_v2.md` says an energy shield "absorbs the
				// damage the class needs to convert", so a shield on a Masochist
				// is a straight loss of resource generation. Putting the whole
				// hit here instead would quietly delete that.
				//
				// THE HIT'S TAGS GO WITH IT, because the tree has a node about
				// Fervour gained from damage OVER TIME specifically, and the tag
				// on the effect is what tells the two apart.
				//
				// WHAT REACHED HEALTH AFTER THE CONVERSION, AND TODAY THAT IS
				// THE SAME NUMBER. Issue #985.
				//
				// SAYING SO PLAINLY, BECAUSE A GUARD PROOF CAUGHT THE CLAIM THAT
				// IT WAS NOT. The Breaking Point converts a blow whole or not at
				// all, so `TurnedIntoBleeding` is either zero or the entire
				// amount -- and when it is the entire amount, `ToHealth` is zero
				// and this whole block is skipped, Fervour included. There is no
				// case today where the two expressions differ HERE, and breaking
				// this line to the other one failed no test at all.
				//
				// IT IS STILL THE RIGHT EXPRESSION. It says what it means -- the
				// bar is filled by health actually lost -- and it is what a
				// PARTIAL conversion would need if one is ever added. What it is
				// not is a fix for a live defect, and calling it one would put a
				// false story in this file.
				//
				// THE FERVOUR A CONVERTED BLOW IS WORTH STILL ARRIVES, a tick at
				// a time, as the Bleeding takes health through this same path.
				//
				// SINCE 2026-10-12 THERE IS A CASE WHERE THE TWO EXPRESSIONS DIFFER HERE. A worn row can take
				// a share of a blow off above and hold it (`UCataclysmDelayedDamage::DelayShare`), and
				// `ToHealth` is then less than what the blow resolved to. Fervour is filled by what was taken
				// at once. THE HELD SHARE FILLS NONE WHEN IT IS DRAINED: the drain writes health directly and
				// does not come through this path.
				//
				// AND A SHARE OF AN ALLY'S BLOW FILLS NONE (R7), ruled 2026-10-15: it is health lost to a blow this
				// character did not take.
				if (!bShareOfAnAllysDamage)
				{
					UCataclysmFervour::GainFromDamage(
						GetOwningAbilitySystemComponent(), ToHealth, AssetTags);
				}
			}

			// THE BONDED CREATURE'S ALLIES PAY THE REST, EVENLY. Issue #1559.
			//
			// EACH IS TOLD WHO STRUCK FIRST, so a death this causes credits
			// whoever landed the blow: on-kill effects read the victim's last
			// blow, and a direct reduction records none. Copied from the record
			// `NoteBlow` just left on this creature, with the killing tags added
			// when the share is enough to kill. So Follow Through counts a
			// redirected melee kill as the player's own, a judgement the
			// coordinating session ruled: the player's melee blow caused it.
			//
			// AS A DIRECT REDUCTION, which the ally's armour and resistances do
			// not meet again: the blow was already resolved against this
			// creature's. Dealt by this creature, which always has an ability
			// system to send it from; who struck is carried by the record.
			//
			// OF THE WHOLE BLOW, LESS THE SHARE THE BONDED CREATURE KEPT, AND NOT LESS WHAT ITS HEALTH TOOK. Issue
			// #2289, ruled 2026-10-08. So an ally can take more than the bonded creature had left: a blow of 1,000
			// on a bonded creature with 100 health and one ally kills it and takes 500 from the ally. It follows
			// from the row's own sentence. Paying the allies from the cut figure instead would give them nothing
			// for a killing blow and half for a slightly smaller one.
			//
			// THE BONDED CREATURE MAY HAVE DIED ON THE WRITE ABOVE, which no blow could do before issue #2289. Its
			// allies are still paid, and still from it, as read: `ReduceHealthDirectly` asks for the sender's
			// ability system and not whether the sender lives, and a dead creature leaves the world on a timer and
			// not at once.
			if (!Sharing.IsEmpty() && Whole > KeptOfWhole)
			{
				const float EachShare = (Whole - KeptOfWhole) / static_cast<float>(Sharing.Num());
				const UCataclysmAbilitySystemComponent* Bonded =
					Cast<UCataclysmAbilitySystemComponent>(GetOwningAbilitySystemComponent());
				for (AActor* Ally : Sharing)
				{
					if (UCataclysmAbilitySystemComponent* Theirs = Cast<UCataclysmAbilitySystemComponent>(
							UCataclysmTargeting::AbilitySystemOf(Ally)))
					{
						FCataclysmLastBlow Blow = Bonded ? Bonded->GetLastBlow() : FCataclysmLastBlow();
						// AND NO OVERKILL COMES WITH THE COPY. A labelled judgement by the coordinating session under the
						// owner's delegation, 2026-10-08: the figure is another creature's. Since issue #2289 the blow can
						// kill the bonded creature, and its record then holds how far the blow went past ITS health. An
						// ally its share kills would carry that figure on its death, and a body is exploded for the
						// overkill on its death notice. So an ally killed by its share still explodes for nought.
						Blow.Overkill = 0.0f;
						Blow.KillingTags = EachShare >= Theirs->GetNumericAttribute(GetHealthAttribute())
							? AssetTags
							: FGameplayTagContainer();
						Theirs->RecordLastBlow(MoveTemp(Blow));
					}
					UCataclysmSkillEffects::ReduceHealthDirectly(GetOwningActor(), Ally, EachShare);
				}
			}

			// AND EACH WEARER FOUND ABOVE TAKES ITS SHARE OF WHAT THE WRITE TOOK, as a blow of its own (R7), in the name
			// of whoever struck this character and of the type this blow was met by. Ruled 2026-10-15. After the bond,
			// so the shares the bond pays its allies are already paid, as direct losses no wearer sees (R6).
			UCataclysmAllyDamageShare::PayShares(
				WearersToShareWith, Data.EffectSpec.GetContext(), TakenFromHealth, Hit.DamageType);

			// AND A HIT OF A CATACLYSM TYPE THIS CHARACTER DOES NOT SHARE
			// OPENS A WINDOW A PASSIVE NODE CAN READ. Issue #975. The
			// Masochist's Cataclysmic Resonance grants "+1% increased damage
			// per point for 5 seconds after you take damage of a Cataclysm
			// type other than Demonic", and nothing recorded that anything of
			// the sort had happened.
			//
			// WHAT REACHED THE CHARACTER, NOT WHAT WAS AIMED AT IT. A blow
			// that was evaded or wholly mitigated removed nothing, and is not
			// damage taken. A shield absorbing it IS damage taken, which is
			// why both figures count here -- unlike Fervour above, where the
			// design says a shield is a straight loss of generation.
			//
			// OTHER THAN THE CHARACTER'S OWN TYPE, read from its weapon. An
			// untyped hit opens nothing, which is what keeps a player's own
			// damage -- untyped by the decision of 2026-08-12 -- from opening
			// this every time it retaliates.
			//
			// NOR BY A SHARE OF AN ALLY'S BLOW, which opens no window of this character's (R7). Ruled 2026-10-15, and
			// read by each of the windows, the counts, the stacks and the two modifiers below.
			if (Outcome.DealtToHealth + Outcome.AbsorbedByShield
					+ Outcome.AbsorbedByTemporary > 0.0f
				&& !Hit.DamageType.IsNone() && !bShareOfAnAllysDamage)
			{
				// THE BODY'S WEAPON, NOT THE SET OWNER'S. Issue #1755. A player's
				// weapon slots are on its character, and asked of the player state
				// this read "" for every player, so a hit of the player's own type
				// opened the foreign-damage window too.
				const FString Own = UCataclysmWeaponSlotsComponent::DamageTypeOf(
					CataclysmDefendingBody(*this));
				if (!Hit.DamageType.ToString().Equals(
						Own, ESearchCase::IgnoreCase))
				{
					if (UCataclysmAbilitySystemComponent* Cataclysm =
							Cast<UCataclysmAbilitySystemComponent>(
								GetOwningAbilitySystemComponent()))
					{
						Cataclysm->NoteForeignDamageTaken();
					}
				}
			}

			// AND A BLOW THIS CHARACTER BLOCKED OPENS A WINDOW AN ENCHANTMENT
			// CAN READ. Issue #1826. "Blocking an attack grants 10%-20%
			// increased damage for 3 seconds" is the row.
			//
			// A SEPARATE BRANCH FROM THE WINDOW ABOVE, AND NOT NESTED IN IT,
			// because the two ask different questions and the answers come
			// apart in both directions. That one wants a Cataclysm type the
			// character does not share, whatever became of the blow; this one
			// wants a block, whatever type it was. A blocked hit of a foreign
			// type opens both, an unblocked foreign one opens only that, and a
			// blocked hit of the character's own type opens only this.
			//
			// WHAT WAS BLOCKED, NOT WHAT GOT THROUGH, which is the one place
			// this differs from its neighbour. That branch is gated on health
			// or shield actually losing something; this is not, because the
			// sentence says "blocking an attack" and says nothing about what
			// survived. A block that stopped the blow completely still opens
			// the window, and this line is reachable for such a blow: the
			// comment on the energy shield's refill wait above records that an
			// evaded blow reaches here too.
			if (Outcome.bBlocked && !bShareOfAnAllysDamage)
			{
				if (UCataclysmAbilitySystemComponent* Cataclysm =
						Cast<UCataclysmAbilitySystemComponent>(
							GetOwningAbilitySystemComponent()))
				{
					// WITH THE ATTACKER AND WHAT THE BLOCK REMOVED, since issue
					// #1833 group E part 3: "Reflect 20%-100% of damage blocked".
					Cataclysm->NoteBlocked(
						UCataclysmCombatEvents::AttackerOf(Data.EffectSpec.GetContext()),
						Outcome.DamageBlocked);
				}
			}

			// AND A BROKEN SHIELD IS ANNOUNCED, read at the shield's write above.
			// Issue #1833 group D part 3.
			if (bBrokeShield && !bShareOfAnAllysDamage)
			{
				if (UCataclysmAbilitySystemComponent* Cataclysm =
						Cast<UCataclysmAbilitySystemComponent>(
							GetOwningAbilitySystemComponent()))
				{
					Cataclysm->NoteEnergyShieldBroken();
				}
			}

			// AND TWO MORE WINDOWS ON THE SAME RESOLVED BLOW. Issue #1815.
			// "When you dodge an attack gain 15%-30% increased damage for 3
			// seconds" and "Taking a hit reduces your damage by 5%-10% for 3
			// seconds" are the rows.
			//
			// THREE SEPARATE BRANCHES ON ONE BLOW, NOT A CHAIN, because the
			// three questions come apart in every direction. A blow is evaded
			// or not, blocked or not, and a hit either way. The evade and the
			// block are exclusive of each other in practice -- an evaded blow
			// was never blocked -- but nothing here relies on that, so a future
			// rule that makes both true at once does not need this code changed.
			//
			// AND `NoteHitTaken` DELIBERATELY OVERLAPS BOTH. Every blow that
			// reaches this point was a hit, whatever became of it, so it fires
			// for blocked blows, evaded ones, and blows that dealt nothing.
			// That is what its row says and it is the one window here that is
			// not exclusive.
			//
			// NEITHER IS GATED ON DAMAGE GETTING THROUGH, for the reason the
			// block stamp above is not: an evaded blow deals nothing by
			// definition, so gating it would mean it never fired at all.
			//
			// NONE OF THEM FOR A SHARE OF AN ALLY'S BLOW (R7): no `hit_taken`, no Nth-hit count, no evade window, no
			// immunity roll, no combat clock for its attacker, nothing stored for Nothing Wasted or the shield's store.
			if (UCataclysmAbilitySystemComponent* Cataclysm = bShareOfAnAllysDamage ? nullptr :
					Cast<UCataclysmAbilitySystemComponent>(
						GetOwningAbilitySystemComponent()))
			{
				if (Outcome.bEvaded)
				{
					// WITH WHO THREW THE BLOW AND WHETHER IT WAS MELEE, ruled
					// 2026-10-07: "When you evade a ranged attack, throw an attack
					// dealing 20-70% of your attack damage at that enemy". The same
					// `AttackerOf` the block above passes, so a minion's blow names
					// the minion, or its summoner when the summoner holds Conduit.
					Cataclysm->NoteEvaded(
						UCataclysmCombatEvents::AttackerOf(Data.EffectSpec.GetContext()),
						Hit.bIsMelee);
				}

				// AND WHETHER IT LANDED, which only a row's own stack asks. Issue
				// #1833: an evaded blow is not a hit (2026-09-04), and the clocks
				// and pool actions go on counting it as they always did.
				//
				// AND WHO THREW IT AND WHETHER IT WAS A TICK, ruled 2026-10-08 for
				// `hit_taken_cancels_skills`: a landed hit cuts short the skills
				// its wearer is using, and neither a tick nor the wearer's own
				// blow does. A TICK REACHES THIS LINE, as the count just below
				// says by refusing one, so it is said here and not assumed. The
				// same `AttackerOf` the evade above passes.
				Cataclysm->NoteHitTaken(
					!Outcome.bEvaded,
					UCataclysmCombatEvents::AttackerOf(Data.EffectSpec.GetContext()),
					Hit.bIsDamageOverTime);

				// AND AN "EVERY Nth HIT YOU TAKE" ROW COUNTS IT, ruled 2026-09-24:
				// a landed hit, not a tick and not an evaded blow; a blocked blow
				// landed. After `NoteHitTaken`, so the combat it opens is the one
				// the count belongs to. Issue #1833, phase 2.
				if (!Outcome.bEvaded && !Hit.bIsDamageOverTime)
				{
					Cataclysm->NoteNthEvent(ECataclysmEveryNth::HitTaken);
				}

				// AND A HIT THAT WAS TAKEN MAY MAKE ITS WEARER IMMUNE TO ITS DAMAGE TYPE FOR A TIME. Ruled
				// 2026-10-12 under the owner's delegation, for "Shard of Anarchy (6-Piece Bonus): When you take
				// damage, you have a 10% chance to become immune to the damage type that hit you for 5 seconds".
				// The chance and the seconds are the row's two stats; see `RollDamageTypeImmunityOnHit`.
				//
				// HERE, AFTER THE HEALTH IS WRITTEN, so the hit that rolls is taken in full and the window covers
				// what comes after it. And here rather than on the event `hit_taken`, which carries no damage
				// type: the type is in hand in this function and nowhere after it.
				//
				// A HIT ONLY, AND ONE THAT DEALT SOMETHING. Not a damage over time tick, as ruled. Not an evaded
				// blow, which did not hit. Not a hit with no type, which has no type to become immune to. Not a
				// hit that dealt nothing to health and nothing to a pool that absorbs: one stopped whole by a
				// block, by armour and resistance, by a row's absorb or reflect, by the no-damage window or by
				// this type's own window. THE SAME FOUR FIGURES the block above reads for "anything that got
				// through", so a hit the energy shield or the temporary absorb took whole rolls: its wearer was
				// struck and something it held was spent.
				//
				// NOT A HIT THAT KILLED. A judgement by the writing session: the death has been announced above
				// by the time this line runs, and a window opened now would be opened on a dead character.
				if (!Outcome.bEvaded && !Hit.bIsDamageOverTime && !Hit.DamageType.IsNone() && GetHealth() > 0.0f
					&& (Outcome.DealtToHealth > 0.0f || Outcome.AbsorbedByShield > 0.0f
						|| Outcome.AbsorbedByMana > 0.0f || Outcome.AbsorbedByTemporary > 0.0f))
				{
					Cataclysm->RollDamageTypeImmunityOnHit(Hit.DamageType);
				}

				// AND WHOEVER DEALT IT IS IN COMBAT TOO. Issue #1815. The same
				// blow and the same `AttackerOf` every other credit uses, so a
				// minion's blow puts the minion in combat, not its summoner,
				// unless the summoner holds Conduit.
				if (UCataclysmAbilitySystemComponent* Dealer =
						Cast<UCataclysmAbilitySystemComponent>(
							UCataclysmTargeting::AbilitySystemOf(
								UCataclysmCombatEvents::AttackerOf(
									Data.EffectSpec.GetContext()))))
				{
					Dealer->NoteHitDealt();
				}

				// AND A MELEE ONE OPENS ITS OWN WINDOW. Issue #1815: "After being
				// hit by a melee attack you gain 10%-20% increased movement speed
				// for 2 seconds". The same reading as `NoteHitTaken` beside it,
				// evaded and blocked blows included, narrowed to the blows the
				// resolved hit already calls melee.
				//
				// AND WHO STRUCK, for a row placing stacks on the attacker. Issue
				// #1833, phase 2. Not for a tick, which is not a hit (ruled
				// 2026-09-24): with no attacker named it places nothing, and every
				// other row on this event reads it as it always did.
				if (Hit.bIsMelee)
				{
					Cataclysm->NoteMeleeHitTaken(!Outcome.bEvaded,
						Hit.bIsDamageOverTime
							? nullptr
							: UCataclysmCombatEvents::AttackerOf(
								  Data.EffectSpec.GetContext()));
				}

				// AND A LANDED MELEE HIT COUNTS TOWARD RENDING BLOWS. Issue
				// #1515. Landed only: an evaded swing neither advances the count
				// nor resets it, and a tick is not an attack. A minion's blow
				// carries no melee tag, so it never counts. After the blow is
				// resolved, so the third hit is not itself reduced.
				if (Hit.bIsMelee && !Hit.bIsDamageOverTime && !Outcome.bEvaded)
				{
					Cataclysm->NoteLandedMeleeHitFrom(
						Cast<UCataclysmAbilitySystemComponent>(
							UCataclysmTargeting::AbilitySystemOf(
								UCataclysmCombatEvents::AttackerOf(
									Data.EffectSpec.GetContext()))));
				}

				// AND WHAT ARMOUR AND DAMAGE REDUCTION TOOK AWAY IS STORED, for
				// Nothing Wasted. Issue #1515. Ticks too: both steps reduce them
				// (ruled 2026-09-24). Resistance, block, evasion and the pools
				// are not in either figure.
				Cataclysm->NoteMitigatedDamage(
					Outcome.RemovedByArmour + Outcome.RemovedByDamageReduction);

				// AND WHAT THE ENERGY SHIELD ABSORBED IS STORED IN A STORE OF ITS
				// OWN, for "Damage absorbed by your energy shield is converted to
				// bonus damage on your next attack". Ruled 2026-10-07. Exactly
				// the figure taken from the shield above, ticks included.
				Cataclysm->NoteShieldAbsorbedDamage(Outcome.AbsorbedByShield);
			}

			// AND ANY HIT THAT REACHED THE CHARACTER BUILDS A STACK. Issue
			// #1003. Blood Offering: "Taking damage grants a stack of Bloodlust
			// for 5 seconds, up to 5 stacks."
			//
			// A SEPARATE BRANCH FROM THE WINDOW ABOVE, because the two ask
			// different questions and the difference is the point. That one
			// wants a Cataclysm type the character does NOT share, so it refuses
			// an untyped hit and refuses the character's own type. This one
			// wants damage of any kind, from anything.
			//
			// ANY TYPE IS A READING RATHER THAN THE NODE'S OWN WORDS. Blood
			// Offering says "physical damage" and this game has eight damage
			// types and no physical one. Issue #1001 carries the question and
			// the recommendation this follows.
			//
			// WHAT REACHED THE CHARACTER, NOT WHAT WAS AIMED AT IT, which is the
			// line the window above draws too: an evaded blow and one armour
			// took to nothing removed nothing and are not damage taken, and a
			// shield absorbing it is.
			if (Outcome.DealtToHealth + Outcome.AbsorbedByShield
					+ Outcome.AbsorbedByTemporary > 0.0f && !bShareOfAnAllysDamage)
			{
				if (UCataclysmAbilitySystemComponent* Cataclysm =
						Cast<UCataclysmAbilitySystemComponent>(
							GetOwningAbilitySystemComponent()))
				{
					UCataclysmStacks::NoteDamageTaken(Cataclysm);
				}
			}

			// A BLUNT WEAPON MAY STUN WHAT IT HITS. The roll is further down,
			// after the two enemy modifiers, and since issue #899 it is one pool
			// with the chance to stun from gear. Issue #639, and the last
			// of the four sub-types to be built. Its effect is the only one that
			// is not damage, which is why it is here rather than inside Resolve:
			// a stun goes through UCataclysmSkillEffects::ApplyStun, which
			// enforces the three anti-stun-lock rules, and Resolve is pure
			// arithmetic with no way to reach any of them.
			//
			// THE THRESHOLD, THE WINDOW AND BOSS IMMUNITY ARE ALL ApplyStun'S,
			// so none of them is checked twice. It is passed the damage this hit
			// actually dealt and told the stun is NOT designed, which is what
			// makes it obey the 10% damage threshold -- a stun rolled on an
			// ordinary hit must obey it, or chip damage from a fast blunt weapon
			// would interrupt a well defended character constantly, which is
			// half of what the rule exists to stop.
			//
			// CROWD CONTROL RESISTANCE NO LONGER TOUCHES THE CHANCE, since
			// 2026-09-05. It used to reduce this total, and that was the ONLY
			// thing the stat did anywhere in the game. It now shortens the stun
			// itself, inside `UCataclysmSkillEffects::ApplyStun`, which is the
			// only rule that can also serve the four skills that stun by design.
			//
			// ONE RULE AND NOT BOTH, deliberately. Reducing the chance here as
			// well would count the stat twice: a character at 50 would take half
			// the chance of a stun half as long, which is a quarter, and "50
			// crowd control resistance" would not mean anything a player could
			// work out.
			// AND TWO DEMONIC ENEMY MODIFIERS FIRE ON A HIT THAT LANDED. Issue
			// #742. Infernal Brand marks whoever this creature hit; Beguiling
			// charms whoever hit THIS creature. Both are here because this is the
			// one place a resolved blow is known -- what was sent, what got
			// through, and who both parties were.
			//
			// ONLY ON A BLOW THAT ACTUALLY HURT. A hit that was evaded or fully
			// absorbed marks nobody and charms nobody, which is the rule
			// docs/DECISIONS.md settled on 2026-09-05: an evaded attack applies
			// nothing it was carrying.
			//
			// NOR FOR A POINT-BLANK SKILL'S SHARE STRUCK AT THIS ALLY, which is damage only (R5). Ruled 2026-10-15.
			if (Outcome.DealtToHealth > 0.0f && !bShareOfAnAllysDamage && !bAreaSkillShareOnAnAlly)
			{
				AActor* Striker =
					Data.EffectSpec.GetContext().GetEffectCauser();

				UCataclysmEnemyModifiers::BrandOnHit(Striker, GetOwningActor());
				UCataclysmEnemyModifiers::CharmWhoeverStruck(GetOwningActor(),
															 Striker);
			}

			// EVERY CHANCE TO APPLY AN AILMENT THE BLOW CARRIED IS ROLLED HERE,
			// AND A BLUNT WEAPON'S STUN IS ONE OF THEM. Issue #899. The chances
			// were worked out on the attacker's side, where the skill's tags were
			// in hand, and they arrive on the effect.
			// `UCataclysmAilments::RollOnLandedBlow` holds every rule about them:
			// the tenth of maximum health, nothing on a blow that killed, and one
			// pool for a blunt weapon's 10% and the chance to stun from gear.
			//
			// NOT FOR A TICK OF DAMAGE OVER TIME. A tick carries no chance of its
			// own. Until #899 a tick of damage over time applied by a character
			// holding a blunt weapon could roll that weapon's stun, if the tick
			// took a tenth of the target's maximum health, because the sub-type
			// is read off the actor the damage is credited to. The chance to stun
			// is a chance to apply an effect on a hit, and a tick is not a hit.
			//
			// AND NO BLUNT STUN FROM A POINT-BLANK SKILL'S SHARE STRUCK AT THIS ALLY, ruled 2026-10-16 by the
			// coordinating session: the share keeps the weapon sub-type, which prices it (R4), and is damage only
			// (R5). It carries no ailment chance, so the blunt weapon's own chance is the one roll left to refuse.
			if (!Hit.bIsDamageOverTime)
			{
				UCataclysmAilments::RollOnLandedBlow(Data.EffectSpec,
					GetOwningActor(), Outcome.DealtToHealth, Hit.bIsBlunt && !bAreaSkillShareOnAnAlly);
			}

			// A MELEE CRITICAL STRIKE MAY APPLY BLEEDING TO WHAT IT HIT.
			// Issue #1032. The Masochist's Mutilation Mastery: "Your melee
			// critical strikes have a 5% chance per point to apply Bleeding."
			// Eight points, so 40% at most.
			//
			// THE CHANCE BELONGS TO WHOEVER SWUNG, which is what makes this
			// different from the blunt stun above. That one reads the DEFENDER'S
			// crowd control resistance; this reads the INSTIGATOR'S stat, the
			// same way the two penetrations and the critical strike multiplier
			// are read further up this function.
			//
			// THREE CONDITIONS AND ALL THREE ARE THE NODE'S OWN WORDS. Melee,
			// critical, and a blow that actually reached health. Dropping any
			// one of them would make the node several times stronger than it
			// reads and nothing at run time would report it.
			//
			// AND THE RULE FOR EVERY AILMENT THAT IS NOT THE SKILL'S OWN. Issue
			// #1565, ruled 2026-10-01. The owner's rule of 2026-09-02 (#917): such
			// an ailment needs the blow to have taken a tenth of the target's
			// maximum health, and a blow that killed applies nothing. A passive
			// node is not the skill's own row. Until #1565 this asked only that the
			// blow reached health, so a scratch could bleed and so could a corpse.
			// `UCataclysmAilments::BlowCanCarryAnAilment` holds both halves, and
			// the gear roll above and the random damage over time enchantment ask
			// it too, so the three cannot hold different rules.
			//
			// READ OFF THE ATTRIBUTE RATHER THAN ASKED FOR, which is safe only
			// because the node's one row carries no condition and no scale, so
			// it IS folded into the attribute. A later node conditioning this
			// stat would need a `StatForSkill` call here or its row would be
			// dropped in silence. Issue #1022.
			if (Hit.bIsMelee && Outcome.bWasCritical
				&& UCataclysmAilments::BlowCanCarryAnAilment(GetOwningActor(), Outcome.DealtToHealth))
			{
				const UAbilitySystemComponent* Attacker =
					Data.EffectSpec.GetContext()
						.GetInstigatorAbilitySystemComponent();
				const UCataclysmCombatAttributeSet* Offence =
					Attacker ? Attacker->GetSet<UCataclysmCombatAttributeSet>()
							 : nullptr;
				const float Chance =
					Offence ? Offence->GetBleedOnCritChance() : 0.0f;

				if (Chance > 0.0f && CataclysmPercentDraw::Draw() < Chance)
				{
					// THE DESIGNED NUMBERS AND NOT INVENTED ONES. The node says
					// only "apply Bleeding" and states neither a magnitude nor a
					// duration, so both come from the `DoT_Bleed` row of
					// `game/Data/StatusEffects.csv`: 20 damage a second for 5
					// seconds. `BleedNumbers` reports the row unusable rather
					// than silently applying nothing if either is missing.
					const FCataclysmStatusEffectNumbers Bleed =
						UCataclysmSkillEffects::BleedNumbers();
					if (Bleed.bUsable)
					{
						// THE SAME CALL THE DAMAGE CONVERSION MAKES, so what
						// this creates is the same thing `UCataclysmDebuffs`
						// counts and the five nodes that read debuffs see.
						//
						// IT SCALES WITH THE ATTACKER, unlike the conversion's,
						// because this is damage one character is doing to
						// another rather than a character's own damage arriving
						// in a different shape.
						UCataclysmSkillEffects::ApplyDamageOverTime(
							Data.EffectSpec.GetContext().GetEffectCauser(),
							GetOwningActor(), Bleed.FlatDamagePerTick,
							Bleed.DurationSeconds,
							UCataclysmDebuffs::BleedTag(),
							/*bScalesWithInstigator=*/true,
							/*DealtBy=*/Cast<AActor>(
								Data.EffectSpec.GetContext().GetSourceObject()),
							/*Skill=*/Data.EffectSpec.GetContext()
								.GetAbilityInstance_NotReplicated());
					}
				}
			}

			// AND A DEBUFF DEALING DAMAGE TO THIS CHARACTER MAY SPREAD TO WHAT
			// IS STANDING NEAR IT. Issue #1058. The Masochist's Contagious
			// Torment: "When a debuff on you deals damage, enemies within 6
			// metres have a 1% chance per point to receive a random debuff you
			// carry."
			//
			// HERE BECAUSE THIS IS THE ONLY PLACE THAT KNOWS A TICK LANDED.
			// `Hit.bIsDamageOverTime` is read off the effect's asset tags at the
			// top of this function and exists nowhere else; the retaliation
			// refusal and the Breaking Point conversion above both hang off the
			// same fact.
			//
			// THE DAMAGE HAS TO HAVE REACHED HEALTH, because the sentence says
			// the debuff DEALS damage. A tick a character's damage-over-time
			// taken stat reduced to nothing dealt nothing.
			//
			// `DealtToHealth` ALONE AND NOT THE SHIELD AS WELL, unlike the two
			// branches above. Damage over time is not absorbed by an energy
			// shield at all -- that is the design's answer to shield stacking,
			// issue #513 -- so the shield figure is zero here and naming it
			// would suggest a case that cannot arise.
			//
			// ITS RETURN VALUE IS DROPPED. Zero is the ordinary answer for every
			// character in the game, because the chance is zero without the
			// node and the function refuses on that before it looks for anybody.
			if (Hit.bIsDamageOverTime && Outcome.DealtToHealth > 0.0f)
			{
				// AROUND THE BODY, NOT THE SET OWNER. Issue #1755. For a player
				// the owner is the player state, at the world's origin, so
				// Contagious Torment searched for enemies near the origin.
				UCataclysmContagion::SpreadOnDebuffDamage(CataclysmDefendingBody(*this));
			}

			PlayImpactEffect(Data, Hit, Outcome);
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		// AND THE REAPER'S HEALTH BASE IS PUT BACK TOO. Issues #1820 and #41.
		// `UCataclysmSkillEffects::ReduceHealthDirectly` takes health off through this
		// branch and never through the damage branch above. `PreAttributeChange` has already
		// held the CURRENT value at the maximum, which is what keeps it alive; the base the
		// effect lowered is left behind, and this writes it back so the two agree.
		//
		// A SHROUDED CREATURE NEEDS NOTHING HERE, Shadowy Enemies: `PreAttributeChange` held its
		// current value where it was, and the clamp below writes that current value to the base.
		const ACataclysmEnemyCharacter* Unhurt =
			Cast<ACataclysmEnemyCharacter>(GetOwningActor());
		if (Unhurt && Unhurt->bCannotBeHurt)
		{
			SetHealth(GetMaxHealth());
		}
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
		NotifyIfHealthReachedZero();
		NotifyHealthChanged();
	}
	else if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.0f, GetMaxMana()));
	}
	else if (Data.EvaluatedData.Attribute == GetEnergyShieldAttribute())
	{
		SetEnergyShield(
			FMath::Clamp(GetEnergyShield(), 0.0f, MaximumEnergyShieldAsked()));
	}
}

void UCataclysmVitalAttributeSet::PlayImpactEffect(
	const FGameplayEffectModCallbackData& Data,
	const FCataclysmIncomingHit& Hit,
	const FCataclysmDamageResult& Outcome)
{
	// Whether this is worth drawing at all lives in UCataclysmImpactEffect, so a
	// test can reach it without a world or a rendering device. It refuses a blow
	// that never connected, and a burn ticking, which is not a blow at all.
	const bool bWorthDrawing =
		UCataclysmImpactEffect::ShouldDrawFor(Hit, Outcome);

	// THE AVATAR, NOT THE OWNER. GetOwningActor answers with the ability
	// system's owner, and for the player that is the player state, which is not
	// placed in the world and reports the origin. Issue #562.
	const AActor* Struck =
		UCataclysmImpactEffect::ActorToDrawOn(GetOwningAbilitySystemComponent());

	// EVERY LANDED HIT IS LOGGED, INCLUDING THE ONES THAT DRAW NOTHING, and that
	// ordering is the point. An earlier version logged only after deciding to
	// draw, so a hit that arrived and did nothing left no trace at all -- which
	// is precisely the case somebody hit while playing: the effect stopped
	// appearing and there was no way to tell whether hits had stopped landing or
	// had stopped counting.
	//
	// healthLeft is what answers it. A character at zero health takes no further
	// damage, because UCataclysmDamageCalculation::Resolve ends with
	// FMath::Min(Damage, Vitals->GetHealth()) and that is zero from then on.
	//
	// THAT CLAMP IS RIGHT AND WAS NEVER THE DEFECT. What was wrong is that a
	// player at zero health was not marked dead, so nothing stopped, nothing
	// stopped attacking it, and the hits went on arriving and dealing nothing --
	// fifty-six of them over seventy seconds in the session that found it.
	// ACataclysmPlayerCharacter::HandleDeath now marks and stops the player, and
	// UCataclysmTargeting no longer finds a dead character at all. Issue #570.
	//
	// Counted is a running total for the session, so a burst of hits can be
	// counted without timestamps. Issue #563 needed exactly that.
	static int32 Counted = 0;
	++Counted;

	UE_LOG(LogCataclysm, Verbose,
		TEXT("hit %d: on=%s type=%s dot=%s area=%s toHealth=%.1f toShield=%.1f "
			 "healthLeft=%.1f drawn=%s"),
		Counted, Struck ? *Struck->GetName() : TEXT("(no avatar)"),
		Hit.DamageType.IsNone() ? TEXT("(none)") : *Hit.DamageType.ToString(),
		Hit.bIsDamageOverTime ? TEXT("yes") : TEXT("no"),
		Hit.bIsArea ? TEXT("yes") : TEXT("no"),
		Outcome.DealtToHealth, Outcome.AbsorbedByShield, GetHealth(),
		bWorthDrawing ? TEXT("yes") : TEXT("no"));

	// THE NUMBER IS RECORDED BEFORE THE PARTICLE'S EARLY RETURN, DELIBERATELY,
	// because the two follow opposite rules and the ordering is what enforces
	// it. The particle refuses a hit that never connected, so that a burst means
	// "that landed" rather than "an attack happened". A number is wanted for
	// exactly those hits: an evaded blow says "Evaded" and one armour and
	// resistance took to nothing shows a zero, which is the only way to see
	// issues #483 and #644 happening while playing rather than in arithmetic.
	//
	// Struck may be null, and UCataclysmCombatOverlay::Record answers that by
	// drawing nothing rather than guessing a position -- the same contract
	// ActorToDrawOn states above.
	UCataclysmCombatOverlay::Record(Struck, Hit, Outcome);

	if (!bWorthDrawing || !Struck)
	{
		return;
	}

	// WHERE THE BLOW LANDED. The choice lives in UCataclysmImpactEffect so a
	// test can reach it without a world or a rendering device, which is the only
	// way any of this is covered -- see issue #559.
	const FHitResult* Landed = Data.EffectSpec.GetContext().GetHitResult();
	FVector Normal = FVector::UpVector;
	const FVector Location =
		UCataclysmImpactEffect::ImpactLocationFor(Landed, Struck, Normal);

	// DRAWN AS THE SKILL'S TYPE, NOT AS THE RESISTED TYPE. For an enemy's hit
	// the two are the same. For a player's they differ on purpose: the hit
	// resists as nothing and draws as the skill's own element. Issue #803.
	UCataclysmImpactEffect::SpawnAt(Struck, Location, Normal,
									Hit.EffectDamageType);
}

void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero() const
{
	if (GetHealth() > 0.0f)
	{
		return;
	}

	// ONCE, WHICH IS WHAT THE TAG IS FOR HERE AS WELL AS FOR ASKING. Health can
	// be written repeatedly at zero -- a burn ticking on a corpse, two hits in
	// the same frame -- and HandleDeath removes the character from the level, so
	// running it twice would be running it on something already leaving.
	// THE AVATAR, NOT THE OWNER, and for the same reason the hit effect needs it
	// in issue #562. GetOwningActor answers with the ability system's owner, and
	// ACataclysmPlayerCharacter::InitAbilityActorInfo makes that the player
	// state -- deliberately, because it survives death -- while the pawn is the
	// avatar. A player state is not a character, so the cast below failed and
	// this returned early. Issue #565 is what fixed that.
	//
	// BOTH DEATHS THIS REACHES ARE BUILT. Issue #1094 replaced a paragraph here
	// that said a player's was not, which was true when it was written and had
	// stopped being true. `ACataclysmPlayerCharacter::HandleDeath` writes the
	// save, marks the character dead, halts it and stands it back up after
	// `RespawnDelaySeconds`; `ACataclysmEnemyCharacter::HandleDeath` marks the
	// creature, drops its loot and removes it. Only `ACataclysmCharacterBase`'s
	// own is inert, and nothing in the game is one.
	//
	// THE REMAINING QUESTION WAS NEVER WHAT THIS DOES BUT WHICH WRITES REACH
	// IT, and that is what `PostAttributeBaseChange` above now answers. The
	// stale paragraph was the one somebody investigating exactly that would
	// read and be sent away by.
	const UAbilitySystemComponent* AbilitySystem =
		GetOwningAbilitySystemComponent();
	ACataclysmCharacterBase* Character = AbilitySystem
		? Cast<ACataclysmCharacterBase>(AbilitySystem->GetAvatarActor())
		: nullptr;
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return;
	}

	// WHOEVER WAS COMMANDING THIS THING GAINS FERVOUR FOR LOSING IT.
	// Issue #1518, the Ritualist's generator: "and 5 when one of them dies".
	//
	// BEFORE `HandleDeath` AND NOT AFTER IT, because the enemy's own
	// `HandleDeath` removes the creature from the level, and
	// `UCataclysmCommand::CommanderOf` walks the ownership chain of an actor
	// that would by then be leaving. A thrall is a subjugated
	// `ACataclysmEnemyCharacter`, so this is the ordinary case rather than a
	// corner.
	//
	// IT RUNS ONCE PER DEATH BECAUSE THE GUARD ABOVE DOES. Health can be
	// written at zero repeatedly and `IsDead` is what stops the second one, so
	// this grant inherits that rule rather than keeping a record of its own.
	// `ACataclysmMinion::HandleDeath` had to be given to it for that to be true
	// of an imp as well as of a thrall.
	//
	// INERT FOR EVERYTHING THAT FOLLOWS NOBODY, which is every creature in the
	// game but a Ritualist's. `CommanderOf` answers null for an ordinary
	// monster and the grant is skipped without asking anything else.
	//
	// THAT NULL IS A DELIBERATE CHECK RATHER THAN A HAPPY ACCIDENT, AND IT WAS
	// NOT ALWAYS THERE. Unreal's `APawn::PossessedBy` calls
	// `SetOwner(NewController)`, so every possessed character has a non-null
	// owner, and until issue #1517 this function answered every monster's own
	// AI controller. It now refuses a controller by name.
	//
	// THIS CALL SITE WAS CORRECT BEFORE THAT FIX AND IS CORRECT AFTER IT, for
	// different reasons, which is why the null check below on the ability
	// system is kept rather than trimmed as redundant. Before the fix the
	// controller came back and carried no ability system, so the grant returned
	// on its first line; after it, nothing comes back at all. Neither of those
	// is a reason to trust the other.
	//
	// A THRALL IS THE CASE THAT HAS TO WORK, AND IT DOES BECAUSE `Subjugate`
	// SETS THE OWNER AFTER POSSESSION HAS ALREADY HAPPENED, so a taken
	// creature's owner is the character that took it rather than its
	// controller. That is a different branch of `CommanderOf` from a summoned
	// imp, which is found through `ACataclysmMinion::Summoner`, and
	// `AMinionDyingGrantsFervour` covers both.
	//
	// THE COMMANDER'S ABILITY SYSTEM, NOT THE DYING THING'S. A minion has no
	// class resource attribute set, so passing its own would answer zero.
	AActor* Commander = UCataclysmCommand::CommanderOf(Character);
	if (Commander)
	{
		UCataclysmFervour::GainOnMinionDeath(
			UCataclysmTargeting::AbilitySystemOf(Commander));
	}

	// WHAT A REPLACEMENT NEEDS, TAKEN BEFORE THE DEATH IS HANDLED. Issue #1515:
	// Press-Ganged and Rekindled. A minion whose death is an explosion is
	// destroyed inside `HandleDeath`, so where it stood and whether it exploded
	// are read now; the commander was found above for the same reason.
	const FVector Where = Character->GetActorLocation();
	const ACataclysmMinion* AsMinion = Cast<ACataclysmMinion>(Character);
	const bool bExplodes = AsMinion && AsMinion->ExplodesOnDeath();
	// AND THE SUMMON SKILL THAT MADE IT, for the re-summon below, read now for
	// the same reason. Null for a deployable, a risen imp and a thrall.
	UCataclysmSummonSkill* MadeBy = AsMinion ? AsMinion->SummonedBy.Get() : nullptr;
	// AND WHETHER IT WAS A TYRANT'S MINION, for the summon and the replacement below. Ruled 2026-10-18.
	const bool bWasTyrantsMinion = AsMinion && AsMinion->bIsTyrantsMinion;
	// AND WHETHER IT WAS A WARHOUND, for the replacement below. Ruled 2026-10-18 (Warlord's Will's six-piece, Q3).
	const bool bWasWarhound = AsMinion && AsMinion->bIsWarhound;

	// AND SHARED RUIN'S BLAST, ALSO BEFORE THE DEATH IS HANDLED. Issue #1515.
	// The dying creature deals it and its maximum health sets it, and an
	// exploding minion is destroyed inside `HandleDeath`. A thrall comes through
	// here as well, which is why it is not inside `ACataclysmMinion::HandleDeath`.
	if (Commander)
	{
		ACataclysmMinion::DeathBlast(Character, Commander);
	}

	Character->HandleDeath();

	// AND THE REPLACEMENT AFTER IT, so the lost minion has already left the
	// living count its summon skill's cap reads. A thrall comes through here
	// too, which a hook inside `ACataclysmMinion::HandleDeath` would have
	// missed: the owner's first wording of Press-Ganged was "a thrall that dies
	// is replaced by an imp".
	// AND WHEN NOTHING REPLACED IT AT ONCE, THE WAIT TO SUMMON IT AGAIN BEGINS.
	// Issue #1833 group E part 4b: "When a minion dies it automatically
	// re-summons after 3-6 seconds". Not both, or one loss would give two.
	// AND NEITHER FOR A TYRANT'S MINION, ruled 2026-10-18 (Q4): it is outside the
	// caps and is never replaced or re-summoned. `ReplaceLost` would otherwise take
	// the commander's first summon skill, since no skill made it, and replace it
	// with what that skill summons.
	//
	// AND A TYRANT'S MINION IS SUMMONED, OR THE ONE STANDING REFRESHED, for the
	// same deaths Shared Ruin's blast above fires for. Tyrant's Chains' six-piece,
	// ruled 2026-10-17 (#3) and 2026-10-18. Its own death summons none.
	if (Commander)
	{
		ACataclysmMinion::SummonTyrantsMinion(Commander, bWasTyrantsMinion, Where);
	}
	// AND NEITHER FOR A WARHOUND, ruled 2026-10-18 (Q3), for the Tyrant's Minion's reason: Warlord's Will's six-piece
	// summons it, no skill does, and it is never replaced or re-summoned. A Warhound's death is still a minion's death
	// above, so Shared Ruin's blast and Tyrant's Chains' summon answer it.
	if (Commander && !bWasTyrantsMinion)
	{
		if (!bWasWarhound && !UCataclysmSummonSkill::ReplaceLost(Commander, Character, Where, bExplodes))
		{
			UCataclysmSummonSkill::ScheduleResummon(Commander, MadeBy);
		}
	}

	// AND A SOUL WHERE IT DIED, where the commander holds the flag: Reaper's Embrace's six-piece, ruled 2026-10-17 (#8)
	// and 2026-10-18 (R1, exactly the deaths this hook answers, a Tyrant's Minion's and a Warhound's own included). A
	// lifetime running out writes no health and never reaches this, so it leaves none.
	if (Commander)
	{
		ACataclysmReaperSoul::LeaveFor(Commander, Where);
	}

	// AND THE DEAD CHARACTER'S OWN WAITS END. Ruled 2026-10-02: a summoner's
	// death clears them, so it gets nothing back from before it fell.
	if (UCataclysmAbilitySystemComponent* Dead = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character)))
	{
		Dead->ClearPendingResummons();
	}
}

void UCataclysmVitalAttributeSet::NotifyHealthChanged() const
{
	// EVERY WRITE TO HEALTH, NOT ONLY THE ONE THAT REACHES ZERO, which is the
	// difference between this and NotifyIfHealthReachedZero beside it. A
	// health-triggered phase begins part way down rather than at the end.
	//
	// THE AVATAR, NOT THE OWNER, for the reason its sibling records: a
	// player's ability system is owned by the player state and the pawn is the
	// avatar, so asking the owner returns something that is not a character.
	const UAbilitySystemComponent* AbilitySystem =
		GetOwningAbilitySystemComponent();

	// AND HERE IS WHERE A HEALTH THRESHOLD IS CROSSED. Issue #985, The Breaking
	// Point: "Dropping below 50% health converts all damage you take into
	// Bleeding".
	//
	// THIS FUNCTION DOES NOW FIRE ON EVERY WRITE TO HEALTH, WHICH IS WHAT A
	// CROSSING NEEDS, AND FOR TWO THIRDS OF THIS PROJECT'S LIFE IT DID NOT.
	// The paragraph that stood here claimed all three routes -- a blow, a health
	// cost, a debt falling due -- reached it, and only the blow did, because the
	// only two calls were inside `PostGameplayEffectExecute`. Issue #1072. What
	// makes the claim true is `PostAttributeBaseChange` at the top of this file,
	// which the engine calls for every direct write. Do not delete it;
	// `tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py`
	// fails if either call in it goes.
	//
	// BEFORE THE CAST BELOW AND NOT AFTER IT. That cast asks for a
	// `ACataclysmCharacterBase`, because what it guards needs the enemy phase
	// machinery. This needs an ability system and nothing more, and refusing it
	// for anything that is not a character would be a rule nobody chose.
	UCataclysmDamageConversion::NoteHealthChanged(
		AbilitySystem ? AbilitySystem->GetAvatarActor() : nullptr);

	// AND HERE IS WHERE A SECOND THRESHOLD IS CROSSED. Issue #1069, Rock
	// Bottom: "Dropping below 20% health clears all outstanding debt and grants
	// 50 Fervour, no more than once every 30 seconds." A different line from the
	// one above, with its own memory of where health was and its own cooldown,
	// so a character holding both nodes gets each at its own moment.
	//
	// BESIDE THE CALL ABOVE AND NOT INSIDE IT. The two watch different lines for
	// different nodes and neither reads what the other wrote; putting one inside
	// the other would tie two options together for no reason a player could see.
	//
	// ITS RETURN VALUE IS DROPPED. Zero is the answer for every character in the
	// game without that capstone option, and it is returned for tests.
	UCataclysmLowHealthRelief::NoteHealthChanged(
		AbilitySystem ? AbilitySystem->GetAvatarActor() : nullptr);

	// AND THE WORN ROWS ON A FALL BELOW A SHARE OF MAXIMUM HEALTH, beside the two
	// hard-coded crossers. Issue #1833 group D part 2.
	if (UCataclysmAbilitySystemComponent* Crossing =
			Cast<UCataclysmAbilitySystemComponent>(GetOwningAbilitySystemComponent()))
	{
		Crossing->NoteHealthForCrossing();
	}

	ACataclysmCharacterBase* Character = AbilitySystem
		? Cast<ACataclysmCharacterBase>(AbilitySystem->GetAvatarActor())
		: nullptr;
	if (!Character)
	{
		return;
	}

	// NOT SKIPPED FOR A DEAD CHARACTER, unlike the death notice, and it costs
	// nothing: a creature at zero health is already in its last phase and
	// RefreshPhase only moves forward, so a burn ticking on a corpse changes
	// nothing. Adding a check here would be a second place that has to agree
	// with what counts as dead.
	Character->HealthChanged();
}

TArray<FGameplayAttribute> UCataclysmVitalAttributeSet::GetAllAttributes()
{
	return {
		GetHealthAttribute(), GetMaxHealthAttribute(),
		GetManaAttribute(), GetMaxManaAttribute(),
		GetEnergyShieldAttribute(), GetMaxEnergyShieldAttribute(),
		GetHealthRegenAttribute(), GetManaRegenAttribute(),
		GetEnergyShieldRegenAttribute(), GetLifeLeechAttribute(),
		GetManaLeechAttribute(), GetEnergyShieldLeechAttribute(),
		GetHealingCeilingReductionAttribute(),
		GetHealingReceivedReductionAttribute(),
		GetDamageAttribute(),
	};
}

CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, Health)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxHealth)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, Mana)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxMana)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShield)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, MaxEnergyShield)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, HealthRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, ManaRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShieldRegen)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, LifeLeech)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, ManaLeech)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, EnergyShieldLeech)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, HealingCeilingReduction)
CATACLYSM_ON_REP(UCataclysmVitalAttributeSet, HealingReceivedReduction)
