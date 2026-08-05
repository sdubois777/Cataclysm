  

  
  

**CATACLYSM**

Game Design Document

*An ARPG Dungeon Crawler with Empire Management and Roguelike Systems*

**This file is the design. Edit it directly.** It is the authoritative copy and has been since 2026-08-02; the Google Drive original it was exported from is historical and is not synced back.

**There is no version number, deliberately.** This file used to carry "Version 0.3" while its filename said `_v2` and the Drive document was titled `Cataclysm_GDD_v2(1)` — three identifiers for one document, none of which was ever advanced when the design changed. The version of this document is its git history: every change arrives through a pull request, and `docs/DECISIONS.md` records the reasoning behind each one. A hand-maintained number would be a fourth thing to keep in step with the other three. The `_v2` in the filename is part of the name inherited from Drive and is not a counter.

**There is no table of contents.** The exported one was 106 links back into the Google Drive document, all pointing at the same anchor, and it went stale the moment a heading changed. GitHub builds an outline from the headings below.

# **I. Executive Summary**

Cataclysm is a dark fantasy ARPG dungeon crawler fused with real-time empire management and roguelike progression. The player defends a crumbling empire against up to eight simultaneous supernatural Cataclysms, each relentlessly spawning dungeons to destroy player cities. The core tension is time — every action costs days, dungeons resolve on timers, and the Cataclysm marches toward the capital whether the player is ready or not.

  

Between dungeon runs, players manage a layered empire of villages, cities, and metropolises, making strategic decisions about which cities to defend, when to sacrifice resources, and how to spend limited upgrade slots. Character power comes from a deep itemization system — weapon type and damage type determine available skills, passive class trees unlock unique identities and resources, and enchantments provide high-variance build-defining modifiers.

  

The game is won by defeating the Cataclysm boss dungeon. It is lost when a clear path to the capital is opened. Each run that fails feeds permanent empire-wide upgrades, making each attempt stronger than the last in a true roguelike loop.

  

|  |  |
| :-: | :-: |
| \*\*KEY PILLARS\*\* | Time is the primary resource. Decisions cascade. Power comes from itemization, not character levels alone. Every run is winnable — and every failure teaches something. |

  

# **II. Game Concept**

## **Genre and Setting**

Cataclysm is a top-down real-time action RPG set in a dark fantasy world on the brink of annihilation. The tone is grim and urgent — ancient evils stir, empires crumble, and only the player stands between civilization and oblivion. The visual and tonal direction draws from high-fantasy medieval warfare fused with eldritch cosmic horror.

  

## **Core Design Pillars**

  - Time is the primary resource — every dungeon, craft, and upgrade costs days. Players must manage their time budget against constantly resolving threats.
  - Build depth through itemization — weapon type determines which skills are available, damage type determines which passive class trees unlock, and enchantments dramatically alter how skills function.
  - Empire under siege — the player is never safe. The Cataclysm actively works to destroy the empire while the player is in dungeons, requiring constant strategic prioritization.
  - Meaningful permanent progression — each run contributes to a meta-progression empire upgrade tree, ensuring no run is wasted.
  - Eight distinct threats — each Cataclysm has a unique world mechanic that fundamentally changes the strategic layer of the game.

  

# **III. Story and Lore**

In the annals of time, an ominous force has lingered — an Ancient Evil, a malevolence veiled in the enigma of its origins. Stirring once every century, this sinister specter manifests in cataclysmic upheavals, unleashing arcane dungeons upon the realm of humanity. Within these labyrinthine depths, malevolent entities emerge, their nefarious intent focused on the unraveling of all that is dear to mankind.

  

Eight times has this cyclical malevolence gripped the world, each iteration carrying a unique harbinger of dread: Demons, Death, War, Famine, Pestilence, Celestial, Chaos, and Void. These cataclysms, etched into history's pages, strike fear into the hearts of those who recount the tales. With each surge, they challenge the resilience of civilizations teetering on the precipice of destruction.

  

As the Ancient Evil awakens, a resounding call echoes across time, summoning forth champions from the diverse tapestry of fate. These heroes bear lineages as diverse as the landscapes they traverse — icy peaks, scorching deserts, and all between. Their collective purpose: to stand against the encroaching darkness and to defy the malevolent forces threatening to plunge the world into eternal night.

  

Amidst the dungeons' perilous challenges, heroes unearth not only artifacts but the complexities of their own existence. Choices made within the dungeons echo through the corridors of time, creating a tapestry of personal struggles, redemption arcs, and sacrifices. The tale of cataclysms, heroes, and Ancient Evil beckons the brave to inscribe their legacy upon the pages of history.

  

# **IV. Gameplay Mechanics**

## **Character Creation and Customization**

When starting a new character, players choose a starting weapon type and damage type, which determines their initial skill set and first available passive class tree. Character appearance is customizable with preset body types, skin tones, hairstyles, and height. The character creator is intentionally streamlined — build identity comes from gear and passives, not appearance.

  

### **Difficulty Options**

**There are two independent choices, not one list.** A character has exactly one
lethality mode, and separately may set the Solo Self-Found flag. Hardcore Solo
Self-Found and Heretic Solo Self-Found are both real combinations.

**Lethality mode. Choose one.**

|  |  |  |  |  |
| :-: | :-: | :-: | :-: | :-: |
| \*\*Mode\*\* | \*\*Dying costs\*\* | \*\*Equipment lost on death\*\* | \*\*Heads-up display\*\* | \*\*Other\*\* |
| Standard | 5 days | none | shown | — |
| Hardcore | 10 days | each of the 18 equipped pieces drops with a 10% chance, so 1.8 on average | map overlay only | — |
| Heretic | 15 days | each equipped piece drops with a 20% chance, and at least 2 always drop, so 3.7 on average | hidden | Surges spawn 25% more dungeons. Cities have 2 upgrade slots instead of 3. |

**Solo Self-Found. Optional. Combines with any lethality mode.**

|  |  |
| :-: | :-: |
| \*\*Flag\*\* | \*\*Rules\*\* |
| Solo Self-Found (SSF) | No auction house, no shared stash. Drop rates are unchanged. |

**No mode grants increased loot.** Drop rate belongs to the difficulty tier,
which is the axis this game already scales content on. That is where every
shipped game in the genre puts it: Path of Exile's Solo Self-Found league has
drop rates identical to trade, and Diablo IV attaches drops to the World Tier
rather than to the Hardcore flag. A player choosing a harder mode is buying the
challenge, not a reward multiplier. `docs/DECISIONS.md` records the sources.

  

## **Controls and Key Bindings**

The following are default controls. Players with multiple damage types can map multiple abilities of the same type to available slots.

  

**The basic attack is on no key.** It fires automatically, as the Combat System section says. Nothing the player presses triggers it.

  

**There are two schemes and only one is active at a time.** They differ in what moves the character. The Support ability and directional movement both want W, and one key cannot be both, so each scheme gives W to one of them. Mouse movement ships as the default. The scheme is chosen by `DefaultMappingContext` in `game/Config/DefaultGame.ini`; there is no in-game setting for it yet, which needs the interface work in the Core Interface Screens section.

  

### **Scheme 1: mouse movement (default)**

|  |  |
| :-: | :-: |
| \*\*Input\*\* | \*\*Action\*\* |
| LMB | Move to the point clicked. Clicking an enemy walks toward it; it does not attack. |
| Left Shift | Held: stand still. Abilities fire without the character moving. |
| RMB | Heavy ability |
| Q | Special ability |
| W | Support ability |
| E | Aura ability (toggle) |
| R | Ultimate ability |
| Spacebar | Movement ability |
| Mouse wheel | Camera distance |
| Left stick | Directional movement |

  

### **Scheme 2: keyboard movement**

WASD moves the character, so the Support ability moves off W to 1 and the left mouse button is left unbound. Every other binding is the same as scheme 1.

  

|  |  |
| :-: | :-: |
| \*\*Input\*\* | \*\*Action\*\* |
| WASD | Directional movement |
| Left Shift | Held: stand still. Abilities fire without the character moving. |
| RMB | Heavy ability |
| Q | Special ability |
| 1 | Support ability |
| E | Aura ability (toggle) |
| R | Ultimate ability |
| Spacebar | Movement ability |
| Mouse wheel | Camera distance |
| Left stick | Directional movement |

  

**Gamepad support is partial.** The left stick moves the character in both schemes. The six ability slots are not bound to a gamepad yet.

  

## **Core Gameplay Loop**

### **Game Start**

Each run begins with a randomly selected Cataclysm. Every time a Cataclysm is defeated, the next run adds one more — so the player will eventually face all eight simultaneously. The run ends when the player defeats the Cataclysm boss dungeon or loses the capital.

  

**The active Cataclysm determines the player's damage type.** Loot is biased toward weapons tuned to the Cataclysm being fought, and because weapon damage type determines both the available skills and the available class trees, this is what shapes the build a run produces. Fighting the Demonic Cataclysm means mostly Demonic weapons drop, which unlocks the Demonic classes.

  

This is the loop the run is built around: the threat chosen at run start decides what the player can become by the end of it. With several Cataclysms active at once, drops are biased across all of them, which is how a player accumulates the multiple damage types that make multiclassing possible.

  

### **The Surge**

A Surge is triggered at run start and recurs after a fixed number of days or when a city falls. During a Surge, the Cataclysm releases a wave of dungeons that assault random player cities. Players must prioritize which dungeons to tackle before they resolve and which cities to sacrifice.

  

### **Dungeon Runs**

The player enters dungeons to defeat enemies, collect loot, and prevent city loss. Each dungeon has a time-to-clear estimate, a resolve timer (what happens if ignored), floor count, modifiers, and a boss at the final floor. Deeper dungeons yield better rewards but cost more time.

  

### **Capital and Crafting**

Between dungeon runs, players return to the capital to manage gear, level up passive trees, visit NPCs, and plan their next moves. All capital services cost time, reinforcing the core tension between power investment and empire defense.

  

### **Last Stand**

If a clear path is opened to the capital, the Cataclysm boss dungeon moves there. All remaining dungeons on the map are absorbed into the boss dungeon as additional floors. Dying in the Last Stand dungeon ends the run.

  

## **Combat System**

### **Real-Time Action**

Combat is real-time top-down action. Players must read and dodge telegraphed enemy attacks, manage their active resource (Fury, Resolve, Preparation, or equivalent based on class), and deploy their skill kit strategically. Positioning matters — some skills reward melee range, others reward distance, and AOE threats punish clustering.

  

### **Skill Slots**

Each player has six skill slots. Basic attacks are handled automatically and are enhanced by all damage types present.

  

The equipped weapons determine the **pool** of skills a player can draw from, not the contents of each slot. Every combination of an equipped weapon type and an available damage type contributes its skills to that pool. The player then chooses which skills to use and assigns them to slots.

  

A player carrying several damage types will have far more skills available than slots to hold them. Choosing which six to take is part of building a character, and it is why gear that widens the pool is valuable even when its raw statistics are no better.

  

|  |  |
| :-: | :-: |
| \*\*Slot\*\* | \*\*Description\*\* |
| Basic Attack | Automatic — augmented by all damage types on the weapon. Can be a damage source or resource generator depending on build. |
| Heavy Attack (RMB) | Moderate cooldown. More impactful hits with slower or longer animations. Often the primary damage button. |
| Special (Q) | Highly varied — traps, deployables, grenades, pets, terrain effects. Defines playstyle more than any other slot. |
| Support (W) | Buffs, shields, stances, curses, banners. Shorter to medium duration utility. |
| Aura (E) | Persistent AOE effect centered on the player. Drains mana or HP per second while active. Toggled on and off. |
| Ultimate (R) | High-impact, long cooldown. Reserved for critical moments or boss phases. |
| Movement (Space) | Gap closers, escapes, repositioning tools. Some also deal damage or deploy effects on use. |

  

### **What a Skill Is Worth**

Every skill deals a percentage of **weapon damage**, which means the weapon's own damage plus any flat added damage from gear. That is what makes flat added damage affixes worth taking.

  

**The Basic Attack is 100% by definition, and every other slot is measured against it.** It is the ordinary hit, so it is the figure the damage target in section VI refers to and the one every affix value was fitted to.

  

|  |  |  |  |
| :-- | :-: | :-: | :-- |
| \*\*Slot\*\* | \*\*Typical\*\* | \*\*Range\*\* | \*\*Why\*\* |
| Basic Attack | 100% | fixed | Automatic and free. It is weapon damage. |
| Movement | 100% | 75–150% | Some also deal damage, so an ordinary hit is the right middle. |
| Support | 0% | 0–100% | Buffs, shields, stances, curses and banners usually deal none. |
| Aura | 25% per second | 15–40% | Persistent and toggled, draining resource while active. |
| Special | 150% | 100–250% | Traps, deployables, grenades and pets. The most varied slot. |
| Heavy Attack | 250% | 175–350% | The primary damage button, on a moderate cooldown. |
| Ultimate | 400% | 300–500% | Long cooldown, reserved for critical moments. |

  

**A skill may state its own figure, and four already do.** Skull Splitter says 500% weapon damage, Annihilator says 300%, Bulwark caps stored damage at 200%, and Haymaker's wall impact adds 100%. **The Ultimate range above is exactly those two Ultimates**, so it is read off the design rather than chosen.

  

At difficulty tier 8 that puts an ordinary hit at 1,681, a Heavy Attack at 4,202 and an Ultimate at 6,724 — the last being more than an average Common enemy's entire health.

  

### **What a Skill Costs**

Two things limit how often a skill is used: a cooldown in seconds, and a mana cost. Both belong to the slot, and a skill states its own only when it differs, exactly as it does for damage. The stat source table above already says the base cooldown comes from the skill being used.

  

|  |  |  |  |
| :-- | :-: | :-: | :-- |
| \*\*Slot\*\* | \*\*Cooldown\*\* | \*\*Band\*\* | \*\*Mana\*\* |
| Basic Attack | none | — | restores 6 on hit |
| Heavy Attack | 1.5s | 1–4s | 15 |
| Support | 4s | 2–10s | 25 |
| Special | 5s | 3–10s | 40 |
| Movement | 5s | 3–10s | 20 |
| Ultimate | 20s | 12–40s | 150 |
| Aura | none | — | 20 per second |

  

**Only two slots have no cooldown, and each has a different reason.** The Basic Attack is automatic, so the weapon's attack speed sets its rate. The Aura is a toggle, so there is nothing to wait for; it is paid for by draining mana instead. Every other slot waits.

  

**Mana costs are flat numbers, and the same number for every class.** That is what makes a large mana pool worth having: it buys more casts of the same skill rather than paying a proportionally larger price for each one. Every source of maximum mana is pure gain for the same reason — the Mind attribute, two affixes and a hybrid.

  

**The numbers above are quoted at level 100, and scale down with character level.** A cost that never moved would be crippling at level 1 and beneath notice at level 100, because a Ravager's mana pool runs from 40 to 436. Costs ride the default mana progression, so a skill takes the same share of a pool at both ends. What the player reads is still a flat quantity of mana.

  

### **The Basic Attack Restores Mana, and This Is Not a Generator**

  

The automatic basic attack returns 6 mana each time it lands. At a typical 1.3 attacks per second that is about 8 mana per second while fighting.

  

**This is deliberately not the generator and spender pattern**, which players of that pattern describe as casting a weak skill about five times to afford one real one. Two things prevent it here.

  

  - **The basic attack is automatic.** There is no button to press and no rotation to perform. It is income for being in a fight rather than a filler action.
  - **The Heavy Attack is affordable from mana regeneration alone.** Used the moment it returns, it costs 10 mana per second against the 10.9 per second a character regenerates at level 100. It works with no basic attacks landing at all. Mana on hit pays for the other slots, so it is a supplement and never the thing that makes the primary damage button function.

  

**What this produces at level 100, with no gear and no attribute points:**

  

|  |  |  |  |  |
| :-- | :-: | :-: | :-: | :-- |
| \*\*Class\*\* | \*\*Mana\*\* | \*\*Regen\*\* | \*\*Income while fighting\*\* | \*\*Everything on cooldown lasts\*\* |
| Ravager | 436 | 10.9/s | 18.6/s | 25s |
| Ritualist | 1,278 | 26.8/s | 34.6/s | effectively unlimited |
| Masochist | 644 | 10.9/s | 19.6/s | 40s |

  

Using every skill the moment it returns costs 35.75 mana per second, the same for all three because the costs are flat. So a character can spend everything for roughly half a minute and must then choose which skills to keep using. The Ritualist is the exception and is meant to be: sustaining its whole kit is what its mana pool and regeneration are for.

  

**The Aura is a commitment rather than something left on.** It drains 20 mana per second, so standing still it empties a Ravager's pool in 48 seconds and a Masochist's in 71. The Ritualist's 26.8 per second regeneration covers the drain, so it alone can hold an aura indefinitely.

  

### **Skill Acquisition**

Skills are not leveled or unlocked through a skill tree. They are determined entirely by weapon type and damage type. Changing your weapon changes your available skill set. This keeps the skill system tightly coupled to itemization and makes gear drops feel meaningful beyond raw stat comparisons.

  

### **Tactics and Strategy**

Players must pay attention to enemy telegraphed attacks to stay alive. Some classes possess enemies or summon minions, others kite using movement and deployables, and others wade in relying on armor, block, and sustain. Builds emerge from the intersection of weapon type, damage type, passive tree investment, and enchantment choices.

  

## **Passive Class Trees**

