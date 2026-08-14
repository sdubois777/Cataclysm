  

  
  

**CATACLYSM**

Game Design Document

*An ARPG Dungeon Crawler with Empire Management and Roguelike Systems*

**This file is the design. Edit it directly.** It is the authoritative copy and has been since 2026-08-02; the Google Drive original it was exported from is historical and is not synced back.

**There is no version number, deliberately.** This file used to carry "Version 0.3" while its filename said `_v2` and the Drive document was titled `Cataclysm_GDD_v2(1)` — three identifiers for one document, none of which was ever advanced when the design changed. The version of this document is its git history: every change arrives through a pull request, and `docs/DECISIONS.md` records the reasoning behind each one. A hand-maintained number would be a fourth thing to keep in step with the other three. The `_v2` in the filename is part of the name inherited from Drive and is not a counter.

**There is no table of contents.** The exported one was 106 links back into the Google Drive document, all pointing at the same anchor, and it went stale the moment a heading changed. GitHub builds an outline from the headings below.

# **I. Executive Summary**

Cataclysm is a dark fantasy ARPG dungeon crawler fused with real-time empire management and roguelike progression. The player defends a crumbling empire against up to eight simultaneous supernatural Cataclysms, each relentlessly spawning dungeons to destroy player cities. The core tension is time — every action costs days, dungeons resolve on timers, and the Cataclysm marches toward the capital whether the player is ready or not.

  

Between dungeon runs, players manage a layered empire of Outposts, Bulwarks, and Sanctuaries ringed around a central Pillar, making strategic decisions about which cities to defend, when to sacrifice resources, and how to spend limited upgrade slots. Character power comes from a deep itemization system — weapon type and damage type determine available skills, passive class trees unlock unique identities and resources, and enchantments provide high-variance build-defining modifiers.

  

The game is won by defeating the Cataclysm boss dungeon. It is lost when a clear path to the capital is opened. Each run that fails feeds permanent empire-wide upgrades, making each attempt stronger than the last in a true roguelike loop.

  

|  |  |
| :-: | :-: |
| **KEY PILLARS** | Time is the primary resource. Decisions cascade. Power comes from itemization, not character levels alone. Every run is winnable — and every failure teaches something. |

  

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

| Mode | Dying costs | Equipment lost on death | Heads-up display | Other |
| :-: | :-: | :-: | :-: | :-: |
| Standard | 5 days | none | shown | — |
| Hardcore | 10 days | each of the 18 equipped pieces drops with a 10% chance, so 1.8 on average | map overlay only | — |
| Heretic | 15 days | each equipped piece drops with a 20% chance, and at least 2 always drop, so 3.7 on average | hidden | Surges spawn 25% more dungeons. Cities have 2 upgrade slots instead of 3. |

**Solo Self-Found. Optional. Combines with any lethality mode.**

| Flag | Rules |
| :-: | :-: |
| Solo Self-Found (SSF) | No auction house, no shared stash. Drop rates are unchanged. |

**Both choices are locked at character creation and cannot be changed
afterwards.** The lethality mode never changes, in either direction, and dying
does not change it — a Hardcore or Heretic character that dies stays Hardcore or
Heretic and pays the day cost in the table above. The Solo Self-Found flag never
comes off, so a Solo Self-Found character can never use the auction house or the
shared stash. A player who wants a different combination makes a new character.
This is what makes a mode worth anything: a Heretic Solo Self-Found character
carried those rules for its whole life, and could not have switched them off at
the moment they became inconvenient.

**Each lethality mode has its own empire upgrade tree.** A character's empire
meta-progression is shared with every other character in the same mode and with
no character in another one, so choosing Hardcore or Heretic for the first time
means starting that mode's tree from nothing. This is the largest cost of the
choice and it is permanent, because the mode cannot be changed afterwards.

**Solo Self-Found is stricter than that, and it is the harsher of the two
choices.** A Solo Self-Found character has its own empire upgrade tree shared
with no other character at all — not with the others in its lethality mode, and
not with another Solo Self-Found character. So it starts the meta-progression
from nothing, and a second Solo Self-Found character starts from nothing again.
The Empire-Wide Upgrades section states both rules in full.

**The shared stash and the auction house are partitioned the same way.** Standard
characters share one stash and one market, Hardcore characters share a second
pair, and Heretic characters share a third. Nothing moves between them. This
exists for the same reason the tree partition does: a fully geared handoff from a
mature Standard character is a larger head start for a first Heretic character
than any number of empire upgrade points, so sealing the tree alone would close
the smaller route and leave the larger one open. A Solo Self-Found character is
unaffected, because it has neither.

**Offline and online are a second partition, crossing the first.** A character is
created as either offline or online and never changes, in either direction.
Those two populations share nothing at all: an offline character and an online
character never share an empire upgrade tree, a stash, a market or a balance of
gold, even when both are Standard. So the partition key is the population
together with the lethality mode, and a player who plays both holds up to six of
everything the account shares rather than three.

**That is forced by the rule that separated the populations, not added to it.**
An offline save is a local file and a local file can be edited, which is why
offline characters have no auction house and no ladder and never exchange
snapshots with the shared table of corrupted characters. A stash that both an
offline and an online character could open would be a route straight through
that rule: an item edited into a local save, placed in the stash, and withdrawn
by an online character reaches the auction house and the ladder. Sealing the
market against offline characters and leaving a shared container open beside it
would close the smaller route and leave the larger one open, which is the same
mistake the lethality mode partition above exists to avoid.

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

| Input | Action |
| :-: | :-: |
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

  

| Input | Action |
| :-: | :-: |
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

  

### **Ending a Run**

**A run ending never costs the character.** There are four ways a run ends:
defeating the Cataclysm boss dungeon, losing the capital, dying in the Last Stand
dungeon, and being killed by the corrupted double that Worn Residue produces. In
every one of them the same character plays the next run, keeping its levels, its
equipment, its class passive trees and its empire upgrade tree. Nothing that
happens in play destroys a character. The one thing that removes a character is
the player deleting it, which is described under Deleting a Character below.

  

**What a failed run costs is the run: the empire and the tier.** The empire map,
the cities, the days elapsed and the progress made toward the active Cataclysms
are all lost, and the next run replays the same tier — the same number of
simultaneous Cataclysms — rather than adding one. Only defeating the boss dungeon
adds a Cataclysm to the next run.

  

**What is kept is everything held outside the run.** The character and its gear
and levels, the empire upgrade tree of its lethality mode, the account's gold and
the shared stash all survive. That is what makes the empire upgrade tree the
primary meta-progression system: a failed run still fed it.

  

**Ordinary death inside a dungeon is not a run ending.** It costs the days listed
in the lethality mode table in section II and, in Hardcore and Heretic, some
equipped items. The run continues.

  

### **Deleting a Character**

**A player can delete a character, and that is the only thing that removes one.**
Nothing that happens in play does. Dying costs days, and in Hardcore and Heretic
some equipped items, and the run continues. A run ending costs the run. Both
leave the character to play again. Deletion is deliberate, it is chosen from
outside a run, and it is the whole of the answer to what can lose a character.

  

**Deleting an ordinary character costs that character and nothing else.** Its
lethality mode's empire upgrade tree, stash, gold and market are untouched,
because none of them ever belonged to the character: they belong to the account,
held once per lethality mode per population. A player who deletes every character
in a mode and then makes a new one there finds the tree exactly as it was left.
That is what the genre does — deleting a Path of Exile character leaves the
account's stash and its atlas progression intact.

  

**Deleting a Solo Self-Found character destroys its private empire tree with
it.** That character is the only owner of its tree, so there is no account left
holding it and no successor that takes it over. Everything belonging to that
character alone goes with it.

  

**Every Solo Self-Found character starts its tree from nothing, always.** There
is no case in which one inherits another's, whether the earlier character is
still being played or was deleted.

  

**So a tree survives everything except the player choosing to end it.** A failed
run feeds it, a death keeps it, and being consumed by Worn Residue keeps it.
Deletion is the single exception, it applies only to a Solo Self-Found
character's private tree, and it is the player's own act rather than an outcome
of play.

  

## **Combat System**

### **Real-Time Action**

Combat is real-time top-down action. Players must read and dodge telegraphed enemy attacks, manage their active resource (Fury, Resolve, Preparation, or equivalent based on class), and deploy their skill kit strategically. Positioning matters — some skills reward melee range, others reward distance, and AOE threats punish clustering.

  

### **Skill Slots**

Each player has six skill slots. Basic attacks are handled automatically and are enhanced by all damage types present.

  

The equipped weapons determine the **pool** of skills a player can draw from, not the contents of each slot. Every combination of an equipped weapon type and an available damage type contributes its skills to that pool. The player then chooses which skills to use and assigns them to slots.

  

A player carrying several damage types will have far more skills available than slots to hold them. Choosing which six to take is part of building a character, and it is why gear that widens the pool is valuable even when its raw statistics are no better.

  

| Slot | Description |
| :-: | :-: |
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

  

| Slot | Typical | Range | Why |
| :-- | :-: | :-: | :-- |
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

  

| Slot | Cooldown | Band | Mana |
| :-- | :-: | :-: | :-- |
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

  

| Class | Mana | Regen | Income while fighting | Everything on cooldown lasts |
| :-- | :-: | :-: | :-: | :-- |
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

  

Each class tree has approximately 74 nodes, 15 keystones, 4 capstone tiers (at 25/50/100/200 points), and a total of ~440 spendable points. The per-character point budget is 230, meaning players invest in roughly 53% of any tree — specialization is required.

  

### **Node Types**

| Type | Description |
| :-: | :-: |
| Basic Nodes | Scaling stat nodes with per-point benefits and threshold bonuses at mid-investment. Most of the tree. |
| Keystones | Single-point investments that fundamentally change how a mechanic works. Build-defining. Require full investment in a parent node. |
| Capstones | One per tier (25/50/100/200 pts). Player chooses one of three options per tier. Escalating power from identity declaration to god-tier mechanics. |

  

### **Class Resource Systems**

Each class has a unique resource that the passive tree unlocks and develops. Resources are central to how the class plays — they are not optional stat bars, they are the engine of the build.

  

| Class | Resource & Summary |
| :-: | :-: |
| Bulwark (War) | Resolve — builds through combat, enables damage reduction and retaliation bursts. Decays out of combat. |
| Berserker (War) | Fury — builds on critical melee hits, decays out of combat. At max Fury, Berserking triggers devastating strikes. Wrath (2H) and Frenzy (DW) are sustained drain states. |
| Saboteur (War) | Preparation — builds by placing and triggering traps and gadgets. Does not decay. Powers trap AOE, evasion, and gadget empowerment. |
| Masochist (Demonic) | Anguish — builds from health you lose, through two generators the tree develops separately: 1 Anguish per 1% of maximum health lost to damage, and 1 per 1% of maximum health spent as an ability cost. Healing removes it at the same rate, so health regeneration is what decays it rather than a timer. Base pool 100. |

  

The Masochist resource is the only one a player controls the decay of. Resolve and Fury decay on a timer out of combat and Preparation does not decay at all; Anguish falls only when the character heals. That is deliberate. The Masochist has by far the largest health regeneration of the three Demonic classes, 37.6 per second against 2,526 maximum health, which is 1.49% per second, so a full pool of 100 drains in about 67 seconds standing still. Healing up and staying powerful are the same resource spent twice.

  

The node graph is `docs/Masochist_Class_Tree_Final.json`. The Ravager and Ritualist resources are not designed yet, and neither are the other 19 class trees; issue #24 covers them.

  

### **Multiclassing**

Players with multiple damage types on their weapon can invest in multiple class trees simultaneously. All trees draw from the same shared point pool, so multiclassing means spreading investment thinner. The deep nodes and capstones in any single tree require focused investment to reach, creating genuine build tradeoffs.

  

## **Classes by Damage Type**

  

### **War**

| Class | Identity |
| :-: | :-: |
| Bulwark | The defensive anchor. Masters of block, armor, and Resolve. Can absorb punishment and retaliate with stored energy. Excels at being an immovable frontliner. Resource: Resolve. |
| Berserker | Fury-driven melee aggressor. Builds Fury on crits, fires off devastating Berserking strikes at max Fury. Two sub-identities: 2H/Wrath (heavy hits, AOE, execute) and DW/Frenzy (attack speed, hit volume, chaining). Resource: Fury. |
| Saboteur | Trap and gadget specialist. Lays proximity mines, deploys turrets and ballistas, and controls space through deployables. Evasion woven throughout the tree as a class-wide survival stat. Pairs with Dagger, Crossbow, and Spear. Resource: Preparation. |

  

### **Demonic**

| Class | Identity |
| :-: | :-: |
| Ravager | Frontline aggressor embodying raw demonic power. Brutal melee combat style with devastating strength. |
| Ritualist | Summoner and manipulator of demonic forces. Commands demonic entities and can possess enemies to turn them against allies. |
| Masochist | Converts received damage into buffs and counterattacks. Uses HP instead of mana for abilities. |

  

### **Death**

| Class | Identity |
| :-: | :-: |
| Soul Collector | Siphons life essence from enemies, weakening foes while strengthening self. Channels stolen life force into stat enhancement and soul-based abilities. |
| Necromancer | Summons and commands the restless dead. Raises skeletal minions, spectral entities, and undead armies. |
| Shadow | Melds with darkness for unparalleled mobility. Can move through obstacles and traverse inaccessible areas. |

  

### **Pestilence**

| Class | Identity |
| :-: | :-: |
| Plague Lord | Harbinger of disease and decay. Commands dark magic and vile concoctions that spread sickness and weaken entire groups of enemies. |
| Virion | Close-quarters combatant with virulent poisons and rotting diseases. Stacks debuffs progressively on enemies to drain defenses and vitality. |
| Poison Master | Concocts potent toxins and brews deadly potions. Deploys poisonous projectiles, clouds, and venomous traps. |

  

### **Famine**

| Class | Identity |
| :-: | :-: |
| Vampire | High health leech sustain. Cannot use energy shields. Survives through aggressive life-stealing. |
| Energy Leech | Disrupts enemy spellcasting by draining mana reserves. Siphoned mana replenishes own pool, enabling devastating abilities. |
| Shield Breaker | Steals and dismantles enemy energy shields. Identifies and exploits defensive barrier weaknesses. |

  

### **Celestial**

| Class | Identity |
| :-: | :-: |
| Nephilim | Forsaken offspring of celestial beings and mortals. Excels at melee combat enhanced by powerful heavenly spells. |
| Zealous Inquisitor | Wields holy fire to cleanse corruption. Mid-range damage dealer that exposes enemy weaknesses and punishes prolonged fights. |
| Dawnbringer | Channels sun and stars for destruction and healing. High mobility hybrid alternating between offensive solar bursts and healing starlight. |

  

### **Chaos**

| Class | Identity |
| :-: | :-: |
| Agent of Chaos | Highly random damage and effects. Damage ranges from extremely low to extremely high. If multiclassing, secondary class perks also gain randomness. |
| Chaos Shaper | Shape-shifting adaptability. Assumes different forms based on weapon type, each with distinct strengths and weaknesses. |
| Discordant Trickster | Master of illusions and deception. Creates lifelike illusions to disorient foes, disrupt formations, and manipulate perception. |

  

### **Void**

| Class | Identity |
| :-: | :-: |
| Singularity | Manipulates gravity for area control. Creates gravitational anomalies that alter movement and positioning of enemies and allies. |
| Avatar of Madness | Induces madness and psychic confusion. Distorts perceptions and drives enemies to hallucination, paranoia, and fear. |
| The Maw | Consumes items and enemies for Attribute points. Rarity of consumed entity determines the reward. |

  

## **Character Stats and Attributes**

### **Power Score**

A player's Power Score combines level, gear quality, gear level, gem quality, socket count, and resistances into a single comparable number, on the same scale as the Enemy Score in section X. Dungeons within a close power range provide average exp and loot. Dungeons below the threshold provide less. Dungeons above provide more.

  

Power Score = (Level Weight × Character Level) + (Gear Weight × Σ over equipped pieces of Rarity × (1 + Upgrade Factor × Gear Level)) + (Gem Weight × Σ over filled sockets of Gem Rarity) + (Resistance Weight × Σ over the eight resistances of Resistance Percent)

  

| Weight | Value |
| :-: | :-: |
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

  

| Gear on every piece at tier 8 | Power Score | Against the anchor |
| :-- | :-: | :-: |
| Cataclysmic | 6,327 | 100% |
| Ascendant | 5,932 | 94% |
| Mythical | 5,536 | 88% |
| Legendary | 5,141 | 81% |
| Masterful | 4,745 | 75% |
| A mix of 4 Cataclysmic, 4 Ascendant, 5 Mythical and 5 Legendary | 5,690 | 90% |

  

So chasing Cataclysmic gear is what pushes a character toward the maximum Power Score, and the anchors describe that ceiling rather than a typical build. **This matters when reading any statement about what a tier 8 character has.** The affix values in section VI were fitted against 72 regular affix slots, which is a full set of Masterful gear; the character sitting exactly on the tier 8 anchor has none of them.

  

| Tier | Level | Gear Rarity | Gear Level | Gems Filled | Each Resistance |
| :-: | :-: | :-: | :-: | :-: | :-: |
| T1 | 12 | Everyday | +3 | 6 | 8.8% |
| T2 | 25 | Quality | +4 | 11 | 17.5% |
| T3 | 38 | Superb | +5 | 17 | 26.2% |
| T4 | 50 | Masterful | +6 | 22 | 35.0% |
| T5 | 62 | Legendary | +7 | 28 | 43.8% |
| T6 | 75 | Mythical | +8 | 34 | 52.5% |
| T7 | 88 | Ascendant | +9 | 39 | 61.2% |
| T8 | 100 | Cataclysmic | +10 | 45 | 70.0% |

  

Gear and gem rarity equal the difficulty tier because there are eight of each, and because the best upgrade stone that can drop is capped by the current difficulty tier. Gear level is tier + 2 capped at +10, which clears every rarity gate in section VI and reaches exactly +10 at tier 8.

  

This reference character scores 6,327 at tier 8, landing exactly on the tier 8 anchor, and 384 against the tier 1 anchor of 385. **The six tiers in between land exactly on their anchors**, because on 2026-08-05 those six anchors were reset to what this formula predicts. Before that they were 871, 1,457, 2,144, 3,251, 4,166 and 5,209, and the six tiers in between were within 5.3% rather than exact. Tiers 1 and 8 were deliberately left alone: the formula's weights are solved from those two anchors and nothing else, so moving either one would shift every prediction. Issue #7.

  

### **Attributes**

Players gain 1 attribute point per level. Attributes are spread across eight categories:

  

| Attribute | Stats | Per Point |
| :-: | :-: | :-: |
| Agility | Move Speed / Evasion | +2% move speed / +0.5% evasion |
| Ferocity | Crit Chance / Crit Multiplier | +0.5% crit chance / +5% crit multiplier |
| Constitution | Armor / Block Chance | +2% armor / +1% block chance |
| Vitality | Max HP / HP Regen | +2% HP / +1% increased HP regeneration |
| Mind | Max Mana / Mana Regen | +2% mana / +1% increased mana regeneration |
| Spirit | Energy Shield / Shield Regen | +2% energy shield / +1% increased shield regeneration |
| Efficacy | CDR / AOE / DoT Frequency | +1% cooldown increase / +2% AOE / +1% increased DoT frequency |
| Luck | Magic Find / Loot Quantity | +0.01% magic find / +1% loot quantity |

  

### **How an Attribute Rounds**

**An attribute is always a whole number of points, rounded to the nearest.** A half rounds up: 36.5 becomes 37.

  

Points come from levelling, from the Maw, and from gear. The gear source is a percentage increase rather than a grant of points — see "What Affixes Do Not Grant" — and a percentage of a whole number is usually not one. A character with 33 Spirit wearing a top-tier +12% Spirit affix reaches 36.96.

  

| Points before rounding | Attribute |
| :-- | :-: |
| 36.96 | 37 |
| 36.5 | 37 |
| 36.4 | 36 |
| 4.48 | 4 |

  

**The rounded number is the only number.** It is not a display convenience laid over a fractional value the calculations keep using. There is one value, it is whole, and the character screen and every stat it drives read the same one.

  

**That is the reason for the rule.** Players of this genre check the arithmetic. A character screen reading 37 Spirit while the maths uses 36.96 means a player works out what 37 Spirit should give, is handed something else, and reports it as a bug. Rounding the value itself removes the disagreement rather than hiding it.

  

**Flooring was considered and rejected.** Rounding down never grants more than was earned, but it takes +12% of 4 Spirit from 4.48 back to 4, so an affix on a lightly invested attribute would be worth exactly nothing. The affix is a percentage so that it is *weak* when spread thin; being worth zero below a threshold is a different thing and reads as broken rather than as a trade-off.

  

**Keeping the fraction was also considered and rejected**, for the reason above: it is the arrangement in which the screen and the maths can disagree.

  

### **The Character Sheet**

A character has 45 stats, grouped the way the gameplay tag list groups its Stat tags.

  

| Group | Stats |
| :-: | :-- |
| Resource | Maximum Health, Maximum Mana, Maximum Energy Shield, Class Resource |
| Recovery | Health Regeneration, Mana Regeneration, Energy Shield Regeneration, Life Leech, Mana Leech, Energy Shield Leech |
| Defence | Armor, Evasion, Block Chance, Damage Reduction, Retaliation, Crowd Control Resistance, and the eight Resistances |
| Offence | Critical Strike Chance, Critical Strike Multiplier, Attack Speed, Area of Effect, Damage over Time, Damage over Time Frequency, Damage over Time Duration, Penetration, Spell Damage, and the eight Damage Against a Type figures |
| Utility | Movement Speed, Cooldown Reduction, Magic Find, Loot Quantity |

  

The tag list also has entries for defence against melee, ranged and spell damage. Those are **scopes that modifiers target**, not separate lines on the sheet. An enchantment reading "you cannot evade or block melee attacks" targets a scope; it does not describe a stat a character has.

  

### **Where Each Stat's Base Comes From**

Attributes only scale. A stat therefore needs a base value before any attribute can do anything with it, and that base comes from one of three places.

  

| Source | Stats |
| :-: | :-- |
| The class | Vitals, recovery, defences, resistances, movement speed, area of effect, and the three damage over time stats |
| The equipped weapon | Attack speed, and off this sheet, attack range and attack damage |
| The skill being used | Critical strike chance, and off this sheet, the base cooldown, projectile count and duration |

  

**A class does not need a base above zero for every stat.** It needs one for every stat it wants its attributes to scale. A class with no base evasion gains no evasion from Agility, and that is the system working rather than failing — it is how a class declines to care about a stat.

  

