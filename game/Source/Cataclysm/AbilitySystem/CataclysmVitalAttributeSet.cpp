// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmVitalAttributeSet.h"
// For asking the attacker what a stat is worth with this hit's tags and the
// attacker's own state in hand, rather than reading the attribute. Issue #959.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For turning health lost to damage into Fervour. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
// For the character's own Cataclysm type, so a hit of another one can be told
// apart from its own. Issue #975.
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
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
	// Damage is a meta attribute. It is never replicated.
}

void UCataclysmVitalAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxMana());
	}
	else if (Attribute == GetEnergyShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxEnergyShield());
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
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UCataclysmVitalAttributeSet::PostGameplayEffectExecute(
	const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const float LocalDamage = GetDamage();
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
			// damage over time decides whether an energy shield absorbs it.
			// Issue #513.
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
						Hit.ResistancePenetration = Offence->GetPenetration();
						Hit.ArmorPenetration = Offence->GetArmorPenetration();
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
						const UCataclysmAbilitySystemComponent* Asking =
							Cast<const UCataclysmAbilitySystemComponent>(Attacker);
						const float OwnCritChance = Asking
							? Asking->StatForSkill(FName(TEXT("crit_chance")),
												   AssetTags,
												   Offence->GetCritChance())
							: Offence->GetCritChance();

						// HELD UNDER THE ATTACKER'S OWN CEILING, which matters only
						// for the stated route. The attribute was already clamped
						// when it was written, but a skill's stated chance never
						// passes through that clamp, so a skill stating 80% on a
						// character an enchantment has capped at 30% would
						// otherwise land at 80%. Issue #680.
						Hit.CritChance = FMath::Min(
							Stated >= 0.0f ? Stated : OwnCritChance,
							Offence->GetMaxCritChance());
						Hit.CritMultiplier = Offence->GetCritMultiplier();
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
			const FCataclysmDamageResult Outcome =
				UCataclysmDamageCalculation::Resolve(
					Hit, GetOwningAbilitySystemComponent(),
					ACataclysmGameMode::DifficultyTierIn(GetOwningActor()),
					/*EvasionRoll=*/-1.0f, /*BlockRoll=*/-1.0f,
					CVarCritRoll.GetValueOnAnyThread());

			// THE ENERGY SHIELD'S REFILL WAIT STARTS HERE. The design gives the
			// shield a three second delay after the character last took damage,
			// restarted by taking damage again inside that window, and says
			// damage over time restarts it as well. That last part is
			// load-bearing: the shield absorbs no damage over time at all, so
			// without it a bleeding character would refill freely and the
			// shield would be strongest against the one thing it ignores.
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
			if (Outcome.DealtToHealth > 0.0f || Outcome.AbsorbedByShield > 0.0f
				|| Outcome.AbsorbedByMana > 0.0f)
			{
				if (ACataclysmCharacterBase* Hurt =
						GetOwningAbilitySystemComponent()
							? Cast<ACataclysmCharacterBase>(
								  GetOwningAbilitySystemComponent()
									  ->GetAvatarActor())
							: nullptr)
				{
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
				if (!AssetTags.HasTag(UCataclysmDamageCalculation::NoLeechTag()))
				{
					UCataclysmLeech::NoteHit(
						const_cast<UAbilitySystemComponent*>(
							Data.EffectSpec.GetContext()
								.GetInstigatorAbilitySystemComponent()),
						Outcome.DealtToHealth + Outcome.AbsorbedByShield
							+ Outcome.AbsorbedByMana);
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
					if (const UCataclysmCombatAttributeSet* Defence =
							GetOwningAbilitySystemComponent()
								? GetOwningAbilitySystemComponent()
									  ->GetSet<UCataclysmCombatAttributeSet>()
								: nullptr)
					{
						// THE RETALIATING CHARACTER'S OWN FIGURE IS ASKED FOR
						// RATHER THAN READ. Issue #980, and it is the same move
						// `crit_chance` made in this file for issue #959. A
						// bonus whose SIZE grows with a state -- Reciprocity
						// gives "+1% for each point of Fervour you currently
						// hold" -- is never written onto the gameplay attribute,
						// because it would be stale the moment the bar moved. So
						// reading the attribute would drop it in silence and the
						// node would grant nothing.
						//
						// THE DEFENDER'S OWN ABILITY SYSTEM, not the attacker's.
						// This is the defender's retaliation, so the state that
						// sizes it is the defender's.
						//
						// NO SKILL TAGS. `AssetTags` belongs to the blow that
						// came IN, and scoping the defender's retaliation by the
						// attacker's skill tags would be the wrong question.
						// Retaliation is not a skill and carries none of its own.
						//
						// A FALLBACK OF THE ATTRIBUTE, so a character with no
						// such bonus gets exactly what it got before.
						const UCataclysmAbilitySystemComponent* Asking =
							Cast<const UCataclysmAbilitySystemComponent>(
								GetOwningAbilitySystemComponent());
						const float Amount = Asking
							? Asking->StatForSkill(FName(TEXT("retaliation")),
												   FGameplayTagContainer(),
												   Defence->GetRetaliation())
							: Defence->GetRetaliation();

						UCataclysmSkillEffects::ReduceHealthDirectly(
							GetOwningActor(),
							Data.EffectSpec.GetContext().GetEffectCauser(),
							Amount);
					}
				}
			}

			if (Outcome.AbsorbedByShield > 0.0f)
			{
				SetEnergyShield(FMath::Clamp(
					GetEnergyShield() - Outcome.AbsorbedByShield,
					0.0f, GetMaxEnergyShield()));
			}
			if (Outcome.DealtToHealth > 0.0f)
			{
				SetHealth(FMath::Clamp(GetHealth() - Outcome.DealtToHealth,
									   0.0f, GetMaxHealth()));
				NotifyIfHealthReachedZero();
				NotifyHealthChanged();

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
				UCataclysmFervour::GainFromDamage(
					GetOwningAbilitySystemComponent(), Outcome.DealtToHealth,
					AssetTags);
			}

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
			if (Outcome.DealtToHealth + Outcome.AbsorbedByShield > 0.0f
				&& !Hit.DamageType.IsNone())
			{
				const FString Own = UCataclysmWeaponSlotsComponent::DamageTypeOf(
					GetOwningActor());
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

			// A BLUNT WEAPON MAY STUN WHAT IT HITS. Issue #639, and the last
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
			// CROWD CONTROL RESISTANCE REDUCES THE TOTAL, NOT THE CAPPED CHANCE,
			// so it bites into the overflow as well and is worth something
			// against a heavy stun build rather than nothing.
			if (Hit.bIsBlunt && Outcome.DealtToHealth > 0.0f)
			{
				float Total = UCataclysmDamageCalculation::BluntStunChance;
				if (const UCataclysmCombatAttributeSet* Defence =
						GetOwningAbilitySystemComponent()
							? GetOwningAbilitySystemComponent()
								  ->GetSet<UCataclysmCombatAttributeSet>()
							: nullptr)
				{
					const float Resisted = FMath::Clamp(
						Defence->GetCrowdControlResistance(), 0.0f, 100.0f);
					Total *= 1.0f - Resisted / 100.0f;
				}

				float Chance = 0.0f;
				float Seconds = 0.0f;
				UCataclysmDamageCalculation::StunApplication(Total, Chance,
															 Seconds);

				if (Chance > 0.0f && FMath::FRandRange(0.0f, 100.0f) < Chance)
				{
					UCataclysmSkillEffects::ApplyStun(
						Data.EffectSpec.GetContext().GetEffectCauser(),
						GetOwningActor(), Seconds, Outcome.DealtToHealth,
						/*bStunIsDesigned=*/false);
				}
			}

			PlayImpactEffect(Data, Hit, Outcome);
		}
	}
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
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
		SetEnergyShield(FMath::Clamp(GetEnergyShield(), 0.0f, GetMaxEnergyShield()));
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

void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero()
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
	// this returned early.
	//
	// IT COSTS NOTHING TODAY, because HandleDeath is inert on the base by design
	// and a player's death is not built. It would silently stop that death ever
	// firing the moment somebody builds it, with no error to follow. Issue #565.
	const UAbilitySystemComponent* AbilitySystem =
		GetOwningAbilitySystemComponent();
	ACataclysmCharacterBase* Character = AbilitySystem
		? Cast<ACataclysmCharacterBase>(AbilitySystem->GetAvatarActor())
		: nullptr;
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return;
	}

	Character->HandleDeath();
}

void UCataclysmVitalAttributeSet::NotifyHealthChanged()
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