### **Overview**

Each damage type unlocks three class passive trees. Players can spec into one class per damage type available on their weapon. If a player has multiple damage types, they can multiclass across multiple trees simultaneously, spending the same shared pool of passive points.

  

Each class tree has approximately 74 nodes, 15 keystones, 4 capstone tiers (at 25/50/100/200 points), and a total of \~440 spendable points. The per-character point budget is 230, meaning players invest in roughly 53% of any tree — specialization is required.

  

### **Node Types**

|  |  |
| :-: | :-: |
| \*\*Type\*\* | \*\*Description\*\* |
| Basic Nodes | Scaling stat nodes with per-point benefits and threshold bonuses at mid-investment. Most of the tree. |
| Keystones | Single-point investments that fundamentally change how a mechanic works. Build-defining. Require full investment in a parent node. |
| Capstones | One per tier (25/50/100/200 pts). Player chooses one of three options per tier. Escalating power from identity declaration to god-tier mechanics. |

  

### **Class Resource Systems**

Each class has a unique resource that the passive tree unlocks and develops. Resources are central to how the class plays — they are not optional stat bars, they are the engine of the build.

  

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Resource & Summary\*\* |
| Bulwark (War) | Resolve — builds through combat, enables damage reduction and retaliation bursts. Decays out of combat. |
| Berserker (War) | Fury — builds on critical melee hits, decays out of combat. At max Fury, Berserking triggers devastating strikes. Wrath (2H) and Frenzy (DW) are sustained drain states. |
| Saboteur (War) | Preparation — builds by placing and triggering traps and gadgets. Does not decay. Powers trap AOE, evasion, and gadget empowerment. |

  

### **Multiclassing**

Players with multiple damage types on their weapon can invest in multiple class trees simultaneously. All trees draw from the same shared point pool, so multiclassing means spreading investment thinner. The deep nodes and capstones in any single tree require focused investment to reach, creating genuine build tradeoffs.

  

## **Classes by Damage Type**

  

### **War**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Bulwark | The defensive anchor. Masters of block, armor, and Resolve. Can absorb punishment and retaliate with stored energy. Excels at being an immovable frontliner. Resource: Resolve. |
| Berserker | Fury-driven melee aggressor. Builds Fury on crits, fires off devastating Berserking strikes at max Fury. Two sub-identities: 2H/Wrath (heavy hits, AOE, execute) and DW/Frenzy (attack speed, hit volume, chaining). Resource: Fury. |
| Saboteur | Trap and gadget specialist. Lays proximity mines, deploys turrets and ballistas, and controls space through deployables. Evasion woven throughout the tree as a class-wide survival stat. Pairs with Dagger, Crossbow, and Spear. Resource: Preparation. |

  

### **Demonic**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Ravager | Frontline aggressor embodying raw demonic power. Brutal melee combat style with devastating strength. |
| Ritualist | Summoner and manipulator of demonic forces. Commands demonic entities and can possess enemies to turn them against allies. |
| Masochist | Converts received damage into buffs and counterattacks. Uses HP instead of mana for abilities. |

  

### **Death**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Soul Collector | Siphons life essence from enemies, weakening foes while strengthening self. Channels stolen life force into stat enhancement and soul-based abilities. |
| Necromancer | Summons and commands the restless dead. Raises skeletal minions, spectral entities, and undead armies. |
| Shadow | Melds with darkness for unparalleled mobility. Can move through obstacles and traverse inaccessible areas. |

  

### **Pestilence**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Plague Lord | Harbinger of disease and decay. Commands dark magic and vile concoctions that spread sickness and weaken entire groups of enemies. |
| Virion | Close-quarters combatant with virulent poisons and rotting diseases. Stacks debuffs progressively on enemies to drain defenses and vitality. |
| Poison Master | Concocts potent toxins and brews deadly potions. Deploys poisonous projectiles, clouds, and venomous traps. |

  

### **Famine**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Vampire | High health leech sustain. Cannot use energy shields. Survives through aggressive life-stealing. |
| Energy Leech | Disrupts enemy spellcasting by draining mana reserves. Siphoned mana replenishes own pool, enabling devastating abilities. |
| Shield Breaker | Steals and dismantles enemy energy shields. Identifies and exploits defensive barrier weaknesses. |

  

### **Celestial**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Nephilim | Forsaken offspring of celestial beings and mortals. Excels at melee combat enhanced by powerful heavenly spells. |
| Zealous Inquisitor | Wields holy fire to cleanse corruption. Mid-range damage dealer that exposes enemy weaknesses and punishes prolonged fights. |
| Dawnbringer | Channels sun and stars for destruction and healing. High mobility hybrid alternating between offensive solar bursts and healing starlight. |

  

### **Chaos**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Agent of Chaos | Highly random damage and effects. Damage ranges from extremely low to extremely high. If multiclassing, secondary class perks also gain randomness. |
| Chaos Shaper | Shape-shifting adaptability. Assumes different forms based on weapon type, each with distinct strengths and weaknesses. |
| Discordant Trickster | Master of illusions and deception. Creates lifelike illusions to disorient foes, disrupt formations, and manipulate perception. |

  

### **Void**

|  |  |
| :-: | :-: |
| \*\*Class\*\* | \*\*Identity\*\* |
| Singularity | Manipulates gravity for area control. Creates gravitational anomalies that alter movement and positioning of enemies and allies. |
| Avatar of Madness | Induces madness and psychic confusion. Distorts perceptions and drives enemies to hallucination, paranoia, and fear. |
| The Maw | Consumes items and enemies for Attribute points. Rarity of consumed entity determines the reward. |

  

## **Character Stats and Attributes**

### **Power Score**

A player's Power Score combines level, gear quality, gear level, gem quality, socket count, and resistances into a single comparable number, on the same scale as the Enemy Score in section X. Dungeons within a close power range provide average exp and loot. Dungeons below the threshold provide less. Dungeons above provide more.

  

Power Score = (Level Weight × Character Level) + (Gear Weight × Σ over equipped pieces of Rarity × (1 + Upgrade Factor × Gear Level)) + (Gem Weight × Σ over filled sockets of Gem Rarity) + (Resistance Weight × Σ over the eight resistances of Resistance Percent)

  

|  |  |
| :-: | :-: |
| \*\*Weight\*\* | \*\*Value\*\* |
| Level Weight | 6.3270 |
| Gear Weight | 6.2330 |
| Upgrade Factor | 0.2525 |
| Gem Weight | 5.2725 |
| Resistance Weight | 1.1298 |

  

Rarity is 1 for Everyday through 8 for Cataclysmic, the same eight tiers used for both gear and gems. Gear Level is the +1 to +10 upgrade level from section VI. Gem quality and gem level are the same axis; a gem has one position on the eight-tier rarity scale. Gear has two independent axes, its rarity and its upgrade level.

  

Four rules follow from the formula:

  

  - **Socket count has no weight of its own.** It is the number of terms in the gem sum, so filling a socket is what a socket contributes.
  - **Gear upgrade level multiplies gear rarity rather than adding to it.** A +10 Cataclysmic piece is worth 3.52 times the same piece at +0, and eight times what a +10 Everyday piece is worth. This is the only place two inputs multiply, and it is what makes the power curve rise faster than the difficulty tier.
  - **Two one-handed weapons count as one equipped piece**, the same way they give the same six sockets a two-handed weapon gives. Dual wielding must not be worth free Power Score.
  - **Resistance above the 70% cap adds no Power Score.** Over-capping remains legal and useful, because enemy penetration reduces effective resistance, but it is headroom against penetration rather than power.

  

Eighteen equipped pieces carry a rarity and an upgrade level: seven armor pieces, eight rings, the necklace, the relic, and the weapon. The four potion slots are consumables rather than gear and contribute through their sockets only.

  

At level 100 with eighteen Cataclysmic pieces at +10, forty-five Cataclysmic gems and all eight resistances capped, the four terms contribute 10% from level, 50% from gear, 30% from gems and 10% from resistances.

  

### **Expected Character by Tier**

Power Score is calibrated against the tier ranges in section XII using the reference character below, which is the **ceiling** at the end of each difficulty tier: the best gear that tier can produce, fully upgraded, with every socket filled. It is a calibration reference, not a requirement. Actual leveling is player-driven, because one player may clear a hundred dungeons in a tier where another clears forty.

  

**A real character sits below the anchor, and that is intended.** At tier 8 the reference character is Cataclysmic on all eighteen pieces, and a Cataclysmic piece spends all four of its slots on enchantments, so that character carries 72 enchantments and no regular affixes at all. Every gear rarity is a trade rather than a straight upgrade, so a build that keeps some ordinary stats is Masterful or Legendary on some pieces and scores less. Measured against the tier 8 anchor of 6,327:

  

|  |  |  |
| :-- | :-: | :-: |
| \*\*Gear on every piece at tier 8\*\* | \*\*Power Score\*\* | \*\*Against the anchor\*\* |
| Cataclysmic | 6,327 | 100% |
| Ascendant | 5,932 | 94% |
| Mythical | 5,536 | 88% |
| Legendary | 5,141 | 81% |
| Masterful | 4,745 | 75% |
| A mix of 4 Cataclysmic, 4 Ascendant, 5 Mythical and 5 Legendary | 5,690 | 90% |

  

So chasing Cataclysmic gear is what pushes a character toward the maximum Power Score, and the anchors describe that ceiling rather than a typical build. **This matters when reading any statement about what a tier 8 character has.** The affix values in section VI were fitted against 72 regular affix slots, which is a full set of Masterful gear; the character sitting exactly on the tier 8 anchor has none of them.

  

|  |  |  |  |  |  |
| :-: | :-: | :-: | :-: | :-: | :-: |
| \*\*Tier\*\* | \*\*Level\*\* | \*\*Gear Rarity\*\* | \*\*Gear Level\*\* | \*\*Gems Filled\*\* | \*\*Each Resistance\*\* |
| T1 | 12 | Everyday | \\+3 | 6 | 8.8% |
| T2 | 25 | Quality | \\+4 | 11 | 17.5% |
| T3 | 38 | Superb | \\+5 | 17 | 26.2% |
| T4 | 50 | Masterful | \\+6 | 22 | 35.0% |
| T5 | 62 | Legendary | \\+7 | 28 | 43.8% |
| T6 | 75 | Mythical | \\+8 | 34 | 52.5% |
| T7 | 88 | Ascendant | \\+9 | 39 | 61.2% |
| T8 | 100 | Cataclysmic | \\+10 | 45 | 70.0% |

  

Gear and gem rarity equal the difficulty tier because there are eight of each, and because the best upgrade stone that can drop is capped by the current difficulty tier. Gear level is tier + 2 capped at +10, which clears every rarity gate in section VI and reaches exactly +10 at tier 8.

  

This reference character scores 6,327 at tier 8, landing exactly on the tier 8 anchor, and 384 against the tier 1 anchor of 385. The six tiers in between are within 5.3%, and that residual is not a defect of the formula: tier 5 is 1,107 points wide where the surrounding trend is about 790, and no smoothly progressing character can pass through that kink.

  

### **Attributes**

Players gain 1 attribute point per level. Attributes are spread across eight categories:

  

|  |  |  |
| :-: | :-: | :-: |
| \*\*Attribute\*\* | \*\*Stats\*\* | \*\*Per Point\*\* |
| Agility | Move Speed / Evasion | \\+2% move speed / +0.5% evasion |
| Ferocity | Crit Chance / Crit Multiplier | \\+0.5% crit chance / +5% crit multiplier |
| Constitution | Armor / Block Chance | \\+2% armor / +1% block chance |
| Vitality | Max HP / HP Regen | \\+2% HP / +1% increased HP regeneration |
| Mind | Max Mana / Mana Regen | \\+2% mana / +1% increased mana regeneration |
| Spirit | Energy Shield / Shield Regen | \\+2% energy shield / +1% increased shield regeneration |
| Efficacy | CDR / AOE / DoT Frequency | \\+1% cooldown increase / +2% AOE / +1% increased DoT frequency |
| Luck | Magic Find / Loot Quantity | \\+0.01% magic find / +1% loot quantity |

  

### **The Character Sheet**

A character has 43 stats, grouped the way the gameplay tag list groups its Stat tags.

  

|  |  |
| :-: | :-- |
| \*\*Group\*\* | \*\*Stats\*\* |
| Resource | Maximum Health, Maximum Mana, Maximum Energy Shield, Class Resource |
| Recovery | Health Regeneration, Mana Regeneration, Energy Shield Regeneration, Life Leech, Mana Leech, Energy Shield Leech |
| Defence | Armor, Evasion, Block Chance, Damage Reduction, Retaliation, Crowd Control Resistance, and the eight Resistances |
| Offence | Critical Strike Chance, Critical Strike Multiplier, Attack Speed, Area of Effect, Damage over Time Frequency, Penetration, Spell Damage, and the eight Damage Against a Type figures |
| Utility | Movement Speed, Cooldown Reduction, Magic Find, Loot Quantity |

  

The tag list also has entries for defence against melee, ranged and spell damage. Those are **scopes that modifiers target**, not separate lines on the sheet. An enchantment reading "you cannot evade or block melee attacks" targets a scope; it does not describe a stat a character has.

  

### **Where Each Stat's Base Comes From**

Attributes only scale. A stat therefore needs a base value before any attribute can do anything with it, and that base comes from one of three places.

  

|  |  |
| :-: | :-- |
| \*\*Source\*\* | \*\*Stats\*\* |
| The class | Vitals, recovery, defences, resistances, movement speed, area of effect, damage over time frequency |
| The equipped weapon | Attack speed, and off this sheet, attack range and attack damage |
| The skill being used | Critical strike chance, and off this sheet, the base cooldown, projectile count and duration |

  

**A class does not need a base above zero for every stat.** It needs one for every stat it wants its attributes to scale. A class with no base evasion gains no evasion from Agility, and that is the system working rather than failing — it is how a class declines to care about a stat.

  

**Critical strike chance belongs to the skill, not the character.** Each skill carries its own base chance, and the character's gear and attributes scale it. A character has no critical strike chance in the abstract.

  

**Area of effect and damage over time frequency belong to the character, not the skill.** They are percentages of whatever the skill itself does, so their baseline is 100% rather than zero. A class naturally better at either starts above 100.

  

**Loot quantity has a baseline of 100% for the same reason.** It is a percentage of whatever the dungeon would otherwise drop, so 100 means unchanged and a character with no bonuses sits there. Every source of loot quantity is an increase — the Luck attribute, the Increased Loot Quantity affix, the hybrid affix pairing it with magic find, and several Explorer branch nodes on the empire tree — and an increase applied to a baseline of zero is zero, so a baseline of 100 is what makes any of them do anything.

  

**Magic find is not the same shape, and its baseline is zero.** It is an added percentage rather than a percentage of something, and it has a flat source: the Flat Magic Find affix. That is the base the Luck attribute then scales.

  

**The stat is called Magic Find, and it has only that name.** It raises the chance that a drop is a higher gear rarity. Gems, affixes, city upgrades, dungeon modifiers and the Luck attribute all feed the same number, and one name is what lets a player add them up. Two other names for it were in use in the shipped tables until 2026-08-05; `docs/DECISIONS.md` records which, and why this one was kept.

  

**Gear rarity and enemy rarity are different things and keep the word "rarity".** Gear rarity is the eight-tier ladder from Everyday to Cataclysmic; enemy rarity is the Common to Cataclysm Boss ladder in section X. Magic find shifts the first of those; it is not one of them.

  

**Movement speed is measured in metres per second.** A tank sits at roughly 3. Agility scales that value, so a tank with points invested moves at 3 × (1 + increases).

  

### **Leech**

There are three leech stats, and all three work the same way. Life leech fills health, mana leech fills mana, energy shield leech fills the energy shield.

  

**Leech is a percentage of the damage actually dealt.** A character with 3% life leech who lands a hit for 400 damage leeches 12 health. It is the damage the target really took, after its resistances, armour and block, not the damage the attack would have done to nothing.

  

**Overkill does not count.** An enemy with 25 health left, hit for 400, contributes 25 to the leech calculation and not 400. Without this rule the last hit on every trash enemy is the largest heal in the game, which rewards overkilling rather than fighting.

  

**Leech arrives over 3 seconds, not instantly.** The amount is worked out on the hit and then paid out across the next 3 seconds. Instant leech makes a character that is winning unkillable and does nothing for one that is losing, because the recovery arrives only as fast as the damage does. Spreading it means a burst of damage is not a burst of health, and it is what makes leech a sustain stat rather than a second health pool.

  

**Leech from several hits runs at the same time.** Each hit starts its own 3-second payout. A character hitting continuously therefore reaches a steady state of roughly three hits' worth of leech in flight.

  

|  |  |
| :-- | :-- |
| \*\*Rule\*\* | \*\*Value\*\* |
| What is leeched | A percentage of damage actually dealt |
| Damage counted | After the target's mitigation, capped at the target's remaining health |
| Payout period | 3 seconds from the hit |
| Concurrent payouts | Unlimited; each hit pays out separately |

  

**Both numbers are a starting point and expected to move.** The 3-second period and the affix values are tuned against real play; see the affix pool section for the values gear supplies.

  

**Where this shape comes from.** Last Epoch pays leech out over a fixed 3-second period and excludes overkill damage, which is the model above. Path of Exile instead caps each leech instance at 10% of maximum life and the total recovery rate at 20% of maximum life per second. Both exist to stop leech being instant. The simpler of the two was taken because this design already prefers one readable rule over a system of caps, in the same way an enemy carries one stack of an effect rather than many.

  