**Critical strike chance belongs to the skill, not the character.** Each skill carries its own base chance, and the character's gear and attributes scale it. A character has no critical strike chance in the abstract.

  

**Area of effect and the three damage over time stats belong to the character, not the skill.** They are percentages of whatever the skill or the effect itself does, so their baseline is 100% rather than zero. A class naturally better at any of them starts above 100.

  

**The three damage over time stats are Damage over Time, Damage over Time Frequency and Damage over Time Duration.** They are the three numbers every damage over time effect has — how much one tick deals, how often it ticks, and how long it runs — and they are separate stats because all three multiply. One set is shared by every damage over time effect rather than each ailment having its own three; see "Applying Damage Over Time and Other Effects".

  

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

  

| Rule | Value |
| :-- | :-- |
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

  

| Skill | Area of Effect | Its Tags |
| :-: | :-: | :-- |
| Smoke Bomb | 140% | Item.Weapon.Dagger, Type.AOE.PointBlank |
| Thrust | 100% | Item.Weapon.Spear, Type.Strike |

  

Both characters wear the same item, granting +40% area of effect restricted to `Type.AOE`. It reaches the first skill and not the second. The character holds the increase either way.

  

### **Class Stat Lines**

A class supplies a level 1 base and a per-level gain for each stat it wants to scale. Across 24 classes and the 33 stats a class supplies — every stat but attack speed, which comes from the weapon, and critical strike chance, which comes from the skill — that is 1,584 numbers, so every class starts from a shared default stat line and overrides only the stats that express its identity. A class may override any stat; the default is a starting point, not a floor.

  

Per-level scaling is linear. Whether it should stay linear is not settled and will be decided by testing rather than argument.

  

**A class is defined as much by what it refuses as by what it takes.** The Berserker tree has almost no armor and no evasion at all. The Saboteur has no armor, no critical strike investment and no leech. Leaving a stat at the default is how a class declines to care about it, and it is what makes classes feel different before a single point is spent.

  

### **The Three Demonic Class Stat Lines**

These are the three classes the vertical slice needs, because a damage type unlocks all three of its class trees. Values are at level 100 with no gear and no attribute points spent. Only the stats any of the three overrides are listed; the remaining 19 are identical across all three.

  

| Stat | Ravager | Ritualist | Masochist |
| :-- | :-: | :-: | :-: |
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

  

Attribute points and every gear affix worded "increased" add together into one bucket per stat, and that bucket multiplies the base once. Only sources worded "more" or "less" multiply separately, and that wording is reserved for **gems, passive tree nodes and enchantments**, where the design already wants outsized effects.

  

**Every node in a passive tree may use that wording, not only its keystones.** Basic nodes, keystones and capstone options are all spent from the same 230 point budget, and a point spent in a tree is a point not spent anywhere else, so the tree as a whole is the place where a player has already paid for an outsized effect. The rule that matters is the one above it: **a gear affix never uses it.** Gear is where a "more" multiplier would be unreadable, because a drop can carry one by accident and a passive node cannot — a node is chosen.

  

This covers the empire upgrade tree as well as the four class trees, it covers capstone options as well as basic nodes, and it covers the things a passive node grants that are not damage. `Economic Zones` gives "+5% more Gold from all sources per point", `Salvage Protocol` gives "25% more crafting materials" and `Thrifty` makes Market items cost "1% less gold per point"; all three are empire basic nodes and all three are correct as written.

  

**An ordinary gear affix is never a "more" multiplier.** It is flat or it is increased. Keeping the multiplicative sources off gear rolls is what keeps a rare drop readable, and it gives the enchantment library a job that ordinary affixes cannot do.

  

**Everything in the increased bucket has diminishing returns and every "more" multiplier does not.** That is the whole reason for having two buckets rather than one. A character at +800% increased who adds another +60% increased gains 6.7%; the same character adding a 60% "more" multiplier gains 60%. Two independent 50% "more" sources give 2.25 times, not 2.0 times.

  

| Already held | Another +60% increased is worth | A 60% "more" is worth |
| :-: | :-: | :-- |
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

  

| Efficacy Points | 4-Second Skill Becomes | Shown As |
| :-: | :-: | :-: |
| 25 | 3.20s | 20.0% |
| 50 | 2.67s | 33.3% |
| 100 | 2.00s | 50.0% |
| 100, plus gear worth another 3.00 | 0.80s | 80.0% |

  

Dividing rather than subtracting is what keeps this from breaking. Subtracting 1% per point would reach zero cooldowns at 100 points of Efficacy. Dividing, all 100 points halve every cooldown, gear pushes further with each point worth progressively less, and zero is unreachable. No cap is needed, and no point is ever wasted.

  

Damage-over-time frequency uses the same form, because it is also a rate. Area of effect at +2% per point stays additive, because a larger radius has no runaway.

  

**Caps.**

  

| Stat | Cap | Hard or Soft |
| :-: | :-: | :-: |
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

  

**What makes a hit area damage is its tags, not its shape.** A skill deals area damage when its tag list carries `Type.AOE.PointBlank`, an explosion centred on the caster or the target, or `Type.AOE.Aura`, a radius that moves with the caster. 37 of the designed skills already carry one of the two.

  

**`Type.AOE.Persistent` does not make a hit unevadable.** It means the skill leaves a zone behind — burning ground, a cloud — and describes that zone rather than the blow. Flamedart is tagged `Keyword.Charge, Type.AOE.Persistent`: the charge makes contact and can be evaded, and the fire trail it leaves damages whatever stands in it. A zone's own damage is area damage, decided where the zone deals it.

  

**A skill with no area tag deals a direct hit.** Cinderslash is `Type.Strike, Type.Melee` and nothing else, so it is one sword blow and evasion applies to it. Being a Strike does not by itself make an attack unevadable, or every melee skill in the game would ignore evasion.

  

### **The Damage Calculation**

One incoming hit is resolved in this order. Each step operates on what the previous step left.

  

| Step | What happens |
| :-: | :-- |
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

  

**Enemy rarity produces an Overwhelm ladder by itself**, because rarity already raises Enemy Score. At tier 8, a player at that tier's maximum Power Score loses 8.4% of their mitigation to a Common enemy and 20.9% to a Cataclysm Boss, with no per-rarity number written anywhere.

  

**This is the reason to over-cap resistance.** Resistance above 70% is the headroom Overwhelm eats into. A character at exactly 70% loses mitigation the moment they fight above their score; one at 95% stays at the cap until the gap is wide.

  

### **The Final Fight Is Never Safe, and That Is Deliberate**

**A player at the absolute ceiling of their own difficulty tier is expected to lose between one Cataclysm dungeon run in six and one in five.** Between 16% and 21% at every tier. That is intended, not a tuning accident.

  

**It is one run in five, not one attempt in five.** Dying in the Cataclysm boss dungeon ends the run. There is no retry.

  

**The Cataclysm Boss out-scores the maximum player Power Score of its own tier at every tier**, from 2.0 times at tier 1 down to 1.2 times at tier 8. So there is no amount of gear reachable within a tier that makes the fight safe, and the question "what Power Score clears it" has no answer on its own — only "what Power Score brings the death chance down to X", and the answer to that is always above the tier's own ceiling.

  

**Why it is built this way.** Overwhelm has no hard gate: being out-scored strips mitigation in proportion to the gap rather than refusing entry. A Cataclysm Boss the player could out-score would make the final fight of a maxed run a formality. Leaving it above the ceiling means the last fight is decided by the odds, and the empire layer is what the player spends between runs to change those odds.

  

**The 16% to 21% is a measurement of the current model, not a target the model was fitted to.** It falls out of the Overwhelm rate, the per-floor risk, the boss risk multiplier and the enemy score formula, none of which were set with this figure in mind. It is written down so that a change to any of them can be seen to have moved it. `sim/tests/test_power_threshold.py` fails if it leaves a band of 10% to 30% at any tier.

  

Measured at 125 floors, the midpoint of the 100 to 150 the Cataclysm boss dungeon spans, with no dungeon modifiers and no subtype.

  

### **Maximum Resistance**

Over-capping and raising the maximum are two different things and are easy to confuse. **Over-capping** is having more than 70% resistance, which any resistance affix does and which is worth having because penetration and Overwhelm are subtracted before the cap. **Raising the maximum** moves the 70% itself, so more of a hit is actually stopped.

  

**Only enchantments raise the maximum. No affix may.** One positive enchantment does it, "You have +10 maximum resists", and three negative enchantments lower it. That placement is not incidental: every affix has seven tiers and can appear on several pieces, and maximum resistance does not tolerate that range. Enchantments have one value rather than seven, carry a weight from 1 to 4 controlling how rare they are, appear only on Legendary items and above, and take one of the four slots a regular affix would have used. The maximum resistance enchantment is weight 1, which is the rarest and most powerful tier.

  

**The maximum is hard capped at 90%.** Two of that enchantment reach it and a third is wasted.

  

| Figure | Value | What it is |
| :-- | :-: | :-- |
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

  

| Metric | The stat that raises it | What raising it does | Total damage |
| :-- | :-- | :-- | :-- |
| Damage per tick | Damage over Time | Each tick hits harder | Rises |
| Tick rate | Damage over Time Frequency | More ticks in the same time | Rises |
| Duration | Damage over Time Duration | The effect runs for longer | Rises |

  

**Each of the three is a stat on the character sheet with a baseline of 100%, and each has one affix worth 52% at T7.** One set of three is shared by every damage over time effect rather than each ailment having its own; "Scaling Damage Over Time" carries where the 52% comes from and why it had to be solved for all three at once.

  

**All three multiply.** A character with 48% more of each does not deal 148% of the base total; it deals 1.48 × 1.48 × 1.48, which is 324%. That is deliberate and it is the reason the three are separate stats rather than one. It also means the values on the affixes and attributes that grant them cannot be set one at a time — see "What Affixes Do Not Grant", which forbids an ordinary affix from being a "more" multiplier, and note that three ordinary increases on the same output produce the same curve as one.

  

**This is the opposite of what Path of Exile and Last Epoch do**, and it is a deliberate departure. Both of those define a damage over time effect as a total spread across a duration, so ticking faster delivers the same total sooner and adds nothing. The reasoning for going the other way is in `docs/DECISIONS.md`.

  

**Chance to apply caps at 100%. Everything above it becomes magnitude instead.**

  

| Chance from all sources | What happens |
| :-- | :-- |
| 60% | Applies on 60% of hits, at its normal magnitude |
| 100% | Applies on every hit, at its normal magnitude |
| 250% | Applies on every hit, at 2.5 times its magnitude |
| 800% | Applies on every hit, at 8 times its magnitude — a 700% increase |

  

**Why the overflow is not simply wasted.** Ailment chance comes from two sources that both scale hard: affixes, and gems, where the gem applying bleed reaches 150% chance on its own at Cataclysmic rarity. Without this rule a build would hit the cap and every point past it would be dead, so an ailment build would stop progressing at exactly the point it was coming together.

  

The chance summed is the total across every source: affixes, gems, keystones and enchantments alike.

  

**What magnitude scales depends on the effect, and it is never wasted.**

  

| The effect has | Magnitude scales |
| :-- | :-- |
| Damage over time, with no cap on it | The damage |
| A strength with a cap, such as a slow | The strength up to that cap, then the duration instead |
| No strength axis at all, such as Madness | The duration |

  

**The effects a player can apply**

  

| Effect | What it does | Magnitude scales |
| :-- | :-- | :-- |
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

  

**Cripple, Weaken, Shred and Madness have one affix each, a chance to apply, and no separate affix for magnitude or duration.** The six damage over time effects have three affixes: a chance to apply, a damage affix and a duration affix. The four weakening effects have one, and that is the design rather than a gap.

  

**Chance to apply is already all three levers.** Chance above 100% becomes magnitude, and magnitude above the cap becomes duration, which is the rule the two tables above state. So one affix raises whichever of the three the build has not yet filled, in that order, and a second affix for magnitude would raise a number that is already capped.

  

**Measured 2026-08-05**, at the top affix tier on fully upgraded gear. Eleven pieces can carry a chance to apply, one each, for 165%. Cripple reaches its 80% cap at 267% and Weaken at 400%, so affixes alone reach 0.6 and 0.4 of the cap and one Cataclysmic gem takes Cripple past it, four take Weaken past it. A build that wants one of these effects fills its cap; a build that does not, does not. `sim/analyse_weakening_ailments.py` prints the working.

  

**This is not a claim that the four are as strong as the six**, or that one affix is priced correctly against three. It is a statement about levers: a magnitude affix and a duration affix would add no way to scale these effects that the chance affix does not already provide.

  

**The comparison games all do give their equivalents a second lever, and they have no rule like this one.** Path of Exile sells increased effect of Chill and of Withered; Diablo IV sells Crowd Control Duration on amulets. In those games a chance to apply stops paying at 100%, so a separate magnitude stat is the only way to keep scaling. Here it does not stop paying, which is why one stat does the work of three. `docs/DECISIONS.md` records the survey and what would reverse this.

  

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

  

| Damage Type | Available Weapon Types |
| :-: | :-: |
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

  

| Weapon | Skill | Description |
| :-: | :-: | :-: |
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

  

| Weapon | Skill | Description |
| :-: | :-: | :-: |
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

  

| Shape | What it does | Numbers it reads |
| :-: | :-: | :-: |
| Strike | Hits everything in a cone or ring around the caster. An angle of 360 is a ring. With a duration and an interval it repeats, which is what a spin is. | Radius, Angle, MaxTargets, Duration, Interval, Knockback |
| Projectile | Sends something out toward where the player is aiming. One that pierces travels a line and hits what it passes; one that does not lands and hits in a radius there. | Range, Radius, Pierce, Returns, Speed |
| Self Buff | Grants the caster an effect for a duration. | Duration, Radius |
| Movement | Moves the caster. A leap hits where it lands, a charge hits everything on the way, a blink hits at both ends and nothing between. | Mode, Range, Radius |
| Summon | Spawns minions that fight for the caster. With a duration and an interval it spawns over time and collapses at the end, which is what a rift is. | Range, Radius, Count, MaxActive, Duration, Interval |
| Aura | A radius around the caster. Held as a toggle when it has no duration, and timed when it has one. | Radius, Duration, Interval |
| Debuff | Applies a named effect to enemies within range, nearest the cursor first, without necessarily damaging them. | Range, Radius, MaxTargets, Duration |

  

**A burning patch of ground is a rider, not a shape.** Eight of the sixteen slice skills leave one behind on top of whatever else they do: Molten Cleave drags a line of slag, Emberhurl leaves its flight path burning, Infernal Plunge leaves a pool of lava. Any shape may carry `GroundRadius` and `GroundDuration`. Six other riders work the same way: `GroundHitsAllies` makes that ground burn everything standing in it whatever side it is on, `Burn` sets what the skill hits alight, `Effect` names a status effect from the Buffs, Debuffs or DoTs sheets, `StunSeconds` is how long a stun lasts, `FinalHitPercent` is a closing blow at the end of something that repeats, and `HealthCostPercent` is a cost in health rather than mana.

  

**`GroundHitsAllies` is off unless a skill sets it, and no player skill does.** Burning ground normally hurts only the caster's enemies, which is what every one of the eight player skills that leaves some wants. The Hellhound's fire trail is the only thing in the game that sets it, and that is what makes the trail the one source of friendly fire.

  

**Every minion type has its own stats.** A minion is not a percentage of its summoner. Each named type — a lesser imp, a mote of living fire, a bolt turret, a ballista, a spike trap — carries its own health, damage per hit, attack interval, movement speed, reach and threat weight. Two skills that produce the same creature produce the same stats, and one skill that produces two kinds of creature produces two stat blocks.

  

**A minion reaches its summoner through exactly three channels, and nothing else crosses.**

  

| What a minion takes from its summoner | How |
| :-: | :-: |
| Its side | Everything the summoner is hostile to, it is hostile to |
| Its base health and base damage | The type's own base, raised by the summoner's **level** |
| Increased damage | One **primary attribute**, declared by the minion type |

  

**Everything else is blocked unless a modifier says "minion".** A minion does not take the summoner's weapon damage, flat added damage, attack speed, critical strike chance or multiplier, penetration, armour, evasion, block, resistances, energy shield, leech, movement speed, cooldown reduction, area of effect, magic find, chance to apply an ailment, damage over time, or increased damage against a type of enemy. If a modifier does not name minions, it does not reach one.

  

**The summoner's level is the floor and gear cannot touch it.** It is what keeps a minion useful for a player who has found no minion gear at all, and because no affix, enchantment, gem or passive can raise it, it can never be counted twice.

  

**One enchantment is the named exception, and it is the model for any future one.** "Summoned minions inherit 10%-25% of your armor and resistances" already exists. Inheritance beyond the three channels above exists **only** where an enchantment says so by name. A blanket rule with no exception would contradict the enchantment table on the day it was written.

  

**Minion count is gear-modifiable, and only through enchantments. It is never an affix.** Two enchantments raise it and the unique-per-character rule means each can appear once across all equipped gear, so the total a player can reach is bounded permanently. That placement follows the same rule as maximum resistance, stated earlier in this document: an affix has seven tiers and can appear on several pieces, and a modifier that does not tolerate that range belongs in the enchantment table. Count tolerates it least of all — there are eight ring slots, so a "+1 minion" suffix would be eight from rings alone before any other piece — and count multiplies every other minion investment at once, because damage, effective health and rider uptime all scale with how many minions are alive.

  

**Count applies to summons, not to deployables.** A summon spawns at the caster and walks, so one more is safe. A deployable is placed in a pattern: Iron Fortress arranges two ballistae and three spike traps around the caster, and adding to a placement pattern is a level design problem as well as a balance one.

  

**This reverses the rule this document used to state**, which was that a minion's attack deals 30% of its summoner's weapon damage, that it attacks once per second, and that it has no stats of its own. That rule was justified "for two skills". There are six, and two of them already state their own health and attack rate: a bolt turret has 200 health and fires every 1.5 seconds, a ballista has 500 health and fires every 2 seconds. A class whose survival depends on its minions — the Ritualist "survives at range and behind what it summons" — had no way to make them tougher, because minion durability was not stated anywhere.

  

**Scaling from an attribute rather than from weapon damage is what makes minion affixes safe.** Under the old rule, weapon damage affixes already scaled minions, so adding minion damage affixes on top would have scaled them twice from one investment. Once minion damage no longer reads the summoner's weapon, that double count cannot happen. The second protection is structural: no ordinary affix in this game is a multiplier, so an attribute's contribution and an affix's contribution add rather than multiply.

  

