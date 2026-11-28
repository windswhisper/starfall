# Starfall Lab: Configuration Reference

Document language: English. Audience: mod authors, AI assistants, and maintainers.
Applies to: Starfall Lab 0.40, as implemented in this repository on 2026-11-28.
Runtime: a 32-bit DLL extension for Command & Conquer: Yuri's Revenge.

This is a reference for the implemented interfaces, not a proposal for future features. Field names in code font are literal configuration keys. `required` means that omission makes the described configuration invalid. Defaults apply when a field is omitted, subject to configuration inheritance. Native game fields such as `Spawns`, `Locomotor`, and `SuperWeapon` belong to the game or another extension; Starfall does not redefine them.

## Authoring contract

See [Authoring contract](setup.md#authoring-contract) in the Installation and Configuration guide.

<a id="installation-and-configuration-sources"></a>
[Installation and configuration sources](setup.md#installation-and-configuration-sources)

<a id="registries-and-a-complete-passive-example"></a>
[Registries and a complete passive example](setup.md#registries-and-a-complete-passive-example)


## Triggers, targets, and event execution

See [Triggers, targets, and event execution](passives.md#triggers-targets-and-event-execution) in the passive guide.

<a id="trigger-catalog"></a>
[Trigger catalog](passives.md#trigger-catalog)

<a id="target-names"></a>
[Target names](passives.md#target-names)

<a id="event-pipeline-and-snapshots"></a>
[Event pipeline and snapshots](passives.md#event-pipeline-and-snapshots)

<a id="synchronous-damage-interception"></a>
[Synchronous damage interception](passives.md#synchronous-damage-interception)

<a id="synchronous-weapon-replacement"></a>
[Synchronous weapon replacement](passives.md#synchronous-weapon-replacement)

<a id="filters-and-state-conditions"></a>
[Filters and state conditions](passives.md#filters-and-state-conditions)

<a id="ordinary-event-fields"></a>
[Ordinary event fields](passives.md#ordinary-event-fields)

<a id="shared-holder-state-conditions"></a>
[Shared holder-state conditions](passives.md#shared-holder-state-conditions)

<a id="whilestate-only-conditions"></a>
[WhileState-only conditions](passives.md#whilestate-only-conditions)

<a id="continuous-modifiers-and-passive-ownership"></a>
[Continuous modifiers and passive ownership](passives.md#continuous-modifiers-and-passive-ownership)

<a id="modifier-catalog"></a>
[Modifier catalog](passives.md#modifier-catalog)

<a id="aura-fields"></a>
[Aura fields](passives.md#aura-fields)

<a id="sources-stacks-and-removal"></a>
[Sources, stacks, and removal](passives.md#sources-stacks-and-removal)

<a id="action-reference"></a>
[Action reference](passives.md#action-reference)

<a id="action-catalog"></a>
[Action catalog](passives.md#action-catalog)

<a id="custom-ground-pickups"></a>
[Custom ground pickups](miscellaneous.md#custom-ground-pickups)

<a id="attached-units"></a>
[Attached units](passives.md#attached-units)

<a id="removing-attached-units"></a>
[Removing attached units](passives.md#removing-attached-units)

<a id="building-creation"></a>
[Building creation](passives.md#building-creation)

<a id="building-transformation"></a>
[Building transformation](passives.md#building-transformation)

<a id="multiple-actions"></a>
[Multiple actions](passives.md#multiple-actions)

<a id="forcemove-fields"></a>
[ForceMove fields](passives.md#forcemove-fields)

<a id="signals"></a>
[Signals](passives.md#signals)

<a id="numeric-formulas"></a>
[Numeric formulas](passives.md#numeric-formulas)


## Research projects

The research reference has moved to [Research Choices](research.md).

<a id="project-schema"></a>
[Project schema](research.md#project-schema)

<a id="effect-catalog"></a>
[Effect catalog](research.md#effect-catalog)

<a id="selection-refresh-and-global-settings"></a>
[Selection, refresh, and global settings](research.md#selection-refresh-and-global-settings)

<a id="buildings-and-free-research"></a>
[Buildings and free research](research.md#buildings-and-free-research)

<a id="free-research-choices"></a>
[Free research choices](research.md#free-research-choices)

<a id="random-research-rewards"></a>
[Random research rewards](research.md#random-research-rewards)

<a id="native-research-query"></a>
[Native research query](research.md#native-research-query)

## External grants and map integration

See the [Run Lua on warhead detonation](lua.md#run-lua-on-warhead-detonation) in the Lua guide.

### Warhead grants

See [Warhead grants](passives.md#warhead-grants) in the passive guide.


### Grant presets

See [grant presets](map-integration.md#grant-presets) in the map guide.

### Map action numbers

See [map action numbers](map-integration.md#map-action-numbers) in the map guide.

See the [Map Lua preset](lua.md#map-lua-preset) in the Lua guide.

See the [Active skills](lua.md#active-skills) in the Lua guide.

See the [Lua API](lua.md#lua-api) in the Lua guide.

## Visual effects submodule

See [weapon tracers](artsp.md#weapon-tracers) and [continuous beams](artsp.md#continuous-beams) in the ArtSp guide.

## Special behavior submodule

See [Special behavior submodule](actsp.md#special-behavior-submodule) in the ActSp Special Behavior guide.

<a id="overview-and-installation"></a>
[Overview and installation](actsp.md#overview-and-installation)

<a id="drone-behavior"></a>
[Drone behavior](actsp.md#drone-behavior)

<a id="aircraft-spawns"></a>
[Aircraft spawns](actsp.md#aircraft-spawns)

<a id="building-ground-guards"></a>
[Building ground guards](actsp.md#building-ground-guards)

<a id="guard-cell-orders"></a>
[Guard cell orders](actsp.md#guard-cell-orders)

<a id="advanced-garrison"></a>
[Advanced garrison](actsp.md#advanced-garrison)

<a id="actsp-bettergarrison"></a>
[Advanced garrison](actsp.md#advanced-garrison)


## ActionSequence superweapons

See [ActionSequence superweapons](miscellaneous.md#actionsequence-superweapons) in the miscellaneous guide.

## Animation, icons, and audio

See [Animation, icons, and audio](resources.md#animation-icons-and-audio) in the Animation, Icons, and Audio guide.

<a id="passive-visual-fields"></a>
[Passive visual fields](resources.md#passive-visual-fields)

<a id="icons-text-and-audio"></a>
[Icons, text, and audio](resources.md#icons-text-and-audio)

<a id="native-animation-references"></a>
[Native animation references](resources.md#native-animation-references)


## Persistence, multiplayer, and compatibility

See [Persistence, multiplayer, and compatibility](compatibility.md#persistence-multiplayer-and-compatibility) in the Persistence, Multiplayer, and Compatibility guide.

<a id="saveload-contract"></a>
[Save/load contract](compatibility.md#saveload-contract)

<a id="multiplayer-contract"></a>
[Multiplayer contract](compatibility.md#multiplayer-contract)

<a id="unit-conversion-and-extension-scope"></a>
[Unit conversion and extension scope](compatibility.md#unit-conversion-and-extension-scope)


## Diagnostics and removed interfaces

See [Diagnostics and removed interfaces](setup.md#diagnostics-and-removed-interfaces) in the Installation and Configuration guide.

<a id="build-tests-and-source-map"></a>
[Build, tests, and source map](setup.md#build-tests-and-source-map)


## Credits

Creator: 斩断云天. Collaborator: ChatGPT, 贪吃的布丁. Acknowledgments: Westwood, the Ares authors, the Phobos team, and ra2jj.com.