### **Increases Are Scoped by Tag**

Every skill carries gameplay tags, which is how the game knows which enchantments and effects apply to it. The character holds all of its own increases, and an increase reaches a skill when the tags match.

  

An item granting increased area of effect is not a property of any one skill. The character holds it, and it applies to every skill tagged for area of effect.

  

Matching is hierarchical, following the tag names. A modifier requiring `Type.AOE` applies to a skill tagged `Type.AOE.PointBlank`. A modifier requiring `Scope.Global` applies to everything. A modifier requiring several tags needs all of them.

  

|  |  |  |
| :-: | :-: | :-- |
| \*\*Skill\*\* | \*\*Area of Effect\*\* | \*\*Its Tags\*\* |
| Smoke Bomb | 140% | Item.Weapon.Dagger, Type.AOE.PointBlank |
| Thrust | 100% | Item.Weapon.Spear, Type.Strike |

  

Both characters wear the same item, granting +40% area of effect restricted to `Type.AOE`. It reaches the first skill and not the second. The character holds the increase either way.

  

### **Class Stat Lines**

A class supplies a level 1 base and a per-level gain for each stat it wants to scale. Across 24 classes and the 33 stats a class supplies — every stat but attack speed, which comes from the weapon, and critical strike chance, which comes from the skill — that is 1,584 numbers, so every class starts from a shared default stat line and overrides only the stats that express its identity. A class may override any stat; the default is a starting point, not a floor.

  

Per-level scaling is linear. Whether it should stay linear is not settled and will be decided by testing rather than argument.

  

**A class is defined as much by what it refuses as by what it takes.** The Berserker tree has almost no armor and no evasion at all. The Saboteur has no armor, no critical strike investment and no leech. Leaving a stat at the default is how a class declines to care about it, and it is what makes classes feel different before a single point is spent.

  

### **The Three Demonic Class Stat Lines**

These are the three classes the vertical slice needs, because a damage type unlocks all three of its class trees. Values are at level 100 with no gear and no attribute points spent. Only the stats any of the three overrides are listed; the remaining 19 are identical across all three.

  

|  |  |  |  |
| :-- | :-: | :-: | :-: |
| \*\*Stat\*\* | \*\*Ravager\*\* | \*\*Ritualist\*\* | \*\*Masochist\*\* |
| Maximum Health | 2,110 | 1,060 | 2,526 |
| Maximum Mana | 436 | 1,278 | 644 |
| Maximum Energy Shield | 0 | 832 | 0 |
| Health Regeneration | 15.8 | 15.8 | 37.6 |
| Mana Regeneration | 10.9 | 26.8 | 10.9 |
| Armor | 371 | 0 | 55 |
| Evasion | 0 | 0 | 0 |
| Damage Reduction | 8% | 0 | 0 |
| Retaliation | 0 | 0 | 158 |
| Life Leech | 3% | 0 | 0 |
| Movement Speed | 4.6 | 3.5 | 4.0 |
| Spell Damage | 0 | 158 | 0 |
| Crowd Control Resistance | 19.9 | 0 | 29.8 |
| Class Resource | 100 | 150 | 100 |

  

**Ravager.** A frontline aggressor that is hard to stop rather than one that hits hardest. Where the Berserker is a shock troop, the Ravager is the consistent fighter: the most armor of the three, flat damage reduction, enough leech to hold a line, crowd control resistance, and the fastest movement so it is always in contact. It takes no evasion and no energy shield. A frontline aggressor that cannot close is not one, which is why movement speed matters as much as armor here.

  

**Ritualist.** The caster of the three and the only one with an energy shield. The Saboteur deploys objects that sit where they are put; the Ritualist commands things that were alive, and in the case of possession, things that still belong to the enemy. Frailest health, largest mana pool, the only meaningful mana regeneration, slowest on foot, no armor and no evasion. It survives at range and behind what it summons rather than by being hard to hit. Its health is low deliberately: attributes, gear and multiclassing all scale it.

  

**Masochist.** The largest health pool and by far the largest regeneration, because for this class health regeneration is resource regeneration — a Masochist that cannot regain health cannot act. It takes retaliation and low armor, and refuses evasion and energy shield: evading is missing out, and a shield absorbs the damage the class needs to convert.

  

The Masochist keeps a normal mana pool. "Uses HP instead of mana" is delivered by a keystone or capstone in its passive tree that converts all mana into added health, so the conversion is a build choice rather than a starting condition.

  

Class resource **behaviour** is not set here, only the pool size. What each resource does, how it builds and how it decays belongs with the passive trees.

  

### **Stat Calculation**

Every percentage in the attribute table needs a base to apply to and a rule for combining with other sources. These are those rules, and they apply to gear affixes as well as attribute points.

  

**Attributes scale values, they do not create them.** Health, mana and energy shield come from three places: the class's base value, its per-level scaling, and flat values from gear. Vitality's +2% HP multiplies the result those three produce. It does not generate health on its own. The same holds for Mind and mana, and Spirit and energy shield.

  

**One bucket per stat, one multiplication.**

  

Final Value = Base Value × (1 + Sum of Increases) × Product of More Multipliers

  

Attribute points and every gear affix worded "increased" add together into one bucket per stat, and that bucket multiplies the base once. Only sources worded "more" or "less" multiply separately, and that wording is reserved for **gems, passive tree keystones and enchantments**, where the design already wants outsized effects.

  

**An ordinary gear affix is never a "more" multiplier.** It is flat or it is increased. Keeping the multiplicative sources off gear rolls is what keeps a rare drop readable, and it gives the enchantment library a job that ordinary affixes cannot do.

  

**Everything in the increased bucket has diminishing returns and every "more" multiplier does not.** That is the whole reason for having two buckets rather than one. A character at +800% increased who adds another +60% increased gains 6.7%; the same character adding a 60% "more" multiplier gains 60%. Two independent 50% "more" sources give 2.25 times, not 2.0 times.

  

|  |  |  |
| :-: | :-: | :-- |
| \*\*Already held\*\* | \*\*Another +60% increased is worth\*\* | \*\*A 60% "more" is worth\*\* |
| +0% | 60.0% | 60.0% |
| +100% | 30.0% | 60.0% |
| +300% | 15.0% | 60.0% |
| +800% | 6.7% | 60.0% |

  

So the question a player is answering when they compare two items is which independent multiplier they are missing, not which number is biggest.

  

**Gear upgrade level multiplies every affix on that piece.** A +10 piece gives about 3.52 times what the same piece gives at +0, using the same factor by which upgrade level multiplies gear rarity for Power Score in section IV. Affix values stated anywhere in this document are the +10 figures.

  

So 50 points of Vitality is +100% health, not 2.7 times health. Compounding 2% per point would give 7.2 times at 100 points, which leaves no room for gear inside the Power Score ranges in section XII.

  

**Regeneration percentages are increases to a base rate, not percentages of the maximum.**

  

Final Regeneration = Base Regeneration × (1 + Sum of Increases)

  

Read literally as 1% of maximum health per second, 50 points of Vitality would return half the character's health every second. The base regeneration rate is a small flat value per second, supplied the same way base health is. This applies to health, mana and energy shield regeneration alike.

  

**Cooldown reduction divides rather than subtracts.**

  

Final Cooldown = Base Cooldown / ((1 + Sum of Increases) × Product of More Multipliers)

  

The skill supplies the base cooldown and the character's accumulated increases apply on top of it. What the interface shows the player is the effective reduction, which is (Divisor − 1) / Divisor, so a character shown as having 25% cooldown reduction turns a 4-second skill into a 3-second one.

  

**A "more" multiplier divides here as well, rather than multiplying.** Cooldown reduction is a rate: an increase makes the interval shorter, so a "more cooldown reduction" source has to make it shorter too. Multiplying would make a cooldown reduction gem lengthen the cooldown. Because both buckets divide, no number of them brings a cooldown to zero, which is why the stat needs no cap.

  

|  |  |  |
| :-: | :-: | :-: |
| \*\*Efficacy Points\*\* | \*\*4-Second Skill Becomes\*\* | \*\*Shown As\*\* |
| 25 | 3.20s | 20.0% |
| 50 | 2.67s | 33.3% |
| 100 | 2.00s | 50.0% |
| 100, plus gear worth another 3.00 | 0.80s | 80.0% |

  

Dividing rather than subtracting is what keeps this from breaking. Subtracting 1% per point would reach zero cooldowns at 100 points of Efficacy. Dividing, all 100 points halve every cooldown, gear pushes further with each point worth progressively less, and zero is unreachable. No cap is needed, and no point is ever wasted.

  

Damage-over-time frequency uses the same form, because it is also a rate. Area of effect at +2% per point stays additive, because a larger radius has no runaway.

  

**Caps.**

  

|  |  |  |
| :-: | :-: | :-: |
| \*\*Stat\*\* | \*\*Cap\*\* | \*\*Hard or Soft\*\* |
| Resistances | 70% | Soft. Resistance above it is worth having; one enchantment raises the cap itself, to a ceiling of 90%. |
| Evasion | 60% | Soft. Gear enchantments may exceed it. |
| Crit chance | 100% | Hard. Above 100% it means nothing. |
| Block chance | none | No cap. A block is not a full avoid. |
| Cooldown reduction | none | No cap needed. The formula cannot reach zero. |

  

Over-capping resistance matters because Overwhelm reduces effective resistance whenever the player fights above their Power Score, so the headroom is what keeps a character at the cap in practice. Over-capped resistance contributes no Power Score, as section IV states.

  

**Avoidance.** Evasion and block behave differently and are not interchangeable.

  

  - **Evasion avoids an attack completely, but applies only to direct attacks.** Area damage lands regardless of evasion. This is why evasion's cap can be soft: even at 100% evasion a character is not immune.
  - **Block reduces the damage of a blocked hit by 50%, it does not prevent it.** Block chance is the chance that reduction applies.
  - **Block applies to area damage as well as direct attacks.** A raised shield helps against an explosion in a way that dodging does not.
  - Because a block removes half the damage rather than all of it, block chance needs no cap. A character at 100% block chance has 50% damage reduction, which is strong but is not immunity.

  

### **The Damage Calculation**

One incoming hit is resolved in this order. Each step operates on what the previous step left.

  

|  |  |
| :-: | :-- |
| \*\*Step\*\* | \*\*What happens\*\* |
| 1. Evasion | Direct attacks only. An evaded hit stops here and does nothing. |
| 2. Block | Removes 50% of what remains. Applies to area damage as well. |
| 3. Armor | Reduces damage by `armor / (armor + K)`, where K is 800 × the difficulty tier, capped at 75%. |
| 4. Resistance | The attacker's Penetration and any Overwhelm are subtracted first, then the result is capped at 70%. |
| 5. Damage reduction | The flat percentage stat. |
| 6. Mana | Only for damage over time, and only if an enchantment grants it. |
| 7. Energy shield | Absorbs before health, one for one. Does not absorb damage over time. |
| 8. Health | Takes whatever is left. |

  

**Armor uses a curve, not a subtraction.** `armor / (armor + K)` never reaches 100%, so no amount of armor is immunity, and it has natural diminishing returns so the first points matter most. K rising with the difficulty tier is what stops armor earned early from keeping its value forever: 371 armor is worth 32% at tier 1 and 5% at tier 8.

  

**Penetration is applied before the 70% cap, not after.** This is the rule that makes over-capping worth anything, and it is the reason the cap is described as soft. Against 30 penetration, a character at 100 resistance still sits at the 70% cap, while one at exactly 70 drops to 40%. Capping first would make every point above 70 worthless and contradict the design's own allowance for over-capping via affixes. Overwhelm is subtracted at the same point and for the same reason.

  

**Penetration is a player stat. Enemies reduce resistance through Overwhelm instead.** Ordinary enemies carry no Penetration value of their own; what cuts into a player's resistance is the Power Score gap, described below. An enemy modifier may still grant penetration as a specific effect, in the same way it grants a burning aura.

  

**Armor penetration and resistance penetration are separate stats.** Affixes grant them separately — ignoring armor and ignoring resistances appear as different modifiers throughout the enchantment tables. Piercing weapons add their 20% on top of whatever gear provides, up to all of a target's armor.

  

**No combination of these layers reaches immunity.** Each has either a cap or a curve that cannot reach zero damage.

  

### **Energy Shield**

Energy shield is a distinct defence with its own rules, not a second health bar.

  

  - **It does not absorb damage over time.** Bleed, poison, burn and the rest pass straight through it to health.
  - **It refills 3 seconds after the character last took damage.** Taking damage again inside that window restarts the wait.
  - **Damage over time restarts that wait as well.** So damage over time both bypasses the shield and holds it empty, which is what makes it the answer to shield stacking rather than a stat check.
  - **Magic weapons strip 10% more of it** per hit than other sub-types.
  - Breaking the shield is a distinct event that other effects can trigger on.

  

Most classes have no energy shield at all. It is given to classes that thematically warrant it, such as casters.

  

### **Resistances**

There are eight resistances, one per damage type. Each caps at 70%. Resistance is reduced by Overwhelm when the player fights above their Power Score. Over-capping resistance is possible via any resistance affix, and raising the 70% itself is possible only via enchantments; see Maximum Resistance below.

  

### **Overwhelm**

There is no hard gate on difficulty. A player may enter any dungeon at any time. What stops a character walking into content far above them is Overwhelm: an enemy above the player's Power Score strips the player's mitigation in proportion to the gap.

  

Overwhelm = min(50%, 25% × (Enemy Score − Player Power Score) ÷ Tier Width)

  

Tier Width is the difference between the maximum Power Score of the current difficulty tier and that of the tier below it.

  

**Overwhelm strips every kind of mitigation, not only resistance.** Armor, block, evasion and resistance are all reduced. This is what stops a character sidestepping the mechanic by choosing a different defensive layer.

  

**It is rated against tier width rather than a flat number of points.** A flat step is worth 17% of a tier 1 tier width but only 6% of a tier 8 one, which made a fully geared tier 1 player lose 13% of their mitigation at their own final boss while a fully geared tier 8 player lost 47% at theirs. Rating against tier width makes the same relative shortfall cost the same everywhere.

  

**It shrinks to nothing as the player out-powers the content.** A player whose Power Score meets or exceeds an enemy's loses nothing at all. This is the difference between Overwhelm and a fixed penalty: gearing up is what removes it, so it pushes the player toward progression rather than taxing them permanently.

  

**Enemy rarity produces an Overwhelm ladder by itself**, because rarity already raises Enemy Score. At tier 8, a player at that tier's maximum Power Score loses 8.9% of their mitigation to a Common enemy and 21.4% to a Cataclysm Boss, with no per-rarity number written anywhere.

  

**This is the reason to over-cap resistance.** Resistance above 70% is the headroom Overwhelm eats into. A character at exactly 70% loses mitigation the moment they fight above their score; one at 95% stays at the cap until the gap is wide.

  

### **Maximum Resistance**

Over-capping and raising the maximum are two different things and are easy to confuse. **Over-capping** is having more than 70% resistance, which any resistance affix does and which is worth having because penetration and Overwhelm are subtracted before the cap. **Raising the maximum** moves the 70% itself, so more of a hit is actually stopped.

  

**Only enchantments raise the maximum. No affix may.** One positive enchantment does it, "You have +10 maximum resists", and three negative enchantments lower it. That placement is not incidental: every affix has seven tiers and can appear on several pieces, and maximum resistance does not tolerate that range. Enchantments have one value rather than seven, carry a weight from 1 to 4 controlling how rare they are, appear only on Legendary items and above, and take one of the four slots a regular affix would have used. The maximum resistance enchantment is weight 1, which is the rarest and most powerful tier.

  

**The maximum is hard capped at 90%.** Two of that enchantment reach it and a third is wasted.

  

|  |  |  |
| :-- | :-: | :-- |
| \*\*Figure\*\* | \*\*Value\*\* | \*\*What it is\*\* |
| Base cap | 70% | Where a resistance caps with nothing done about it |
| Ceiling on the cap | 90% | Hard. No stacking goes past it |
| Damage taken at the base cap | 30% | |
| Damage taken at the ceiling | 10% | Three times less than at the base cap |

  

**Why there is a ceiling at all.** Damage taken is proportional to 100% minus resistance, so the last points are worth far more than the first. Going from 70% to 80% removes a third of what still gets through; 80% to 90% removes half of what is left; 100% removes all of it. A modifier that is worth more the more of it you take needs a hard stop rather than careful pricing.

  

**Where 90% comes from.** Path of Exile caps resistances at 75% and hard caps maximum resistance at 90%, reached in 1% steps from rare modifiers, and it exists there for this reason. It is the only figure available from a shipped game and the ratio it produces carries over: 75% to 90% is a 2.5 times reduction in damage taken there, and 70% to 90% is 3 times here.

  

### **Applying Damage Over Time and Other Effects**

Bleed, poison, disease, void splinter and the other effects a player can inflict are applied by chance on hit. Gems grant that chance, and so do affixes.

  

**A skill may also apply an effect outright, with no chance roll.** When a skill's own description says it applies one, it always does. That is the second route into the same effect, and it is how every War skill applies bleed and every Demonic skill applies burn.

  

**An enemy carries at most one stack of any effect the player applies.** One enemy is bleeding or it is not. There is no counting stacks on a screen full of enemies.

  