**Four numbers are not settled and are tracked rather than guessed here.** Which attribute minions scale from (#335). The per-type base health and damage, which need the simulation rather than a judgement (#336). The four minion affixes and their values (#337). Whether the three deployable skills state their numbers in data rather than in prose (#338).

  

**The Shape column is deliberately separate from the Tags column.** The tags already have a job: an increase from gear applies to a skill only if the skill carries the tags that increase requires. Deciding behaviour from them as well would mean adding a tag to make a skill work silently changed which gear applied to it. Path of Exile draws the same line, keeping the internal type list that gates support gems separate from the identifier that names a skill's code.

  

# **VI. Itemization**

## **Item Slots**

  - Armor: Head, Chest, Shoulders, Gloves, Pants, Boots, Belt
  - Jewelry: 8 Rings, Necklace, Relic
  - Consumables: 4 Potion slots
  - Weapons: See weapon types above

  

## **Weapon Sub-Types**

Weapons have a physical sub-type that determines baseline combat properties:

| Sub-Type | Property |
| :-: | :-: |
| Piercing | Ignores 20% of enemy armor |
| Slashing | 10% more damage vs. HP |
| Blunt | 10% chance to stun for 0.75 seconds |
| Magic | 10% more damage vs. shields |

  

**Blunt stuns rather than doing bonus damage against armor.** Its original property put it in direct competition with Piercing, which already beats armor and has a whole family of affixes that scale it — ignoring armor appears on skills, on critical hits, on traps and on first hits. Nothing anywhere scales damage against armored targets, so Blunt was a flat bonus with nowhere to grow.

  

The stun uses the shortest duration any designed skill uses. A weapon sub-type that can stun on every hit must not outclass the skills whose entire purpose is stunning, which run to 3 seconds. Crowd control resistance reduces the chance proportionally, so a character at 100% cannot be stunned at all. An evaded hit never stuns, because nothing made contact; a blocked hit still can, because a block reduces damage rather than preventing contact — provided what got through still clears the damage threshold below.

  

### **Stun and the Anti-Stun-Lock Rule**

**Being stunned repeatedly with no chance to act is the failure this rule exists to prevent.** Many games in the genre let the smallest hit stun, and a player can then be held still until they die. Three rules together stop it, and all three are needed.

  

| Rule | What it stops |
| :-- | :-- |
| A hit must take at least 10% of the target's maximum health to stun | Constant interruption by small hits |
| A target that is stunned cannot be stunned again for 5 seconds | Being chain-stunned by large hits |
| A boss cannot be stunned at all | The player holding a boss still for the whole fight |

  

**A damage threshold alone is not enough**, because large hits could still chain. **An immunity window alone is not enough**, because small hits could still interrupt constantly. The two are a pair.

  

**The threshold is measured against damage actually dealt, not the damage swung.** A hit that armor and resistance reduced to a scratch is a scratch. This is what makes defensive investment stop the interruption rather than only reduce the damage.

  

**A skill whose stated effect is to stun ignores the damage threshold.** Shield Bash, Shockwave Leap, Lunge and Whip Swing all state that they stun, and a threshold that made them fail against a healthy target would leave them doing nothing they were written to do. Such a skill does **not** ignore boss immunity, and it does not ignore the immunity window.

  

**Where 10% comes from.** It is the middle of what the genre ships, and the three games surveyed do not agree with each other.

  

| Game | Threshold to be able to stun |
| :-- | :-- |
| Last Epoch | More than 5% of the target's maximum health |
| Path of Exile | About 10% of effective maximum life, because a computed stun chance at or below 20% is discarded |
| Path of Exile 2 | 15%, below which the chance is zero |

  

Taking the middle rather than the strictest is deliberate. This design also has a 5 second immunity window, which is longer than Last Epoch's 1 second and longer than the 4 seconds Path of Exile gives its unique bosses, so the window is doing most of the work and the threshold does not also need to be the harshest of the three.

  

**A boss cannot be stunned at all.** Not resistant, not harder to stun — immune. This is the half of the rule that had to be decided alongside the other two: whatever stops the player being stun-locked is what stops the player chain-stunning a boss, and a boss that can be held still is not a fight.

  

**Why immunity rather than resistance.** None of the three surveyed games uses plain immunity: Path of Exile makes a unique boss immune only while stunned and for 4 seconds after, Last Epoch counts a boss as having 50% more health for the stun calculation, and Diablo IV routes crowd control into a separate stagger meter that must be filled before any of it applies. Outright immunity is the simplest of the four and it is what was chosen.

  

**What this rule covers: hard stops, and nothing else.** An effect is covered when it completely stops the target operating any part of its character. Everything short of that is manageable by the player it is used on, and is limited by its own numbers rather than by this rule.

  

| Effect | Covered | Because |
| ----- | ----- | ----- |
| Stun | **Yes** | The target cannot act at all |
| Knockdown | **Yes** | The target cannot act at all; it is simply on the floor while it happens |
| Slow, such as Cripple | No | Slower is still able to act |
| Damage reduction, such as Weaken | No | Weaker is still able to act |
| Displacement, such as a 4 metre knockback | No | The target can act on arrival |
| Disarm | No | Movement and any skill that does not need the weapon still work |
| Madness | **Open** | The target operates every part of itself, but not for the side that owns it. Issue #303 |

  

**A slow is not a stun and is not covered by this rule.** Cripple reduces an enemy's movement and attack speed and leaves it able to act, which is why its reduction caps below total — a full stop would be a stun by another name. The same reasoning applies to Weaken.

  

**Knocking a target back and knocking a target down are two different effects, and only one of them is covered.** Twelve weapon skills push or floor an enemy, and they do not all do the same thing.

  

| Effect | What a skill says | Covered by this rule |
| ----- | ----- | ----- |
| Displacement | "knocks them back 4 meters", "knocking them aside" | **No** |
| Knockdown | "knocked down for 2 seconds" | **Yes, in full** |

  

**A knockdown is a hard stop, so it carries all three parts of the rule.** It stops the target acting for a stated number of seconds, which is the whole of what a stun does; the target simply happens to be on the floor. Two Ultimates knock down — Warlord's Decree for 2 seconds and Cataclysm for 3 — and both are longer than every stun any skill grants, which run 0.75 to 1.5 seconds. Leaving knockdown outside the rule would mean the longest hold in the game is the one nothing limits, and that it works on a boss while Shield Bash's 1.5 seconds does not. The same exemption applies as for stun: a skill whose stated effect is to knock down ignores the damage threshold, and does **not** ignore boss immunity or the immunity window. The two share one window rather than one each, because two 3-second holds taken in turn is exactly the failure the window exists to stop.

  

**Displacement is not covered, because it does not hold the target still.** Being pushed four meters costs the target the distance and nothing else; it can act on arrival. There is no damage threshold on it, so a weak hit can still shove, and a boss can be pushed.

  

**It is limited instead by diminishing distance.** Each displacement applied to a target that has already been displaced within the last 5 seconds moves it **half as far as the one before** — the full distance, then half, then a quarter, and so on. The count resets once 5 seconds pass in which that target is not displaced at all. There is no damage threshold, no immunity flag and no boss exemption: a boss is pushed under the same halving as anything else, because a boss pushed four meters is still fighting, while a boss held still is not a fight. That is the whole difference between this rule and the one above it.

  

**The 5 seconds is the same 5 seconds.** It is the stun immunity window, reused rather than a second number to remember. The two do not interact otherwise — being displaced does not delay a stun and being stunned does not reset the displacement count.

  

**Why distance rather than immunity.** Path of Exile 2 already treats distance as the quantity rather than as a yes or no: skills carry increased knockback distance, and defensive modifiers reduce the distance an incoming knockback moves you. Diablo IV takes the other route, adding a flat 40% to a hidden crowd control resistance per knockback and making the target immune once that resistance reaches 65%. Immunity works there because knockback in Diablo IV comes from skills that can be repeated quickly. **Here it cannot**: all nine displacing skills sit in the Heavy or Movement slot, and nothing in any other slot displaces at all, so the case Diablo IV's threshold exists to stop is already bounded by which slots the effect lives in. A Heavy attack is the slow one by design — Molten Crush is described as "the slowest heavy blow in the game" — and a Movement skill goes on cooldown.

  

Halving is also the only option under which no skill ever visibly does nothing. Bull Rush and Cinder Rush charge through a crowd "knocking them aside"; under an immunity flag the player would run through enemies that do not react, which reads as a defect rather than as a rule. A halved shove still looks like a shove.

  

**It cannot produce the failure it was written for.** Three displacements inside 5 seconds move a target 4 meters, then 2, then 1 — seven meters in total, and nothing worth measuring after that. A target cannot be held at the far end of a room.

  

**Outright immunity to displacement still exists, as a skill effect rather than as a rule.** Living Pyre, Unstoppable Force and Forge Stance each state that their user cannot be knocked back, and Bull Rush and Cinder Rush grant immunity to all crowd control while charging. Those are unchanged. Note that nothing in the game currently knocks the *player* back, so all five clauses are written against a threat the data does not yet contain; issue #310 carries that.

  

**Where this split comes from.** The genre draws the same line. In Path of Exile a knockback does not interrupt the target's actions at all, which is the difference from a stun. Path of Exile 2 keeps knockback as pure movement and puts the act-prevention in Heavy Stun, which is harder to reapply for a short time afterwards. Diablo IV is the clearest: it ships Knockback and Knock Down as separate effects, where Knock Down pins the target in place, and its own documentation says Knock Down does not count as a Stun. Diablo IV also limits repeated knockback, but by an escalating resistance rather than by a damage threshold, which is where the diminishing distance rule above started.

  

**What offensive crowd control gear exists is not yet decided.** Cripple is the only crowd control effect available as an affix today. Stun reaches the game through the Blunt weapon sub-type, through four weapon skills, and through two enchantments, but no affix grants a chance to stun and none scales a stun's duration. Issues #298 and #299 carry these.

**A slow's position is settled and is not among those open questions**, and neither is a knockdown's or a displacement's. All three are stated above: a slow is not a stun and this rule does not cover it, a knockdown is covered in full, and a displacement is not covered but is limited by halving its distance on repeat. This paragraph used to list a slow and a knockback as open as well, which made the document say two things at once about each.

  

## **Item Rarities**

**Rarity is not a property an item carries. It is a label for what fills its four slots.** An item that drops with an enchantment is a Legendary; one that drops with three regular affixes is a Superb. Every piece has four slots, and each holds either a regular affix or an enchantment.

  

| Rarity | Enchantments | Regular affixes | Notes |
| :-: | :-: | :-: | :-- |
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



| Difficulty tier | Highest affix tier a drop can roll | What else that tier brings |
| :-: | :-: | :-- |
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

  

| Family | Covers | T7 value each | Best when |
| :-- | :-: | :-: | :-- |
| Single resistance | 1 | 20% | Few Cataclysms are active |
| Two resistances | 2 | 14% | The middle of a run |
| All resistances | 8 | 6% | Many Cataclysms are active |

  

The efficient family changes as a run goes on, which is the point of having three. A difficulty tier is a run and each tier adds a Cataclysm, so the number of resistances that matter grows from one to eight. A single-resistance affix is the best use of a slot when one Cataclysm is active and nearly worthless when eight are; an all-resistance affix is the reverse.

  

Capping all eight resistances at tier 8 costs about 12 affix slots out of 72 with perfect rolls, and about 16 with the worst rolls. That difference of roughly 4 slots of gear is what the perfecting and rerolling materials are worth.

  

### **Health and Damage Affixes**

These have no breadth axis. What they have instead is the two ends of the stat pipeline from section IV: a flat affix enters the base bracket, an increased affix joins the multiplier.

  

| Affix | T7 value | Rolls between |
| :-- | :-: | :-- |
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

  

| Affix | T7 value | Best when |
| :-- | :-: | :-- |
| Increased damage | 125% | Many Cataclysms are active |
| Increased damage against one type | 400% | Few Cataclysms are active |

  

**Where 400% comes from.** It is the ratio this game already pays for narrowing a modifier from all eight damage types to one. The resistance families give 20% per type at breadth one and 6% per type at breadth eight, so narrowing is worth about 3.33 times. The generic damage affix is the breadth-eight case, because it applies whatever the target is. 125% times 3.33 is 417%, rounded to 400%.

  

**What that produces over a campaign.** A run starts with one Cataclysm active and adds one each time a Cataclysm is defeated. The generic affix is worth 125% whatever stands in front of the player; a type-specific one is worth 400% against its own type and nothing against the other seven, so across C active Cataclysms it averages 400/C. The two are equal at C = 3.2. The type-specific affix is the better use of a prefix for the first three Cataclysms of a campaign and the generic one from four onward. That is the same shape the resistance ladder has, and it is where the reason to change equipment between runs comes from.

  

**There is no two-type or all-type version.** The all-type version is the generic Increased Damage affix, which already exists; a second one would be the same affix twice. A two-type version would sit between them, in the way the two-resistance affix does, and is deliberately not built yet: the two ends have to be played before a middle rung can be priced.

  

### **Scaling Damage Over Time**

Three affixes, one for each of the three numbers a damage over time effect has. Each gives **52% at T7**.

  

| Affix | T7 value | Raises |
| :-- | :-: | :-- |
| Increased damage over time | 52% | How much one tick deals |
| Increased damage over time frequency | 52% | How many ticks happen per second |
| Increased damage over time duration | 52% | How long the effect runs |

  

**All three affixes exist because all three numbers multiply**, which the "Applying Damage Over Time and Other Effects" section states. Two of the three had no affix and no stat at all until this was written, so a character could roll ten different chances to apply an ailment and never make one of those ailments hurt more.

  

**Where 52% comes from.** It is solved, not chosen, and it has to be solved because the three multiply each other. Six affix slots spent on Increased Damage multiply a direct-hit build's damage by 8.5, and six slots is the build the damage numbers in "The Damage Target" are fitted against. Two slots on each of the three damage over time affixes has to reach the same 8.5, so each affix is the value that satisfies (1 + 2v)³ = 8.5. That is v = 52.04%, rounded down to 52%.

  

**Setting any one of them against an existing affix would give the wrong number.** Increased Damage is 125%, but three affixes at 125% each would multiply damage over time by 8.5 × 8.5 × 8.5, not 8.5. This is why the three could not be priced one at a time and why the affix that already existed, Increased Damage over Time Frequency, was wrong at 12%: it had been set to match Increased Armour and Increased Maximum Health, on the assumption that ticking faster only changed when damage arrived rather than how much of it there was.

  

**The comparison holds the slot count fixed and nothing else, and it only holds at six slots.** An additive bracket and a product of three brackets cross exactly once. Below six slots a damage over time build is behind a direct-hit build spending the same, and above six it is ahead — about 1.8 times at twelve slots and about 3 times at eighteen. That shape follows from the three levers multiplying, which is deliberate, but the size of it has not been played and is expected to move.

  

**One set of three stats is shared by every damage over time effect.** Not one set per ailment. Six of the ten ailment affixes apply a damage over time effect and skills apply burn outright as well, so three levers each would be eighteen affixes serving one build archetype, against the eight the whole Damage Against a Target's Type family costs. It also matches the stat that already existed: there has only ever been one Damage over Time Frequency, shared by everything.

  

**The Efficacy attribute keeps granting 1% increased damage over time frequency per point and does not grant the other two.** One attribute point buying three multiplying increases would make Efficacy strictly the best attribute for any damage over time build, and no other attribute compounds that way. The 1% is also already generous once frequency counts as damage: 100 points of Efficacy multiply damage over time output by 2.0, where 100 points of Ferocity — the direct-hit damage attribute — multiply a direct hit by about 1.56 through critical strike chance and multiplier together.

  

### **The Damage Target**

The damage numbers are not chosen. They are read off the enemy statistics in section X and fitted to them.

  

An average Common enemy at difficulty tier 8 has 3,362 effective health and should take **2 non-critical hits** to kill, so a player needs **1,681 damage per hit**. Solving the pipeline backwards, a character spending 6 slots on flat damage and 6 on increased damage needs a base of 198, of which the affixes supply 108. The weapon and its skill together supply the remaining 90.

  

Everything else follows from that one number rather than being set separately:

  

| Enemy at tier 8 | Non-critical hits to kill |
| :-- | :-: |
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

  

| Position | What it carries | Examples |
| :-- | :-- | :-- |
| Prefix | How big a character's numbers are | Health, mana, energy shield, armor, evasion, damage, spell damage, class resource |
| Suffix | How often, how fast, and how much gets through | Resistances, attack speed, critical strikes, penetration, regeneration, leech, block, movement speed, cooldown reduction, area of effect, magic find |

  

### **One Affix Per Group**

The split above says which pool an affix is drawn from. It does not say what an affix may sit beside, and without a second rule a four-affix Masterful piece can roll **Flat maximum health** four times over. Slot restrictions do not help: every affix is restricted against the slot and against nothing else, least of all itself.



**The rule. An affix belongs to a group for every stat it grants, named by the stat and the kind together. One piece holds at most one affix from any group.**



**The group is derived from what the affix grants, not written on it.** Two affixes granting the same stat in the same kind are in the same group because they grant the same thing, so a new affix cannot be added without a group and two copies of one stat cannot be given different groups by mistake.



| Case | What the rule gives | Why |
| :-- | :-- | :-- |
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

| Base | Implicit |
| :-- | :-- |
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

  

| Base | Hands | Sub-Type | Max Damage Types | Attacks/sec | Implicit |
| :-- | :-: | :-: | :-: | :-: | :-- |
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

  

| Difficulty tier | Most on one one-hander | Most on one two-hander | Most while dual wielding |
| :-: | :-: | :-: | :-: |
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

|  | Two-handed | Dual wielding |
| :-- | :-: | :-: |
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

  

| Affix | T7 Chance | Kind | Same effect as gem |
| :-- | :-: | :-- | :-- |
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

  

| Family | Slots | Where |
| :-- | :-: | :-- |
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

  

**An attribute is always a whole number of points, rounded to the nearest.** A percentage of a whole number is not one — 33 Spirit with a top-tier +12% Spirit affix is 36.96 — so the result is rounded, and it is rounded in the maths and not only on the character screen. See "How an Attribute Rounds" below.

  

**They are suffixes, and no hybrid grants one.** One attribute per affix, never two.

  

**Which slots each one rolls on follows the stats it drives**, rather than being chosen separately. Ferocity drives critical strike and Efficacy drives area of effect, both of which already roll on a weapon, so those two can appear on a weapon. Vitality drives health and Constitution drives armour, which do not roll on weapons, so those two cannot. That keeps a weapon offensive without needing a rule of its own.

  

**No ordinary affix is a "more" multiplier.** An affix is flat or increased. Multiplicative sources come from gems, passive tree nodes and enchantments, as section IV states. Every node in a passive tree may carry one, not only its keystones, because a node is chosen and paid for out of a fixed point budget; a gear affix is rolled, which is why gear is the one place the wording is refused.

  

## **Enchantment System**

### **Overview**

Enchantments are high-power modifiers available only on Legendary and above items. Each item has a maximum of 4 total affix slots shared between regular affixes and enchantments. Players must choose between stacking powerful enchantments or filling slots with standard affixes — both have merit depending on the build.

  

### **How Enchantments Roll**

Enchantments are tag-based rather than skill-specific, ensuring the loot pool remains manageable while still feeling relevant to builds. Each enchantment has one or more tags that determine which items it can appear on. Positives and negatives roll independently — a strong positive is not guaranteed to come with a weak negative.

  

|  |  |
| :-: | :-: |
| **UNIQUE PER CHARACTER** | Each enchantment can only appear once across all of a player's equipped gear. You cannot equip the same enchantment on multiple pieces. This prevents degenerate stacking of powerful effects (e.g. equipping '50% increased HP' on every ring slot), keeps the power ceiling consistent, and makes build assembly a genuine puzzle — players must find a complementary set of enchantments rather than farming one great roll repeatedly. |

  

The weight system governs rarity and balance simultaneously. Weight 1 enchantments are rare and very powerful. Weight 4 enchantments are common and modest. Because positives and negatives roll separately, a player could theoretically land a weight 1 positive paired with a weight 1 negative — extremely powerful but extremely costly.

  

| Weight | Rarity / Power Level |
| :-: | :-: |
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

  

| Upgrade Level | +1 Stones Required (cumulative) |
| :-: | :-: |
| +1 | 1 |
| +2 | 3 |
| +3 | 7 |
| +4 | 15 |
| +5 | 31 |
| +6 | 63 |
| +7 | 127 |
| +8 | 255 |
| +9 | 511 |
| +10 | 1,023 |

  

## **Sockets and Gems**

Gear has sockets that accept gems. Gems provide stat bonuses and have the same rarity tiers as gear. Gems are upgraded by combining lower-tier gems. The total socket count across all equipment is 45.

  

| Slot | Sockets | Notes |
| :-: | :-: | :-: |
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

**The Forge cannot cost a player anything permanent.** Its worst outcome is that residue accumulated across worn equipment reaches a threshold at which the character is hunted by a corrupted copy of itself, and losing that fight ends the run. Ending a run costs the run, not the character, which is stated in full under Ending a Run in section II. That worst outcome is described under Worn Residue and Consumption below. It is warned before it can happen, and it is avoidable by managing residue.

  

## **Cataclysmic Residue (CR)**

Every modification made to an item adds Cataclysmic Residue (CR). This residue represents the instability and corruption inherent in forcefully modifying powerful gear. As CR increases, crafting becomes exponentially more expensive and time-consuming.

  

| CR Range | Effect |
| :-: | :-: |
| 0 - 99 | Cost penalty (gold multiplier increases). Time penalty minimal. |
| 100+ | Critical Time Penalty kicks in — crafting costs real in-game days. |

  

**The penalty is one global rule, not a property of any material.** Two formulas
apply to every craft, whatever material it uses:

  - **Gold cost multiplier: `(CR / 50) + 1`**
  - **Craft time penalty in days: `CR / 100`, rounded down**

  

The residue used is the item's own residue at the moment the craft begins, before
the craft's own CR is added.

  

| Item's current CR | Gold cost multiplier | Craft time penalty |
| :-: | :-: | :-: |
| 0 (new item) | 1x | +0 days |
| 50 | 2x | +0 days |
| 99 (maximum before the time penalty) | 3x | +0 days |
| **100 (the break point)** | 3x | **+1 day** |
| 200 | 5x | +2 days |
| 500 | 11x | +5 days |

  

Both formulas reproduce every row of that table. The 99 row is `(99/50)+1 = 2.98`,
shown rounded; every other row is exact. **Whether the multiplier is rounded for
display only or rounded in the calculation is not settled**, and it matters below
100 where the two readings differ — at CR 10 the multiplier is either 1.2 or 2.

  

## **What a Player Can Do at the Forge**

Eighteen operations. Each names the material it consumes, the residue it adds to
the item, and its base craft time in days before the CR time penalty above is
applied.

  

| Action | Material | CR added | Base days | Note |
| :-- | :-- | :-: | :-: | :-- |
| Reroll Affix Value | Corrupted Mote | 2 | 0 | Low-risk, high-frequency action |
| Unsocket Gem | Dismantling Dust | 3 | 1 | |
| Imprint Enchantment | Aetherial Shard | 5 | 2 | |
| Socket Gem | Jeweler's Setting Agent | 5 | 1 | |
| Deterministic Affix | Schematic Fragments | 5 per affix tier | 1 per affix tier, maximum 8 | Guaranteed affix |
| Change Socket Type | Focusing Lens | 10 | 2 | |
| Infuse Random Damage Type | Elemental Cinder | 10 | 1 | |
| Upgrade Affix Tier | Potency Crystal | 10 × current affix tier | Current affix tier | Raises an affix by one tier, to a maximum of 7 |
| Affix Shifting | Crystal of Instability | 15 | 1 | Rerolls the stat type, not its value. Cannot be used on enchantments |
| Add Socket | Shattered Core | 15 | 3 | |
| Change Damage Type | Prismatic Catalyst | 20 | 0 | |
| Upgrade Item Level | Upgrade Stone | 25 per tier | 1 per tier, maximum 10 | Consumes two upgrade stones |
| Perfect Roll Attempt | Primal Spark | 50 | 5 | Forces the maximum stat roll |
| Infuse Specific Damage Type | Sovereign Ingot | 50 | 5 | |
| Change Enchantment Negative | Regret Essence | 100 | 7 | |
| Accelerate Craft | Tainted Shard | 0 | −1 | Removes one day of the accumulated time penalty per shard |
| Reduce CR Gain | Chaos Stabilizer | −50% of the next craft's CR | 0 | Applies to one craft |
| CR Cleanse | Purified Essence | −50% of accumulated CR | 1 per 500 CR removed | The resource-heavy safety valve |

  

**The last three are the tools that manage residue rather than add it**, and they
are what makes the Consumption Threshold below avoidable. Accelerate Craft buys
back time, Chaos Stabilizer halves what the next craft costs in residue, and
Purified Essence halves what has already accumulated.

  

**Ordering the table by CR is deliberate.** The cheapest operations are the ones a
player repeats — rerolling a value costs 2 — and the ones that change what an item
fundamentally is cost an order of magnitude more. Changing an enchantment's
negative effect is the most expensive single action in the game at 100 CR, which
by itself takes a new item to the break point where days start being charged.

  

## **Crafting Materials**

Eighteen materials. Tier is the rarity band it drops in, not an item level.

  

| Material | Tier | Source | What it does |
| :-- | :-: | :-- | :-- |
| Corrupted Mote | 1 (Common) | | Affix reroll currency |
| Dismantling Dust | 1 (Common) | Dismantling items with sockets | Safe unsocketing |
| Schematic Fragments | 1 (Common) | Dismantling items | Deterministic crafting |
| Tainted Shard | 2 (Uncommon) | | Removes one day of crafting time |
| Upgrade Stone | 2 (Uncommon) | | Raises item level by one tier |
| Elemental Cinder | 2 (Uncommon) | | Adds a random damage type to a weapon |
| Potency Crystal | 2 (Uncommon) | | Raises an affix to the next tier, to a maximum of 7 |
| Aetherial Shard | 3 (Rare) | Dungeon bosses and elites | Imprinting catalyst |
| Chaos Stabilizer | 3 (Rare) | Dungeon bosses and elites | Halves the next craft's CR gain |
| Shattered Core | 3 (Rare) | | Adds a socket |
| Prismatic Catalyst | 3 (Rare) | | Changes a weapon's damage type |
| Purified Essence | 3 (Rare) | | Halves accumulated CR |
| Jeweler's Setting Agent | 3 (Rare) | | Sockets a gem |
| Crystal of Instability | 4 (Very Rare) | | Affix shifting |
| Focusing Lens | 4 (Very Rare) | | Changes a socket's type |
| Sovereign Ingot | 4 (Very Rare) | | Adds a specific damage type to a weapon |
| Primal Spark | 4 (Very Rare) | | Forces a maximum stat roll |
| Regret Essence | 5 (Extremely Rare) | | Changes an enchantment's negative effect |

  

**Seven of the eighteen have no stated source.** Where a material drops is not
designed yet for those, and the loot tables that would answer it do not exist.
That is issue #531, and it does not block the operations above.

  

## **Worn Residue and Consumption**

Cataclysmic Residue is a property of an item. **Worn Residue** is the sum of the residue on every item the character currently has equipped. It is a property of the character, it changes whenever equipment changes, and it is shown on the character sheet at all times.

  

Worn Residue grants nothing. It is not a resource and it does not make the character stronger. Residue is a cost throughout, and it becomes dangerous only if the player ignores the tools that manage it: Purified Essence, which halves accumulated residue, and the Residue Protocols node on the Empire tree, which ignores 5% of residue per point.

  

**The Consumption Threshold.** A single fixed number, to be tuned. If equipping an item would take Worn Residue to or past the threshold, the game states the resulting total and the consequence, and asks the player to confirm before the equip happens. There is no path across the threshold that does not show the warning first. Crossing it is always a decision the player made on purpose.

  

**What crossing it does.** The character is marked. On entering the next dungeon floor, a corrupted double of the character is placed in the dungeon and hunts the player: same class, same level, same equipment, same skills. It is the same enemy described under Corrupted Stalker in section VIII, aimed at the character it was copied from.

  

**This half needs no connection.** The double is built from the player's own character, on the player's own machine. Nothing about crossing the threshold, being hunted, winning or being consumed requires a network connection or the shared table. Only the Corrupted Stalker dungeon modifier, which draws a character somebody else had consumed, needs either.

  

**In a party.** Each marked player produces one double, and every double is present for the whole party. A party of four in which three players are marked enters the next dungeon against three doubles. A player who managed their residue properly still fights their team-mates' doubles.

  

This is deliberate. Party play scales enemy health and damage with the number of players in the session, so a double copied from an over-equipped character arrives scaled for the whole party. A player who ignores residue management is not only risking their own character; they are handing the party a party-scaled copy of their own build. The consequences of that sit with the player who caused it.

  

Whether anyone is actually consumed is decided by the party rule in section VIII: if at least one player leaves the dungeon alive, nobody is consumed. Leaving alive is a reprieve rather than a solution — residue is unchanged and the doubles return on the next dungeon. Only killing the double clears residue.

  - **If the player kills the double**, residue is set to zero on every equipped item. The character keeps its equipment and the run continues.
  - **If the double kills the player**, the character is consumed. The run ends, exactly as dying in the Last Stand ends a run.

  

**Consumed does not mean destroyed.** The character survives with its levels, its equipment, its class passive trees and its empire upgrade tree, and plays the next run at the same tier, exactly as any other run ending works. What "consumed" names is that a snapshot of the character is written to the shared library of corrupted characters described in section VIII, where it joins the pool that the Corrupted Stalker dungeon modifier draws from and becomes an enemy other players fight. The character is taken into that pool; it is not taken from the player.

  

**Empire progress is kept, in every mode, including Solo Self-Found.** For an ordinary character the lethality mode's shared tree is untouched, because it never belonged to the character. A Solo Self-Found character is the only owner of its tree, and it keeps it, because it is still there to own it. The Empire-Wide Upgrades section states the general rule that no empire tree is ever destroyed.

  

**Why the run ends rather than the character being replaced mid-run.** This is the alternative the design rejected, not a description of what happens. A run is played at a fixed tier. Replacing a tier 5 character with a fresh one would leave the player at a tier they cannot survive, which is a loss presented as a continuation. Ending the run states the same penalty honestly, and it reuses a rule the game already has rather than inventing a new category of death. No character is replaced, and none is destroyed.

  

**What being consumed actually costs.** The run, and nothing else — the same price as dying in the Last Stand. This is deliberately smaller than the Consumption Threshold warning makes it sound, and the warning is still worth showing, because losing a run at a high tier is the largest setback in the game that is not permanent: the empire, the cities and every day spent building them are gone, and the next run replays the same tier.

  

# **VIII. Dungeon System**

## **Dungeon Basics**

  - Procedurally generated layouts.
  - Each dungeon has: a time-to-clear estimate, a resolve timer, floor count, modifiers, and a dungeon score.
  - More floors = better rewards but more time cost.
  - Every dungeon has a boss on the final floor.
  - If a dungeon resolves undefeated, its listed consequence occurs (usually city damage or population loss).
  - If a city falls, it becomes a Dungeon City — a staging ground with more floors and multiple boss fights, triggering a Surge.
  - Dungeon Cities can be retaken. Floor count equals the number of dungeons that were in the city when it fell (minimum 20/40/60 for Outpost/Bulwark/Sanctuary).
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

| Type | Description |
| :-: | :-: |
| Basic | Standard dungeon. Most common type. |
| Quest | Does not resolve — refreshes and may move to adjacent city. Required to challenge the Cataclysm. |
| Fallen City | Captured player city. Must be retaken to restore it. Higher floor count and multiple bosses. |
| Cataclysm | The final boss dungeon for the current run tier. Grows with every dungeon the player fails to clear. |

  

## **Dungeon Sub-Types**

| Sub-Type | Description |
| :-: | :-: |
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

  

## **Corrupted Stalker (Dungeon Modifier)**

While this modifier is active, one corrupted former player character is placed in the dungeon. It hunts the player across floors rather than waiting to be found.

  

**Where they come from.** Every character consumed by Worn Residue, in any player's game, is snapshotted into a shared table of corrupted characters. A dungeon carrying this modifier draws one entry from that table at random. The snapshot holds class, level, passive tree allocation, equipped items with their rolled affixes, and skill setup — enough to rebuild the character as an enemy that fights with player skills rather than a monster ability list.

  

**Scaling.** The drawn character is rebuilt at the tier of the dungeon it appears in, not the tier it was consumed at. The target is the peak of player power for that tier: level, item level, affix tiers and residue are all set so that the rebuilt character's Power Score matches what a character at the top of that tier would have.

  

**This works in both directions.** A character consumed at tier 8 and met at tier 1 is brought down to peak tier 1. A character consumed at tier 3 and met at tier 7 is brought up to peak tier 7. The downward direction stops a player getting a high-tier character consumed on purpose and then farming its equipment at a tier where the fight is trivial, which matters more than it looks: being consumed costs the run and not the character, so deliberately feeding a character into this table is cheap. The upward direction is the more common case, and it is what stops a tier 1 player being handed a peak tier 8 enemy.

  

**Drops.** On death it drops its equipped items, at the tier it was rebuilt to rather than the tier it was consumed at. This follows the Rogue Exiles in Path of Exile, which drop one item from every equipment slot and are one of that game's most reliably interesting random encounters.

  

**The snapshot supplies the shape of the equipment and none of its numbers.** Which affixes sit on which base item comes from the consumed character, as do its class, level, passive allocation and skill setup. Every affix value is generated at the encountering player's tier from the game's own affix tables.

  

**That is forced by the scaling rule rather than added to it.** Equipment cannot be raised from a low tier to a high one by multiplying stored numbers, because affix tiers have defined value ranges, so raising an item means rolling new values inside the higher tier's range. Running the same generation downward costs nothing and buys something else: an entry in the shared table can never carry a number the game would not itself have produced, so the table is not a route by which an item with impossible values reaches another player's game. Matching a target Power Score does not achieve that on its own, because a target can be met with one extreme value offset by weak ones elsewhere, and a single extreme value is worth having by itself once the auction house exists.

  

**Network.** The game ships an offline mode. It is a commitment rather than a possibility, and it covers the whole game rather than parts of it. This modifier reads the shared table, which offline play has no access to, so an offline character never contributes a snapshot to that table and never draws one from it.

  

**Offline, the slot is filled from the authored pool rather than left empty.** The dungeon still carries the modifier and the encounter still happens; only the source of the character changes. The authored entries described under Seeding below exist for the launch case and serve this one as well. Nioh, Dragon's Dogma 2 and Deathloop all substitute an authored or game-controlled stand-in for missing player data rather than removing the content, and this follows them.

  

The over-corruption mechanic in section VII works offline in full, because the double a player fights at their own threshold is built locally from their own character.

  

**Seeding.** The shared table is empty until the first character anywhere is consumed. It ships with authored entries so the modifier is not blank at launch.

  

**Weight.** Not set. The Dungeon Modifiers sheet in `docs/All_Things_Cataclysm.xlsx` holds 116 modifiers, with weights of 5, 10, 15 and 20, and this modifier is not one of them. It also has no Cataclysm Type, where every one of the 116 belongs to exactly one of the eight. Issue #504 covers adding the row and setting both values.

  

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

A major aspect of Cataclysm is the management of the player's empire. The empire consists of 12 Outposts, 8 Bulwarks, 4 Sanctuaries, and the Pillar at its centre. Players must fight off dungeons to prevent city loss while working toward the Cataclysm boss dungeon. This requires strategic city upgrade decisions, empire tree investment, and time management to ensure the capital isn't overrun.

  

## **City Tiers**

**The empire is a diamond lattice of 25 cities, four rings deep, with the Pillar
at the centre.** Ring N holds exactly 4N cells, so the counts below are a property
of the geometry rather than a separate decision.

| Ring | Tier | Count | Minimum floors if captured |
| :-: | :-- | :-: | :-: |
| 0 | **Pillar** | 1 | losing this ends the run |
| 1 | **Sanctuary** | 4 | 60 |
| 2 | **Bulwark** | 8 | 40 |
| 3 | **Outpost** | 12 | 20 |

  

**"City" is the general word for a place on the map at any tier. It is not a tier
name.** A Bulwark is a city and so is an Outpost.

  

**"The capital" is the hub inside the Pillar, not another name for it.** The
Pillar is the city at ring 0, with defence and population like any other city.
The capital is the settlement the player walks around between runs, where the
NPC services in section IX are. Losing the Pillar ends the run and takes the
capital with it.

  

**Distance is counted in rings.** Adjacency is orthogonal in lattice space, and
every step changes the ring by exactly one, so a city's ring is also its distance
from the Pillar. "Within 2 rings of the Pillar" means the four Sanctuaries and
the eight Bulwarks — twelve cities.

  

## **City Upgrades**

Each city has upgrade slots (3 normally, 2 on Heretic difficulty). Upgrades affect defenses, population, dungeon timers, and other city-specific stats. Strategic allocation of upgrade resources is one of the primary meta-decisions of each run.

  

## **Empire-Wide Upgrades**

Players accumulate empire upgrade points by defeating dungeons. These points are spent on a permanent empire-wide upgrade tree that persists through all runs — including failed ones. Empire upgrades provide lasting bonuses to city defenses, population, dungeon floor counts, and more. This is the primary meta-progression system.

**The empire upgrade tree belongs to the account, not to the character.** Every
character on the account shares one tree with every other character in the same
lethality mode. Making another character in a mode already being played costs
that character's levels and gear and nothing else, and the meta-progression
carries over. That is what makes trying a different class cheap enough to be
worth doing — and because the lethality mode and the Solo Self-Found flag are
both locked at character creation, making a new character is the only way to try
either.

**The sharing is scoped to the lethality mode.** Standard characters share one
empire upgrade tree, Hardcore characters share a second, and Heretic characters
share a third. Progress made in one never appears in another, so **a player
moving to a harder mode starts that mode's tree from nothing.** That is the
point: the tree cannot be run up on Standard and then carried into Heretic as a
head start. Trying a different class is cheap; trying a harder mode is not, and
it is not meant to be.

**The points are scoped as well as the tree they are spent on.** An empire
upgrade point earned by a character is earned into that character's lethality
mode, and can only be spent there. There is no single account balance of points
that three trees draw from; there are three balances. Without that, a player
could farm points on Standard and spend them into Heretic, which is exactly the
thing the scope exists to prevent.

**A Solo Self-Found character is the exception. It has its own empire tree,
shared with nothing** — not with the other characters in its lethality mode, and
not with other Solo Self-Found characters. Its points come only from its own
runs, and no other character can spend them or benefit from them. That follows
from what the flag means: a Solo Self-Found character already has no auction
house and no shared stash, and inheriting a mature account's empire tree would be
the largest handout of the three. A player running Solo Self-Found is starting
the whole game over, which is the point of the flag.

**A tree is never destroyed by anything that happens in play, in any mode,
including Solo Self-Found.** A failed run keeps it, a death keeps it, and being
consumed by Worn Residue keeps it: section II states under Ending a Run that a
run ending costs the run and not the character. That matters for Solo Self-Found
in a way it does not for the others, because a Solo Self-Found character is the
only owner of its tree, so it is the only case where the question can arise at
all.

**The one thing that destroys a tree is the player deleting the character that
owns it, and only under Solo Self-Found.** Deleting an ordinary character leaves
its lethality mode's tree untouched, because that tree belongs to the account and
never belonged to the character. **Deleting a Solo Self-Found character takes its
private tree with it**: there is no account holding it and no successor that
inherits it. **Every Solo Self-Found character starts its tree from nothing,
always**, whether an earlier one is still being played or was deleted. Section II
states the rule in full under Deleting a Character.

**Why the tree survives everything else.** Nothing that happens in play destroys
empire upgrade points. A failed run keeps them, a death keeps them, and the whole
meta-progression system is built on the promise that a run is never wasted.
Destroying the tree on a failed run would make failure the one thing in the game
that costs meta-progression, and under Solo Self-Found it would fall on the
harshest flag and on the tree that took longest to build. It would also make the
tree worth less the more of it a player has, which inverts the reason to invest
in it at all. Deletion is different in kind: the player chooses it, from outside
a run, knowing what is in the tree.

**So an account holds three shared trees per population, plus one for every Solo
Self-Found character.** A player who plays only online, or only offline, has
three; a player who plays both has six, because offline and online characters
share nothing with each other either. Each tree persists across every run and
every character that feeds it, exactly as before. The scope narrows which
characters share a tree; it does not shorten how long a tree lasts.

**The tree can be respecced, at a cost in days.** The Trainer at the capital
moves empire upgrade points as well as class passive skill points, and like every
other capital service it is paid for in days rather than gold. How many days is a
tuning value and is not fixed here. A respec moves an allocation inside one tree;
it is not a route between trees, because the points are scoped to the lethality
mode that earned them and moving where they sit does not change which mode that
was.

**The four tier-milestone capstones belong to the tree, not to the character.**
`Empire_Development_Tree_Final.json` holds four decision capstones — Foundations
of the Empire at 25 points, Edicts of Power at 50, The Imperial Vanguard at 100
and The Imperial Zenith at 200 — and each is one choice from three. They are
allocations in the tree and the tree belongs to the lethality mode, so **the
second character in a mode inherits all four choices already made** and never
faces the decisions. It can change them by respeccing, at the same cost in days
any other character pays.

**That is intended rather than an oversight.** A capstone chosen at 25 points
says what the empire is for, not what one character is for, so it belongs at the
same level as the rest of the tree. Inheriting it is the same benefit as
inheriting everything else in the tree, which is the whole reason trying a
different class is cheap. Respec is what stops it being permanent: the first
character in a mode fixes a starting position for the ones that follow, not a
life sentence. Without respec it would be a life sentence, which is why the two
questions were answered together.

**The partition is storage, not content.** There is one node graph,
`Empire_Development_Tree_Final.json`, and every tree allocates the same nodes at
the same values. Heretic does not get a tree tuned for Heretic.

**Nothing crosses the boundary, because no character can.** The lethality mode is
locked at character creation and never changes, in either direction, and dying
does not change it. So a character stays in one partition for its whole life:
there is no migration, no conversion on death, and no transfer.

**The boundary covers everything the account shares, not the tree alone.** The
shared stash and the auction house are partitioned by lethality mode exactly as
the tree is: three stashes and three markets, with nothing passing between them.
So gear and crafting materials cannot reach a Heretic character from a Standard
one, because there is no shared container both can open and no market both can
trade in. The rule generalises: **anything the account shares between characters
is held once per lethality mode, never once for the account.** Anything a
character holds by itself needs no rule, because a character never changes mode.

**And once per population as well.** Offline and online characters never share
any of it either, so the full key is the population together with the lethality
mode: up to six of everything the account shares, for a player who plays both.
That partition holds for the same reason this one does — the population is fixed
at character creation and never changes — and it exists for a different reason,
which section II states: an offline save is a local file that can be edited, and
a container both populations could open would carry an edited item into the
auction house and the ladder.

**Gold is one of the things the account shares**, so it is held once per
lethality mode like the rest. Three balances, and nothing moves between them; a
Standard character cannot fund a Heretic one. The Storage section states it in
full alongside the stash, because the two are the same rule applied twice.

That is what the genre does. Path of Exile, Diablo III, Diablo IV, Last Epoch and
Grim Dawn each partition the stash on the same axis as their meta-progression,
and none of them partitions the two on different axes. Diablo IV and Last Epoch
partition gold on that axis as well. `docs/DECISIONS.md` records the survey and
the cost in auction house liquidity.

  

## **Capital Services**

The capital houses all NPC services. All services cost time, reinforcing the time pressure.

| NPC | Service |
| :-: | :-: |
| Enchanter | Adds or modifies enchantments on gear |
| Smith (Cataclysmic Forge) | Crafting, upgrading gear, Forge interactions |
| Jeweler | Combines and sockets gems |
| Auction House | Buy and sell items. One market per lethality mode, and offline characters have none at all (disabled in SSF) |
| Trainer | Respec class passive skill points, and respec the empire upgrade tree. Both cost days. |
| Side Quests | Random missions for crafting materials, gear, and gold |
| Stash | Shared storage. One stash per lethality mode per population, offline and online never sharing one (disabled in SSF) |

  

## **Storage**

**There are two containers and they are different things.** A character's
inventory is carried, belongs to that character alone, and goes into a dungeon
with it. The stash sits at the capital, belongs to the account, and never enters
a dungeon. **A character has no private stash.** Everything a character stores is
either in its own inventory or in the shared one.

**The carried inventory is 48 slots, four rows of twelve, and nothing increases
it.** One item takes one slot whatever it is, the same rule the stash uses. No
empire upgrade node grants slots, no affix grants slots, and no city upgrade
grants slots. A character carries the same 48 on its first dungeon and its last.

**Why nothing increases it.** Diablo IV fixes its inventory at 33 slots and says
why: "To avoid interrupting gameplay with pockets of inventory management, we're
not planning to bring back different-sized items." Path of Exile and Last Epoch
also never grow the carried inventory. A scaling source would also weaken a
pressure this design created on purpose — a dungeon floor costs a day, so a
dungeon is a long way from anywhere to put things down, and how much can be
carried is part of how deep it is worth going.

**48 rather than Diablo IV's 33** because that game's dungeons are minutes long
with a free town portal at the end of them, and here a dungeon is many floors at
a day each. Diablo III used 60. The number is a tuning value; the rule is that it
does not change.

**The stash is shared between characters, once per lethality mode.** Standard
characters open one stash, Hardcore characters a second and Heretic characters a
third, with nothing passing between them. That is the general rule stated in the
Empire-Wide Upgrades section: anything the account shares between characters is
held once per lethality mode. A Solo Self-Found character has no stash at all,
which is what its table row says, so everything it owns is carried.

**Offline and online characters never open the same stash**, whatever their
lethality mode, so a player who plays both has up to six stashes rather than
three. This is the partition that protects the auction house: an offline save is
a local file that can be edited, and a stash both populations could reach would
carry an edited item into the market. Gold is partitioned the same way, for the
same reason.

**Size: 600 slots, as six tabs of 100.** Every tab is the same 10 by 10 grid, and
one item takes one slot whatever it is. The tabs can be renamed and colour-coded
and nothing else about them differs; they are one container divided for the
player's convenience, not six containers with rules of their own.

**The stash does not grow and there is nothing to buy.** All six tabs are open
from the first character in that lethality mode. No empire upgrade node grants
stash space, no gold price expands it, and the monetisation section already rules
out the third possibility: "no stash or storage fees of any kind". A player's
storage is the same on their first day in a mode as on their last.

**It holds items: gear, gems and crafting materials.** It does not hold gold,
because gold is a balance rather than an item and is not stored anywhere.

**Gold is held by the account, once per lethality mode**, exactly as the stash
is. Every Standard character spends from one balance, every Hardcore character
from a second and every Heretic character from a third, and nothing moves between
them. A Solo Self-Found character is the exception again: its gold is its own and
is shared with nothing, for the same reason its empire tree and its lack of a
stash are.

**Why the account rather than the character.** The Empire-Wide Upgrades section
already says that making another character in a mode already being played "costs
that character's levels and gear and nothing else". Gold held per character would
be a third thing lost, and that sentence would be false. It is also what the
general partition rule was written to cover: anything the account shares between
characters is held once per lethality mode, and gold is one of those things.

**The auction house lists from the stash, not from the carried inventory.** That
is why a Solo Self-Found character loses both together and why one market and one
stash exist per lethality mode: a market can only offer what the stash it draws
from can hold.

**Why fixed rather than earned or bought.** 600 slots is close to what Path of
Exile 2 gives for free, which is four tabs of 144. Diablo IV starts at 50 and
sells more for gold; Last Epoch sells up to 200 tabs for gold. Selling tabs for
gold is the common answer and was not taken here, because this design has no
other gold sink written down and pricing the first one in isolation would be a
guess with nothing to calibrate against. `docs/DECISIONS.md` records the sources
and the case for revisiting it. It rested on a second reason as well until issue
#306 removed it: gold is now settled as an account balance held once per
lethality mode, so a gold price would have a clear owner to charge.

**600 is a tuning value.** The rule is that storage is fixed, free and shared per
lethality mode; the number is an anchor to the nearest comparable game and should
move once there is real play to measure it against.

  

# **X. Enemy System**

## **Enemy Stat Blocks**

Enemy Score is a power rating. It says what an encounter is worth, not how much health the enemy has, how hard it hits, how often, or what it resists. Those come from two layers that own different things.

  

| Layer | What it sets |
| :-: | :-- |
| Rarity | Magnitude only: health, damage, armor, energy shield |
| Archetype | Everything else: attack interval, critical strike chance and multiplier, movement speed, evasion, energy shield as a fraction of health, resistances, and how big this kind of creature is relative to average |

  

**Rarity scales magnitude and nothing else.** A Legendary Imp is a bigger Imp. It does not start critting more often, resisting more, or moving differently, because none of those describe how large something is. Per step of rarity above Common, health is multiplied by 1.85, damage by 1.55 and armor by 1.35.

  

**Health grows faster than damage.** Across the six rarities a Cataclysm Boss ends up with roughly 23 times a Common enemy's health and hits about 6 times as hard. Growing both together would produce something both unkillable and instantly lethal, which is a wall rather than a fight.

  

### **How Long a Geared Character Survives**

Enemy damage is the one enemy figure fitted to the player rather than set on its own terms. It has to be: it only means something against what a character can survive, and a geared character's mitigation is four layers deep and they multiply.

  

A reference character at difficulty tier 8 — a level 100 Ravager spending every affix slot, half on staying alive and half on killing things — reaches these:

  

| Layer | What it removes |
| :-- | :-: |
| Armor, 7,299 points against the tier 8 curve | 53.3% |
| Resistance, at the cap | 70.0% |
| Block chance 28%, removing half a hit each time | 14.0% on average |
| Damage reduction | 15.9% |
| **All four together** | **89.9%** |

  

So a hit lands for about **a tenth** of itself. Any enemy damage figure chosen without reference to that is chosen against nothing, which is what happened before: an average Common enemy needed 176 hits to kill that character.

  

**What the enemies do to it now**

  

| Enemy at tier 8 | Hits to kill it | Seconds |
| :-- | :-: | :-: |
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

  

| Enemy | Resistance | Why |
| :-- | :-: | :-- |
| Imp | 0% | Swarm fodder should die to whatever the player has |
| Hellhound | 10% | A beast, relying on speed rather than soaking hits |
| Succubus | 10% | Little of its own; the energy shield is what keeps it alive |
| Brute | 15% | Thick hide on top of its armor, which is its main defence |
| Corrupted Sentinel | 20% | A construct rather than a living thing, and it cannot retreat |
| Abyssal Warden | 35% | The design describes this one, and only this one, as having high damage resistance |
| Gatekeeper | 30% | High, but its real threat is its phases |

  

**An enemy still has a damage type of its own**, which is its Cataclysm's. That is what decides which of the player's eight resistances applies when it hits them.

  

**Only enemy damage carries a damage type. Player damage does not, and that is deliberate.** A player has eight resistances because eight Cataclysms attack them, so an enemy's hit has to say which one applies. An enemy resists everything equally, so a player's hit has nothing to choose between and says nothing.

  

**An enemy does not have the eight resistances at all.** It has a single all-damage resistance instead, met by a hit of any type including an untyped one. The two are not interchangeable: one figure written into eight typed slots is met by nothing once player damage stops naming a type, because there is no slot to select.

  

| | How it resists | How its damage is typed |
| :-- | :-- | :-- |
| Enemy | one all-damage figure, and no typed resistances | as its Cataclysm's damage type |
| Player | eight typed figures, and no all-damage one | not typed at all |

  

**Enemy resistance is what the player's resistance penetration works on.** Against an Abyssal Warden at 35%, a player with no penetration lands 65% of a hit and one with 20 penetration lands 85%. Penetration beyond an enemy's resistance grants no bonus, so over-stacking it does not become a damage multiplier against the enemies that need it least.

  

**Those two percentages describe the resistance layer alone, and not what reaches the creature.** Resistance is one layer of several and a hit passes through all of them, multiplying. The Abyssal Warden also carries 5,954 armor at tier 8, which removes a further 48.2%, so 33.7% of an unpenetrated hit actually reaches it rather than 65%. Killing it in 30 swings needs 7,584 damage a swing, where counting resistance alone said 3,929. The full order is in the Damage Calculation section: evasion, block, armor, resistance, flat damage reduction, energy shield, health. Enemies have no block chance and no flat damage reduction today, so for an enemy the live layers are evasion, armor, resistance and the energy shield.

  

**Enemies carry no Penetration stat.** Overwhelm, in section IV, already reduces the player's mitigation in proportion to the Power Score gap, and rarity already raises Enemy Score. Giving enemies a penetration value as well would be the same mechanic written twice.

  

## **Attack Telegraphs**

Sections III and IV both say the player must read and dodge telegraphed enemy attacks. This section says what a telegraph is, how long it lasts, and which attacks get one.

  

### **A telegraph is a wind-up on a shape the game already has**

  

There is no separate telegraph shape vocabulary. A telegraphed attack is an ordinary attack in one of the shapes the skill system already uses, shown on the ground before it lands.

  

| Shape | Parameters it already carries | Marker |
| :-- | :-- | :-- |
| Strike | `Radius`, `Angle` | A filled circle at `Angle` 360, a cone otherwise |
| Projectile | `Range`, `Radius`, `Speed` | A line of width 2 × `Radius` running to `Range` |
| Aura | `Radius`, `Duration`, `Interval` | A ring at `Radius`, persisting for `Duration` |
| Movement | `Mode`, `Range`, `Radius` | The path the enemy will travel, of width 2 × `Radius` |

  

Those are the same `Shape` and `ShapeParams` columns that `game/Data/WeaponSkills.csv` already uses for player skills, in the same units: radii and ranges in metres. Reusing them means an enemy ability is authored the same way a player skill is, and that the marker can be drawn from the ability's own numbers rather than authored twice and allowed to disagree.

  

### **How long a wind-up lasts**

  

**Every telegraph is escaped by walking.** There is no dodge roll and no evade button. Walking is always available to every class, so designing every marker to be walked out of is what makes the promise "every class can clear every telegraph" true rather than aspirational.

  

A Movement skill — one of the six skill slots, on a cooldown of 5 seconds typically and 3 at the shortest — is a **recovery**, not the intended answer. A player who read the marker too late can still cross it, because the largest legal marker is smaller than the shortest Movement-shape skill range. It is never required.

  

**Markers differ in how much time they leave, not in which button answers them.** The rules are set out immediately below, and the ceiling that makes a large marker genuinely harder follows under the heading after that.

  

A walk-out wind-up has to last long enough for the slowest class to notice the marker and walk clear of it:

  

    Wind-up seconds = 0.4 + Radius ÷ 3.5

  

**0.4 seconds is the reaction allowance.** Simple visual reaction time is 200 to 250 milliseconds, and reaction to a new stimulus appearing on a screen is measured at 300 to 500 milliseconds. 0.4 sits in that band. It is not the fastest a person can react; it is what an ordinary player can do reliably while also doing something else.

  

**3.5 metres per second is the slowest class.** The Ritualist moves at 3.5, the Masochist at 4.0 and the Ravager at 4.6. Designing against the slowest means every class can clear every telegraph, and the faster classes clear it with margin rather than only just.

  

### **Which attacks get a telegraph**

  

**An attack is telegraphed when its wind-up fits inside half the enemy's attack interval.** Half, not all of it, because an enemy that spends its whole cycle winding up is never actually attacking, and a marker that is on the ground continuously stops being a warning.

  

Rearranged, that gives the largest area each enemy can telegraph, straight from the attack intervals in `sim/cataclysm_sim/enemy_stats.py`:

  

    Largest telegraphed radius = 3.5 × (attack interval ÷ 2 − 0.4)

  

| Enemy | Attack interval | Largest telegraphed radius | Telegraphed? |
| :-- | :-: | :-: | :-- |
| Imp | 0.9 s | 0.2 m | **No** |
| Hellhound | 1.1 s | 0.5 m | **No**, for its basic attack |
| Corrupted Sentinel | 2.0 s | 2.1 m | Yes |
| Abyssal Warden | 2.4 s | 2.8 m | Yes |
| Succubus | 2.6 s | 3.2 m | Yes |
| Brute | 1.2 s | 0.7 m | **No**, for its basic attack |
| Gatekeeper | 3.0 s | 3.9 m | Yes |

  

**A marker smaller than 1 metre is not a telegraph.** It is smaller than the creature standing in it, so there is nowhere to walk. The Imp at 0.2 m, the Hellhound at 0.5 m and the Brute at 0.7 m fall below that, which is what puts them in the No column rather than a judgement about what those creatures are like.

  

**The Brute joined that list on 2026-08-09, and it does still telegraph.** Its attack interval was shortened from 1.6 seconds to 1.2 by play testing, which took the largest marker its basic attack could draw from 1.4 metres down to 0.7. Its stomp is unaffected and still draws a 3.5 metre circle, because an ability on a cooldown is telegraphed against that cooldown rather than against the attack interval — the rule stated below. This column is about the basic attack alone.

  

**That result is the design the document already asserts, arrived at from the numbers rather than assumed.** Section X says of the Imp that "a single Common enemy is not the threat, a pack is". An Imp that could telegraph would be individually dangerous and would stop being swarm fodder. The rule produces that outcome without anybody choosing it per enemy.

  

**An ability on a cooldown is telegraphed against its own cooldown, not the attack interval.** The Hellhound's charge is a Movement-shape ability rather than its 1.1 second basic attack, so it is telegraphed even though its basic attack is not. Substitute the cooldown for the attack interval in the formula above.

  

### **The wind-up has a ceiling, and that is what makes radius mean difficulty**

  

There is **one** wind-up formula and one kind of telegraph. Every marker is escaped by walking, and every class can clear every marker. What separates a mini-boss's signature attack from a Brute's stomp is not a different rule; it is that the big one leaves the player almost no time to spare.

  

**The wind-up stops growing at 2 seconds.**

  

    Wind-up seconds = the lesser of (0.4 + Radius ÷ 3.5) and 2.0

  

**Why a ceiling is needed at all.** Without one the formula hands the player back exactly as much ground as a bigger radius takes away — that is the property stated above, that the escape margin is 2.3 metres at every radius. A bigger marker denied more ground and warned for longer and was no harder to escape. Above the radius where the wind-up reaches 2 seconds the warning stops growing while the ground to cross keeps growing, so **the margin falls by one metre for every metre of radius.** Radius finally means difficulty.

  

**A telegraph caps at the radius where the slowest class still has its full reaction allowance and nothing more.**

  

    Cap = 3.5 × (2.0 − 0.4) + the contact distance

  

For a creature with the standard 0.48 metre body that is **6.5 metres**. Above it the slowest class cannot both react and walk clear, which is the point at which an attack stops being a telegraph and becomes a damage event. So the promise that every class clears every telegraph with at least the stated reaction allowance is now a property of the rules, true at every legal radius, rather than something to be checked attack by attack.

  

| Radius | Wind-up | Slowest class's spare time |
| :-- | :-: | :-: |
| 3.50 m (the Brute's stomp) | 1.40 s | 0.657 s |
| 5.60 m | 2.00 s | 0.657 s |
| **6.50 m (the cap)** | **2.00 s** | **0.400 s** |

  

**The 2 second ceiling is a judgement.** Nothing derives it. It was already the longest telegraph in the game when it was adopted, so it changed no existing attack, and no shipped game in the genre publishes a telegraph duration to check it against. `docs/DECISIONS.md` records the reasoning and what was rejected.

  

**The cap stays under the shortest Movement-shape skill range.** The ten Movement-shape skills in `game/Data/WeaponSkills.csv` have ranges of 8, 9, 10 and 12 metres, so the shortest escape any build can have is **8 metres** — the Sword's charge and the Axe's leap. At 6.5 metres the cap clears that by 1.5, so a player who read the marker too late can still cross the whole area with a Movement skill from dead centre. That is a recovery, not the intended answer.

  

**A large telegraph looks different from a small one.** The player has to read the size at a glance, and radius alone is not readable in a fight. That is presentation, and it no longer implies a different rule.

  

**There used to be a second tier here**, for areas larger than the walk-out limit, escaped with a Movement skill instead of by walking, with its own wind-up of `0.8 + Radius ÷ 16`, its own 8 metre cap and a 5 second minimum cooldown. It was deleted on 2026-08-09 for two reasons. It was unreachable: the walk-out limit grows with the cooldown and its cap did not, so above a 5.36 second cooldown every legal radius was already a walk-out radius, and no designed attack was ever in it. And it would not have done its job: its escape margin was 13.7 metres at every radius against the walk-out tier's 2.3, so it was between identical and twice as forgiving — never harder.

  

### **What the player can do during a wind-up, and what the attack does**

  

- **The area is fixed when the wind-up starts.** It does not follow the player. An attack that tracks cannot be walked out of, which would make the wind-up decorative.
- **The player can do anything during it.** Attacking, casting and using a Movement skill are all allowed. A wind-up that stops the player acting is a stun with a warning, and stuns are governed separately.
- **Leaving the area avoids the attack completely.** Not reduced damage — nothing. Partial credit for a dodge means the correct play is to ignore telegraphs and stack mitigation, which is the behaviour this whole section exists to prevent.
- **The enemy is committed once the wind-up starts.** It does not cancel because the player left. Committing is what makes a telegraph a chance to punish the enemy rather than only a chance to survive it.
- **Interrupting the enemy cancels the attack.** This is what makes crowd control offensive rather than only defensive.

  

### **Twenty markers on screen at once**

  

This is the failure the genre is worst at. Diablo IV has shipped complaints that ground markers disappear into the visual noise of a dense fight, and that its own warnings are inconsistent about which attacks get the dramatic treatment.

  

**The rule above solves it rather than needing a separate fix.** Only enemies with an attack interval of 1.37 seconds or more can telegraph anything, and the two swarm enemies — the Imp at 0.9 and the Hellhound at 1.1 — are excluded by their own attack speed. A pack cannot fill the screen with markers, because the creatures that come in packs do not produce any. The markers that do appear come from the slow, heavy, individually dangerous enemies, which are also the ones that arrive in small numbers.

That 1.37 is not a chosen number; it falls out of the formula above. The smallest useful marker is 1 metre, so the smallest cycle that can carry one is `2 × (0.4 + 1 ÷ 3.5)`, which is 1.371 seconds. **This paragraph said 2 seconds until 2026-08-08 and that was wrong** — it excluded the Brute, whose interval moved to 1.6 after play testing, and the baseline enemy at 1.5. Both can telegraph. The conclusion about swarms was never affected, because the two enemies it names are below the real threshold as well as the stated one.

  

**Two markers of the same shape overlapping is one marker, drawn once.** This matters at the Gatekeeper, where phases can stack area attacks, and it is the case that produces unreadable overlap everywhere else.

  

## **Enemy Modifiers**

Enemy modifiers apply to an **individual enemy**, and are separate from the dungeon modifiers in section VIII, which apply to a whole dungeon.

  

An enemy carries **one modifier per rarity above Common**:

  

| Rarity | Enemy Modifiers |
| :-: | :-: |
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

| Enemy Rarity | Weight |
| :-: | :-: |
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

| Dungeon Type | Weight |
| :-: | :-: |
| Basic | 0.0 |
| Quest | 0.05 |
| Fallen City | 0.1 |
| Cataclysm | 0.2 |



### **Subtype Weights**

| Subtype | Weight |
| :-: | :-: |
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

| Dungeon Type | Base |
| :-: | :-: |
| Basic | 100 |
| Quest | 200 |
| Fallen City | 300 |
| Cataclysm | 400 |



Divided by 20 and multiplied by the floor ratio. This is the only place a dungeon type contributes flat points rather than a fraction of tier width.



### **Base Type Scores**

| Dungeon Type | Score |
| :-: | :-: |
| Basic | 30 |
| Quest | 60 |
| Fallen City | 90 |
| Cataclysm | 120 |



**Nothing reads these.** They are declared in the authoritative source and the current formula does not use them; the Floor Scaling Bases above took over the job. They are listed because the port still verifies them against the source, so they are part of the shipped data, and a reader finding them in the code deserves to be told they do nothing.



### **Player Maximum Power Scores**

The anchor every score is measured against: the maximum Power Score a player is expected to reach by the end of each difficulty tier.



| Tier | Maximum | Tier width |
| :-: | :-: | :-: |
| 1 | 385 | 385 |
| 2 | 883 | 498 |
| 3 | 1508 | 625 |
| 4 | 2225 | 717 |
| 5 | 3078 | 853 |
| 6 | 4057 | 979 |
| 7 | 5120 | 1063 |
| 8 | 6327 | 1207 |



**These are a design choice, not a derived result.** They began as a flat arithmetic progression 283 points wide per tier and were revised at least four times. **Tier width now climbs at every tier**, from 385 to 1,207, at a step between 1.09 and 1.29 times. It did not until 2026-08-05: tier 5 was 1,107 wide where the surrounding trend was about 790, and tier 6 was narrower than tier 5 at 915. Because tier width multiplies every weighted term in the Enemy Score formula, a tier 6 Boss gained less power over a tier 6 Common enemy than a tier 5 Boss gained over a tier 5 Common, and a player at their own tier's ceiling died measurably less often at tier 5 than at the tiers either side. Issue #7 has the measurements. They can be revisited again.



## **Vertical Slice Enemies (Demonic Cataclysm)**

The vertical slice will feature five to seven base enemy types from the Demonic Cataclysm as a proof of concept:

  

| Enemy | Role |
| :-: | :-: |
| The Imp | Fast, swarming melee. Weak individually, overwhelming in packs. |
| The Succubus | Ranged caster. Debuffs player and buffs nearby allies. Slow but powerful attacks. |
| The Hellhound | Aggressive charger that leaves fire trails. Trail can damage other enemies. |
| The Brute | Heavily armored slow melee. Stomp stun attack. Can be outmaneuvered. |
| The Corrupted Sentinel | Stationary ranged. Forces the player to stay mobile. |
| The Abyssal Warden (Mini-Boss) | Massive stone and lava demon. High damage resistance. |
| The Gatekeeper (Boss) | Multi-phase towering demon. Each phase introduces new mechanics. |

  

## **Vertical Slice Enemy Behaviour**

The table above says what each of the seven is. This says what each one does.

  

**An enemy ability is written the same way a player skill is**: a `Shape` and its parameters, the two columns `game/Data/WeaponSkills.csv` already carries, in the same units — radii and reaches in metres, durations in seconds. The seven shapes are listed in section V. Nothing about an enemy needs a second vocabulary, and the Attack Telegraphs subsection above already draws a ground marker from those same numbers.

  

**An enemy ability also declares a slot**, one of the seven in `game/Data/SkillSlots.csv`, for the same reason a player skill does: the slot says what kind of thing it is and what its cooldown band is. Only two of the seven behave specially on an enemy.

  

| Slot | What paces it |
| :-: | :-- |
| Basic | The archetype's attack interval, from `sim/cataclysm_sim/enemy_stats.py`. Every enemy has exactly one Basic ability and it carries no cooldown of its own. |
| Aura | Nothing. It is held on for as long as the creature is alive, which is the design document's own rule for an Aura with no `Duration`. |
| The other five | Their own cooldown, which is also what a telegraph on them is measured against. |

  

The machine-readable copy is `ABILITIES` in `sim/cataclysm_sim/enemy_abilities.py`. **Only the Abyssal Warden, the Brute, the Corrupted Sentinel, the Gatekeeper, the Hellhound, the Imp and the Succubus are designed** — which is all seven. The Gatekeeper was the last, in issue #354.

  

**The Abyssal Warden's description used to end "but vulnerable at legs and back", and it no longer does.** The project owner ruled positional weak points out on 2026-08-09, in these words: "we don't do positional weak points. That's too tedious in a diablo like arpg". Nothing in the project ever implemented damage that varies by where a creature is hit, so the clause described behaviour that did not exist and was not going to. What "high damage resistance" means instead is set out in that creature's subsection below.

  

### **The Imp**

  

**The Imp has one attack and nothing else, and that is the design rather than an omission.**

  

| Ability | Slot | Shape | Parameters | Runs on | Telegraphed |
| :-- | :-: | :-: | :-- | :-: | :-: |
| Rend | Basic | Strike | `Radius=1.32; Angle=90; MaxTargets=1` | its 0.9 s attack interval | No |

  

**It cannot telegraph, by the rule the section above already sets.** A 0.9 second attack interval allows a marker of 0.2 metres, which is smaller than the creature standing in it. That is not a choice made for the Imp; it falls out of the enemy being fast.

  

**A second ability is refused for two reasons.** The first is that whatever an Imp does is multiplied by the pack. Ten Imps with one extra ability between them is ten of that ability going off at once, and if it were large enough to telegraph it would be ten markers — which is precisely the failure the Twenty Markers subsection above exists to prevent, arriving from the one enemy that subsection clears.

  

The second is that a second ability cannot be quick. The smallest useful marker is 1 metre, and by the wind-up formula that needs a cycle of at least 1.4 seconds — longer than the Imp's whole attack interval. So any telegraphed ability an Imp had would be the slowest thing it does, and the creature's entire role is that one of them is not worth reacting to. An untelegraphed one is worse: an unavoidable extra hit landing from ten directions at once.

  

#### **Its reach is set by the pack, not by the creature**

  

**Bodies cannot overlap, so a swarm queues in rings around whatever it is attacking.** How many fit in a ring is arithmetic. A body of radius `r` standing `D` from the centre covers `2 × arcsin(r ÷ D)` of the circle, so a full ring holds:

  

    Ring capacity = pi ÷ arcsin(Body radius ÷ Distance)

  

Two body measurements feed it, and both are already in the project. The player's capsule radius is **0.42 m**, from `CapsuleRadius` in `game/Source/Cataclysm/Character/CataclysmPlayerCharacter.cpp`. The Imp's body radius is **0.30 m**, the same as the lesser imp minion's capsule in `game/Source/Cataclysm/AbilitySystem/CataclysmMinion.cpp`, because it is the same creature.

  

| Rank | Distance from the player | How many fit | Running total |
| :-: | :-: | :-: | :-: |
| First | 0.72 m | 7 | 7 |
| Second | 1.32 m | 13 | 20 |
| Third | 1.92 m | 20 | 40 |

  

**The Imp's reach is 1.32 metres, which is exactly the second rank.** It is set there because the document's own pack figures require it. Section X states that ten Imps kill a geared character in 4.9 seconds and twenty in 2.4. One rank is seven Imps, and seven take 6.9 seconds. A reach that let only the rank in contact swing would make both of the stated figures false, because the eighth Imp onward would be standing behind the first seven doing nothing.

  

Reach is measured centre to centre, which is how the engine measures it: `ACataclysmEnemyController::Think` compares the distance between the two actors' locations against `MeleeReachCm`.

  

#### **Twenty is the cap, and the geometry is the only thing enforcing it**

  

Two ranks are twenty Imps and the third is out of reach, so **twenty is the most that can ever hit a player at once** — the same twenty section X already uses as the lethal pack. A twenty-first Imp stands behind them and waits for one to die.

  

**There is no attack-token rule and there deliberately will not be one.** Limiting how many enemies may swing at once is the standard answer in melee action games: Doom (2016) makes each enemy request a token before attacking, and the Batman Arkham games allow two or three at a time. It is the wrong answer here. This document already commits to ten Imps killing a geared character in 4.9 seconds and twenty in 2.4, and a token limit of two or three would make both false and flatten the difference between a pack of ten and a pack of twenty to nothing. Physical crowding produces the same protection — a player is never surrounded by more than twenty — without capping the damage the design has already promised.

  

#### **A pack is ten**

  

Ten, because this document already names ten as the pack that kills a geared character in 4.9 seconds. It is three more than one full rank, which is what makes the second rank, and therefore the reach above, matter in an ordinary encounter rather than only in a swarm event.

  

**Imps surround; they do not funnel.** Each one takes the nearest free place in the innermost rank that has one. Nothing coordinates the pack and nothing needs to: filling the closest gap produces a ring because a ring is what is left once the closer places are taken.

  

#### **The player can always leave, and it costs the Movement slot**

  

An Imp moves at 6.5 metres per second. The three Demonic classes move at 3.5, 4.0 and 4.6. **Walking away from an Imp is never an escape**, so a pack that has closed cannot be un-closed on foot, and the Movement slot is what breaks it — the same slot the large telegraphs above are designed against.

  

**A Leap and a Blink clear a ring of bodies. A Charge does not.** A leap goes over and a blink is not travel at all, where a charge runs along the ground and meets whatever is in the way. That is a decision this section makes rather than something section V already said, and it is what stops "surrounded" meaning "dead regardless of what you built". Path of Exile 2's most repeated Early Access complaint is being body-blocked by a monster clump with no way out, and its dodge roll deliberately does not pass through monsters; two of this game's three Movement modes do.

  

**Bring area damage.** An Imp has 25% evasion and evasion avoids direct attacks only, so a single-target build fighting twenty of them misses a quarter of its swings while an area skill does not. That is the general rule in section X applied to the enemy it was written for.

  

#### **What the engine has to do**

  

The ring behaviour is not scripted. It is what crowd avoidance produces when twenty agents path to the same point and cannot overlap. Unreal's Detour Crowd is the one to use rather than the older RVO avoidance on the character movement component: Epic's own documentation describes Detour Crowd as the system for dense crowds and the one that stops agents being pushed off the navigation mesh, and running both at once causes the agents to jitter. The crowd agent radius is the body radius above.

  

### **The Succubus**

  

**The Succubus is the only enemy in the slice that makes the others stronger, and that is what has to be read off it.** It is a ranged caster with three abilities: a slow telegraphed bolt, a curse on the player, and an aura that buffs every allied enemy near it.

  

| Ability | Slot | Shape | Parameters | Runs on | Telegraphed |
| :-- | :-: | :-: | :-- | :-: | :-: |
| Soulfire | Basic | Projectile | `Range=8; Radius=3.15; Speed=1200` | its 2.6 s attack interval | Yes, 1.3 s wind-up |
| Wither the Living | Support | Debuff | `Range=8; MaxTargets=1; Duration=5; Effect=Withered Touch` | a 10 s cooldown | No |
| Dominion | Aura | Aura | `Radius=8; Effect=Commander` | held on while it lives | No |

  

#### **Its wind-up is exactly half its attack interval, which is the most the rule allows**

  

The Attack Telegraphs subsection above caps a telegraphed radius at `3.5 × (attack interval ÷ 2 − 0.4)`, and for a 2.6 second interval that is 3.15 metres — the figure its per-enemy table rounds to 3.2. **The Succubus uses all of it**, so its wind-up is `0.4 + 3.15 ÷ 3.5`, which is 1.3 seconds, which is exactly half of 2.6.

  

That is what "slow but powerful attacks" has to mean mechanically. Its damage share of 1.60 is the second highest in the slice, and a hit that large has to be visible for as long as the rule permits. It is the biggest marker any ordinary Demonic enemy produces except the Brute's 3.5 metres.

  

**A Projectile's marker is a line, not a circle**, of width 2 × `Radius` running out to `Range`, which the telegraph table above already specifies. So the player leaves it by stepping sideways, and the 3.15 metres is how far sideways. Its speed of 1200 is the slowest player projectile in `game/Data/WeaponSkills.csv`, Magma Quake's; a slow bolt is a readable one.

  

#### **The curse is chosen from the effect table, not invented**

  

**Wither the Living applies Withered Touch**, which is already in `game/Data/StatusEffects.csv`: it reduces the player's damage output and their mana and energy shield. There is no new effect here.

  

**An innate ability must not duplicate a modifier its own Cataclysm can roll.** Several names in `game/Data/StatusEffects.csv` are also enemy modifiers in `game/Data/EnemyModifiers.csv`, and an enemy carries one modifier per rarity above Common, drawn from its own Cataclysm's column and the Generic one. An ability that duplicated a modifier the same creature could roll would let it hold the effect twice with nothing saying what that means. Withered Touch is in neither the Demonic nor the Generic column, so the Succubus may have it innately.

  

**A modifier belonging to a different Cataclysm is not a clash**, because that enemy can never roll it. Two Demonic modifiers, Abyssal Aura and Infernal Brand, are also named in the effect table and are therefore closed to Demonic enemies as innate abilities. Nothing else is.

  

**Five seconds, because that is the duration the enemy-applied debuffs in that table state.** Battle Cry and Final Curse both say five, and they are the only enemy-applied entries that give a figure at all.

  

**Ten seconds of cooldown, so the player has as long without the curse as with it.** Ten is also the top of the Support slot's cooldown band in `game/Data/SkillSlots.csv`, which is the slot curses belong to.

  

#### **Three of the seven shapes have no ground marker at all**

  

The telegraph table above draws a marker for Strike, Projectile, Aura and Movement. It draws nothing for SelfBuff, Summon or Debuff, and that is not an omission: **there is no ground for a curse to be drawn on.** An ability in one of those three shapes is read off the caster's animation rather than off the floor, and the counter to it is the one the Attack Telegraphs subsection already names — interrupting the enemy cancels the attack. That is what makes crowd control the answer to a Succubus rather than footwork.

  

#### **The ally buff is an aura held on, not a cast**

  

**Dominion grants Commander** — "all nearby allies gain 20% increased stats", already in `game/Data/StatusEffects.csv` — to every allied enemy within 8 metres, for as long as the Succubus is alive. Commander is also a **War** enemy modifier, which a Demonic enemy can never roll, so the rule above is satisfied.

  

**Held on rather than cast, because killing it first is the correct play and only an aura makes that true.** A buff that is cast and then lasts a duration survives the caster, so killing the Succubus achieves nothing until the timer runs out. An aura ends the instant it dies. The Succubus is the only enemy in the slice that changes how the others fight, and the lesson it has to teach is target priority.

  

**Its radius is its own attack range.** The Succubus stands 8 metres from the player, so an ally fighting the player is at most 8 metres from the Succubus. A smaller radius would buff nothing at the moment it matters; a larger one would buff a fight the Succubus is not in.

  

#### **It holds its ground and does not kite**

  

The Succubus walks to 8 metres and stops. **Eight metres because that is the shortest Movement-shape skill range in `game/Data/WeaponSkills.csv`** — the Sword's charge and the Axe's leap — which is the same anchor the Attack Telegraphs subsection uses. A ranged enemy standing further out than the player's shortest gap-closer could not be answered by every build.

  

**It does not retreat when the player closes.** It moves at 3.5 metres per second, which matches the slowest Demonic class and loses to the other two, so kiting would produce a chase it cannot win and would waste the player's time rather than test anything. The enemy that punishes a player for standing still is the Corrupted Sentinel, which never moves at all; duplicating that on the Succubus would take the Sentinel's job.

  

#### **Its energy shield behaves exactly like the player's**

  

It is one of only two enemies in the slice with an energy shield, at 50% of its health. **Nothing about it is special**, and the five rules in section IV apply unchanged. Three of them decide how the fight goes:

  

- **Damage over time passes straight through the shield and holds it empty**, because taking damage over time restarts the three second refill wait. 42 of the 51 designed Demonic skills carry `Burn=1`, so a Demonic player already carries the answer to a Succubus without building for it.
- **Magic weapons strip 10% more of it per hit** than other weapon sub-types.
- **It refills three seconds after the last damage it took.** A Succubus left alone while the player clears the Imps around it gets its shield back, which is the second reason to kill it first.

  

### **The Hellhound**

  

**The Hellhound is the enemy that teaches a player to read a wind-up**, because its charge is the one attack in the slice that is both clearly telegraphed and severely punishing to stand in. It has two abilities: a bite it uses constantly, and a charge on a cooldown that leaves the ground on fire.

  

| Ability | Slot | Shape | Parameters | Runs on | Telegraphed |
| :-- | :-: | :-: | :-- | :-: | :-: |
| Maul | Basic | Strike | `Radius=0.9; Angle=90; MaxTargets=1; Burn=1` | its 1.1 s attack interval | No |
| Hellrush | Movement | Movement | `Mode=Charge; Range=10; Radius=1.5; Burn=1; GroundRadius=1.5; GroundDuration=4; GroundHitsAllies=1` | a 5 s cooldown | Yes, 0.83 s wind-up |

  

#### **Its bite reaches contact and no further, so only five fit around a player**

  

0.9 metres is 0.42 for the player's body plus the Hellhound's own 0.48. Put through the ring arithmetic in the Imp's subsection above, **one rank of Hellhounds is five**, against twenty Imps. A Hellhound is more than half again as wide as an Imp, and its threat is the charge rather than the mass.

  

The Attack Telegraphs subsection calls the Imp and the Hellhound "the two swarm enemies", and that is true in the narrow sense it means there: both are excluded from telegraphing their basic attacks by their own attack speed. It does not mean they arrive in comparable numbers.

  

#### **The charge**

  

**A 5 second cooldown**, which is the Movement slot's typical cooldown in `game/Data/SkillSlots.csv`. That is what puts the charge on the telegraph rule's cooldown clock rather than its 1.1 second attack interval — which the Attack Telegraphs subsection already names the Hellhound's charge as the example of.

  

**A corridor 1.5 metres to either side.** That is the narrowest radius any player Charge-mode skill uses, Flamedart's, so the marker is a lane to step out of rather than a wall to run from. Its wind-up is `0.4 + 1.5 ÷ 3.5`, which is 0.83 seconds.

  

**Ten metres long, and that length is what makes it a charge rather than running.** During a 0.83 second wind-up the Hellhound could simply walk 6.2 metres at its own 7.5 metres per second. A charge shorter than that would be strictly worse than not winding up at all. Ten is also the range three of the four player Charge-mode skills use.

  

**It cannot turn.** The lane is fixed when the wind-up starts, which is the general rule the Attack Telegraphs subsection already sets: the area does not follow the player, because an attack that tracks cannot be walked out of.

  

**The player leaves the lane when their centre leaves it.** That is how the engine measures every area — `UCataclysmTargeting` tests an actor's location against the shape — so the 1.5 metres in the wind-up formula is the whole distance to cover, not a distance plus a body.

  

**A miss punishes itself, and no separate recovery rule is needed.** The Hellhound is committed once the wind-up starts, so it runs the full ten metres whether or not anything is still there. It ends up ten metres past the player, facing away, and covering that ground again takes 1.33 seconds at its own speed before it has even turned. That is the window the telegraph buys.

  

#### **The fire trail**

  

**The trail is not a third ability.** It is the same three riders the player's Flamedart already carries — `Burn`, `GroundRadius` and `GroundDuration` — on the charge itself, plus one that is new.

  

| Question | Answer | Where it comes from |
| :-- | :-- | :-- |
| How wide | 1.5 m to either side | the charge corridor; the trail is the lane it burned |
| How long it lasts | 4 seconds | Flamedart's `GroundDuration`, the shortest of the player charges |
| How often it deals damage | once a second | `ACataclysmGroundZone::TickSeconds`, which is already 1.0 |
| How much | a quarter of one of its bites per tick | so that standing in it for the whole 4 seconds costs exactly one bite |

  

**That last figure has nowhere to live in data yet.** No rider states what burning ground deals per tick, and the eight player skills that leave some do not state it either, so it is prose here and issue #361 is where the field gets added.

  

**The trail is worth one bite, not a second damage source.** It exists to take ground away, not to kill. A player who walks through it once loses a fraction of a hit; a player who fights inside it for its whole life loses a hit they could have avoided by moving two metres.

  

**`GroundHitsAllies=1` is what makes it burn other demons.** Section V's rider list carries this and nothing else in the game sets it. Every enemy is on one side, so no enemy's attack can touch another; this rider is a property of the burning ground rather than a change of side, in the same way the Madness debuff is an attitude rather than a third team.

  

**It burns the Hellhound too.** That is the simple version of the rule rather than an exception bolted onto it, and it earns its place: a Hellhound whose return path crosses the lane it just laid takes its own fire, so a player who moves along the trail rather than across it is rewarded for reading the ground.

  

**The trail outlives the Hellhound.** Killing it does not put the fire out, in the same way a player's burning ground is not removed when the player who left it dies. Two cases follow from rules already stated:

  

- **Killed during the wind-up**: the charge is cancelled and there is no trail, because interrupting an enemy cancels the attack.
- **Killed during the charge**: it stops where it fell, and the lane it has already burned keeps burning for the rest of its 4 seconds.

  

### **The Brute**

  

**The Brute is the enemy the anti-stun-lock rule was written for**, and it is the first thing in the game that stuns the player. It has three abilities: a swing it uses constantly, a stomp that stuns, and a rock it tears out of the ground and throws at anything standing out of reach.

  

| Ability | Slot | Shape | Parameters | Runs on | Telegraphed |
| :-- | :-: | :-: | :-- | :-: | :-: |
| Slam | Basic | Strike | `Radius=0.9; Angle=90; MaxTargets=1` | its 1.2 s attack interval | No |
| Stomp | Heavy | Strike | `Radius=3.5; Angle=360; StunSeconds=1.5` | an 8 s cooldown | Yes, 1.4 s wind-up |
| Rip and Toss | Special | Projectile | `Range=10; Radius=2.1; Pierce=0; Arc=0.25` | a 12 s cooldown | Yes, 1.0 s wind-up |

  

#### **Three abilities, not five, because it is a basic mob**

  

The imported art carries animations for five: the two above, a rock throw, a second heavy smash, and two self-buffs. **The Brute takes three.** Decided by the project owner on 2026-08-07: a common enemy that opens with five abilities leaves nothing for rarity and modifiers to add, and those are where extra abilities belong. The heavy smash duplicates what the Stomp already means, and the two self-buffs contradict a role built on being slow and outmanoeuvrable.

  

#### **Rip and Toss answers standing still out of reach**

  

Its purpose is the one thing the other two cannot do. A Brute with only a Slam and a Stomp is answered by walking backwards; this is what makes that cost something.

  

**Range 10 metres is its notice radius**, so there is no distance at which the Brute is aware of the player and can do nothing.

  

**It will not throw at anything closer than 2.58 metres**, which is the marked circle's 2.1 metre radius plus the creature's own 0.48 metre body radius. That figure is derived rather than picked: below it the circle the throw marks covers the ground the Brute is standing on, which makes the throw a melee attack wearing a thrown attack's telegraph. Until 2026-08-09 the minimum was the creature's 0.9 metre melee reach, which is exactly the distance at which the two bodies touch, so it refused nothing and the Brute threw rocks at point blank.

  

**The minimum is checked when the ability is chosen, not when it lands.** The wind-up then runs for a second, during which a player walking at 4 metres per second can close four metres. That is deliberate: the rule for every telegraphed attack is that it arrives where it marked, so a player who walks inside the minimum range during the wind-up has dodged the throw rather than triggered a special case.

  

**Radius 2.1 metres is set by the animation.** A telegraph has to last long enough to play the attack's own wind-up, the way the Stomp's 1.4 seconds covers the 0.83 second ground smash start. The toss animation is 0.87 seconds, and `0.4 + Radius ÷ 3.5 ≥ 0.87` needs at least 1.65 metres; 2.1 gives exactly a 1 second wind-up and clears the animation by 0.13. It is also well under the Stomp's 3.5, so the two markers read as different sizes, and well under the 19.6 metres its own 12 second cooldown would allow.

  

**Arc 0.25 is a shape, not a speed.** A lobbed rock follows real projectile motion, so there is no one speed to state: it is slowest at the top of its arc and fastest as it lands. What is stated is how high it rises above the straight line to where it lands, as a fraction of the distance thrown, and gravity supplies the rest. The figure is not chosen — a projectile launched at 45 degrees, the angle that throws an object furthest, reaches an apex of one quarter of its range.

  

**The flight time follows from it.** A parabola sags `g × t² ÷ 8` below the straight line between its two ends, so an arc of `0.25 × range` is in the air for `√(8 × 0.25 × range ÷ 980)` seconds: 1.43 at the full 10 metres, 0.78 at 3 metres, 0.55 at 1.5. The player's window to move is the 1 second wind-up plus that flight, so it runs from about 1.6 seconds at close range to 2.4 at maximum range.

  

**It is bounded by the speed ceiling.** A thrown rock should not outrun the Succubus's Soulfire at 1200 centimetres per second, the slowest projectile any player skill uses. The rock is fastest as it lands, having fallen longest: at the full 10 metre throw it arrives at 1073, under the ceiling.

  

**This row has carried three answers, and the middle one is the instructive failure.** It was `Speed=600` until 2026-08-09. The rock had been moving at a constant speed *along its path*, which is not how anything thrown moves: the steeper the path, the less ground it covered per second, so it crossed the ground early and then sank slowly onto its marker. Real projectile motion holds the *horizontal* speed constant and lets only the vertical one change.

  

The first replacement was `Flight=1.4`, a fixed flight time, on the argument that a telegraphed attack should take the same readable moment at every range. It does, and the cost is too high: gravity and a fixed time together fix the whole *vertical* part of the trajectory independently of the distance, so the rock left the hand at 570 centimetres per second straight up and rose 166 centimetres above it whether it was travelling 2 metres or 10. Every short throw was a near-vertical mortar. A constant reaction window is worth less than a lob that reads as a throw at the ranges a melee creature actually fights at. See `docs/DECISIONS.md`.

  

**A 12 second cooldown means one throw per approach, with room to spare.** The Brute crosses its own 10 metre throwing range in 2 seconds at its 5 metre per second chase speed, so a cooldown under that would let it throw twice on the way in and it would read as a ranged enemy rather than a bruiser with a rock. The approach time is the floor; 12 seconds was settled by play on 2026-08-09, raised from 5, and makes the throw something that happens once as the creature comes in rather than a rhythm. It is longer than the stomp's 8 deliberately: the stomp answers somebody standing next to the Brute, which is the common case, so it comes round more often.

  

#### **Being in the telegraph table's Yes column does not mean everything an enemy does is telegraphed**

  

The Attack Telegraphs subsection lists the Brute's largest telegraphed radius as 3.5 metres and puts it in the Yes column. **That says how big a marker it could draw, not that all of its attacks draw one.** Its ordinary slam reaches 0.9 metres, which is under the one-metre floor that subsection sets, so the slam gets no marker at all. Only the stomp does.

  

The rule is worth stating plainly because it applies to every enemy still to be designed: **a marker under one metre is not drawn, whatever the enemy's attack interval allows.**

  

#### **The three anti-stun-lock rules apply with the player as the target**

  

Section VI states them for a target, and a player is a target. The Brute is where that becomes concrete, and each of the three does real work here.

  

| Rule | What it does to the Brute |
| :-- | :-- |
| A hit must take at least 10% of the target's maximum health to stun | Its slam cannot stun. See below. |
| A stunned target is immune for 5 seconds | Sets the FLOOR under the stomp's cooldown, not its slot |
| A boss cannot be stunned | Not engaged; the Brute is not a boss, and the player is not one either |

  

**Its ordinary slam lands at exactly 10% of the reference build's effective health, which is exactly the threshold.** An Elite Brute at difficulty tier 8 kills the reference character in 10 hits. A stun that sits precisely on its own threshold is a coin flip decided by rounding, so **the slam does not stun at all** and only the stomp does. That is the clearest evidence the threshold rule is doing work rather than being decoration: it lands on a real enemy, at the middle rarity, at the top tier.

  

**The stomp is a Heavy-slot ability at the Heavy slot's 250%, which lands at 25% of the same pool.** Two and a half times the threshold, so it stuns through any reasonable amount of extra mitigation rather than only against the reference build.

  

**Its cooldown is 8 seconds, and the 5 second immunity window is the floor under it rather than the figure itself.** The Heavy slot's cooldown band in `game/Data/SkillSlots.csv` is 1 to 4 seconds, and the whole band sits inside the window, so a Brute stomping on the Heavy cadence would spend most of its stomps on a player who cannot be stunned. **Any ability whose stated effect is a stun sits at least 5 seconds apart, whatever slot it is in**, and that is a slot-independent rule. The 8 seconds was settled by play on 2026-08-09, raised from the 5 it used to sit exactly on. Raising it is always legal, because a stomp arriving later than the window is refused by nothing. Going under the window is what is not.

  

**It stuns for 1.5 seconds, which is the longest any designed player skill grants** — Shield Bash's. The four skills that stun run 0.75, 0.75, 1.0 and 1.5. An enemy holding the player still for longer than the player's own best hold is the failure the whole anti-stun-lock section is written against, so that is the ceiling.

  

#### **The stomp is telegraphed against its own cooldown, and takes well under what that allows**

  

3.5 metres, so its wind-up is `0.4 + 3.5 ÷ 3.5` = 1.4 seconds.

  

**It runs on its 8 second cooldown, not on the attack interval**, which is the general rule stated above for any ability with a cooldown. That cooldown allows 12.6 metres and a 4 second wind-up, and the stomp takes far less than that deliberately. Taking the maximum is legal and it is wrong here: the design document says the Movement-skill-sized telegraph "is what makes a mini-boss or a boss feel different from a Brute". The Brute gets the walk-out kind, and a marker that large on a creature this slow is unmissable rather than dodgeable.

  

**This wording is a correction, made 2026-08-07.** It previously said the stomp took the largest marker its *attack interval* allowed, and that the 1.4 second wind-up being exactly half the 2.8 second interval was the point. Both facts were true and the reasoning was not: the stomp never ran on the attack interval. When that interval moved from 2.8 to 1.6 seconds, because 2.8 played as too slow to be a threat, the stomp did not move with it and nothing about it needed to. The same happened again on 2026-08-09 when the interval moved to 1.2.

  

**It is a ring, not a cone.** `Angle=360`, because a stomp is a shockwave at its feet and because a cone would make the answer to a Brute "stand behind it and ignore the marker" once its turn rate is halved.

  

**Walking out of it is comfortable and that is intended.** A player standing at the Brute's reach is 0.9 metres from its centre and needs to cover 2.6 metres. At the slowest class's 3.5 metres per second that takes 0.74 seconds, against the 1.0 second of walking the wind-up budgets after the reaction allowance.

  

#### **What "can be outmanoeuvred" means, in numbers**

  

Three things, and all three come from figures that already exist.

  

| | The Brute | What the player has |
| :-- | :-: | :-- |
| Movement speed | 2.5 m/s | 3.5, 4.0 and 4.6: every class outruns it by at least 40% |
| Turn rate | 180 degrees per second | circling at its reach is 223 degrees per second, in the slowest class |
| Commitment | committed once the stomp's wind-up starts | the general telegraph rule; the Brute does not cancel because you left |

  

**The turn rate is the new number and the ceiling on it is derived.** Every other enemy turns at 480 degrees per second, which is the `RotationRate` yaw `ACataclysmEnemyCharacter` gives them all. A player circling at the Brute's 0.9 metre reach sweeps 3.5 ÷ 0.9 radians per second even in the slowest Demonic class, which is 223 degrees per second. **Anything under that can be got behind by every build**, and 180 is the round figure inside it.

  

That ceiling is why the stomp is a ring. A slow-turning enemy whose only heavy attack was a cone would be answered once and never again.

  

### **The Corrupted Sentinel**

  

**The Corrupted Sentinel never moves, and everything about it follows from that.** It cannot close a gap, cannot retreat from one, and cannot re-position to see round an obstacle. It has two abilities: a bolt down a marked lane that it fires constantly, and a shell it lobs over cover on a cooldown.

  

| Ability | Slot | Shape | Parameters | Runs on | Telegraphed |
| :-- | :-: | :-: | :-- | :-: | :-: |
| Siege Bolt | Basic | Projectile | `Range=14; Radius=2.1; Pierce=0; Speed=1400` | its 2.0 s attack interval | Yes, 1.0 s wind-up |
| Brimstone Mortar | Special | Projectile | `Range=14; Radius=3.0; Pierce=0; Arc=0.25` | an 8 s cooldown | Yes, 1.26 s wind-up |

  

#### **Its reach is the longest in the game, because reach is the only tool it has**

  

**14 metres**, which is the longest range any player attack reaches: Emberbolt on the wand and Hellbrand on the greatsword both state `Range=14` in `game/Data/WeaponSkills.csv`, and no attack states more.

  

It gets all of it because it has nothing else. Its movement speed is **0.0 at every rarity**, so a reach shorter than the player's longest would mean any ranged build could stand one metre outside it and kill the creature for nothing. That is the same rule the Brute's rock throw already states from the other side: there is no distance at which an enemy is aware of the player and can do nothing.

  

**Its notice radius has to be at least 14 metres**, or the reach is unreachable. No enemy has a designed notice radius yet — the Brute's 1000 cm was set by playing — and that is a separate open question.

  

#### **Its wind-up is exactly half its attack interval, and its speed falls out of the same sum**

  

The wind-up rule caps a telegraphed radius at `3.5 × (attack interval ÷ 2 − 0.4)`, which for 2.0 seconds is **2.1 metres**. The Sentinel takes all of it, so its wind-up is `0.4 + 2.1 ÷ 3.5` = **1.0 second**, exactly half the interval. That is the same place the Succubus sits, and for a related reason: a creature whose role is forcing the player to move should mark as much ground as the rule permits.

  

**The bolt's speed is then decided rather than chosen.** It has to land before the next one is marked, or the creature has a shot in the air and a marker on the ground at the same time and neither reads as a warning. The wind-up takes 1.0 second of the 2.0 second cycle, so the flight has the other 1.0. Fourteen metres in one second is **1400 centimetres per second**, and 1400 is one of the ten speeds `game/Data/WeaponSkills.csv` uses.

  

**So its cycle is exactly two seconds with nothing idle in it**: one second of marker on the ground, one second of flight, and the next marker appearing as the shot lands. That is what "forces the player to stay mobile" means as a number rather than as an adjective.

  

**It is faster than the Succubus's 1200 and that is deliberate.** The 1200 figure is the ceiling on the Brute's thrown rock, which is a lob and is fastest as it lands. This is a flat bolt and is not bound by it. 1400 is still the second slowest of the ten speeds in the player skill table, and what makes this shot readable is the second of ground marker in front of it, not its speed.

  

#### **It does not lead the player, and cover works**

  

**The lane is fixed when the wind-up starts.** That is the general rule the Attack Telegraphs subsection already sets for every telegraph, and it is what makes the marker mean something: an attack that tracks cannot be walked out of. Leading a moving target is the standard way to make a projectile land, which is exactly why this one does not do it.

  

**Geometry blocks the bolt and the Sentinel does not fire without line of sight.** Path of Exile's projectiles travel until they hit an enemy or an obstacle, and Diablo IV treats an enemy shooting through a wall as a bug rather than a feature. Breaking line of sight is therefore real counterplay against this creature, and it is the counterplay a stationary enemy should have.

  

#### **Standing on top of it is not safe, and no minimum range is needed to say so**

  

**A Projectile's marker is a lane, not a circle** — the telegraph table above says so: a line of width 2 × `Radius` running out to `Range`. A melee character who walks up and stands against the Sentinel is standing in that lane. So they have to step out of it every two seconds like everybody else, and at contact range stepping out of a 4.2 metre wide lane means walking around the creature.

  

**That is its whole answer to melee and it needs no new mechanic.** The rule that an attack must not mark the ground its own caster stands on was written for the Brute's rock, which is *lobbed* and marks a circle the creature would be standing inside. A lane starting at the caster does not do that: the caster is at the lane's origin, which is where a shooter stands.

  

**The rest of that answer is in the stat block rather than in an ability.** 2.20 armour share, an energy shield worth 35% of its health, 20% resistance and a 1.30 health share. A creature that cannot retreat has to be able to take hits, and this one survives being stood on rather than preventing it.

  

#### **The mortar is the only answer a creature that cannot walk has to cover**

  

A Sentinel that only shoots in straight lines is answered by one pillar, and it cannot step around it. The mortar is what stops that being the end of the fight.

  

| Question | Answer | Where it comes from |
| :-- | :-- | :-- |
| How it flies | an arc of 0.25 of the distance thrown | a projectile launched at 45 degrees, the angle that throws furthest, reaches an apex of a quarter of its range |
| How long it is in the air | 1.69 s at full range | derived from the arc: `√(8 × Arc × range ÷ g)` |
| How fast it lands | 1171 cm/s | under the 1200 ceiling the Brute's rock is held to, and a lob is fastest as it lands |
| How near it will fire | 3.48 m | the marked circle's 3.0 m plus the creature's own 0.48 m body radius |
| How often | every 8 seconds | inside the Special slot's 3 to 10 second band, and exactly four bolts between one shell and the next |

  

**The minimum range applies to this one and not to the bolt**, because this is the case the rule was written for: below `marked radius + body radius` the creature is standing inside the circle it is about to hit, which makes the attack a melee attack wearing a thrown attack's telegraph.

  

**Its marker is 3.0 metres against the bolt's 2.1**, so the two read as different sizes at a glance. Its wind-up is `0.4 + 3.0 ÷ 3.5` = 1.26 seconds, well inside the 12.6 metres its 8 second cooldown would allow and nowhere near the 8 metre cap that would make it cost a Movement skill.

  

#### **What here is a judgement rather than a derivation**

  

Three things. Everything else above is read off `game/Data/WeaponSkills.csv`, `game/Data/SkillSlots.csv`, the wind-up formula in the Attack Telegraphs subsection, or the stat block in `sim/cataclysm_sim/enemy_stats.py`.

  

- **The mortar's 3.0 metre radius.** Bounded below by the bolt's 2.1, so the two markers differ, and above by the 8 metre cap and by what its cooldown allows. The value inside that window is chosen, and 3.0 is the second largest radius any player Projectile uses.
- **Two abilities rather than one or three.** The Imp's subsection above argues that one ability can be a complete design, and that argument could have been made here. It was not, because nothing except the mortar answers cover.
- **The 8 second cooldown.** Inside the slot's band, and it puts four bolts between shells, but the band is wide.

  

### **The Abyssal Warden**

  

**The Abyssal Warden is the hardest thing in the slice to hurt, and one of two melee enemies that cannot catch anybody — the other is the Gatekeeper, whose answer is a mortar where this one's is a charge.** Those two facts are its whole design. It has three abilities: a swing it uses constantly, a charge that closes the gap it cannot close on foot, and a ring at its feet that is the largest telegraph in the game.

  

The word melee is doing work there. The Succubus moves at 3.5 metres per second and cannot catch the fastest class either, and does not need to — it reaches 8 metres. Being unable to close only matters for a creature that has to.

  

| Ability | Slot | Shape | Parameters | Runs on | Telegraphed |
| :-- | :-: | :-: | :-- | :-: | :-: |
| Sunder | Basic | Strike | `Radius=0.9; Angle=90; MaxTargets=1` | its 2.4 s attack interval | No |
| Stampede | Movement | Movement | `Mode=Charge; Range=8; Radius=1.5` | a 5 s cooldown | Yes, 0.83 s wind-up |
| Molten Roar | Ultimate | Strike | `Radius=6.5; Angle=360` | a 12 s cooldown | Yes, 2.0 s wind-up |

  

#### **What "high damage resistance" means, in numbers**

  

It is not one figure. The project owner settled that on 2026-08-09: "'High damage resistance' can be a combination of things. Enemies get layers of defense just like the player. So armor/resistances/damage reduction/etc".

  

The Abyssal Warden's layers today are the two highest in the vertical slice, and no others:

  

| Layer | The Warden | Next highest |
| :-- | :-: | :-- |
| Armour share | **3.50** | the Brute at 3.00 |
| Resistance | **35%** | the Gatekeeper at 30% |
| Evasion | 0% | — |
| Energy shield | 0% of health | — |

  

At Herald rarity on the last floor of a 50-floor Cataclysm dungeon that is 5,954 armour, worth 48.19% at tier 8, and 35% resistance: together **66.3% of a hit stopped**. The player's counter is the resistance penetration and armour penetration they already have.

  

**Two of the player's mitigation layers have no enemy equivalent at all** — block chance and flat damage reduction — and giving enemies them is a change to every enemy rather than to this one. It has its own issue.

  

#### **Its swing is not telegraphed, and the reason differs from the Brute's**

  

A 2.4 second attack interval allows a 2.80 metre marker, comfortably over the one metre floor. **So this creature can telegraph; its ordinary swing simply does not need to**, because a 0.9 metre reach is under that floor. The Brute fails both conditions and the Warden fails only the one about its own radius.

  

**Its interval is long enough for two authored swings, and it is the only one of the seven that is.** `PrimaryAttack_LA` and `PrimaryAttack_RA` are 1.1333 seconds each, measured 2026-08-09, and 2.2667 fits inside 2.4 with a tenth of a second to spare. That is presentation rather than a second ability.

  

#### **The charge is what stops it being walked away from**

  

**It moves at 2.8 metres per second and its chase speed is 0.0.** The three Demonic classes move at 3.5, 4.0 and 4.6. It is the only designed melee enemy that cannot catch anybody, so without a gap-closer a player walks backwards and it never fights. That is the rule the Brute's rock throw already states from the other side: there is no distance at which an enemy is aware of the player and can do nothing.

  

**Eight metres**, the shortest Movement-shape skill range in `game/Data/WeaponSkills.csv`. The shortest is the right one for the slowest creature, and it still passes the test the Hellhound's charge sets: during the 0.83 second wind-up this creature could walk 2.32 metres at its own speed, and eight metres is more than three times that.

  

**A 1.5 metre corridor**, the narrowest any player Charge-mode skill uses, so the marker is a lane to step out of rather than a wall. It leaves nothing behind — the burning lane belongs to the Hellhound, and this creature's job is to arrive.

  

**It travels at 11.43 metres per second, and that figure is a judgement rather than something this document derives.** No charge speed is stated anywhere here, and no shipped game in the genre publishes one — Path of Exile's monster charge publishes a 4 second cooldown and a 2.75 second cast time and no travel speed. The rule chosen instead is that **a charge covers its range in the length of its own animation clip**: eight metres in the 0.700 second `Stampede` clip. That is the rule every other timing in the project already follows, so the speed follows from two measured numbers rather than being picked.

  

Two things bound it from below. It has to beat the fastest class at 4.6 metres per second or the charge closes nothing, and 11.43 is two and a half times that. And it has to be far quicker than walking, or winding up is strictly worse than not — the same test the Hellhound's charge is judged by. At this creature's own 2.8 metres per second those eight metres would take 2.86 seconds, longer than its whole attack interval.

  

**A miss costs it 2.86 seconds and buys 0.70.** It is committed once the wind-up starts and runs the full eight metres whether or not anything is still there, so a player who steps out of the lane leaves it up to eight metres away facing the wrong way, and walking that back takes 2.86 seconds before it has even turned. That exchange is what the telegraph buys, and it is the same one the Hellhound's charge is written around.

  

**It hits everything it passes, once each, and does not stop on what it hits.** Hitting along the way is what separates a charge from a leap in the shape table above. Not stopping is what makes the overshoot real: a charge that halted on contact would arrive in melee range, which would be a reward for missing rather than a cost. It is stopped by the level, not by bodies.

  

**It refuses a target closer than 2.32 metres**, which is how far the creature walks during its own 0.83 second wind-up. Inside that it arrives sooner by taking a step, so charging would be strictly worse than not winding up — the rule stated for the Hellhound, applied to the minimum range rather than to the range.

  

**It repeats the Hellhound's Charge mode, and the art decided that.** A Leap was the first proposal, because a leap clears a ring of bodies where a charge meets it. Measuring the Grux pack settled it the other way: `Stampede` is a single 0.700 second clip that fits inside the 0.83 second wind-up at its authored speed, where a leap has to be stitched from five clips, which the current one-clip-at-a-time playback cannot do.

  

#### **Molten Roar is the largest telegraph in the game**

  

A ring 6.5 metres across at its feet, marked for 2 seconds, every 12 seconds. The Brute's stomp is 3.5 metres and the Succubus's bolt 3.15.

  

**It sits exactly at the cap, and that is the point of the number.** 6.5 metres is the largest marker the rules permit for a creature with a 0.48 metre body: the radius at which the slowest class still has its full 0.4 second reaction allowance and not a moment more. Nothing in the game may be larger, so this is the hardest telegraph the rules allow.

  

**It was 5.6 metres until 2026-08-09**, and it was raised because the project owner played it and reported that it was too easy to escape. Raising it only means anything because the wind-up now stops growing at 2 seconds. Before that ceiling existed a bigger ring warned for proportionally longer and was no harder at all. What the change buys, against the slowest class: the escape margin falls from 2.30 metres to 1.40, and the spare time from 0.657 seconds to 0.400 — a 39% cut in both. The warning stays at 2 seconds, so the 1.4 second roar animation still fits inside it unchanged.

  

**The geometry is now exhausted.** If it still reads as too easy, the answer cannot be more radius, because 6.5 is the ceiling. The next lever is what the attack leaves behind, and it currently leaves nothing.

  

**The 12 second cooldown is derived from how long the creature needs to kill.** A Herald Abyssal Warden kills the reference geared character in 5 hits, which at a 2.4 second interval is 12.0 seconds. A cooldown longer than that could come round zero times in a fight the player is losing. It is also the bottom of the Ultimate slot's 12-to-40 second band, and exactly five swings apart.

  

**It is the first thing in the game to use the Ultimate slot.** At that slot's 400% it lands at about four of this creature's ordinary hits, which is four fifths of what the reference geared character survives. That is the right weight for something that warns for two seconds and is avoided completely by walking out of it.

  

**It does not stun.** The Brute's stomp is the one thing in this slice that holds the player still, and a second one would spend most of its uses inside the 5 second stun immunity window.

  

#### **A bigger marker was not harder to escape, until the wind-up got a ceiling**

  

Below the ceiling the wind-up is `0.4 + Radius ÷ 3.5`, so the slowest class walks `1.4 + Radius` metres during it, while a player standing at contact has to cross `Radius − 0.9`. **The difference is 2.3 metres at every radius.** The Brute's stomp at 3.5 metres and a 5.6 metre ring give the player exactly the same margin. Under the ceiling, a marker's size says how much ground it denies and how long it warns for, and says nothing about how hard it is to avoid.

  

**Above the ceiling that stops being true, and that is the whole reason the ceiling exists.** The wind-up stops at 2 seconds, which it reaches at a radius of 5.6 metres. Past that the warning is fixed while the ground to cross keeps growing, so the margin falls one metre per metre. Molten Roar at 6.5 metres has a margin of 1.4 metres against the stomp's 2.3.

  

So the rule in one line: **up to 5.6 metres, size is ground denied; past it, size is difficulty.** The two ranges behave differently and the document should not be read as claiming otherwise.

  

#### **What here is a judgement rather than a derivation**

  

Two things. Everything else is read off `game/Data/WeaponSkills.csv`, `game/Data/SkillSlots.csv`, the wind-up formula above, the stat block in `sim/cataclysm_sim/enemy_stats.py`, or measured from the art on 2026-08-09.

  

- **Molten Roar's 6.5 metre radius.** No longer a judgement, since 2026-08-09: it is the cap, the largest marker the rules permit for a creature with a 0.48 metre body. It is bounded below by the 2.80 metres its own attack interval allows, so it is categorically different from what the creature does every 2.4 seconds. What is a judgement is the 2 second ceiling on the wind-up, from which the cap is derived.
- **Three abilities rather than two.** The Corrupted Sentinel has two. The third here is the charge, and without it the creature can be walked away from and never fought.

  

### **The Gatekeeper**

  

**The Gatekeeper is the boss, and its whole fight is the promise that two hits kill.** It kills the reference geared character in 2 hits and 6.0 seconds — the fastest of the seven — on the slowest attack interval in the slice, 3.0 seconds. So every attack it has is readable, including the ordinary one, and the fight is about never being hit rather than about trading.

  

| Ability | Phase | Slot | Shape | Parameters | Runs on | Telegraphed |
| :-- | :-: | :-: | :-: | :-- | :-: | :-: |
| Dread Cleave | 1 | Basic | Strike | `Radius=2.0; Angle=120` | its 3.0 s attack interval | Yes, 0.97 s wind-up |
| Soulfall | 1 | Special | Projectile | `Range=14; Radius=3.0; Pierce=0; Arc=0.25; Burn=1; GroundRadius=3.0; GroundDuration=10; GroundHitsAllies=1` | a 10 s cooldown | Yes, 1.26 s wind-up |
| Call the Damned | 2 | Special | Summon | `Range=4; Radius=2; Count=3; MaxActive=6` | a 10 s cooldown | No |
| Soul Harvest | 3 | Ultimate | Strike | `Radius=6.5; Angle=360` | a 20 s cooldown | Yes, 2.0 s wind-up |

  

#### **Three phases, on health, adding and never taking away**

  

**Phase 2 begins at 60% health and phase 3 at 30%.** The bands are 40, 30 and 30 percent of the bar: a long opening that teaches the base kit, then two additions. Each phase adds exactly one ability and removes nothing, so the last third of the fight is everything at once — which is what the Attack Telegraphs subsection already promises when it says phases can stack area attacks.

  

**The stat block does not change per phase, and that is the genre's answer as well as this design's.** Research across ten bosses in Path of Exile 1 and 2 and Last Epoch, recorded in `docs/DECISIONS.md`, found not one that gains damage, armour, attack speed or critical strike at a transition. Escalation is adding a named ability or using one more often. So a phase owns only which abilities are in the rotation, and the two-layer rule — rarity scales magnitude, archetype sets behaviour — is untouched by the boss having phases.

  

**A transition is brief and partial, never a stop.** At each threshold the creature channels its soul-siphon animation for about 2 seconds and takes 90% reduced damage while it plays. It stays killable and it stays interruptible by nothing — a boss cannot be stunned. Last Epoch shipped full invulnerability windows and removed them after player backlash; the lesson is recorded with the research. Health is not reset or refilled at any transition.

  

#### **Dread Cleave is the only telegraphed basic attack in the slice**

  

A hammer sweep across a 120 degree cone, 2 metres out, warned for 0.97 seconds, every 3.0 seconds. The other six enemies' ordinary attacks are contact swings under the 1 metre marker floor; this one is drawn, because a hit that takes half the player's survivable damage cannot arrive unannounced. Its 2.0 metre radius is a judgement bounded twice — above the 1 metre floor, below the 3.85 metres its own interval would allow — so the basic swing stays visibly smaller than anything on a cooldown. Unlike every other basic attack it has no target cap: everything standing in the cone is hit.

  

#### **Soulfall is how a boss that cannot walk anybody down fights at range**

  

The Gatekeeper moves at 3.0 metres per second against classes at 3.5, 4.0 and 4.6: like the Abyssal Warden it can never catch anybody on foot. The Warden's answer is a charge. Giving the boss the same answer would make it a bigger Warden, so its answer is the Corrupted Sentinel's instead: a lobbed mortar, `Range=14` and `Arc=0.25` exactly as the Sentinel's, bursting 3 metres wide where it lands, marked at the landing circle for 1.26 seconds.

  

**What it leaves behind is the arena changing.** The burst leaves burning ground the same 3 metres wide for 10 seconds — equal to its cooldown, so in steady state one patch is always on the floor and standing ground is lost one circle at a time. That is the persistence lever the genre research found real bosses use, and it is the Hellhound's existing riders (`Burn`, `GroundRadius`, `GroundDuration`) rather than a new mechanic. `GroundHitsAllies=1`: the Gatekeeper's own summons burn in it, and kiting them through the fire is intended counterplay.

  

#### **Call the Damned is the first use of the Summon shape**

  

From phase 2, it drives its hammer down and 3 Imps claw out of the ground within 4 metres, to a cap of 6 alive. The Imp is the slice's swarm enemy and arrives here unchanged — a boss is built from the vocabulary the other six establish, not from new creatures. A Summon draws no marker and is answered by killing the adds; the cap makes killing them worth it, because dead Imps are only replaced on the next cast, 10 seconds later.

  

#### **Soul Harvest is the Warden's ring at a boss's weight**

  

From phase 3, the ground erupts in a ring 6.5 metres across at its feet, marked for 2.0 seconds, every 20 seconds. The radius is the cap — the largest marker the rules permit, identical to Molten Roar and for the same reason: a boss finale should be the hardest legal telegraph, and the cap is what hardest-legal means. **At the Ultimate slot's 400% it kills from full health**: four of this creature's ordinary hits against a character who survives two. That is designed, not incidental — the genre's rule for a long-telegraph boss ultimate is that standing in it is death and the 2 second warning is the answer. The 20 second cooldown sits inside the Ultimate slot's 12-to-40 band, above the Warden's 12, because this creature kills in 6 seconds and a shorter cooldown would put a second ring inside almost every fight the player is already losing.

  

#### **What here is a judgement rather than a derivation**

  

- **The phase thresholds, 60% and 30%.** The shape — a long opening band, then additions — is the genre's, but the exact figures are a judgement: the genre's own numbers are community-derived, not developer-published, and vary per boss.
- **Dread Cleave's 2.0 metre radius**, bounded by the 1 metre floor and the 3.85 metre interval allowance.
- **Soul Harvest's 20 second cooldown**, inside the Ultimate band and above the Warden's 12 for the reason its subsection gives.
- **Three phases rather than two.** The research supports either; three is chosen because the design document promises "each phase introduces new mechanics", plural, and two phases introduce one.

Everything else is reused: Soulfall's figures are the Sentinel's mortar's, the ground riders are the Hellhound's, the summons are Imps, and the ring is the Warden's at a longer cooldown.

  

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

| Tier | Expected Power Score Range |
| :-: | :-: |
| T1 | 0 — 385 |
| T2 | 386 — 883 |
| T3 | 884 — 1,508 |
| T4 | 1,509 — 2,225 |
| T5 | 2,226 — 3,078 |
| T6 | 3,079 — 4,057 |
| T7 | 4,058 — 5,120 |
| T8 | 5,121 — 6,327 |

  

## **Roguelike Meta Progression**

Empire upgrade points are earned by defeating dungeons and persist through all runs including failures. The empire upgrade tree provides permanent bonuses to city defenses, dungeon parameters, and empire management. This system ensures every run — even a failed one — makes the next attempt slightly stronger, rewarding persistence without making early runs trivial.

**The tree persists across characters as well as across runs, within one
lethality mode.** It belongs to the account, so a new character inherits it —
but only from characters in the same mode. Standard, Hardcore and Heretic each
have their own tree, and a character starting in a mode the account has not
played before starts that tree from nothing. The one exception is a Solo
Self-Found character, which has its own tree and inherits nothing from the
account and nothing from another Solo Self-Found character either. Every Solo
Self-Found character starts its tree from nothing. The Empire-Wide Upgrades
section states the rule in full.

**So "no run is wasted" means no run within a mode is wasted.** Every run feeds
the tree of the mode it was played in, whether it was won or lost. What a run
never does is feed a different mode's tree. No run feeds nothing: nothing that
happens in play destroys the tree a run built, in any mode. The only thing that
destroys a tree is the player deleting the Solo Self-Found character that owns
it, which is their own act and not an outcome of play.

  

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

  

## **Visual Identity per Cataclysm**

Each of the eight Cataclysms has a colour and material theme. **The environment
follows the theme directly** — a Demonic dungeon is lava, fire and smoke — so the
art direction for a Cataclysm falls out of its theme rather than being designed
separately.

  

| Cataclysm | Theme | The environment is |
| :-- | :-- | :-- |
| Demonic | Red, fire, lava | Lava, fire, smoke |
| War | Grey, steel, pride | Steel, banners, fortification |
| Death | Black, shadows | Shadow, bone, cold |
| Famine | Brown, dying | Withered, cracked, drained |
| Void | Black and purple, nothingness | Absence, erasure, warped space |
| Celestial | Gold and white, holy | Light, gilding, ward |
| Pestilence | Putrid green and brown, rot | Rot, spores, wet decay |
| Chaos | Black and white, no hue at all | Unstable, streaking, never settling. See below |

  

### **The telegraph is one colour everywhere, and it is not the Cataclysm's**

**What makes an attack warning visible is its shape and its edge, not its
colour.** A telegraph is a hard-edged geometric shape — a circle, a cone, a line,
a ring — with a fill that sweeps as the wind-up runs out. Nothing in an
environment has that shape or that motion, which is what lets it be seen against
lava, against shadow, or against a crowd of twenty enemies.

  

**There is one telegraph colour for the whole game.** It does not vary by
Cataclysm, by damage type, or by enemy. The creature's own art and its
Cataclysm's environment already say what is attacking; the warning shape only has
to say *where* and *when*, and it has to do that everywhere.

  

**It is four concentric bands: three opaque rings around a see-through middle.**
Outermost first, with the widths in centimetres:

  

| Band | Width | Value | What it is for |
| :-- | :-: | :-- | :-- |
| Outer ring | 6 | **`#0A0F12`** | Near-black. Carries Celestial and Chaos, where everything else is brighter than the ground. |
| Bright ring | 8 | **`#FF3020`** | Red. Carries the dark environments, and is the colour the marker reads as. |
| Inner line | 4 | **`#FFD9CF`** | Warm near-white. Carries War's mid grey and Demonic's lava, where the other two are close to the ground's own brightness. |
| Fill | — | **`#FF3020` at 35% opacity** | Covers exactly the ground that hurts. Tints it rather than covering it. |

  

**Three rings rather than one, because no single colour survives every
environment.** Death and Void are built on black, Celestial is gold and white,
and War's steel sits in the middle where both a bright colour and a near-black
one are close to the ground. Which ring does the work changes with the ground,
and at least one is always in strong contrast.

  

Measured against the extreme of each of the eight themes, the weakest case is
**3.92:1** against Demonic lava, and every other environment is better. Three to
one is the accessibility threshold for a graphical object that is not text.

  

**The inner line is what makes the red work.** Without it the worst case is
2.47:1, which fails. The red ring and the near-black ring are both close to War's
mid grey, and adding a light band gives that environment something to contrast
against. It is also why the red is more readable than the cyan it replaced, whose
two-band worst case was 3.22:1.

  

**The fill carries none of the readability, and that is why it can be
see-through.** A translucent band's contrast against the ground beneath it falls
toward 1:1 as it fades — at 25% over War's steel grey it is 1.54:1 — so it could
never have been the thing the guarantee rested on. At 35% the marked ground is
tinted and the floor still reads through it.

  

**Red because it is the genre's colour for danger.** Shipped action role-playing
games use red or orange for an incoming attack, and a cool colour does not read
as threatening. The cost is real and is accepted: red is at its weakest against
Demonic lava, which is the one environment this project has any art direction
for, and the inner line is what keeps it above the threshold there.

  

**The exact value is a judgement rather than a derivation.** It was chosen by
looking at it in the sandbox, which is what the console variables under Live
Tuning exist for.

  

A telegraph is drawn with an **unlit, emissive** material, so its brightness is
fixed by its own material rather than by whatever light the room happens to have.
This is what makes the measured contrasts above hold in play rather than only on
paper.

  

### **The effect palette, which is not the environment palette**

The table above says what a **place** looks like. This one says what a **skill's
effect** looks like, and they are deliberately different.

  

**Every damage type gets two colours.** The primary is the hue the effect reads
as. The secondary is a dark anchor in the same family, and it is not decoration —
it is what keeps the effect legible when the primary matches the floor it is
standing on.

  

| Damage type | Primary | Secondary | Reads as |
| :-- | :-- | :-- | :-- |
| Demonic | `#FF7A2E` | `#3A0A02` | Fire and lava |
| Death | `#8FD8EC` | `#0C1418` | Icy blue, cold, bone |
| War | `#9AA7B4` | `#1C2229` | Gunmetal and steel |
| Famine | `#BCA95F` | `#221A0E` | Dying leaves, drained |
| Pestilence | `#B4E84A` | `#16200A` | Toxic, putrid, acid |
| Void | `#B978F5` | `#140A20` | Erasure |
| Celestial | `#FFE9A8` | `#3A2A08` | Light and gilding |
| Chaos | `#D6D6D6` | `#232323` | No hue at all |

  

### **The one rule that generates the rest**

**A world surface may not exceed 30% brightness. An effect's primary may not fall
below 60%.** Those two numbers are the whole readability guarantee, and they
replace checking eight colours against eight environments one pair at a time.

  

The arithmetic: to reach the 3:1 accessibility threshold for a graphical object
that is not text, against the brightest surface the world is allowed to have, a
colour has to reach **60.5%** brightness. Every primary above does. The darkest is
Void at 61.9%.

  

**This is why the environment cap is load-bearing rather than a style
preference.** If a floor is ever built brighter than 30%, it does not look
slightly wrong — it silently breaks the readability of every effect in the game,
and nothing would report it.

  

### **These are base values, not the finished look**

**A flat swatch of one of these colours is not what the game will look like, and
judging the palette from one is misleading.** The project owner said so directly
on 2026-08-13: "there's just something wrong with looking at a flat color and
trying to envision it as part of a video game. Needs a material and texture and
all that."

  

That is correct, and it is the same objection that produced the attack warning
marker's rework. What the table above fixes is the **starting constraint** — the
hue an effect reads as, and the guarantee that it stays visible. The finished
appearance comes from the material on top of it: how far the emissive pushes past
the base value, how the surface moves, how dense it is, and how fast.

  

`Niagara_Conventions.md` in this folder carries those as separate per-damage-type
values alongside the two colours, exactly so the colour is one axis of five rather
than the whole identity:

  

| Value | What it varies |
| :-- | :-- |
| `EmissiveMultiplier` | how far above the base value the effect glows |
| `SpawnRateScale` | denser for Pestilence, sparser for Void |
| `VelocityScale` | fast for War, slow-drifting for Famine |
| `MotionCurve` | the damage type's characteristic movement |

  

**So the palette cannot be signed off from a swatch and is not being asked to
be.** It becomes judgeable when the first effect exists in the sandbox, which is
step 4 of the build order in `Niagara_Conventions.md`. Expect to adjust these
values then; the two brightness numbers are what must not move.

  

### **Two of these are deliberately not what they were asked for**

**Void was asked for as "darker purple" and is not dark.** A dark effect on a
dark floor cannot be seen, and Void's own environment is the darkest of the eight.
Its darkness lives in its secondary and in its **form** — erasure, pulling inward,
a centre darker than the floor — rather than in its primary. This is the same
conclusion the attack warning marker reached: what reads as absence is a bright
rim around a dark centre, not a dark shape.

  

**War was asked for as "basic looking" and is gunmetal rather than white.** A
neutral white steel measured as confusable with Chaos, which is defined as having
no hue at all. Gunmetal keeps War reading as armour rather than as an element,
and separates the two. Blood red stays as War's accent for bleeding effects
specifically; it is not the damage type's colour.

  

### **Where hue cannot separate a pair, form does**

Three damage types are built on darkness and two share brown, so some pairs are
separated by shape and motion rather than by colour:

  

  - **Death settles downward.** Bone, frost, things falling still.
  - **Void pulls inward.** Erasure, geometry warping toward a point.
  - **Chaos never settles.** It is the only one with no hue, and it is the only
    one whose surface is always moving.
  - **Famine drains and drifts.** Slow, desaturating, falling apart.
  - **Pestilence spreads and clings.** Fast, dense, wet.

  

A shape and motion difference survives twenty enemies attacking at once. A subtle
hue difference does not.

  

### **What Chaos looks like**

Chaos is the hardest of the eight to state, because "black and white, random" is
a description of a feeling rather than of a surface. The buildable version:

  

**Light moves across the surface and the amount of light on screen does not
change.** Chaos is the only damage type with no hue at all, and that absence is
what identifies it — not brightness swinging up and down.

  

**The randomness is in the roll, not in the animation.** Each Chaos effect gets
one instability value when it is cast. A single instance looks stable for as long
as it exists; two instances look different from each other. That one value drives
how fast the surface moves, how far apart its lightest and darkest points are,
how far the geometry displaces, and how dense the streaking is. A low roll reads
calm and grey, a high roll reads violent, and **the player can see which they got
before the damage lands** — which is worth having, because section IV gives Chaos
the widest damage range in the game.

  

Risk of Rain 2's Void Fiend is the shipped precedent: one Corruption gauge from 0
to 100% swaps its abilities at the ends, and the character's shake amplitude
scales continuously with the gauge, so the number is readable as motion before it
matters mechanically.

  

**The streaking is made by moving textures against each other, not by switching
between states.** Two greyscale samples multiplied together at mismatched tiling
rates and mismatched scroll speeds, read through a gradient whose position is
driven by elapsed time. That is Destiny 2's method, described by one of its
senior visual effects artists, and its point is that nothing pops — the surface
is always moving and never steps.

  

### **Three rules that keep Chaos safe, and one that is not about flashing**

This document commits to an epilepsy-safe mode. Chaos is the theme that mode
exists for, and the rules below are what let it be built at all rather than
softened afterwards.

  

  - **Never a full black-to-white swap.** Run the light and dark ends of the
    ramp between roughly 8% and 85% brightness rather than 0% and 100%. A true
    black-to-white change is the largest possible, and about ten times the
    published general-flash threshold.
  - **Keep the alternation slow.** Any one point on the surface should cross
    from light to dark fewer than twice a second. The published limit is three
    general flashes per second.
  - **Displace geometry, do not spawn and remove it.** A particle appearing or
    disappearing is a step change in brightness. Moving vertices is not.

  

**The fourth rule is the one that is easy to miss, because it has nothing to do
with flashing.** Xbox's accessibility guidance fails alternating high-contrast
bands with **no flashing at all**, when the contrast difference is more than 10%
and the pattern covers about a fifth of the screen or more. **Black and white
streaking across a floor is exactly that pattern.** So Chaos streaks must be
irregular in spacing and direction and must never settle into parallel bands.

  

**The epilepsy-safe mode clamps Chaos rather than disabling it**: it caps the
instability value, reduces the distance between the ramp's ends, halves the
movement speed, and turns off geometry displacement. A Chaos dungeon still looks
like Chaos.

  

### **One thing this leaves open**

  - **How Death and Void environments are told apart**, given both are darkness,
    and how Famine and Pestilence are told apart, given both are brown. This is an
    environment and enemy question. It does not affect telegraphs.

  

## **Audio**

**Audio is part of the readability system, not decoration.** Two things in this
design make that literal rather than a general sentiment about immersion.

  

**The art direction fights the combat design.** The world is deliberately dark and
low-light, and the combat requires players to read and dodge telegraphed attacks.
An attack that is off-screen, behind the character, or lost in a crowd of twenty
Imps is not readable by sight. Audio is the channel that still works.

  

**Two of the three lethality modes hide the interface.** Hardcore shows the map
overlay only and Heretic hides the heads-up display entirely. A Heretic player has
no health bar, so **the low-health warning is the only channel telling them they
are about to die**. That is not an accessibility concession; it is the mode
working as designed, and it only works if the audio exists.

  

The reduced ability effect opacity option above has the same consequence. A player
who turns it on has deliberately weakened the visual channel and is relying on
this one.

  

### **The telegraph rule**

**An audio telegraph carries the same information as its visual telegraph, in the
same window, and never more.** It begins when the marker appears and ends when the
marker ends.

  

That follows from the wind-up cap in section X, which limits an enemy's wind-up to
half its attack interval. **The audio cue lives inside the same cap.** A cue that
fires late is worse than no cue, because it teaches the player to trust a signal
that does not give them time to act.

  

An audio telegraph never announces something the marker does not. If a player can
hear an attack coming that they could not have seen coming, the two channels
disagree and the visual one becomes the unreliable half.

  

**Positional, always.** A telegraph is placed in the world at its source, so
direction and distance are audible. This is what makes the off-screen case work,
and it is the reason telegraphs are not played as flat interface sounds.

  

### **Per-damage-type identity**

Each of the eight damage types has its own audio identity, matching the visual
identity in the style guide, so a player can name what hit them without seeing it.
The thematic anchors are the ones the Tags sheet already gives — Demonic is fire,
lava and rage; Void is abyss, gravity and erasure.

  

**The identity lives in timbre, not in pitch or volume.** Pitch and volume are
already spoken for: pitch carries enemy size and volume carries distance. A damage
type that announced itself by being louder would be indistinguishable from one
that is closer.

  

### **Combat audio, in priority order**

When several of these want to be heard at once, the earlier one wins. The order is
the design, not a mixing convenience.

  

| Priority | Cue | Why it is where it is |
| :-: | :-- | :-- |
| 1 | Enemy attack telegraph | The only one the player can still act on |
| 2 | Low health | In Heretic it is the only warning that exists |
| 3 | Damage taken | Confirms the thing that just went wrong |
| 4 | Hit confirmation | Confirms the thing that went right |
| 5 | Class resource full or spent | A build decision, not a survival one |
| 6 | Everything else | Ambience, footsteps, loot |

  

**A telegraph ducks everything below it.** In a fight against twenty Imps the
telegraph is the sound that must survive, and the crowd is what would otherwise
bury it.

  

### **Empire layer audio**

The empire runs on a day clock, and its events are the ones a player can miss by
looking at the wrong part of the screen. These interrupt rather than blend.

  

  - **Surge warning** — the strategy layer's equivalent of a telegraph. It has to
    arrive with enough time to act, on the same principle as a combat wind-up.
  - **A city falling** — a permanent loss inside the run. It is heard once and it
    is unmistakable.
  - **A dungeon resolving** — the timer the player has been spending days on
    completing.

  

### **Music**

Three states, following where the player is rather than what they are doing.

  

| Where | Direction |
| :-- | :-- |
| The capital | The only place that is not under threat. Sparse and low, but not dread — this is where the player plans |
| The empire map | Time passing and pressure building. Tied to the day clock, so it tightens as a surge approaches |
| A dungeon | Dread, and it gets worse with depth. One floor is one day, so descending and running out of time are the same movement |

  

**Music yields to combat audio.** It is the first thing ducked and the last thing
restored, because nothing in it is load-bearing.

  

### **Voice**

**Enemy vocalisations only. No dialogue, and no narration.** The design has no
dialogue system and no branching conversation anywhere, so there is nothing for
voice acting to attach to.

  

Enemy vocalisation is not flavour here: a creature's voice is part of its
telegraph, and it is the part that works when the creature is off-screen. The
seven Demonic enemies each need one, and the Paragon packs the vertical slice is
cast from ship them.

  

**This keeps localisation cheap.** Section XIII commits to multiple language
support, and a game with no spoken dialogue localises as text alone.

  

The implementation plan — middleware, naming conventions, mixing buses, and how a
telegraph cue is authored alongside its animation — is in `Audio_Design_Plan.md`
in this folder.

  

# **XIV. Monetization**

## **Base Game**

**Cataclysm is bought once and owned.** It is sold at Early Access for $25 to $30.
There is no free client, no subscription, and no trial.

  

**Buying the game buys all of it.** All 8 Cataclysms, all 24 classes, empire
management, city upgrades, the capital hub, all crafting systems and all weapon
types are included in the purchase. There are no paywalls on gameplay content, no
pay-to-win mechanics, and no stash or storage fees of any kind.

  

**Why bought rather than free.** A free client earns nothing until a cosmetics
shop exists, and a cosmetics shop earns nothing without a very large returning
audience. Path of Exile sustains that model at roughly 100,000 concurrent players
at a league launch, with thirteen years of league cadence behind it. This game has
no route to that audience before it has been played, and the free-to-play plan
would have built the most server-expensive configuration in the genre and taken
payment last. `DECISIONS.md` records the reasoning and its sources.

  

**Nothing about this reduces what a player gets.** The two things free-to-play
games normally sell alongside cosmetics — stash space and anything affecting
gameplay — were already ruled out above and stay ruled out.

  

## **Seasonal / League Updates (Free)**

**Seasons are free content patches that are added permanently to the game, and
they work offline.** A season is not a server-side ladder that the game needs a
connection to reach. An online ladder runs alongside it for online characters,
and it is optional.

  

That follows from the offline commitment in section VIII: offline play covers the
whole game rather than parts of it, so a season that existed only online would
make offline a second-class version of the game.

  

  - New temporary league mechanic resetting or changing the core loop for 3-4 months.
  - A few new powerful non-set unique items or generic enchantments.
  - Class/skill rebalances and QoL improvements.
  - Permanently added to the game when the season ends, rather than removed.

  

## **Cataclysm Expansions (Paid, $10-$20)**

Each expansion adds a permanent new Cataclysm to the game. The base game remains fully playable without expansions — expansions layer new content on top of what already exists rather than replacing or gating it.

  - New Cataclysm with unique world mechanic.
  - New enemy types, dungeon modifiers, and zones.
  - 3-4 new build-defining enchantment sets.
  - New permanent progression content tied to the expansion's theme.
  - Released every 6-12 months, timed with a new season launch.

## **Cosmetics**

**Cosmetics are a supplement to the purchase price, not the thing that funds the
game.** The game is paid for when it is bought and again at each expansion. A
cosmetics shop is optional revenue on top, and the game has to be viable without
it.

  

That is the practical difference the business model makes here: under the previous
free-to-play plan the shop was the only revenue outside expansions, which made it
a launch requirement. It is now a Phase 3 item that can stay in Phase 3.

  

Nothing in the cosmetics shop affects gameplay in any way.

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

| Risk | Mitigation |
| :-: | :-: |
| Skill matrix scope creep (11 weapons × 8 damage types × 6 slots = 528+ skills) | Launch with one complete damage type per patch. Use tag-based design to maximize re-use and make each skill feel distinct. |
| Time pressure mechanics frustrating casual players | The lethality mode (Standard, Hardcore or Heretic) lets players tune the urgency, and Standard is the default. Permanent meta-progression ensures no run feels wasted. |
| Passive tree complexity overwhelming new players | Strong visual design with clear branching and class fantasy. Beginner preset builds. In-game tooltips on all node interactions. |
| Enchantment system creating too much variance in loot quality | Weight system ensures common drops are consistently useful. Weight-1 enchantments are rare enough that they feel like jackpots, not baseline expectations. |
| Multiplayer balance (empire shared vs. individual) | Design empire as shared resource in co-op with individual character builds. Extensive playtesting during Early Access. |

  

# **XVII. Conclusion**

Cataclysm is designed to be a relentlessly engaging ARPG that respects player time and rewards deep knowledge of its systems. The fusion of dungeon crawling, empire management, and roguelike meta-progression creates a game that is immediately accessible at the surface but deeply strategic underneath.

  

The core promise is simple: every decision matters, every run teaches something, and every build feels distinct. The weapon-type-driven skill system, class passive trees, and tag-based enchantment system combine to create a build space large enough to sustain hundreds of hours of theorycrafting without ever feeling arbitrary.

  

The empire management layer ensures that even the time between dungeon runs is engaging and consequential, making Cataclysm more than just a dungeon crawler — it is a game about managing chaos under pressure, and the satisfaction of building something powerful enough to stand against the end of the world.

  