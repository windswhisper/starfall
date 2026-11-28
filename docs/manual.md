# Starfall Lab Manual

English reference for mod authors, AI assistants, and maintainers. The current source implements Starfall Lab 0.40. Configuration and Lua references describe the repository implementation.

## Find a task

| Task | Guide |
| --- | --- |
| Write a Lua event, active skill, weapon or map script | [Lua guide](manual/lua.md) |
| Install Starfall and choose a configuration source | [Installation and configuration sources](manual/setup.md#installation-and-configuration-sources) |
| Launch infantry passengers in sequence with parachutes | [Passenger ejection](manual/passives.md#passenger-ejection) |
| Give ground vehicles a group jump command | [Vehicle jump and landing rules](manual/passives.md#vehicle-jump) |
| Create passive effects and triggers | [Passive example](manual/setup.md#registries-and-a-complete-passive-example), [passive guide](manual/passives.md) |
| Configure research projects and rewards | [Research projects](manual/research.md#research-projects) |
| Configure ActSp drones or garrisons | [Special behavior submodule](manual/actsp.md#special-behavior-submodule) |
| Add a moving tracer to a weapon | [ArtSp weapon tracers](manual/artsp.md#weapon-tracers) |
| Show a text label above a unit | [ArtSp unit labels](manual/artsp.md#unit-labels) |
| Keep a laser, electric arc or radiation beam on the target between shots | [ArtSp continuous beams](manual/artsp.md#continuous-beams) |
| Integrate map actions or superweapons | [Map integration](manual/map-integration.md), [ActionSequence](manual/miscellaneous.md#actionsequence-superweapons) |
| Diagnose errors or build and test | [Diagnostics](manual/setup.md#diagnostics-and-removed-interfaces), [build and tests](manual/setup.md#build-tests-and-source-map) |

## Core conventions

A cell is a map coordinate. A frame is a simulation frame, with seconds-based settings using 15 frames per game second. A house identifies a runtime player instance. A Lua unit handle retains an ID across callbacks and should be checked with `unit:alive()` before accessing a live unit.

Engine-loaded rules and `Starfall.ini` can both supply configuration, with lookup precedence depending on the subsystem. Lua files are discovered in the game root. Native game fields belong to the engine or their respective extensions. See [configuration sources](manual/setup.md#installation-and-configuration-sources) and [authoring contract](manual/setup.md#authoring-contract).

## Reference guides

- [Lua guide](manual/lua.md) covers script entry points, context, APIs, state, timers, examples, and execution limits.
- [Active skills guide](manual/active-skills.md) covers targeting, cooldowns, indicators, and scripted skills.
- [Miscellaneous guide](manual/miscellaneous.md) covers custom ground pickups and ActionSequence superweapons.
- [Passive guide](manual/passives.md) covers triggers, filters, actions, continuous modifiers, formulas, and passive visuals.
- [Research guide](manual/research.md) covers projects, offers, rewards, buildings, and the native query API.
- [Map guide](manual/map-integration.md) covers map triggers, team scripts, grant presets, and map Lua entry points.
- [Installation and configuration](manual/setup.md) covers setup, configuration sources, diagnostics, and development.
- [ActSp guide](manual/actsp.md) covers [continuous unit orders](manual/actsp.md#continuous-unit-orders), drone behavior, advanced garrisons, raised platforms, [entity walls](manual/actsp.md#entity-walls), [projectile interception by buildings](manual/actsp.md#projectile-interception-by-buildings), and [piercing projectiles](manual/actsp.md#piercing-projectiles).
- [Resources guide](manual/resources.md) covers icons, text, audio, and animations.
- [ArtSp visual effects](manual/artsp.md) covers unit labels, tracer styles, per-weapon tracer intervals and continuous beams.
- [Compatibility guide](manual/compatibility.md) covers saves, multiplayer, and conversions.
- [Manual writing guide](manual/style.md) records the structure and INI formatting conventions for maintainers.
- [Configuration reference](manual/reference.md) provides links for earlier section bookmarks.
- [Chinese HTML manual](manual.html) provides a browsable configuration guide.

## Previous section links

The links below preserve previous section bookmarks and lead to the current reference.

<a id="authoring-contract"></a>
[Authoring contract](manual/setup.md#authoring-contract)

<a id="installation-and-configuration-sources"></a>
[Installation and configuration sources](manual/setup.md#installation-and-configuration-sources)

<a id="registries-and-a-complete-passive-example"></a>
[Registries and a complete passive example](manual/setup.md#registries-and-a-complete-passive-example)

<a id="triggers-targets-and-event-execution"></a>
[Triggers, targets, and event execution](manual/passives.md#triggers-targets-and-event-execution)

<a id="trigger-catalog"></a>
[Trigger catalog](manual/passives.md#trigger-catalog)

<a id="target-names"></a>
[Target names](manual/passives.md#target-names)

<a id="event-pipeline-and-snapshots"></a>
[Event pipeline and snapshots](manual/passives.md#event-pipeline-and-snapshots)

<a id="synchronous-damage-interception"></a>
[Synchronous damage interception](manual/passives.md#synchronous-damage-interception)

<a id="synchronous-weapon-replacement"></a>
[Synchronous weapon replacement](manual/passives.md#synchronous-weapon-replacement)

<a id="weapon-firing-scripts"></a>
[Weapon firing scripts](manual/lua.md#weapon-firing-scripts)

<a id="filters-and-state-conditions"></a>
[Filters and state conditions](manual/passives.md#filters-and-state-conditions)

<a id="ordinary-event-fields"></a>
[Ordinary event fields](manual/passives.md#ordinary-event-fields)

<a id="shared-holder-state-conditions"></a>
[Shared holder-state conditions](manual/passives.md#shared-holder-state-conditions)

<a id="whilestate-only-conditions"></a>
[WhileState-only conditions](manual/passives.md#whilestate-only-conditions)

<a id="continuous-modifiers-and-passive-ownership"></a>
[Continuous modifiers and passive ownership](manual/passives.md#continuous-modifiers-and-passive-ownership)

<a id="modifier-catalog"></a>
[Modifier catalog](manual/passives.md#modifier-catalog)

<a id="aura-fields"></a>
[Aura fields](manual/passives.md#aura-fields)

<a id="sources-stacks-and-removal"></a>
[Sources, stacks, and removal](manual/passives.md#sources-stacks-and-removal)

<a id="action-reference"></a>
[Action reference](manual/passives.md#action-reference)

<a id="action-catalog"></a>
[Action catalog](manual/passives.md#action-catalog)

<a id="custom-ground-pickups"></a>
[Custom ground pickups](manual/miscellaneous.md#custom-ground-pickups)

<a id="attached-units"></a>
[Attached units](manual/passives.md#attached-units)

<a id="removing-attached-units"></a>
[Removing attached units](manual/passives.md#removing-attached-units)

<a id="building-creation"></a>
[Building creation](manual/passives.md#building-creation)

<a id="building-transformation"></a>
[Building transformation](manual/passives.md#building-transformation)

<a id="multiple-actions"></a>
[Multiple actions](manual/passives.md#multiple-actions)

<a id="forcemove-fields"></a>
[ForceMove fields](manual/passives.md#forcemove-fields)

<a id="signals"></a>
[Signals](manual/passives.md#signals)

<a id="numeric-formulas"></a>
[Numeric formulas](manual/passives.md#numeric-formulas)

<a id="research-projects"></a>
[Research projects](manual/research.md#research-projects)

<a id="free-research-choices"></a>
[Free research choices](manual/research.md#free-research-choices)

<a id="random-research-rewards"></a>
[Random research rewards](manual/research.md#random-research-rewards)

<a id="project-schema"></a>
[Project schema](manual/research.md#project-schema)

<a id="effect-catalog"></a>
[Effect catalog](manual/research.md#effect-catalog)

<a id="selection-refresh-and-global-settings"></a>
[Selection, refresh, and global settings](manual/research.md#selection-refresh-and-global-settings)

<a id="native-research-query"></a>
[Native research query](manual/research.md#native-research-query)

<a id="buildings-and-free-research"></a>
[Buildings and free research](manual/research.md#buildings-and-free-research)

<a id="external-grants-and-map-integration"></a>
[External grants and map integration](manual/reference.md#external-grants-and-map-integration)

<a id="run-lua-on-warhead-detonation"></a>
[Run Lua on warhead detonation](manual/lua.md#run-lua-on-warhead-detonation)

<a id="warhead-grants"></a>
[Warhead grants](manual/passives.md#warhead-grants)

<a id="grant-presets"></a>
[Grant presets](manual/map-integration.md#grant-presets)

<a id="map-action-numbers"></a>
[Map action numbers](manual/map-integration.md#map-action-numbers)

<a id="map-lua-preset"></a>
[Map Lua preset](manual/lua.md#map-lua-preset)

<a id="active-skills"></a>
[Active skills](manual/active-skills.md#active-skills)

<a id="lua-api"></a>
[Lua API](manual/lua.md#lua-api)

<a id="module-lifecycle-and-configuration"></a>
[Module lifecycle and configuration](manual/lua.md#module-lifecycle-and-configuration)

<a id="context-values"></a>
[Context values](manual/lua.md#context-values)

<a id="unit-handles-and-house-snapshots"></a>
[Unit handles and house snapshots](manual/lua.md#unit-handles-and-house-snapshots)

<a id="unit-orders"></a>
[Unit orders](manual/lua.md#unit-orders)

<a id="queries-and-action-calls"></a>
[Queries and action calls](manual/lua.md#queries-and-action-calls)

<a id="timers-state-and-synchronized-randomness"></a>
[Timers, state, and synchronized randomness](manual/lua.md#timers-state-and-synchronized-randomness)

<a id="movable-animation-api"></a>
[Movable animation API](manual/lua.md#movable-animation-api)

<a id="runtime-limits-and-unavailable-libraries"></a>
[Runtime limits and unavailable libraries](manual/lua.md#runtime-limits-and-unavailable-libraries)

<a id="special-behavior-submodule"></a>
[Special behavior submodule](manual/actsp.md#special-behavior-submodule)

<a id="overview-and-installation"></a>
[Overview and installation](manual/actsp.md#overview-and-installation)

<a id="drone-behavior"></a>
[Drone behavior](manual/actsp.md#drone-behavior)

<a id="aircraft-spawns"></a>
[Aircraft spawns](manual/actsp.md#aircraft-spawns)

<a id="building-ground-guards"></a>
[Building ground guards](manual/actsp.md#building-ground-guards)

<a id="guard-cell-orders"></a>
[Guard cell orders](manual/actsp.md#guard-cell-orders)

<a id="advanced-garrison"></a>
[Advanced garrison](manual/actsp.md#advanced-garrison)

<a id="entity-walls"></a>
[Entity walls](manual/actsp.md#entity-walls)

<a id="projectile-interception-by-buildings"></a>
[Projectile interception by buildings](manual/actsp.md#projectile-interception-by-buildings)

<a id="target-relative-weapon-origin"></a>
[Target-relative weapon origin](manual/actsp.md#target-relative-weapon-origin)

<a id="actionsequence-superweapons"></a>
[ActionSequence superweapons](manual/miscellaneous.md#actionsequence-superweapons)

<a id="animation-icons-and-audio"></a>
[Animation, icons, and audio](manual/resources.md#animation-icons-and-audio)

<a id="passive-visual-fields"></a>
[Passive visual fields](manual/passives.md#passive-visual-fields)

<a id="icons-text-and-audio"></a>
[Icons, text, and audio](manual/resources.md#icons-text-and-audio)

<a id="native-animation-references"></a>
[Native animation references](manual/resources.md#native-animation-references)

<a id="persistence-multiplayer-and-compatibility"></a>
[Persistence, multiplayer, and compatibility](manual/compatibility.md#persistence-multiplayer-and-compatibility)

<a id="saveload-contract"></a>
[Save/load contract](manual/compatibility.md#saveload-contract)

<a id="multiplayer-contract"></a>
[Multiplayer contract](manual/compatibility.md#multiplayer-contract)

<a id="unit-conversion-and-extension-scope"></a>
[Unit conversion and extension scope](manual/compatibility.md#unit-conversion-and-extension-scope)

<a id="diagnostics-and-removed-interfaces"></a>
[Diagnostics and removed interfaces](manual/setup.md#diagnostics-and-removed-interfaces)

<a id="build-tests-and-source-map"></a>
[Build, tests, and source map](manual/setup.md#build-tests-and-source-map)

<a id="credits"></a>
[Credits](manual/reference.md#credits)

<a id="actsp-bettergarrison"></a>
[Advanced garrison](manual/actsp.md#advanced-garrison)