**A damage over time effect deals a fixed amount per tick.** It is not a total handed out in instalments. A bleed that deals 20 damage per tick, ticks once per second and lasts 5 seconds deals 100 damage in total, and every one of those three numbers can be raised on its own.

  

|  |  |  |
| :-- | :-- | :-- |
| \*\*Metric\*\* | \*\*What raising it does\*\* | \*\*Total damage\*\* |
| Damage per tick | Each tick hits harder | Rises |
| Tick rate | More ticks in the same time | Rises |
| Duration | The effect runs for longer | Rises |

  

**All three multiply.** A character with 48% more of each does not deal 148% of the base total; it deals 1.48 × 1.48 × 1.48, which is 324%. That is deliberate and it is the reason the three are separate stats rather than one. It also means the values on the affixes and attributes that grant them cannot be set one at a time — see "What Affixes Do Not Grant", which forbids an ordinary affix from being a "more" multiplier, and note that three ordinary increases on the same output produce the same curve as one.

  

**This is the opposite of what Path of Exile and Last Epoch do**, and it is a deliberate departure. Both of those define a damage over time effect as a total spread across a duration, so ticking faster delivers the same total sooner and adds nothing. The reasoning for going the other way is in `docs/DECISIONS.md`.

  

**Chance to apply caps at 100%. Everything above it becomes magnitude instead.**

  

|  |  |
| :-- | :-- |
| \*\*Chance from all sources\*\* | \*\*What happens\*\* |
| 60% | Applies on 60% of hits, at its normal magnitude |
| 100% | Applies on every hit, at its normal magnitude |
| 250% | Applies on every hit, at 2.5 times its magnitude |
| 800% | Applies on every hit, at 8 times its magnitude — a 700% increase |

  

**Why the overflow is not simply wasted.** Ailment chance comes from two sources that both scale hard: affixes, and gems, where the gem applying bleed reaches 150% chance on its own at Cataclysmic rarity. Without this rule a build would hit the cap and every point past it would be dead, so an ailment build would stop progressing at exactly the point it was coming together.

  

The chance summed is the total across every source: affixes, gems, keystones and enchantments alike.

  

**What magnitude scales depends on the effect, and it is never wasted.**

  

|  |  |
| :-- | :-- |
| \*\*The effect has\*\* | \*\*Magnitude scales\*\* |
| Damage over time, with no cap on it | The damage |
| A strength with a cap, such as a slow | The strength up to that cap, then the duration instead |
| No strength axis at all, such as Madness | The duration |

  

**The effects a player can apply**

  

|  |  |  |
| :-- | :-- | :-- |
| \*\*Effect\*\* | \*\*What it does\*\* | \*\*Magnitude scales\*\* |
| Bleed | Damage over time | The damage |
| Poison | Damage over time | The damage |
| Disease | Damage over time | The damage |
| Burn | Damage over time | The damage |
| Void Splinter | 1% of current health per second over 4 seconds | The damage |
| Necrosis | Reduces the target's healing by 25% and deals damage over time, for 5 seconds | The damage and the healing reduction, to a cap of 100%, then the duration |
| Madness | The enemy attacks anything nearby, friend or foe, for 3 seconds | The duration |
| Cripple | Reduces the enemy's movement and attack speed by 30% for 4 seconds | The reduction, to a cap of 80%, then the duration |
| Shred | Reduces the enemy's resistance by 10 for 6 seconds | The reduction, until that resistance reaches zero, then the duration |
| Weaken | Reduces the enemy's damage by 20% for 5 seconds | The reduction, to a cap of 80%, then the duration |

  

**Cripple's slow caps below total** because a full stop is a stun, and stunning is a separate mechanic with its own counter in Crowd Control Resistance. **Weaken's reduction caps for the same reason**: an enemy that deals no damage is harmless, which is a stun by another name. **Shred stops at zero resistance** for the same reason armor penetration does: reducing a defence below nothing grants no bonus.

  

**Weaken and Wither are two different effects.** Weaken is applied by the player and lowers an enemy's damage. Wither is applied by an enemy to the player and lowers the player's movement and attack speed. Neither replaces the other, and Cripple is the player's equivalent of Wither.

  

**Necrosis no longer stacks.** Its earlier description had it stacking and reducing healing by 10% per stack, which the single-stack rule above rules out. It now carries the whole reduction in one application and scales with magnitude like everything else.

  

These figures may need tuning once the game is playable.

  

# **V. Skill System**

## **Skill Acquisition**

Skills in Cataclysm are not learned or leveled independently. They are determined entirely by the combination of weapon type and damage type on the player's weapon. Every weapon type paired with every damage type produces a unique set of six skills (one per non-basic slot). This design ensures that gear upgrades are never just stat checks — changing weapon types fundamentally changes the player's available kit.

  

## **Weapon Types**

  - One-Handed: Sword, Dagger, Axe, Fist, Wand, Whip, Shield, Crossbow
  - Two-Handed: Greatsword, Greataxe, Spear, Staff, 2H Crossbow, Warhammer

There are no offhand items. A player equips either one two-handed weapon or two
one-handed weapons; both configurations give the same 6 gem sockets.

### **Dual Wielding and Damage Types**

A single weapon can carry more than one damage type. What the player has access to
is determined by the set of damage types across **all** equipped weapons, not by
either weapon alone.

  - Every damage type present unlocks that type's three class passive trees. Four
    damage types across the player's weapons unlocks 12 classes.
  - Every damage type present also unlocks every skill matching an equipped weapon
    type paired with that damage type.
  - The player does not receive a button for every available skill. They choose
    from the available pool and assign chosen skills to slots.

This is why dual wielding matters beyond raw damage: it is the primary route to
multiclassing, because it is how a player carries more damage types at once.

  

## **Damage Types and Skill Availability**

Not all damage types are available on all weapons. Damage type availability is tied to thematic fit:

  

|  |  |
| :-: | :-: |
| \*\*Damage Type\*\* | \*\*Available Weapon Types\*\* |
| War | Sword, Greatsword, Dagger, Axe, Greataxe, Spear, Fist, Shield, Crossbow, 2H Crossbow, Warhammer, Whip |
| Demonic | Sword, Greatsword, Dagger, Axe, Greataxe, Fist, Whip, Warhammer, Wand, Staff |
| Death | Sword, Greatsword, Dagger, Greataxe, Spear, Fist, Whip, Wand, Staff |
| Pestilence | Sword, Dagger, Spear, Fist, Whip, Crossbow, 2H Crossbow, Wand, Staff |
| Famine | Sword, Dagger, Axe, Fist, Whip, Warhammer, Wand, Staff |
| Celestial | Sword, Greatsword, Spear, Shield, Crossbow, Warhammer, Wand, Staff |
| Chaos | All weapon types (chaos is unpredictable) |
| Void | Greatsword, Dagger, Spear, Fist, Whip, Warhammer, Wand, Staff |

  

Each list is set by what the damage type's three classes need to function. Void is
the most restricted at 8 weapons because it is the least physical type; Chaos is
unrestricted because the Chaos Shaper changes form based on weapon type. Wand and
Staff are excluded from War, which has no caster build.

  

Every one of the 24 classes has at least two weapons available to it under this
table, and no weapon is unused by every damage type.

  

## **War Skill Examples**

The following is a sample of War damage type skills across weapon types to illustrate the breadth and distinctiveness of the skill system:

  

|  |  |  |
| :-: | :-: | :-: |
| \*\*Weapon\*\* | \*\*Skill\*\* | \*\*Description\*\* |
| Warhammer / Heavy | Earthquake | Slam into the ground, shockwave in 6m radius, knocks down enemies, reduces armor, leaves damaging fissure. |
| Dagger / Special | Proximity Mine | Place a concealed mine that arms in 0.5s and detonates on trigger for heavy damage in a 3m blast with bleed. |
| Spear / Special | Ballista | Deploy a ballista that fires at the furthest enemy in 15m every 2 seconds, dealing enormous damage and pinning targets. |
| Crossbow / Special | Bolt Turret | Fire a bolt into the ground that deploys a turret firing at nearby enemies every 1.5s with bleed on each hit. |
| Shield / Ultimate | Fortress | Become immovable for 5 seconds. 60% damage reduction. Reflect 500% of blocked damage. Allies within 6m take 30% less damage. |
| Whip / Ultimate | Whirlwind of Steel | Spin the whip for 4 seconds in a 7m zone. Every enemy in range struck repeatedly. Each hit applies bleed. |
| Greataxe / Ultimate | Annihilator | Channel spin for 3 seconds dealing rapid hits to all in melee range, applying bleed and reducing armor each revolution. |
| All / Aura | Blood and Iron | Martial dominance aura in 10m. Enemies: -10% armor, -15% move speed. Allies: +8% physical damage. Drains mana. |

  

## **Demonic Skill Examples**

Demonic is the vertical slice's damage type, and **all ten of the weapon types it can roll on are now designed**: Greataxe, Fist and Staff for the three Demonic classes, plus Sword, Greatsword, Dagger, Axe, Wand, Whip and Warhammer. That is 51 rows. The slice ships the first three, one for each class: Greataxe for the Ravager, Fist for the Masochist, and Staff for the Ritualist. Greataxe and Fist reuse the War animation sets for the same weapon and slot, so the Staff is the only new set the slice buys.

  

Every skill below applies burn, which is Demonic's damage over time effect in the same way bleed is War's.

  

|  |  |  |
| :-: | :-: | :-: |
| \*\*Weapon\*\* | \*\*Skill\*\* | \*\*Description\*\* |
| Greataxe / Heavy | Molten Cleave | Horizontal arc across a wide cone, setting each enemy alight and dragging a line of molten slag that burns for 6s. |
| Greataxe / Ultimate | Pyroclasm | Spin 3s hitting all in melee range, setting every enemy alight and leaving 5m of burning ground for 8s. Final hit 300% weapon damage. |
| Fist / Heavy | Searing Hook | Burning hook, 4m knockback, sets alight. Deals 1% increased damage per 1% of maximum health missing. |
| Fist / Support | Martyr's Ember | For 10s, store 40% of all damage taken and spend it as bonus fire damage on your hits. Store capped at 200% weapon damage. |
| Fist / Ultimate | Living Pyre | Burn for 6s, immune to stun, slow and knockback. Enemies within 4m set alight. Each hit taken raises pyre damage 8% and returns 25% of it as health. |
| Staff / Special | Summon Imp | Summon a lesser imp for 20s that sets what it hits alight. Up to 3 active; a fourth destroys the oldest, which explodes in 3m. |
| Staff / Support | Subjugate | Seize an enemy's mind, applying Madness. Lasts twice as long on an enemy that is already burning. |
| Staff / Ultimate | Open the Rift | Tear a rift for 10s that burns everything within 6m and spawns an imp every 2s to a maximum of 5. Collapses for 400% weapon damage. |
| All / Aura | Conflagration | Hellfire aura in 10m. Enemies burn continuously and lose 15% Demonic resistance. Allies: +8% fire damage. Drains mana. |

  

## **How a Skill Behaves: the Seven Shapes**

A skill is a row in the Weapon Skills sheet, not a piece of code. Two columns decide what it does: **Shape** names which of seven shared behaviours runs it, and **Shape Params** carries that behaviour's numbers as `Key=Value` pairs. Adding a skill of an existing shape is a workbook edit and needs no programming at all.

  

The full weapon-and-damage-type matrix is 398 rows. Building each skill by hand would make the other 382 unaffordable once the first sixteen were done, which is why the shapes are shared.

  

|  |  |  |
| :-: | :-: | :-: |
| \*\*Shape\*\* | \*\*What it does\*\* | \*\*Numbers it reads\*\* |
| Strike | Hits everything in a cone or ring around the caster. An angle of 360 is a ring. With a duration and an interval it repeats, which is what a spin is. | Radius, Angle, MaxTargets, Duration, Interval, Knockback |
| Projectile | Sends something out toward where the player is aiming. One that pierces travels a line and hits what it passes; one that does not lands and hits in a radius there. | Range, Radius, Pierce, Returns, Speed |
| Self Buff | Grants the caster an effect for a duration. | Duration, Radius |
| Movement | Moves the caster. A leap hits where it lands, a charge hits everything on the way, a blink hits at both ends and nothing between. | Mode, Range, Radius |
| Summon | Spawns minions that fight for the caster. With a duration and an interval it spawns over time and collapses at the end, which is what a rift is. | Range, Radius, Count, MaxActive, Duration, Interval |
| Aura | A radius around the caster. Held as a toggle when it has no duration, and timed when it has one. | Radius, Duration, Interval |
| Debuff | Applies a named effect to enemies within range, nearest the cursor first, without necessarily damaging them. | Range, Radius, MaxTargets, Duration |

  

**A burning patch of ground is a rider, not a shape.** Eight of the sixteen slice skills leave one behind on top of whatever else they do: Molten Cleave drags a line of slag, Emberhurl leaves its flight path burning, Infernal Plunge leaves a pool of lava. Any shape may carry `GroundRadius` and `GroundDuration`. Four other riders work the same way: `Burn` sets what the skill hits alight, `Effect` names a status effect from the Buffs, Debuffs or DoTs sheets, `FinalHitPercent` is a closing blow at the end of something that repeats, and `HealthCostPercent` is a cost in health rather than mana.

  

**What a summoned minion hits for is one rule for every minion, not a number per skill.** A minion's attack deals **30% of its summoner's weapon damage**, and it attacks **once per second**. It has no damage of its own, so it grows exactly as the character does and never stops mattering. Neither figure is a Shape Param, because neither varies between summoning skills; what varies is how many minions a skill makes and how long they last, and those are already `Count`, `MaxActive` and `Duration`.

  

|  |  |
| :-: | :-: |
| \*\*What a minion has\*\* | \*\*Where it comes from\*\* |
| Damage per attack | 30% of the summoner's weapon damage |
| Attacks per second | 1 |
| How many, and for how long | The summoning skill's `Count`, `MaxActive` and `Duration` |
| What its attacks are worth in total | 3 imps x 30% x 1 per second \= 90% of weapon damage per second |

  

Ninety percent per second sits below an automatic basic attack, which is 128% to 150% per second depending on weapon speed. So a Ritualist holding three imps has added meaningfully to their damage without the minions becoming the whole of it. Diablo IV uses the same shape and very nearly the same number: its Necromancer minions gain 30% of the player's weapon damage and take their attack rate from the player's weapon. Path of Exile does the opposite and gives minions damage entirely their own; that route needs a whole separate family of minion-only stats to scale, which this game does not have and does not want for two skills.

  

**The Shape column is deliberately separate from the Tags column.** The tags already have a job: an increase from gear applies to a skill only if the skill carries the tags that increase requires. Deciding behaviour from them as well would mean adding a tag to make a skill work silently changed which gear applied to it. Path of Exile draws the same line, keeping the internal type list that gates support gems separate from the identifier that names a skill's code.

  

# **VI. Itemization**

## **Item Slots**

  - Armor: Head, Chest, Shoulders, Gloves, Pants, Boots, Belt
  - Jewelry: 8 Rings, Necklace, Relic
  - Consumables: 4 Potion slots
  - Weapons: See weapon types above

  

## **Weapon Sub-Types**

Weapons have a physical sub-type that determines baseline combat properties:

|  |  |
| :-: | :-: |
| \*\*Sub-Type\*\* | \*\*Property\*\* |
| Piercing | Ignores 20% of enemy armor |
| Slashing | 10% more damage vs. HP |
| Blunt | 10% chance to stun for 0.75 seconds |
| Magic | 10% more damage vs. shields |

  

**Blunt stuns rather than doing bonus damage against armor.** Its original property put it in direct competition with Piercing, which already beats armor and has a whole family of affixes that scale it — ignoring armor appears on skills, on critical hits, on traps and on first hits. Nothing anywhere scales damage against armored targets, so Blunt was a flat bonus with nowhere to grow.

  

The stun uses the shortest duration any designed skill uses. A weapon sub-type that can stun on every hit must not outclass the skills whose entire purpose is stunning, which run to 3 seconds. Crowd control resistance reduces the chance proportionally, so a character at 100% cannot be stunned at all. An evaded hit never stuns, because nothing made contact; a blocked hit still can, because a block reduces damage rather than preventing contact.

  

## **Item Rarities**

**Rarity is not a property an item carries. It is a label for what fills its four slots.** An item that drops with an enchantment is a Legendary; one that drops with three regular affixes is a Superb. Every piece has four slots, and each holds either a regular affix or an enchantment.

  

|  |  |  |  |
| :-: | :-: | :-: | :-- |
| \*\*Rarity\*\* | \*\*Enchantments\*\* | \*\*Regular affixes\*\* | \*\*Notes\*\* |
| Everyday | 0 | 1 | Common drops, basic affixes |
| Quality | 0 | 2 | Slightly improved base stats |
| Superb | 0 | 3 | Better affix rolls |
| Masterful | 0 | 4 | Strong affixes, good base |
| Legendary | 1 | 3 | Requires gear level 4+. |
| Mythic | 2 | 2 | Requires gear level 6+. |
| Ascendant | 3 | 1 | Requires gear level 8+. |
| Cataclysmic | 4 | 0 | Requires gear level 10. |
| Sets | — | — | Legendary and above can be part of a named set with 2/6/10 piece bonuses. |

  

**Adding an affix promotes the piece.** An Everyday item with an affix added becomes a Quality item, and a Superb item with a fourth becomes Masterful. That is not a special crafting rule; it follows from rarity being the name for the contents.

  

