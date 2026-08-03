  

  
  

**CATACLYSM**

Game Design Document

Version 0.3

  
  

*An ARPG Dungeon Crawler with Empire Management and Roguelike Systems*

# **Table of Contents**

[**Table of Contents** **2**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**I. Executive Summary** **3**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**II. Game Concept** **3**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Genre and Setting 3](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Core Design Pillars 3](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**III. Story and Lore** **4**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**IV. Gameplay Mechanics** **4**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Character Creation and Customization 4](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Difficulty Options 4](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Controls and Key Bindings 5](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Core Gameplay Loop 5](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Game Start 5](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[The Surge 5](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Dungeon Runs 5](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Capital and Crafting 6](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Last Stand 6](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Combat System 6](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Real-Time Action 6](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Skill Slots 6](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Skill Acquisition 7](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Tactics and Strategy 7](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Passive Class Trees 7](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Overview 7](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Node Types 7](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Class Resource Systems 8](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Multiclassing 8](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Classes by Damage Type 8](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[War 8](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Demonic 8](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Death 9](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Pestilence 9](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Famine 9](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Celestial 10](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Chaos 10](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Void 10](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Character Stats and Attributes 11](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Power Score 11](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Attributes 11](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Resistances 11](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**V. Skill System** **12**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Skill Acquisition 12](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Weapon Types 12](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Damage Types and Skill Availability 12](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[War Skill Examples 12](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**VI. Itemization** **14**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Item Slots 14](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Weapon Sub-Types 14](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Item Rarities 14](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Enchantment System 15](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Overview 15](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[How Enchantments Roll 15](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Enchantment Tag Categories 15](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Set Enchantments 16](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Gear Leveling 16](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Sockets and Gems 16](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**VII. Crafting — The Cataclysmic Forge** **17**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Cataclysmic Residue (CR) 17](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Crafting Materials 18](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**VIII. Dungeon System** **18**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Dungeon Basics 18](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Dungeon Types 18](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Dungeon Sub-Types 18](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Dungeon Score Formula 19](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**IX. City Management and Empire Building** **19**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Overview 19](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[City Tiers 19](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[City Upgrades 19](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Empire-Wide Upgrades 20](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Capital Services 20](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**X. Enemy System** **20**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Enemy Score Formula 20](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Parameter Values 20](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Base Type Score 20](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Rarity Multipliers 21](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Dungeon Type Multipliers 21](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Vertical Slice Enemies (Demonic Cataclysm) 21](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**XI. Cataclysm Quest Mechanics** **22**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Hell on Earth (Demonic) 22](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Dead Rising (Death) 22](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[World War (War) 22](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Pestilence 22](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Famine 22](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Heaven's Wrath (Celestial) 23](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Chaos Lord (Chaos) 23](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[The Void 23](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**XII. Progression System** **23**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Character Leveling 23](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Power Score Ranges by Tier 23](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Roguelike Meta Progression 24](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**XIII. User Interface and User Experience** **24**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Key Screens 24](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[HUD Elements 24](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Accessibility 24](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**XIV. Monetization** **25**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Base Game 25](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Seasonal / League Updates (Free) 25](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Cataclysm Expansions (Paid, $10-$20) 25](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Cosmetics 25](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**XV. Development Roadmap** **25**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Phase 1 — Vertical Slice 25](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Phase 2 — Early Access Launch 25](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[Phase 3 — Full Release 26](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**XVI. Risks and Mitigations** **26**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

[**XVII. Conclusion** **26**](https://docs.google.com/document/d/1YMuQLR1e4C3q7aDB0bq49isKyEzbqV8mhJ3HdclA8TE/edit#heading=)

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

|  |  |
| :-: | :-: |
| \*\*Mode\*\* | \*\*Rules\*\* |
| Standard | Default experience. Dying costs 5 days. |
| SSF (Solo Self-Found) | No auction house, no shared stash. Increased loot drops. |
| Hardcore | Dying costs 10 days and each piece of equipment has a chance to drop on death. No HUD except map overlay. Increased loot drops. |
| Heretic | Surges spawn 25% more dungeons. Cities have only 2 upgrade slots instead of 3. Dying costs 15 days and drops at least 2 pieces of equipment. No HUD. Increased loot drops. |

  

## **Controls and Key Bindings**

The following are default controls. Players with multiple damage types can map multiple abilities of the same type to available slots.

  

|  |  |
| :-: | :-: |
| \*\*Input\*\* | \*\*Action\*\* |
| LMB | Player movement and basic attack |
| RMB | Heavy ability |
| Q | Special ability |
| W | Support ability |
| E | Aura ability (toggle) |
| R | Ultimate ability |
| Spacebar | Movement ability |
| WASD | Optional directional movement |

  

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

Power Score is calibrated against the tier ranges in section XII using the reference character below, which is what a player is expected to look like at the **end** of each difficulty tier. It is a calibration reference, not a requirement. Actual leveling is player-driven, because one player may clear a hundred dungeons in a tier where another clears forty.

  

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
| Luck | Magic Find / Loot Quantity | \\+0.01% rarity find / +1% loot quantity |

  

### **The Character Sheet**

A character has 33 stats, grouped the way the gameplay tag list groups its Stat tags.

  

|  |  |
| :-: | :-- |
| \*\*Group\*\* | \*\*Stats\*\* |
| Resource | Maximum Health, Maximum Mana, Maximum Energy Shield, Class Resource |
| Recovery | Health Regeneration, Mana Regeneration, Energy Shield Regeneration, Life Leech |
| Defence | Armor, Evasion, Block Chance, Damage Reduction, Retaliation, Crowd Control Resistance, and the eight Resistances |
| Offence | Critical Strike Chance, Critical Strike Multiplier, Attack Speed, Area of Effect, Damage over Time Frequency, Penetration, Spell Damage |
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

  

**Movement speed is measured in metres per second.** A tank sits at roughly 3. Agility scales that value, so a tank with points invested moves at 3 × (1 + increases).

  

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

A class supplies a level 1 base and a per-level gain for each stat it wants to scale. Across 24 classes and 33 stats that is 1,584 numbers, so every class starts from a shared default stat line and overrides only the stats that express its identity. A class may override any stat; the default is a starting point, not a floor.

  

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

  

Attribute points and every gear affix worded "increased" add together into one bucket per stat, and that bucket multiplies the base once. Only sources worded "more" or "less" multiply separately, and that wording is reserved for enchantments and keystones, where the design already wants outsized effects.

  

So 50 points of Vitality is +100% health, not 2.7 times health. Compounding 2% per point would give 7.2 times at 100 points, which leaves no room for gear inside the Power Score ranges in section XII.

  

**Regeneration percentages are increases to a base rate, not percentages of the maximum.**

  

Final Regeneration = Base Regeneration × (1 + Sum of Increases)

  

Read literally as 1% of maximum health per second, 50 points of Vitality would return half the character's health every second. The base regeneration rate is a small flat value per second, supplied the same way base health is. This applies to health, mana and energy shield regeneration alike.

  

**Cooldown reduction divides rather than subtracts.**

  

Final Cooldown = Base Cooldown / (1 + Sum of Increases)

  

The skill supplies the base cooldown and the character's accumulated increases apply on top of it. What the interface shows the player is the effective reduction, which is Increases / (1 + Increases), so a character shown as having 25% cooldown reduction turns a 4-second skill into a 3-second one.

  

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
| Resistances | 70% | Soft. Affixes may raise the cap itself. |
| Evasion | 60% | Soft. Gear enchantments may exceed it. |
| Crit chance | 100% | Hard. Above 100% it means nothing. |
| Block chance | none | No cap. A block is not a full avoid. |
| Cooldown reduction | none | No cap needed. The formula cannot reach zero. |

  

Over-capping resistance matters because enemy penetration reduces effective resistance, so the headroom is what keeps a character at the cap in practice. Over-capped resistance contributes no Power Score, as section IV states.

  

**Avoidance.** Evasion and block behave differently and are not interchangeable.

  

  - **Evasion avoids an attack completely, but applies only to direct attacks.** Area damage lands regardless of evasion. This is why evasion's cap can be soft: even at 100% evasion a character is not immune.
  - **Block reduces the damage of a blocked hit by 50%, it does not prevent it.** Block chance is the chance that reduction applies.
  - **Block applies to area damage as well as direct attacks.** A raised shield helps against an explosion in a way that dodging does not.
  - Because a block removes half the damage rather than all of it, block chance needs no cap. A character at 100% block chance has 50% damage reduction, which is strong but is not immunity.

  

### **Resistances**

There are eight resistances, one per damage type. Each caps at 70%. Resistance is reduced by enemy penetration scaling. Over-capping resistance is possible via certain affixes.

  

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
| War | Sword, 2H Sword, Dagger, Axe, 2H Axe, Spear, Fist, Shield, Crossbow, 2H Crossbow, 2H Warhammer, Whip |
| Demonic | Sword, 2H Sword, Dagger, Axe, 2H Axe, Fist, Whip, 2H Warhammer, Wand, Staff |
| Death | Sword, 2H Sword, Dagger, 2H Axe, Spear, Fist, Whip, Wand, Staff |
| Pestilence | Sword, Dagger, Spear, Fist, Whip, Crossbow, 2H Crossbow, Wand, Staff |
| Famine | Sword, Dagger, Axe, Fist, Whip, 2H Warhammer, Wand, Staff |
| Celestial | Sword, 2H Sword, Spear, Shield, Crossbow, 2H Warhammer, Wand, Staff |
| Chaos | All weapon types (chaos is unpredictable) |
| Void | 2H Sword, Dagger, Spear, Fist, Whip, 2H Warhammer, Wand, Staff |

  

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
| 2H Warhammer / Heavy | Earthquake | Slam into the ground, shockwave in 6m radius, knocks down enemies, reduces armor, leaves damaging fissure. |
| Dagger / Special | Proximity Mine | Place a concealed mine that arms in 0.5s and detonates on trigger for heavy damage in a 3m blast with bleed. |
| Spear / Special | Ballista | Deploy a ballista that fires at the furthest enemy in 15m every 2.5 seconds, dealing enormous damage and pinning targets. |
| Crossbow / Special | Bolt Turret | Fire a bolt into the ground that deploys a turret firing at nearby enemies every 1.5s with bleed on each hit. |
| Shield / Ultimate | Fortress | Become immovable for 5 seconds. 60% damage reduction. Reflect 100% of blocked damage. Allies within 6m take 30% less damage. |
| Whip / Ultimate | Whirlwind of Steel | Spin the whip for 4 seconds in a 7m zone. Every enemy in range struck repeatedly. Each hit applies a bleed stack. |
| 2H Axe / Ultimate | Annihilator | Channel spin for 3 seconds dealing rapid hits to all in melee range, stacking bleed and reducing armor each revolution. |
| All / Aura | Blood and Iron | Martial dominance aura in 10m. Enemies: -10% armor, -15% move speed. Allies: +8% physical damage. Drains mana. |

  

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
| Blunt | 10% more damage vs. armor |
| Magic | 10% more damage vs. shields |

  

## **Item Rarities**

|  |  |
| :-: | :-: |
| \*\*Rarity\*\* | \*\*Notes\*\* |
| Everyday | Common drops, basic affixes |
| Quality | Slightly improved base stats |
| Superb | Better affix rolls |
| Masterful | Strong affixes, good base |
| Legendary | Minimum 1 enchantment slot. Requires gear level 4+. |
| Mythic | Up to 2 enchantment slots. Requires gear level 6+. |
| Ascendant | Up to 3 enchantment slots. Requires gear level 8+. |
| Cataclysmic | Up to 4 enchantment slots. Requires gear level 10. |
| Sets | Legendary and above can be part of a named set with 2/6/10 piece bonuses. |

  

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

The Cataclysmic Forge is a high-stakes, deterministic crafting system built around the game's core theme of Time Management. Unlike systems where failure destroys items, the Forge's primary penalty is a strategic setback — crafting expensive items costs the player valuable days needed to manage the Empire and defend against the next Surge.

  

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

  

## **Dungeon Score Formula**

Dungeon Score = (Common Enemy Score × 0.6) + (Elite Enemy Score × 0.2) + (Rare Enemy Score × 0.15) + (Legendary Enemy Score × 0.04) + (Boss Enemy Score × 0.01)

  

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

## **Enemy Score Formula**

Enemy Score = (((Base Type Score + Floor Scaling Score) × 0.75 + Floor Scaling Score × 0.25) × Difficulty Multiplier + Modifier Score) × Rarity Multiplier × Dungeon Type Multiplier × Subtype Multiplier

  

## **Parameter Values**

### **Base Type Score**

|  |  |
| :-: | :-: |
| \*\*Dungeon Type\*\* | \*\*Score\*\* |
| Basic | 30 |
| Quest | 60 |
| Fallen City | 90 |
| Cataclysm | 120 |

  

### **Rarity Multipliers**

|  |  |
| :-: | :-: |
| \*\*Enemy Rarity\*\* | \*\*Multiplier\*\* |
| Common | 1.0 |
| Elite | 1.3 |
| Rare | 1.6 |
| Legendary | 2.0 |
| Boss | 2.5 |

  

### **Dungeon Type Multipliers**

|  |  |
| :-: | :-: |
| \*\*Dungeon Type\*\* | \*\*Multiplier\*\* |
| Basic | 1.0 |
| Quest | 1.2 |
| Fallen City | 1.3 |
| Cataclysm | 1.5 |

  

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
| Time pressure mechanics frustrating casual players | Difficulty modes (Casual/Standard/HC/Heretic) let players tune the urgency. Permanent meta-progression ensures no run feels wasted. |
| Passive tree complexity overwhelming new players | Strong visual design with clear branching and class fantasy. Beginner preset builds. In-game tooltips on all node interactions. |
| Enchantment system creating too much variance in loot quality | Weight system ensures common drops are consistently useful. Weight-1 enchantments are rare enough that they feel like jackpots, not baseline expectations. |
| Multiplayer balance (empire shared vs. individual) | Design empire as shared resource in co-op with individual character builds. Extensive playtesting during Early Access. |

  

# **XVII. Conclusion**

Cataclysm is designed to be a relentlessly engaging ARPG that respects player time and rewards deep knowledge of its systems. The fusion of dungeon crawling, empire management, and roguelike meta-progression creates a game that is immediately accessible at the surface but deeply strategic underneath.

  

The core promise is simple: every decision matters, every run teaches something, and every build feels distinct. The weapon-type-driven skill system, class passive trees, and tag-based enchantment system combine to create a build space large enough to sustain hundreds of hours of theorycrafting without ever feeling arbitrary.

  

The empire management layer ensures that even the time between dungeon runs is engaging and consequential, making Cataclysm more than just a dungeon crawler — it is a game about managing chaos under pressure, and the satisfaction of building something powerful enough to stand against the end of the world.

  