**An enchantment takes an affix's slot rather than adding one.** Applying an enchantment to a Masterful piece makes it Legendary, and the piece gives up a regular affix to do it. That is the choice the enchantment section describes, made concrete: a player stacking enchantments is trading away ordinary stats for high-power modifiers that carry drawbacks.

  

**Enchantments arrive either way.** One can roll when the item drops, and a player can also apply one afterwards.

  

**A CATACLYSMIC ITEM HAS NO REGULAR AFFIXES.** All four of its slots hold enchantments. So the 72 regular affix slots across a set is what eighteen **Masterful** pieces reach, not eighteen Cataclysmic ones, and every affix value in this document was fitted against that figure. A top build is expected to be a mix of the two rather than all of either.

  

**A higher rarity is not automatically a better item.** It is a different item, weighted further toward enchantments and away from ordinary stats.

  

**A piece with fewer than 4 regular affixes splits them between prefixes and suffixes.** Two prefixes and two suffixes remain the caps, so one affix is a prefix or a suffix, two are one of each, and three are two of one and one of the other.

  

## **Affixes**

Affixes are the ordinary stats on gear, separate from the enchantments below. Every piece has up to 4 affix slots, so a full set of 18 pieces has 72. A dual wielder carries a nineteenth piece and has 76; see A Two-Handed Weapon Is Worth Double below for why the two come out equal in affix value regardless.

  

### **Affix Tiers**

Every affix has seven tiers, T1 to T7, because the crafting material that raises them (the Potency Crystal) levels an affix to T7. One shared curve produces all seven from the affix's top value. Which of the seven a DROP can roll is set by the difficulty tier; crafting is limited by cost rather than by tier. See What Tier an Affix Can Roll At below.

  

**The curve is linear.** Tier N is worth N/7 of the affix's T7 value. Every step up is worth the same as every other, so the value of one more upgrade never falls off.

  

That is a deliberate pressure point rather than a convenience. The game's central tension is that a day at the forge is a day not defending the empire, so the choice to upgrade gear rather than run a dungeon has to stay uncomfortable for a whole run. A front-loaded curve hands over most of an affix's value in the first few tiers and makes the later ones easy to skip. The cost side already curves, because gear upgrade levels cost 2^N − 1 stones, so diminishing returns arrive through rising cost rather than falling value.

  

### **Every Tier Is a Range**

A tier is not a single number. An affix rolls somewhere in a band reaching **25% below** its tier's top value, and where it lands is the difference between a good item and one worth rerolling.

  

Without ranges, two crafting materials do nothing at all. The Corrupted Mote rerolls an affix value, and the Primal Spark perfects a roll. Perfecting is meaningless if a tier has one value, and rerolling is meaningless if the reroll cannot change it.

  

**Bands overlap between adjacent tiers, and that is intended.** A perfect T6 roll can beat a poor T7 one. With seven tiers there is no way to have both non-overlapping bands and rolls large enough to change a build, because a band worth caring about is necessarily wider than the gap between tiers. A roll that matters is worth more than a clean ordering.

  

**The overlap reaches exactly one tier and never two.** A tier's floor is 0.75 of its own fraction, so tier N is undercut by tier N−1 only when N is above 4, and by tier N−2 only when N is above 8, which cannot happen with seven tiers.

  

### **What Tier an Affix Can Roll At**

Seven tiers do nothing for progression unless something says which of them a drop can reach. Without a gate a tier 1 dungeon drops a T7 affix, and the tier ladder is decoration.



**A drop rolls affixes up to `min(7, difficulty tier + 1)`. Crafting has no tier gate at all: an affix can be raised as high as the player can afford.**



**The difficulty tier is the design's own gate, three times already.** Gear and gem rarity equal the difficulty tier. The best upgrade stone that can drop is capped by the current difficulty tier. A weapon rolls damage types up to the lower of its own limit and the tier it dropped on. This is the fourth use of the same shape, not a new mechanism.



|  |  |  |
| :-: | :-: | :-- |
| \*\*Difficulty tier\*\* | \*\*Highest affix tier a drop can roll\*\* | \*\*What else that tier brings\*\* |
| 1 | T2 | Everyday gear, +3 upgrade level |
| 2 | T3 | Quality gear, +4 |
| 3 | T4 | Superb gear, +5 |
| 4 | T5 | Masterful gear, +6 |
| 5 | T6 | Legendary gear, +7 |
| 6 | T7 | Mythical gear, +8 |
| 7 | T7 | Ascendant gear, +9 |
| 8 | T7 | Cataclysmic gear, +10 |

*Gear rarity and upgrade level are the reference progression stated in section IV.*



**The drop cap reaches T7 at difficulty tier 6 and stays there.** There are eight difficulty tiers and seven affix tiers, and the one-above rule spends the difference at the top: tiers 6, 7 and 8 all reach T7 on a drop. That is where it costs least, because gear rarity, gear upgrade level and filled sockets are all still rising through those tiers.



**Every tier at or below the cap stays in the pool.** A drop rolls uniformly from T1 up to the cap, so a tier 8 drop averages T4 and reaches T7 about one time in seven. A drop that always handed over the cap would not be a drop, it would be a delivery, and the crafting materials that reroll and perfect a value would have nothing left to do.



**That is what the genre does.** Path of Exile gates modifier tiers on item level: item level expands which tiers are available rather than removing the low ones, so a high item level gives an item better potential and guarantees nothing. Last Epoch gates the same way on area level. It is also the shape this document already uses for damage types on a weapon, one section below.



**Crafting is not gated by the difficulty tier, and cost is what limits it.** The Potency Crystal raises an affix one tier at a time and may take it to T7 at any difficulty tier. What stops a tier 1 player owning a set of T7 affixes is what it costs: each step is a craft, the deterministic affix craft is priced at one day per tier of affix, and a day at the forge is a day not defending the empire. Reaching the top early is possible and expensive, which is a decision rather than a rule.



**No affix tier is drop-only, and the one-above rule is what gives a drop its own reason to exist.** Last Epoch makes its top two tiers uncraftable, and its stated reason is that crafting made near-perfect items too easy to reach, which removed the reason to hunt for gear. This design answers that with the plus one instead: the best affix a dungeon can drop is one tier above what the player has otherwise reached, so a good drop is always something worth having. A dropped high tier also saves the days at the forge that raising it would have cost, and a day at the forge is a day not defending the empire, which is this game's scarcest resource.



### **Resistance Affixes**

Three families, differing in how many resistances one roll covers. Per-type value falls as breadth rises; total coverage rises, which is what stops the narrow family being strictly better.

  

|  |  |  |  |
| :-- | :-: | :-: | :-- |
| \*\*Family\*\* | \*\*Covers\*\* | \*\*T7 value each\*\* | \*\*Best when\*\* |
| Single resistance | 1 | 20% | Few Cataclysms are active |
| Two resistances | 2 | 14% | The middle of a run |
| All resistances | 8 | 6% | Many Cataclysms are active |

  

The efficient family changes as a run goes on, which is the point of having three. A difficulty tier is a run and each tier adds a Cataclysm, so the number of resistances that matter grows from one to eight. A single-resistance affix is the best use of a slot when one Cataclysm is active and nearly worthless when eight are; an all-resistance affix is the reverse.

  

Capping all eight resistances at tier 8 costs about 12 affix slots out of 72 with perfect rolls, and about 16 with the worst rolls. That difference of roughly 4 slots of gear is what the perfecting and rerolling materials are worth.

  

### **Health and Damage Affixes**

These have no breadth axis. What they have instead is the two ends of the stat pipeline from section IV: a flat affix enters the base bracket, an increased affix joins the multiplier.

  

|  |  |  |
| :-- | :-: | :-- |
| \*\*Affix\*\* | \*\*T7 value\*\* | \*\*Rolls between\*\* |
| Flat maximum health | 120 | 90 and 120 |
| Increased maximum health | 12% | 9% and 12% |
| Flat damage | 18 | 13.5 and 18 |
| Increased damage | 125% | 93.8% and 125% |

  

**Neither kind is strictly better, which is the reason for having both.** A flat affix is multiplied by every increase already on the character; an increased affix multiplies every flat point already there. So flat wins early in a build and increased wins later, and every increase already present pushes that crossover further away.

  

**Increased damage is ten times increased health because damage and health are on different scales**, which is ordinary for the genre. That is also what forces flat damage to be small: a character with six increased damage affixes is already multiplying by 8.5, so the bracket those multiply has to stay around 200 at tier 8.

  

### **Damage Against a Target's Type**

Eight affixes, one per damage type: **increased damage against War / Demonic / Death / Pestilence / Famine / Celestial / Chaos / Void enemies**. Each gives **400% at T7**, against the generic Increased Damage affix's 125%.

  

**They read the target, not the weapon.** An enemy has a damage type of its own, which is its Cataclysm's; see section X. This affix applies when that type matches and does nothing otherwise. Because it reads the enemy, how many damage types the player's weapon carries has no effect on it.

  

**They add into the same bracket as Increased Damage.** The pipeline is (base + flat) x (1 + increases) x more1 x more2, and a conditional increase joins the increases bracket rather than becoming a third multiplier. That is what Diablo 4 and Last Epoch both do: a damage bonus with a stated condition is additive.

  

**They are prefixes, in the same slots as Increased Damage**, so a player choosing one gives up the other on that piece. That competition is what makes it a choice.

  

|  |  |  |
| :-- | :-: | :-- |
| \*\*Affix\*\* | \*\*T7 value\*\* | \*\*Best when\*\* |
| Increased damage | 125% | Many Cataclysms are active |
| Increased damage against one type | 400% | Few Cataclysms are active |

  

**Where 400% comes from.** It is the ratio this game already pays for narrowing a modifier from all eight damage types to one. The resistance families give 20% per type at breadth one and 6% per type at breadth eight, so narrowing is worth about 3.33 times. The generic damage affix is the breadth-eight case, because it applies whatever the target is. 125% times 3.33 is 417%, rounded to 400%.

  

**What that produces over a campaign.** A run starts with one Cataclysm active and adds one each time a Cataclysm is defeated. The generic affix is worth 125% whatever stands in front of the player; a type-specific one is worth 400% against its own type and nothing against the other seven, so across C active Cataclysms it averages 400/C. The two are equal at C = 3.2. The type-specific affix is the better use of a prefix for the first three Cataclysms of a campaign and the generic one from four onward. That is the same shape the resistance ladder has, and it is where the reason to change equipment between runs comes from.

  

**There is no two-type or all-type version.** The all-type version is the generic Increased Damage affix, which already exists; a second one would be the same affix twice. A two-type version would sit between them, in the way the two-resistance affix does, and is deliberately not built yet: the two ends have to be played before a middle rung can be priced.

  

### **The Damage Target**

The damage numbers are not chosen. They are read off the enemy statistics in section X and fitted to them.

  

An average Common enemy at difficulty tier 8 has 3,362 effective health and should take **2 non-critical hits** to kill, so a player needs **1,681 damage per hit**. Solving the pipeline backwards, a character spending 6 slots on flat damage and 6 on increased damage needs a base of 198, of which the affixes supply 108. The weapon and its skill together supply the remaining 90.

  

Everything else follows from that one number rather than being set separately:

  

|  |  |
| :-- | :-: |
| \*\*Enemy at tier 8\*\* | \*\*Non-critical hits to kill\*\* |
| Common Imp | 0.7 |
| Common Hellhound | 1.5 |
| Elite Succubus | 3.4 |
| Elite Brute | 8.2 |
| Legendary Corrupted Sentinel | 12.2 |
| Herald Abyssal Warden | 45.4 |
| Cataclysm Boss Gatekeeper | 234.7 |

  

A Common enemy is the right thing to anchor on rather than a boss, because the spread between the two is 117 times and no single hits-to-kill figure suits both. Trash is what the player fights almost all of the time.

  

### **Prefixes and Suffixes**

Every piece has four affix slots, and they are **two prefixes and two suffixes**, drawn from separate pools. A stat that appears as a prefix never appears as a suffix.

  

**What this buys.** Without the split, four slots means four of whatever is strongest, and one item can carry a whole build. With it, every piece has to give something up, which is the trade that makes reading a drop interesting rather than arithmetic.

  

|  |  |  |
| :-- | :-- | :-- |
| \*\*Position\*\* | \*\*What it carries\*\* | \*\*Examples\*\* |
| Prefix | How big a character's numbers are | Health, mana, energy shield, armor, evasion, damage, spell damage, class resource |
| Suffix | How often, how fast, and how much gets through | Resistances, attack speed, critical strikes, penetration, regeneration, leech, block, movement speed, cooldown reduction, area of effect, magic find |

  

### **One Affix Per Group**

The split above says which pool an affix is drawn from. It does not say what an affix may sit beside, and without a second rule a four-affix Masterful piece can roll **Flat maximum health** four times over. Slot restrictions do not help: every affix is restricted against the slot and against nothing else, least of all itself.



**The rule. An affix belongs to a group for every stat it grants, named by the stat and the kind together. One piece holds at most one affix from any group.**



**The group is derived from what the affix grants, not written on it.** Two affixes granting the same stat in the same kind are in the same group because they grant the same thing, so a new affix cannot be added without a group and two copies of one stat cannot be given different groups by mistake.



|  |  |  |
| :-- | :-- | :-- |
| \*\*Case\*\* | \*\*What the rule gives\*\* | \*\*Why\*\* |
| Flat and increased of one stat | Both may sit on one piece | Different kinds, so different groups. The design already says neither kind is strictly better and that is the reason for having both |
| A hybrid and one of its halves | Cannot sit on one piece | A hybrid grants each half at 70%, so the piece would carry the same stat twice |
| Two single-resistance rolls | May sit on one piece if they cover different damage types | The eight resistances are eight stats, so they are eight groups |
| An all-resistance roll | Excludes every other resistance affix on that piece | It occupies all eight resistance groups at once |
| A prefix and a suffix | Never collide | A stat that appears as a prefix never appears as a suffix, so the two pools share no group |



**Where the shape comes from.** Path of Exile calls this a **mod group**, and it is the only thing that makes two modifiers on one item mutually exclusive: a group is an identifier shared by one or more modifiers, and only one modifier from a group may exist on an item at a time. Path of Exile 2 keeps the rule and the name. Both games write the group onto each modifier by hand; this design derives it instead, which is the one deliberate difference.



**A hybrid excluding its own halves is the other deliberate difference.** Path of Exile 2 gives a hybrid modifier its own group, so a weapon there can carry both the pure and the hybrid version. That allowance concentrates far more here, because a piece has two prefix slots rather than that game's three, and because the point of a hybrid in this design is to commit a build to two stats at once rather than to deepen one.



**What it does not do.** It does not stop a stat appearing across several pieces. Capping a resistance takes roughly twelve affix slots and is meant to be spread over a set; the rule constrains one piece, not one character.



### **Item Bases and Implicits**

Every slot is a **category**, not a single item. Each category contains several **bases**, and each base carries one to three **implicit** stats. An implicit does not roll and cannot be changed. It is what the item **is**, so picking a base commits a character to a defensive layer or an offensive property before any affix is involved.

  

There are 55 bases across the 11 slots. Gear upgrade level multiplies an implicit exactly as it multiplies an affix, so the values below are the fully upgraded ones.

  

#### **Armor and Jewelry Bases**

|  |  |
| :-- | :-- |
| \*\*Base\*\* | \*\*Implicit\*\* |
| **Head** — Helm | 200 armor |
| Hood | 4 evasion |
| Circlet | 55 maximum energy shield |
| Visage | 70 maximum health, 4 crowd control resistance |
| **Chest** — Cuirass | 440 armor |
| Jerkin | 8 evasion |
| Vestment | 120 maximum energy shield |
| Hauberk | 180 maximum health |
| Carapace | 220 armor, 90 maximum health |
| **Shoulders** — Pauldrons | 165 armor |
| Mantle | 3.5 evasion |
| Epaulets | 1.1 health regeneration |
| Spaulders | 11 retaliation |
| **Gloves** — Gauntlets | 130 armor |
| Grips | 9% increased attack speed |
| Handwraps | 5 critical strike chance |
| Vambraces | 12 flat damage |
| **Pants** — Greaves | 250 armor |
| Leggings | 5 evasion |
| Kilt | 130 maximum health |
| Trousers | 65 maximum energy shield |
| **Boots** — Sabatons | 145 armor, 5% increased movement speed |
| Treads | 12% increased movement speed |
| Striders | 3 evasion, 8% increased movement speed |
| Sollerets | 80 maximum health, 6% increased movement speed |
| **Belt** — Girdle | 130 maximum health |
| Sash | 60 maximum mana |
| Cord | 1.3 health regeneration |
| Cinch | 150 armor |
| **Ring** — Band | 10 flat damage |
| Signet | 16 critical strike multiplier |
| Loop | 60 maximum health |
| Circle | 30 maximum mana, 0.5 mana regeneration |
| **Necklace** — Amulet | 60 maximum mana |
| Pendant | 6 critical strike chance |
| Torc | 95 maximum health |
| Locket | 55 maximum energy shield |
| **Relic** — Idol | 28 critical strike multiplier |
| Fetish | 10% increased area of effect |
| Reliquary | 10% increased cooldown reduction |
| Effigy | 10% increased damage over time frequency |

  

**Every slot offers at least three bases**, or picking one would not be a choice.

  

#### **Weapon Bases**

A weapon carries two things no other item has: a physical **sub-type**, and a limit on **how many damage types it can hold**.

  

**The Max Damage Types column below is a limit, not a count.** A one-handed weapon can hold at most four damage types; a two-handed weapon at most eight. How many a particular weapon actually holds is rolled when it drops.

  

|  |  |  |  |  |  |
| :-- | :-: | :-: | :-: | :-: | :-- |
| \*\*Base\*\* | \*\*Hands\*\* | \*\*Sub-Type\*\* | \*\*Max Damage Types\*\* | \*\*Attacks/sec\*\* | \*\*Implicit\*\* |
| Sword | 1 | Slashing | 4 | 1.30 | 40 flat damage, 5% increased attack speed |
| Dagger | 1 | Piercing | 4 | 1.50 | 26 flat damage, 8 critical strike chance |
| Axe | 1 | Slashing | 4 | 1.25 | 46 flat damage |
| Fist | 1 | Blunt | 4 | 1.45 | 30 flat damage, 10% increased attack speed |
| Wand | 1 | Magic | 4 | 1.35 | 38 flat damage, 18% increased spell damage |
| Whip | 1 | Slashing | 4 | 1.40 | 32 flat damage, 12% increased area of effect |
| Shield | 1 | Blunt | 4 | 1.20 | 12 block chance, 260 armor |
| Crossbow | 1 | Piercing | 4 | 1.35 | 38 flat damage, 20 critical strike multiplier |
| Greatsword | 2 | Slashing | 8 | 1.25 | 78 flat damage |
| Greataxe | 2 | Slashing | 8 | 1.28 | 72 flat damage, 22 critical strike multiplier |
| Spear | 2 | Piercing | 8 | 1.35 | 64 flat damage, 6 penetration |
| Staff | 2 | Magic | 8 | 1.30 | 66 flat damage, 32% increased spell damage |
| Two-Handed Crossbow | 2 | Piercing | 8 | 1.30 | 66 flat damage, 7 critical strike chance |
| Warhammer | 2 | Blunt | 8 | 1.20 | 84 flat damage |

  

**Neither how many damage types a weapon holds, nor which ones, is a property of the base.** Both are decided when the item drops. Section IV says loot is biased toward the Cataclysm being fought, which is what decides *which* types fill them. The base says only the most it could ever hold.

  

**The difficulty tier caps the count as well, and it is the tighter limit for most of the game.** A weapon rolls from one damage type up to the lower of its own limit and the tier it dropped on. So a two-handed weapon cannot roll five damage types until tier 5, and reaches its full eight only at tier 8. A one-handed weapon is identical to a two-hander up to tier 4, and never rolls more than four however deep the player goes.

  

|  |  |  |  |
| :-: | :-: | :-: | :-: |
| \*\*Difficulty tier\*\* | \*\*Most on one one-hander\*\* | \*\*Most on one two-hander\*\* | \*\*Most while dual wielding\*\* |
| 1 | 1 | 1 | 2 |
| 2 | 2 | 2 | 4 |
| 3 | 3 | 3 | 6 |
| 4 | 4 | 4 | 8 |
| 5 | 4 | 5 | 8 |
| 6 | 4 | 6 | 8 |
| 7 | 4 | 7 | 8 |
| 8 | 4 | 8 | 8 |

  

**Dual wielding is still the primary route to multiclassing that section V describes, and the tier cap is what makes it so.** The raw limits tie — two one-handers reach eight damage types and so does a single two-hander. What separates them is when. A dual wielder holds all eight from tier 4; a two-hander gains one type per tier and does not catch up until tier 8. So dual wielding leads at every tier from 1 to 7, by the widest margin at tier 4, and is only matched at the very end. Every damage type present unlocks that type's three class trees, while the two-hander stays ahead on raw damage throughout.

  

### **A Two-Handed Weapon Is Worth Double, Per Implicit and Per Affix**

A two-handed weapon multiplies **both** its implicit values and every affix rolled on it by **2**. The values in the table above are the stated ones; a Greatsword therefore supplies 156 flat damage, and a flat damage affix on it is worth twice what the same affix is worth on an Axe.

  

**The figure is derived, not chosen.** A dual wielder carries two weapons with four affix slots each, so eight against a two-hander's four. Two is the multiplier that makes the two loadouts worth the same in affixes.

  

**Section VII already requires that equality.** It states that two one-handed weapons count as one equipped piece for Power Score so that dual wielding is not worth free Power Score. The rating model deliberately scores both loadouts the same, so whichever side had the larger affix budget would be carrying power its rating does not count.

  

**It has to reach the implicits, not only the affixes.** Two one-handed weapons **sum** their base damage, so an Axe and a Sword give 86 against a Greatsword's stated 78. With the affix half alone the two-hander would lose on damage while also holding one fewer damage type, which makes it strictly worse. Reaching a damage advantage through the affix half alone would need a multiplier near 2.75, which would hand the two-hander three affix slots the dual wielder does not have — the same free power the rule above forbids, pointed the other way.

  

With the multiplier applied to both, a two-handed weapon deals about **1.33 times** the damage per hit and about **1.26 times** the damage per second, and the dual wielder holds a fourth damage type and a wider spread of affixes.

  

### **What a Dual Wielder Has**

|  |  |  |
| :-- | :-: | :-: |
| \*\*\*\* | \*\*Two-handed\*\* | \*\*Dual wielding\*\* |
| Equipped pieces | 18 | 19 |
| Affix slots | 72 | 76 |
| Damage types | 3 | 4 |
| Weapon affix slots, in one-handed terms | 8 | 8 |

  

**Both weapons' base damage is summed.** One attack deals the damage of both.

  

**Attack speed is the average of the two weapons.** Not the sum, and not the slower. This is what stops summed damage becoming a strict advantage: a dual wielder deals more per swing than either weapon alone but does not also swing at the faster weapon's rate.

  

**There is no defensive penalty for dual wielding.** Some games in the genre charge one; this design does not.

  

**The Shield is the one weapon whose base defends.** Section V lists it among the one-handed weapon types and states there are no offhand items, so it is a weapon with nowhere else to be. No other weapon base grants health, energy shield, armor, evasion, block or damage reduction, and no weapon can **roll** any of those as an affix.

  

### **Hybrid Affixes**

One roll granting two stats, each at **70%** of what the single affix for that stat gives. That is the same ratio the two-resistance affix already has against the single-resistance one.

  

A hybrid is worth 1.4 affixes spread across two stats, where a single affix is worth 1.0 concentrated in one. So it wins a slot when a build needs both and loses when it needs one badly.

  

Prefix hybrids pair defensive layers: health and armor, health and energy shield, armor and evasion, evasion and energy shield, mana and energy shield, and increased health and armor. Suffix hybrids pair stats a single build wants together: attack speed and critical strike chance, critical strike chance and multiplier, health and mana regeneration, penetration and critical strike multiplier, block chance and crowd control resistance, and magic find with loot quantity.

  

**A hybrid can never appear on a slot one of its halves could not.**

  

### **Ailment Affixes**

A chance to apply an effect on hit. These grant no number on the character sheet; what they grant is a chance, and the effect is defined in the status effect data.

  

|  |  |  |  |
| :-- | :-: | :-- | :-- |
| \*\*Affix\*\* | \*\*T7 Chance\*\* | \*\*Kind\*\* | \*\*Same effect as gem\*\* |
| Chance to bleed | 15% | Damage over time | Of Rending |
| Chance to poison | 25% | Damage over time | Of The Viper |
| Chance to disease | 20% | Damage over time | Of Rot |
| Chance to apply void splinter | 15% | Damage over time | Of The Abyss |
| Chance to necrose | 15% | Damage over time | Of Wasting |
| Chance to burn | 15% | Damage over time | Of Embers |
| Chance to madden | 15% | Weakening effect | Of Madness |
| Chance to cripple | 15% | Weakening effect | Of Maiming |
| Chance to weaken | 15% | Weakening effect | Of Withering |
| Chance to shred | 15% | Weakening effect | Of Shredding |

  

**Each of these is five points above the gem that applies the same effect.** A gem states its own starting chance — the one applying poison starts at 20%, the one applying disease at 15%, and the rest at 10% — and the affix is that number plus five. This is what makes the chance on a new ailment a derivation rather than a fresh choice.

  

**These roll on weapons, necklaces, relics and rings only.** An ailment affix only makes sense where a hit comes from, so no armor piece carries one.

  

**The gem stays the stronger source.** A gem applying bleed reaches 150% chance at Cataclysmic rarity against this affix's 15% at top tier, so a socket is still where an ailment build lives. Having both means a build that wants an ailment can chase it two ways, and one that wants it badly can do both.

  

### **Affixes Are Restricted by Gear Slot**

An affix cannot appear on every piece. Without restrictions every slot is interchangeable and gearing has no puzzle in it.

  

|  |  |  |
| :-- | :-: | :-- |
| \*\*Family\*\* | \*\*Slots\*\* | \*\*Where\*\* |
| Damage | 48 | Weapon, Rings, Relic, Necklace, Gloves |
| Health and armor | 56 | Head, Chest, Shoulders, Belt, Pants, Boots, Rings |
| Resistance | 68 | Everything except the Weapon |

  

**Rings are in every list on purpose.** There are eight of them, so they are the flexible slots a build uses to fix whatever it is short of, which is what makes them worth chasing. Capping eight resistances is the hardest defensive requirement, so resistance is the least restricted; damage is the most.

  

**A weapon defends nothing.** No health, energy shield, armor, evasion, block, damage reduction or resistance can appear on it, as an affix or as an implicit.

  

**Every slot can fill all four of its affix slots.** A slot with fewer available prefixes than it has prefix slots would roll duplicates or blanks, so each is checked against both pools separately.

  

### **What Affixes Do Not Grant**

**Gear can grant primary attributes.** An earlier version of this section said it could not, on the grounds that the design gives one attribute point per level and the Maw consumes items and enemies for more, so gear granting them would be a new mechanic rather than a filled gap. That was reversed on 2026-08-04: attributes must be slottable on gear.

  

**The Maw is still a source, so an attribute affix is priced against both.** A character's attribute points come from levelling, from the Maw, and now from gear. An attribute affix competes with what the Maw already gives, not only with the hundred points a character earns by reaching level 100.

  

**Each of the eight primary attributes has exactly one affix, and it is a percentage increase.** Gear does not grant attribute points. It increases the attribute the character already has.

  

That is the whole point of the design. An attribute affix is worth little to a character spread across several attributes and a great deal to one that has specialised, so it rewards a decision the player already made rather than handing everyone the same value. A flat version would do the opposite, which is why there is none.

  

**They are suffixes, and no hybrid grants one.** One attribute per affix, never two.

  

**Which slots each one rolls on follows the stats it drives**, rather than being chosen separately. Ferocity drives critical strike and Efficacy drives area of effect, both of which already roll on a weapon, so those two can appear on a weapon. Vitality drives health and Constitution drives armour, which do not roll on weapons, so those two cannot. That keeps a weapon offensive without needing a rule of its own.

  

**No ordinary affix is a "more" multiplier.** An affix is flat or increased. Multiplicative sources come from gems, passive tree keystones and enchantments, as section IV states.

  

## **Enchantment System**

### **Overview**

Enchantments are high-power modifiers available only on Legendary and above items. Each item has a maximum of 4 total affix slots shared between regular affixes and enchantments. Players must choose between stacking powerful enchantments or filling slots with standard affixes — both have merit depending on the build.

  

### **How Enchantments Roll**

Enchantments are tag-based rather than skill-specific, ensuring the loot pool remains manageable while still feeling relevant to builds. Each enchantment has one or more tags that determine which items it can appear on. Positives and negatives roll independently — a strong positive is not guaranteed to come with a weak negative.

  

|  |  |
| :-: | :-: |
| \*\*UNIQUE PER CHARACTER\*\* | Each enchantment can only appear once across all of a player's equipped gear. You cannot equip the same enchantment on multiple pieces. This prevents degenerate stacking of powerful effects (e.g. equipping '50% increased HP' on every ring slot), keeps the power ceiling consistent, and makes build assembly a genuine puzzle — players must find a complementary set of enchantments rather than farming one great roll repeatedly. |

  

The weight system governs rarity and balance simultaneously. Weight 1 enchantments are rare and very powerful. Weight 4 enchantments are common and modest. Because positives and negatives roll separately, a player could theoretically land a weight 1 positive paired with a weight 1 negative — extremely powerful but extremely costly.

  

|  |  |
| :-: | :-: |
| \*\*Weight\*\* | \*\*Rarity / Power Level\*\* |
| 1 | Rare — very powerful effect, severe consequence |
| 2 | Uncommon — strong effect, significant drawback |
| 3 | Moderate — solid effect, manageable drawback |
| 4 | Common — modest effect, minor drawback |

  

### **Enchantment Tag Categories**

Enchantments are organized by the following tag types, with multiple enchantments per tag:

  - Element tags (Element.War, Element.Demonic, etc.) — affect all skills of that damage type
  - Skill type tags (Type.Strike, Type.Projectile, Type.Deployable, Type.Channel, etc.)
  - Stat tags (Stat.Offense.Global, Stat.Defense.Armor, Stat.Recovery.Leech, etc.)
  - Keyword tags (Keyword.DoT.Bleed, Keyword.CC, etc.)
  - Trigger tags (Trigger.OnKill, Trigger.OnHit, Trigger.LowLife, etc.)
  - Set tags — guaranteed paired bonuses for named set items

  

### **Set Enchantments**

Set items are Legendary and above items that belong to a named set. Sets provide 2-piece, 6-piece, and 10-piece bonuses. Unlike generic enchantments, set positive and negative rolls are paired and guaranteed — the set functions as a complete package. Sets are the highest-power itemization option in the game and represent the endgame loot chase.

  

## **Gear Leveling**

Gear can be upgraded from +1 to +10 using upgrade stones obtained as dungeon drops. Two +1 stones combine into a +2, and so on. Upgrades are sequential — you cannot skip levels. The max upgrade stone tier that can drop is capped by the current difficulty tier.

  

|  |  |
| :-: | :-: |
| \*\*Upgrade Level\*\* | \*\*+1 Stones Required (cumulative)\*\* |
| \\+1 | 1 |
| \\+2 | 3 |
| \\+3 | 7 |
| \\+4 | 15 |
| \\+5 | 31 |
| \\+6 | 63 |
| \\+7 | 127 |
| \\+8 | 255 |
| \\+9 | 511 |
| \\+10 | 1,023 |

  

## **Sockets and Gems**

Gear has sockets that accept gems. Gems provide stat bonuses and have the same rarity tiers as gear. Gems are upgraded by combining lower-tier gems. The total socket count across all equipment is 45.

  

|  |  |  |
| :-: | :-: | :-: |
| \*\*Slot\*\* | \*\*Sockets\*\* | \*\*Notes\*\* |
| Helmet | 2 |   |
| Chest | 6 | Most sockets of any armor piece |
| Shoulders | 2 |   |
| Gloves | 2 |   |
| Pants | 4 |   |
| Boots | 2 |   |
| Belt | 4 |   |
| Rings (x8) | 1 each | 8 total |
| Necklace | 1 |   |
| Relic | 4 |   |
| 1H Weapons | 3 | Two of these give 6, matching a 2H weapon |
| 2H Weapons | 6 |   |
| Potion slots | 1 each | Gem potions, 4 slots |

  

# **VII. Crafting — The Cataclysmic Forge**

The Cataclysmic Forge is a high-stakes, deterministic crafting system built around the game's core theme of Time Management. A craft never destroys the item being crafted. The Forge's primary penalty is a strategic setback — crafting expensive items costs the player valuable days needed to manage the Empire and defend against the next Surge.

There is one exception, and it is the only way the Forge can cost a player anything permanent: residue accumulated across worn equipment can reach a threshold at which the character is hunted, and can be lost. That is described under Worn Residue and Consumption below. It is warned before it can happen, and it is avoidable by managing residue.

  

## **Cataclysmic Residue (CR)**

Every modification made to an item adds Cataclysmic Residue (CR). This residue represents the instability and corruption inherent in forcefully modifying powerful gear. As CR increases, crafting becomes exponentially more expensive and time-consuming.

  

|  |  |
| :-: | :-: |
| \*\*CR Range\*\* | \*\*Effect\*\* |
| 0 - 99 | Cost penalty (gold multiplier increases). Time penalty minimal. |
| 100+ | Critical Time Penalty kicks in — crafting costs real in-game days. |

  

## **Crafting Materials**

  - **Accelerate Craft — reduces crafting time by 1 day per shard. Counters the CR Time Penalty.** Tainted Shard
  - **CR Cleanse — instantly reduces accumulated CR by 50%. Resource-heavy safety valve.** Purified Essence

  

## **Worn Residue and Consumption**

Cataclysmic Residue is a property of an item. **Worn Residue** is the sum of the residue on every item the character currently has equipped. It is a property of the character, it changes whenever equipment changes, and it is shown on the character sheet at all times.

  

Worn Residue grants nothing. It is not a resource and it does not make the character stronger. Residue is a cost throughout, and it becomes dangerous only if the player ignores the tools that manage it: Purified Essence, which halves accumulated residue, and the Residue Protocols node on the Empire tree, which ignores 5% of residue per point.

  

**The Consumption Threshold.** A single fixed number, to be tuned. If equipping an item would take Worn Residue to or past the threshold, the game states the resulting total and the consequence, and asks the player to confirm before the equip happens. There is no path across the threshold that does not show the warning first. Crossing it is always a decision the player made on purpose.

  

**What crossing it does.** The character is marked. On entering the next dungeon floor, a corrupted double of the character is placed in the dungeon and hunts the player: same class, same level, same equipment, same skills. It is the same enemy described under The Corrupted in section VIII, aimed at the character it was copied from.

  

**This half needs no connection.** The double is built from the player's own character, on the player's own machine. Nothing about crossing the threshold, being hunted, winning or being consumed requires a network connection or the shared table. Only The Corrupted dungeon modifier, which draws a character somebody else lost, needs either.

  

**In a party.** Each marked player produces one double, and every double is present for the whole party. A party of four in which three players are marked enters the next dungeon against three doubles. A player who managed their residue properly still fights their team-mates' doubles.

  

This is deliberate. Party play scales enemy health and damage with the number of players in the session, so a double copied from an over-equipped character arrives scaled for the whole party. A player who ignores residue management is not only risking their own character; they are handing the party a party-scaled copy of their own build. The consequences of that sit with the player who caused it.

  

Whether anyone is actually consumed is decided by the party rule in section VIII: if at least one player leaves the dungeon alive, nobody is consumed. Leaving alive is a reprieve rather than a solution — residue is unchanged and the doubles return on the next dungeon. Only killing the double clears residue.

  - **If the player kills the double**, residue is set to zero on every equipped item. The character keeps its equipment and the run continues.
  - **If the double kills the player**, the character is consumed. The run ends, exactly as dying in the Last Stand ends a run. Empire progress is kept. A snapshot of the character is written to the shared library of corrupted characters described in section VIII.

  

**Why the run ends rather than the character being replaced mid-run.** A run is played at a fixed tier. Replacing a tier 5 character with a fresh one leaves the player at a tier they cannot survive, which is a loss presented as a continuation. Ending the run states the same penalty honestly, and it reuses a rule the game already has rather than inventing a new category of death.

  

# **VIII. Dungeon System**

## **Dungeon Basics**

  - Procedurally generated layouts.
  - Each dungeon has: a time-to-clear estimate, a resolve timer, floor count, modifiers, and a dungeon score.
  - More floors = better rewards but more time cost.
  - Every dungeon has a boss on the final floor.
  - If a dungeon resolves undefeated, its listed consequence occurs (usually city damage or population loss).
  - If a city falls, it becomes a Dungeon City — a staging ground with more floors and multiple boss fights, triggering a Surge.
  - Dungeon Cities can be retaken. Floor count equals the number of dungeons that were in the city when it fell (minimum 20/40/60 for village/city/metropolis).
  - Dying costs 5 days (modified by difficulty setting) and respawns the player at the capital.
  - Every dungeon defeated adds one floor to the Cataclysm boss dungeon.

  

## **Co-operative Play**

Up to four players share a dungeon run. Co-operative multiplayer is a Phase 2 item in the roadmap in section XV; the rules below are the design it is built to.

  

**Enemy scaling.** Enemy health and damage scale with the number of players in the session.

  

**Dying in a dungeon does not end that player's run.** A player who dies during a dungeon run becomes a spectator for the remainder of that run. They are not returned to the capital and no penalty is applied at the moment they die.

  

**The run is decided by the party, not by the individual.**

  - If every player is dead, the run has failed and the death penalty applies.
  - If at least one player leaves the dungeon alive, every dead team-mate is recovered. No death penalty applies to anyone.

  

This makes a surviving player's escape worth something to the whole party, and it makes a single death a setback rather than an ending.

  

**The death penalty is paid once for the party, not once per player.** A four-player wipe costs the same number of days a solo death costs, charged once against the shared empire clock. It is not multiplied by the size of the party.

  

**Party play is held to the same standard as solo play.** Enemy scaling with party size is not a formality. Co-operative play in this genre is commonly easier than solo play, because scaling is applied loosely and a group ends up feeling consequences a solo player does not. This game does not do that. A consequence a solo player would feel is a consequence a party feels too, and party scaling is set to make that true rather than to make group play comfortable.

  

**Over-corruption in a party.** The doubles produced by Worn Residue, described in section VII, follow the same rule. If the party wipes, every marked player is consumed. If at least one player leaves alive, nobody is consumed, and the marked players keep their residue and face their doubles again on the next dungeon.

  

## **Dungeon Types**

|  |  |
| :-: | :-: |
| \*\*Type\*\* | \*\*Description\*\* |
| Basic | Standard dungeon. Most common type. |
| Quest | Does not resolve — refreshes and may move to adjacent city. Required to challenge the Cataclysm. |
| Fallen City | Captured player city. Must be retaken to restore it. Higher floor count and multiple bosses. |
| Cataclysm | The final boss dungeon for the current run tier. Grows with every dungeon the player fails to clear. |

  

## **Dungeon Sub-Types**

|  |  |
| :-: | :-: |
| \*\*Sub-Type\*\* | \*\*Description\*\* |
| Timed | Failing the time limit is treated as dying. Killing enemies adds time. Rewards scale with clear speed. |
| Horde | Number of floors equals number of enemy waves. |
| Sacrificial | Double modifiers. Player can sacrifice materials to remove the extra modifiers, or accept them for bonus rewards. |
| Elite | Every floor ends with a boss fight. |
| Siege | Deals 1% damage to city defenses and population per day while active. Increases in power by 10 points per day. Pauses city upgrades. Max 1 per city. |
| Volatile | Dungeon modifiers change every floor. |
| Cow Level | Enemies drop ridiculous amounts of loot. Time to complete is doubled and cannot be reduced. |

  

## **Dungeon Modifiers**

Dungeon modifiers apply to a **whole dungeon**, and are separate from the enemy modifiers in section X, which apply to an individual enemy.

  

A dungeon carries **one modifier per difficulty tier**, so a tier 8 dungeon carries eight. A Sacrificial dungeon carries double that, which the player may either shed by sacrificing materials or keep for bonus rewards.

  

**Each dungeon modifier carries a weight**, and the sum of the weights on a dungeon is the Modifier Score in the Enemy Score formula in section X. This is how a dungeon modifier makes the enemies inside it harder, and it is why dungeon modifiers are scored where enemy modifiers are not: an environmental effect applies to everything in the dungeon, so a score is the only way its difficulty is expressed.

  

## **The Corrupted (Dungeon Modifier)**

While this modifier is active, one corrupted former player character is placed in the dungeon. It hunts the player across floors rather than waiting to be found.

  

**Where they come from.** Every character consumed by Worn Residue, in any player's game, is snapshotted into a shared table of corrupted characters. A dungeon carrying this modifier draws one entry from that table at random. The snapshot holds class, level, passive tree allocation, equipped items with their rolled affixes, and skill setup — enough to rebuild the character as an enemy that fights with player skills rather than a monster ability list.

  

**Scaling.** The drawn character is rebuilt at the tier of the dungeon it appears in, not the tier it was consumed at. Level, item level, affix tiers and residue all scale to the dungeon's tier. Without this, a player could lose a high-tier character on purpose and then farm its equipment at a tier where the fight is trivial.

  

**Drops.** On death it drops its equipped items, scaled by the same rule. This follows the Rogue Exiles in Path of Exile, which drop one item from every equipment slot and are one of that game's most reliably interesting random encounters.

  

**Network.** The game requires a network connection by default. Co-operative multiplayer is already a Phase 2 item in the roadmap in section XV, so this modifier adds no commitment the game had not already made. The modifier reads the shared table and is therefore online only.

  

If an offline mode is offered, this modifier is excluded from dungeon generation in it. The over-corruption mechanic in section VII still works there in full, because the double a player fights at their own threshold is built locally from their own character.

  

**Seeding.** The shared table is empty until the first character anywhere is consumed. It ships with authored entries so the modifier is not blank at launch.

  

**Weight.** To be set when the dungeon modifier list is entered into the workbook. That list does not exist yet in either document.

  

## **Dungeon Score Formula**

A dungeon's score is **its middle floor**, with that floor's six rarity scores collapsed into one number by how common each rarity is.

  

Dungeon Score = round( (Common × 0.6) + (Elite × 0.2) + (Legendary × 0.15) + (Herald × 0.04) + (Boss × 0.01) )

  

**The middle floor, not the first or the last.** A dungeon's difficulty is what a player meets on average across it, and the Enemy Score formula in section X makes depth the thing that raises difficulty, so the middle floor is the average by construction.

  

**The five weights are how common each rarity is, and they sum to 1.** Cataclysm Boss is absent because it does not appear on an ordinary floor.

  

**There is no Rare tier.** An earlier version of this formula weighted Rare at 0.15; that rarity does not exist. The 0.15 belongs to Legendary, Herald takes 0.04 and Boss 0.01. See section X.

  

## **Destructible Environment**

The environment reacts to damage through the physics system, not through authored responses to particular events. Anything built as a fracturable asset breaks when it is hit, by whatever hit it, according to the force applied and its own material.

  

**There is no per-event authoring.** A meteor, an explosion, a melee swing and a stray projectile all resolve through the same system. A new skill, a new enemy attack or a new environmental hazard needs no destruction work of its own; it damages things and things break. Each destructible asset is prepared once, and after that it reacts to everything in the game.

  

**What is destructible.** Walls, pillars, statues, railings, furniture, hanging fixtures, ceiling sections, and decorative layers sitting on top of the floor.

  

**What is not.** The walkable surface itself. Ground that the player and enemies stand on keeps its shape and stays navigable for the whole dungeon. Scorching, cracking, scattered rubble and settled debris appear on it. Holes, pits and impassable gaps do not.

  

**Why the walkable surface is excluded.** Not because the engine cannot do it. Unreal Engine 5.8 does update navigation when a fracturable asset breaks. The reasons are latency and cost, and they were read out of the engine source rather than assumed:

  - The automatic update runs on a fixed 256-frame cycle per destructible asset. At 60 frames per second that is up to roughly four seconds between a floor breaking and enemy pathing reflecting it. Enemies would keep walking over a hole that is already there.
  - Removing that delay means triggering a navigation rebuild explicitly on every destruction event that matters, which puts that rebuild directly in the path of combat.
  - The project's navigation would also have to change from static generation to fully dynamic, which turns every navigation rebuild into a runtime cost instead of a build-time one.

Keeping destruction off the walkable plane avoids all three, and it costs the player nothing they would notice: everything they hit still breaks.

  

**Cost is governed by how much is moving, not how much could break.** A dungeon may hold a large number of fracturable assets cheaply, because the expense is in pieces actively simulating rather than pieces that exist. The controls are a cap on how many pieces simulate at once, and putting debris to rest or removing it once it settles.

  

**Terrain is not deformable.** The player cannot dig, tunnel, or cut a new path through ground or rock.

  

The reasoning behind these choices, and the measurements still needed to confirm them, are recorded in `DECISIONS.md`.

  

# **IX. City Management and Empire Building**

## **Overview**

A major aspect of Cataclysm is the management of the player's empire. The empire consists of 12 villages, 8 cities, 4 metropolises, and the capital. Players must fight off dungeons to prevent city loss while working toward the Cataclysm boss dungeon. This requires strategic city upgrade decisions, empire tree investment, and time management to ensure the capital isn't overrun.

  

## **City Tiers**

|  |  |
| :-: | :-: |
| \*\*Tier\*\* | \*\*Count / Min Floors on Fall\*\* |
| Village | 12 cities — minimum 20 floors if captured |
| City | 8 cities — minimum 40 floors if captured |
| Metropolis | 4 cities — minimum 60 floors if captured |
| Capital | 1 — losing this ends the run |

  

## **City Upgrades**

Each city has upgrade slots (3 normally, 2 on Heretic difficulty). Upgrades affect defenses, population, dungeon timers, and other city-specific stats. Strategic allocation of upgrade resources is one of the primary meta-decisions of each run.

  

## **Empire-Wide Upgrades**

Players accumulate empire upgrade points by defeating dungeons. These points are spent on a permanent empire-wide upgrade tree that persists through all runs — including failed ones. Empire upgrades provide lasting bonuses to city defenses, population, dungeon floor counts, and more. This is the primary meta-progression system.

  

## **Capital Services**

The capital houses all NPC services. All services cost time, reinforcing the time pressure.

|  |  |
| :-: | :-: |
| \*\*NPC\*\* | \*\*Service\*\* |
| Enchanter | Adds or modifies enchantments on gear |
| Smith (Cataclysmic Forge) | Crafting, upgrading gear, Forge interactions |
| Jeweler | Combines and sockets gems |
| Auction House | Buy and sell items (disabled in SSF) |
| Trainer | Respec passive skill points |
| Side Quests | Random missions for crafting materials, gear, and gold |

  

# **X. Enemy System**

## **Enemy Stat Blocks**

Enemy Score is a power rating. It says what an encounter is worth, not how much health the enemy has, how hard it hits, how often, or what it resists. Those come from two layers that own different things.

  

|  |  |
| :-: | :-- |
| \*\*Layer\*\* | \*\*What it sets\*\* |
| Rarity | Magnitude only: health, damage, armor, energy shield |
| Archetype | Everything else: attack interval, critical strike chance and multiplier, movement speed, evasion, energy shield as a fraction of health, resistances, and how big this kind of creature is relative to average |

  

**Rarity scales magnitude and nothing else.** A Legendary Imp is a bigger Imp. It does not start critting more often, resisting more, or moving differently, because none of those describe how large something is. Per step of rarity above Common, health is multiplied by 1.85, damage by 1.55 and armor by 1.35.

  

**Health grows faster than damage.** Across the six rarities a Cataclysm Boss ends up with roughly 23 times a Common enemy's health and hits about 6 times as hard. Growing both together would produce something both unkillable and instantly lethal, which is a wall rather than a fight.

  

### **How Long a Geared Character Survives**

Enemy damage is the one enemy figure fitted to the player rather than set on its own terms. It has to be: it only means something against what a character can survive, and a geared character's mitigation is four layers deep and they multiply.

  

A reference character at difficulty tier 8 — a level 100 Ravager spending every affix slot, half on staying alive and half on killing things — reaches these:

  

|  |  |
| :-- | :-: |
| \*\*Layer\*\* | \*\*What it removes\*\* |
| Armor, 7,299 points against the tier 8 curve | 53.3% |
| Resistance, at the cap | 70.0% |
| Block chance 28%, removing half a hit each time | 14.0% on average |
| Damage reduction | 15.9% |
| **All four together** | **89.9%** |

  

So a hit lands for about **a tenth** of itself. Any enemy damage figure chosen without reference to that is chosen against nothing, which is what happened before: an average Common enemy needed 176 hits to kill that character.

  

**What the enemies do to it now**

  

|  |  |  |
| :-- | :-: | :-: |
| \*\*Enemy at tier 8\*\* | \*\*Hits to kill it\*\* | \*\*Seconds\*\* |
| Common Imp | 54 | 48.6 |
| Common Hellhound | 24 | 26.4 |
| Elite Succubus | 11 | 28.6 |
| Elite Brute | 10 | 28.0 |
| Legendary Corrupted Sentinel | 12 | 24.0 |
| Herald Abyssal Warden | 5 | 12.0 |
| Cataclysm Boss Gatekeeper | 2 | 6.0 |

  

**A single Common enemy is not the threat. A pack is.** One Imp takes 48 seconds to kill a geared character; ten take 4.9 seconds and twenty take 2.4. That is what makes the design's own description of the Imp — weak individually, overwhelming in packs — a mechanical fact rather than flavour.

  

**A geared character is never one-shot, and a gearless one still is.** Nothing in the table above kills in a single hit. The Gatekeeper's hit is several times a level 100 character's health before any gear, which is what the design intends for a character who arrives unequipped.

  

**The archetype decides everything about how a creature behaves.** The Imp is fast at every rarity. The Corrupted Sentinel never moves at any rarity. The Brute is heavily armored even as a Common enemy, which is why armor is not forced to zero at Common; an archetype with no armor simply has none.

  

**Only some archetypes have an energy shield**, on the same principle as the classes: it is given where it thematically fits. Among the vertical slice enemies the Succubus and the Corrupted Sentinel have one and the rest do not.

  

**Enemy evasion is answered by area damage.** Evasion avoids direct attacks only, so an evasive enemy is a reason to bring area damage rather than a flat reduction in the player's output.

  

**An enemy has one resistance, applied to all incoming damage.** Not eight figures, one per damage type.

  

The reason is that player damage is **adaptive**: a weapon deals one damage number rather than eight separate pools, because a weapon carrying eight damage types would be unworkable to calculate. Once player damage adapts, a per-type enemy profile stops changing any outcome, so it would be authoring work that buys nothing.

  

**The player still has all eight resistances defensively.** That is unchanged and unrelated. Eight Cataclysms attack the player, so the player needs eight. It is only the enemy side that collapses to one figure.

  

|  |  |  |
| :-- | :-: | :-- |
| \*\*Enemy\*\* | \*\*Resistance\*\* | \*\*Why\*\* |
| Imp | 0% | Swarm fodder should die to whatever the player has |
| Hellhound | 10% | A beast, relying on speed rather than soaking hits |
| Succubus | 10% | Little of its own; the energy shield is what keeps it alive |
| Brute | 15% | Thick hide on top of its armor, which is its main defence |
| Corrupted Sentinel | 20% | A construct rather than a living thing, and it cannot retreat |
| Abyssal Warden | 35% | The design describes this one, and only this one, as having high damage resistance |
| Gatekeeper | 30% | High, but its real threat is its phases |

  

**An enemy still has a damage type of its own**, which is its Cataclysm's. That is what decides which of the player's eight resistances applies when it hits them.

  

**Enemy resistance is what the player's resistance penetration works on.** Against an Abyssal Warden at 35%, a player with no penetration lands 65% of a hit and one with 20 penetration lands 85%. Penetration beyond an enemy's resistance grants no bonus, so over-stacking it does not become a damage multiplier against the enemies that need it least.

  

**Enemies carry no Penetration stat.** Overwhelm, in section IV, already reduces the player's mitigation in proportion to the Power Score gap, and rarity already raises Enemy Score. Giving enemies a penetration value as well would be the same mechanic written twice.

  

## **Enemy Modifiers**

Enemy modifiers apply to an **individual enemy**, and are separate from the dungeon modifiers in section VIII, which apply to a whole dungeon.

  

An enemy carries **one modifier per rarity above Common**:

  

|  |  |
| :-: | :-: |
| \*\*Rarity\*\* | \*\*Enemy Modifiers\*\* |
| Common | 0 |
| Elite | 1 |
| Legendary | 2 |
| Herald | 3 |
| Boss | 4 |
| Cataclysm Boss | 5 |

  

**Common enemies carry no modifiers at all.**

  

**Enemy modifiers do not change an enemy's score.** They are mechanical effects rather than stat increases: a burning aura deals its own damage, and a charm stops the player dealing damage for a few seconds. Scoring them as well would count the same difficulty twice, once in the effect and once in the larger health and damage pool that a higher score produces.

  

This is the opposite of dungeon modifiers, which do carry a score. That difference is deliberate. A dungeon modifier is environmental and applies to everything inside the dungeon, so a score is the only way its difficulty is expressed at all. An enemy modifier is already expressed by what it does.

  

## **Enemy Score Formula**

Every enemy's score is built from **the width of its difficulty tier**: the gap between the maximum Power Score a player is expected to reach at the end of this tier and the end of the tier below. Nothing in the formula multiplies. Each contribution is a fraction of that width, or a flat number of points, and they are added.



Write **W** for the tier width and **Pmin** for the previous tier's maximum. Write **f** for the floor ratio, which is the current floor divided by the total floors, and **m** for the middle floor, which is the total floors divided by two and rounded up.



    Enemy Score = round(
        Pmin + W x 0.9 x f                          the baseline for this depth
      + W x Type Weight                             which kind of dungeon
      + W x Subtype Weight                          which variant of it
      + W x Rarity Weight                           what kind of enemy
      + Floor Scaling Base / 20 x f + Floor x 0.5   procedural depth
      + (Floor - m) x Tier x 1.2                    depth tension
      + Modifier Score                              the dungeon's modifiers
    )



**Rounding is JavaScript's, not Python's.** Halves go up rather than to the nearest even number. The formula is built from halves and fifths so exact .5 values are common, and the difference showed up on about 2% of inputs.



**Depth tension is negative above the middle floor and positive below it.** An enemy on floor 1 of a 10-floor dungeon is worth less than the tier baseline; one on floor 10 is worth more. That is what makes going deeper the thing that raises difficulty, and it is why one dungeon floor costs one day.



**THIS FORMULA IS NOT AUTHORED HERE.** It is a transcription of `sim/cataclysm_sim/scoring.py`, which is a verified port of `src/utils/calculateScores.tsx` in the separate, private `sdubois777/DungeonSimulator` repository. That file is authoritative for every number in this section. `sim/verify_scoring_port.py` checks the port against it, and `tools/tests/test_enemy_score_formula.py` checks this section against the port. Changing a number here changes nothing; it only makes this section wrong.



**An earlier version of this section documented a different formula**, which multiplied a base score by a rarity multiplier, a dungeon type multiplier and a subtype multiplier. That was the model's first-commit form from January 2025. It was replaced upstream by the version above and this document was never re-exported, so the section described a formula the game had not used for a year. Issue #30 records how that was found.



## **Parameter Values**

### **Rarity Weights**

|  |  |
| :-: | :-: |
| \*\*Enemy Rarity\*\* | \*\*Weight\*\* |
| Common | 0 |
| Elite | 0.05 |
| Legendary | 0.1 |
| Herald | 0.15 |
| Boss | 0.3 |
| Cataclysm Boss | 0.5 |



**There is no Rare tier.** It existed in the first-commit version of the model and does not exist now.



**A weight is a fraction of the tier width, added.** A Boss is not two and a half times a Common enemy, which the superseded multiplier table said. It is 0.3 of one tier's width above it. On the last floor of a 10-floor Basic dungeon with no subtype at tier 8, a Common enemy scores 6,273 and a Boss scores 6,609: about 5% apart, not 150%.



**This is where the Overwhelm ladder in section IV comes from.** Rarity raises Enemy Score, and Overwhelm is rated against the gap between Enemy Score and Player Power Score as a share of tier width, so the rarity weight IS the share of mitigation that rarity strips. No per-rarity Overwhelm number is written anywhere and none is needed.



### **Dungeon Type Weights**

|  |  |
| :-: | :-: |
| \*\*Dungeon Type\*\* | \*\*Weight\*\* |
| Basic | 0.0 |
| Quest | 0.05 |
| Fallen City | 0.1 |
| Cataclysm | 0.2 |



### **Subtype Weights**

|  |  |
| :-: | :-: |
| \*\*Subtype\*\* | \*\*Weight\*\* |
| None | 0 |
| Timed | 0 |
| Horde | 0.05 |
| Siege | 0.05 |
| Cow Level | 0.1 |
| Elite | 0.15 |
| Volatile | 0.17 |
| Sacrificial | 0.2 |



**Timed carries no weight, deliberately.** A time limit is a constraint on the player rather than on the enemies, so it changes nothing about what an encounter is worth.



### **Floor Scaling Bases**

|  |  |
| :-: | :-: |
| \*\*Dungeon Type\*\* | \*\*Base\*\* |
| Basic | 100 |
| Quest | 200 |
| Fallen City | 300 |
| Cataclysm | 400 |



Divided by 20 and multiplied by the floor ratio. This is the only place a dungeon type contributes flat points rather than a fraction of tier width.



### **Base Type Scores**

|  |  |
| :-: | :-: |
| \*\*Dungeon Type\*\* | \*\*Score\*\* |
| Basic | 30 |
| Quest | 60 |
| Fallen City | 90 |
| Cataclysm | 120 |



**Nothing reads these.** They are declared in the authoritative source and the current formula does not use them; the Floor Scaling Bases above took over the job. They are listed because the port still verifies them against the source, so they are part of the shipped data, and a reader finding them in the code deserves to be told they do nothing.



### **Player Maximum Power Scores**

The anchor every score is measured against: the maximum Power Score a player is expected to reach by the end of each difficulty tier.



|  |  |  |
| :-: | :-: | :-: |
| \*\*Tier\*\* | \*\*Maximum\*\* | \*\*Tier width\*\* |
| 1 | 385 | 385 |
| 2 | 871 | 486 |
| 3 | 1457 | 586 |
| 4 | 2144 | 687 |
| 5 | 3251 | 1107 |
| 6 | 4166 | 915 |
| 7 | 5209 | 1043 |
| 8 | 6327 | 1118 |



**These are a design choice, not a derived result.** They began as a flat arithmetic progression 283 points wide per tier and were revised at least three times. Tier 5 is 1,107 wide where the surrounding trend is about 790, and tier 6 is narrower than tier 5; issue #7 records that anomaly. They can be revisited.



## **Vertical Slice Enemies (Demonic Cataclysm)**

The vertical slice will feature five to seven base enemy types from the Demonic Cataclysm as a proof of concept:

  

|  |  |
| :-: | :-: |
| \*\*Enemy\*\* | \*\*Role\*\* |
| The Imp | Fast, swarming melee. Weak individually, overwhelming in packs. |
| The Succubus | Ranged caster. Debuffs player and buffs nearby allies. Slow but powerful attacks. |
| The Hellhound | Aggressive charger that leaves fire trails. Trail can damage other enemies. |
| The Brute | Heavily armored slow melee. Stomp stun attack. Can be outmaneuvered. |
| The Corrupted Sentinel | Stationary ranged. Forces the player to stay mobile. |
| The Abyssal Warden (Mini-Boss) | Massive stone and lava demon. High damage resistance but vulnerable at legs and back. |
| The Gatekeeper (Boss) | Multi-phase towering demon. Each phase introduces new mechanics. |

  

# **XI. Cataclysm Quest Mechanics**

Each Cataclysm has a unique world mechanic that layers onto the standard dungeon defense gameplay. These mechanics define the strategic challenge of each run and must be addressed to unlock the Cataclysm boss dungeon.

  

## **Hell on Earth (Demonic)**

Infernal Rifts tear open across the map, spawning dungeons without needing a direct path. Players must assault Rifts, destroy the four altars powering each one, and seal 10 Rifts to challenge the enemy capital. Unchecked Rifts spawn a dungeon on an adjacent city every 5 days. Dying during a Rift assault triggers a Surge.

  

## **Dead Rising (Death)**

Cursed Abominations spawn as cities fall, growing stronger with each subsequent loss. Players must defeat Abominations in their quest dungeon lairs to collect 5 Seeds of Undeath and unlock the enemy capital. Each fallen city adds a more powerful Abomination.

  

## **World War (War)**

Player cities become aggressive and begin attacking each other. Conquered cities immediately become Dungeon Cities. Quest dungeons spawn from major battles. Players collect 10 Essences of War from quest dungeons to unlock the enemy capital.

  

## **Pestilence**

A spreading plague infects cities via expanding Plague Zones. Each city has a Contamination Meter that drains population daily. Fallen cities become plague zones and spread infection. Players must defeat quest dungeons to progress toward a vaccine and stem the tide.

  

## **Famine**

Essential resources become scarce. A rationing system distributes limited supplies. Dungeons drop 25% fewer resources per active quest dungeon. Players must defeat 5 quest dungeons to unlock the enemy capital.

  

## **Heaven's Wrath (Celestial)**

Celestial gates open and angelic armies pour through, imposing Divine Edicts as global penalties. Gates reduce healing, empower enemies, and cause random smite attacks. Players must seal 10 gates by destroying their Heavenly Cores. Edicts are 50% stronger inside gate dungeons.

  

## **Chaos Lord (Chaos)**

Chaotic Resolutions mean dungeons can have completely unpredictable outcomes when they resolve. Chaos Fractures spawn environmental hazards and empower enemies. Players must locate and reactivate 8 Pillars of Order to unlock the Chaos Lord's domain.

  

## **The Void**

Void dungeons accumulate void stacks on cities. When stacks reach a threshold, the city is permanently erased from the game world and cannot be reclaimed. Players must deploy void countermeasures, complete Sealing Rituals, and ultimately defeat the Void Nexus.

  

# **XII. Progression System**

## **Character Leveling**

The max level is 100. Players earn experience by killing dungeon enemies and defeating bosses. Per level: 1 passive skill point and 1 attribute point. Every 10 levels: 5 bonus passive points. Defeating a unique Cataclysm boss for the first time: 10 bonus passive points.

  

## **Power Score Ranges by Tier**

|  |  |
| :-: | :-: |
| \*\*Tier\*\* | \*\*Expected Power Score Range\*\* |
| T1 | 0 — 385 |
| T2 | 386 — 871 |
| T3 | 872 — 1,457 |
| T4 | 1,458 — 2,144 |
| T5 | 2,145 — 3,251 |
| T6 | 3,252 — 4,166 |
| T7 | 4,167 — 5,209 |
| T8 | 5,210 — 6,327 |

  

## **Roguelike Meta Progression**

Empire upgrade points are earned by defeating dungeons and persist through all runs including failures. The empire upgrade tree provides permanent bonuses to city defenses, dungeon parameters, and empire management. This system ensures every run — even a failed one — makes the next attempt slightly stronger, rewarding persistence without making early runs trivial.

  

# **XIII. User Interface and User Experience**

## **Key Screens**

  - Login / Character select / Character creator
  - Main Empire Overview — shows all cities, active dungeons, next surge timer, empire status
  - City screen — upgrades, defenses, population, active dungeons per city
  - Dungeon screen — dungeon details, modifiers, floor count, resolve timer, consequence
  - Player Equipment / Inventory / Stat Sheet
  - Passive Tree viewer (class trees + empire tree)
  - Blacksmith / Jeweler / Enchanter / Trainer screens
  - Marketplace / Auction House
  - Settings / Accessibility

  

## **HUD Elements**

  - Empire status bar — active dungeon count, next surge countdown, cities at risk
  - Player resource bars — HP, Mana, class resource (Fury / Resolve / Preparation / etc.)
  - Active skill slots with cooldown indicators
  - Minimap with dungeon overlay

  

## **Accessibility**

**What accessibility means here, and what it does not.** It means removing barriers that make the game impossible to play for some people: perception, language, and physical safety. It does not mean removing difficulty, and it does not guarantee any particular interface element.

  

A mode that hides the heads-up display is a difficulty choice the player opts into. It is not an accessibility failure, and Hardcore and Heretic hiding it is not in conflict with anything below. Where a mode hides the display, the options below apply to whatever remains visible.

  - Multiple language support
  - Colorblind-friendly palette options
  - Scalable HUD elements (text size, cursor size)
  - Reduced ability VFX opacity option
  - Epilepsy-safe mode (reduces flashing effects)

  

# **XIV. Monetization**

## **Base Game**

Cataclysm is free to play. The full game — all 8 Cataclysms, all 24 classes, empire management, city upgrades, the capital hub, all crafting systems, and all weapon types — is available to all players at no cost. There are no paywalls on gameplay content, no pay-to-win mechanics, and no stash or storage fees of any kind.

  

## **Seasonal / League Updates (Free)**

  - New temporary league mechanic resetting or changing the core loop for 3-4 months.
  - A few new powerful non-set unique items or generic enchantments.
  - Class/skill rebalances and QoL improvements.

  

## **Cataclysm Expansions (Paid, $10-$20)**

Each expansion adds a permanent new Cataclysm to the game. The base game remains fully playable without expansions — expansions layer new content on top of what already exists rather than replacing or gating it.

  - New Cataclysm with unique world mechanic.
  - New enemy types, dungeon modifiers, and zones.
  - 3-4 new build-defining enchantment sets.
  - New permanent progression content tied to the expansion's theme.
  - Released every 6-12 months, timed with a new season launch.

## **Cosmetics**

All revenue outside of expansions comes from purely cosmetic purchases. Nothing in the cosmetics shop affects gameplay in any way.

  - Armor and weapon skins
  - Skill visual effects
  - Capital and city skins
  - Empire map themes
  - Pets and companions
  - Back attachments and character effects

All cosmetics are purely visual — no pay-to-win, ever.

# **XV. Development Roadmap**

## **Phase 1 — Vertical Slice**

  - Implement one full Cataclysm (Demonic) with 5-7 enemy types and a complete boss.
  - Build and validate the core combat loop with one weapon type and one damage type.
  - Implement the basic empire map with Surge mechanics and city loss.
  - Implement the passive class tree system with one fully designed tree (Demonic / Masochist).
  - Implement the skill system with Demonic skills across 3 weapon types.

  

## **Phase 2 — Early Access Launch**

  - All 8 Cataclysms implemented with unique quest mechanics.
  - All 24 classes with complete passive trees.
  - Full skill matrix for all weapon types and at least 4 damage types.
  - Complete itemization system including enchantments, gems, and the Cataclysmic Forge.
  - Multiplayer co-op support.

  

## **Phase 3 — Full Release**

  - Complete skill matrices for all damage types.
  - All empire upgrade content.
  - Seasonal league infrastructure.
  - Full cosmetic system.
  - Platform ports as applicable.

  

# **XVI. Risks and Mitigations**

|  |  |
| :-: | :-: |
| \*\*Risk\*\* | \*\*Mitigation\*\* |
| Skill matrix scope creep (11 weapons × 8 damage types × 6 slots = 528+ skills) | Launch with one complete damage type per patch. Use tag-based design to maximize re-use and make each skill feel distinct. |
| Time pressure mechanics frustrating casual players | The lethality mode (Standard, Hardcore or Heretic) lets players tune the urgency, and Standard is the default. Permanent meta-progression ensures no run feels wasted. |
| Passive tree complexity overwhelming new players | Strong visual design with clear branching and class fantasy. Beginner preset builds. In-game tooltips on all node interactions. |
| Enchantment system creating too much variance in loot quality | Weight system ensures common drops are consistently useful. Weight-1 enchantments are rare enough that they feel like jackpots, not baseline expectations. |
| Multiplayer balance (empire shared vs. individual) | Design empire as shared resource in co-op with individual character builds. Extensive playtesting during Early Access. |

  

# **XVII. Conclusion**

Cataclysm is designed to be a relentlessly engaging ARPG that respects player time and rewards deep knowledge of its systems. The fusion of dungeon crawling, empire management, and roguelike meta-progression creates a game that is immediately accessible at the surface but deeply strategic underneath.

  

The core promise is simple: every decision matters, every run teaches something, and every build feels distinct. The weapon-type-driven skill system, class passive trees, and tag-based enchantment system combine to create a build space large enough to sustain hundreds of hours of theorycrafting without ever feeling arbitrary.

  

The empire management layer ensures that even the time between dungeon runs is engaging and consequential, making Cataclysm more than just a dungeon crawler — it is a game about managing chaos under pressure, and the satisfaction of building something powerful enough to stand against the end of the world.

  