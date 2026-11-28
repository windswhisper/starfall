# ActSp Special Behavior

[Manual home](../manual.md) · [Configuration reference](reference.md)

## Contents

- [Overview and installation](#overview-and-installation)
- [Continuous unit orders](#continuous-unit-orders)
- [Drone behavior](#drone-behavior)
- [Advanced garrison](#advanced-garrison)
- [Raised platform](#raised-platform)
- [Mobile build nodes](#mobile-build-nodes)
- [Entity walls](#entity-walls)
- [Projectile interception by buildings](#projectile-interception-by-buildings)
- [Lua projectile motion](#lua-projectile-motion)
- [Target-relative weapon origin](#target-relative-weapon-origin)
- [Piercing projectiles](#piercing-projectiles)
- [ActionSequence superweapons](miscellaneous.md#actionsequence-superweapons)

## Special behavior submodule

### Overview and installation

ActSp is built into `Starfall.dll`. It adds patrol modes for native aircraft spawns, ground guards, independent occupant fire for garrisons, raised platforms and entity walls. Each feature uses its rules settings to determine where it applies.

Run `build.cmd` from the repository root to build `build/Starfall.dll`. Place it beside `gamemd.exe` and load it with SyringeEx. When upgrading from a two-DLL installation, move the old `ActSp.dll` out of the game directory and remove `-i=ActSp.dll` from the launch configuration.

Add the fields below to `rulesmd.ini`. ActSp reads the engine's merged rules. All multiplayer peers need the same `Starfall.dll` and rules. [ActSp module guide](../../modules/ActSp/README.md)

### Continuous unit orders

An area attack holds the caster in place. Eligible targets must be inside the selected circle and within the unit's current weapon range. An empty circle stays active. Each unit picks independently using synchronized randomness. A complete burst or Dreadnought missile salvo stays on one target while it remains eligible. At the next scan after the salvo, the unit prefers another eligible target.

```ini
[Starfall.UnitOrders]
0=(order ID)
; Registers an order definition.

[OrderID]
Type=(AreaAttack or AutoEnter)
; AreaAttack or AutoEnter.

CommandBar.Button=(button name)
; Optional button name, for example AreaAttack. The command is visible only when that name appears in the current uimd.ini ButtonList. Use a unique name for each binding.

CommandBar.Image=(SHP filename)
; Optional SHP filename, e.g. button06.shp to reuse the Guard icon. Defaults to the button name plus .shp.

CommandBar.Tooltip=(text or TIP: CSF label)
; Text shown in the native command bar tooltip. Enter plain text or a CSF label beginning with TIP:. If omitted, the tooltip shows the order name and configured radius. A missing label also uses this default.

Radius=(integer cells)
; Circle radius for the command bar button, 1–128 map cells, default 6.

ScanInterval=(integer frames)
; Interval in simulation frames between area attack searches or auto-enter approach checks. Positive integer, default 15.

AreaAttack.Houses=(house selector list)
; Comma-separated Enemies, Neutral. Default Enemies.

AreaAttack.Kinds=(unit kind list)
; Comma-separated Infantry, Vehicle, Aircraft, Building, or All (default).

AutoEnter.Kinds=(host type list)
; Comma-separated host types. Transport boards a friendly ground vehicle with sufficient remaining capacity, Garrison occupies an available native garrison slot, and TankBunker sends an eligible vehicle into an empty tank bunker. Defaults to all three.

AreaAttack.Select=(Random)
; Random (default).

[ActiveSkillID]

Action=StartUnitOrder
; Use Trigger=Active, Active.Target=Area and a registered Action.Order. The ability supplies the radius in cells.

Action=CancelUnitOrder
; Cancels the unit's current order. Optional Action.Order restricts cancellation to that ID. For a cancel button use Active.Target=None.

Action.Order=(registered order ID)
; Selects the order to start or restricts the order to cancel.
; Required for StartUnitOrder; optional for CancelUnitOrder.

```

Click the reticle beside the deployment and team controls, then use the native Attack cursor to aim. Pulsing concentric rings from the stock nuclear targeting display show the configured radius. Left-click confirms the circle. One of the units receiving the order gives its native attack response. Right-click or Esc cancels. Map clicks, other command-bar buttons and command hotkeys are captured while aiming. Each selected controllable armed unit receives the order, including armed buildings. The following rules bind a global order available to the whole army.

```ini
[Starfall.UnitOrders]
0=NavalBombardment

[NavalBombardment]
Type=AreaAttack
CommandBar.Button=AreaAttack
CommandBar.Tooltip=Choose the center of the strike area
Radius=6
```

To supply your own icon, include `AreaAttack.shp` as a loose game resource or in a loaded MIX. Use the sidebar palette. A one-frame SHP is sufficient. With additional frames, the button uses frame 1 while pressed or targeting and frame 2 when disabled (zero-based). If a state frame is absent, it uses frame 0.

An explicit `CommandBar.Image` takes precedence over the same-name file. A missing image shows the built-in reticle. Starfall logs an explicitly configured missing file once for that button when it is displayed.

To show the button, insert `AreaAttack` at the desired position in the existing `uimd.ini` list. The example places it between Deploy and Guard for solo and skirmish games. For multiplayer, edit `[MultiplayerAdvancedCommandBar]` separately. Omit the name in either mode to hide it there. Preserve the other commands in each list.

```ini
[AdvancedCommandBar]
ButtonList=Team01,Team02,TypeSelect,Deploy,AreaAttack,Guard,PlanningMode
```

To offer a separate Dreadnought active ability, the skill can reference the same order:

```ini
[Starfall.UnitOrders]
0=NavalBombardment

[NavalBombardment]
Type=AreaAttack
ScanInterval=15

[Starfall.PassiveTypes]
0=DreadnoughtBombard

[DRED]
Starfall.Passives=DreadnoughtBombard

[DreadnoughtBombard]
Trigger=Active
Active.Name=Area bombardment
Active.Target=Area
Active.Radius=6
Active.CastMode=All
Action=StartUnitOrder
Action.Order=NavalBombardment
```

Move, attack, stop or deploy to take manual control. Casting again replaces the circle. Confirming the location hides the range indicator. Reselect the unit to view its current order area. Orders survive saving and loading and end when the caster dies or changes owner.

```lua
-- Inside an area skill's onCast(ctx)
local started = ctx.self:startOrder("NavalBombardment", ctx.area)
local id = ctx.self:currentOrder()  -- registered ID or nil
-- In a separate cancel skill
-- ctx.self:cancelOrder("NavalBombardment")
```

Lua accepts `{center={x=70,y=61}, radius=6}` as well as `ctx.area`. Radius is 1–128 map cells. Start and cancel return a boolean. An invalid replacement preserves the current order.

#### Auto-enter

Select infantry or vehicles, click the order button and then left-click the center of the search circle with the native Enter cursor. The native building range ring shows the search radius. On confirmation, one of the units receiving the order gives its native move response. Right-click or press Esc to cancel targeting. While targeting, clicks and command hotkeys are captured. Each unit seeks the nearest compatible host with room inside the circle. Approaching passengers reserve transport capacity or one garrison slot each. More infantry can join an occupied transport or garrison until full. Each tank bunker holds one vehicle. Infantry may also occupy a neutral garrison if the game allows it. Each command assigns spaces once. Units without a space immediately return to guard where they stand. Issue a new command to use spaces that open later. Assigned units proceed to their destination. If it becomes unavailable or the unit stops making progress for 180 frames, the unit returns to guard. A manual move or stop cancels the approach.

```ini
[Starfall.UnitOrders]
boarding=AutoBoard

[AutoBoard]
Type=AutoEnter
AutoEnter.Kinds=Transport,Garrison,TankBunker
CommandBar.Button=AutoBoard
Radius=6
```

Add `AutoBoard` to the desired `ButtonList`. To offer a transport-only button, set `AutoEnter.Kinds=Transport`. Area active abilities and Lua can start the same order by its ID.

### Drone behavior

#### Aircraft spawns

Put `Drone.*` fields in the carrier type's section. Aircraft modes require native spawns and slots. Launching without an attack target requires `Spawner=yes` on the carrier's current launcher, including its elite weapon when applicable. Missile spawns use their native behavior.

`Native` follows native launch behavior. `Guard` uses a fixed map cell. `Follow` guards a map-axis offset from the carrier. `Orbit` updates waypoints around the carrier and staggers its slots.

`Drone.GuardRange` sets the acquisition radius around the guard cell. It defaults to `5` and accepts `1..32` cells. The carrier's assigned target takes priority. Otherwise drones choose the nearest eligible enemy within the radius. When out of ammo, aircraft use their native return and reload behavior.

```ini
[CarrierTypeID]
Drone.Mode=(Native, Guard, Follow, or Orbit)
; Selects the carrier's drone behavior.
; Default: Native

Drone.GuardCell=(x,y integer map cells)
; Sets the initial fixed station for Guard mode.
; Default: omitted

Drone.Offset=(x,y integer map cells)
; Sets the map-axis offset used by Follow mode.
; Default: 2,0; each axis: -32..32

Drone.Radius=(integer cells)
; Sets the orbit radius in Orbit mode.
; Default: 3; range: 1..32

Drone.Period=(integer frames)
; Sets the orbit period in Orbit mode.
; Default: 180; range: 30..3600

Drone.GuardRange=(integer cells)
; Sets the acquisition radius around the guard station.
; Default: 5; range: 1..32
```

```ini
[CARRIER]
Drone.Mode=Orbit
Drone.Radius=3
Drone.Period=180
```

#### Visible parking at an aircraft dock

Set `Drone.UseBuildingDocks=yes` on a building that already launches aircraft and has airport docks. Include the host in the spawned aircraft's `Dock=` list. For example, add these lines to an Allied Air Force Command configured to launch Hornets.

```ini
[GAAIRC]
Drone.UseBuildingDocks=yes

[HORNET]
Dock=GAAIRC
AirportBound=yes
```

Each new or regenerated aircraft appears at a free berth. Returning aircraft wait when the airport is full. A landed spawn remains visible and occupies its berth throughout `SpawnReloadRate` and while waiting for orders. It takes off from that berth on its next sortie. Ordinary aircraft and spawns share the available berths. The setting defaults to `no` and applies to both native launches and patrol modes.

#### Building ground guards

Set `Drone.Mode=Guard` and `Drone.GroundUnit` in a building type's section. Use a registered `VehicleTypes` or `InfantryTypes` ID with normal ground movement. Each building manages one ground guard. Native aircraft slots take priority on buildings with a spawn manager.

```ini
[BuildingTypeID]
Drone.GroundUnit=(registered ground vehicle or infantry type ID)
; Selects the guard managed by a Guard-mode building.
; Default: omitted

Drone.GroundRespawnDelay=(integer frames)
; Sets the replacement delay after the guard is lost or the building changes owner.
; Default: 300; range: 0..1000000
```

Example fragment for a building type section:

```ini
Drone.Mode=Guard
Drone.GroundUnit=MTNK
Drone.GroundRespawnDelay=300
Drone.GuardRange=5
```

Use `Drone.GroundUnit=E1` to dispatch a GI. Once assigned a guard cell, the building dispatches its guard from a passable empty cell next to its foundation. It waits if all exits are blocked. The guard uses its own pathfinding and weapons. `Drone.GroundRespawnDelay` is measured in game frames and defaults to `300` (`0..1000000`). On capture, the previous owner keeps the old guard and the new owner gets a replacement after the delay. If the building is sold or destroyed, a surviving guard becomes an ordinary unit. The guard assignment and replacement timer survive save/load. [Ground outpost example](../../examples/GrizzlyGroundOutpost/README.md)

#### Parking a ground vehicle at its building

Add the following to the ground outpost's building type in `rulesmd.ini`. Each outpost still manages one vehicle. A one-cell building can use its center as the parking spot.

```ini
[ASGNDPOST]
Drone.Mode=Guard
Drone.GroundUnit=MTNK
Drone.UseBuildingDocks=yes
NumberOfDocks=1
```

The vehicle appears at a free berth and waits there, visible on the map. Set a rally point beyond the cells touching the foundation to dispatch it. Set the rally on the foundation, on a berth, or on a cell touching the foundation to recall it. The same commands work through `setDroneGuardCell`. A returning vehicle drives along the ground and waits if the berth is reserved or occupied.

Parking works at any health and leaves hit points unchanged. A vehicle with finite `Ammo` returns when empty, refills its magazine after the building's `SpawnReloadRate` in game frames, and resumes its last patrol. An unlimited-ammo vehicle can stay on patrol until recalled. The vehicle, its berth reservation and the reload deadline survive a save/load.

#### Guard cell orders

Set `Drone.GuardCell=x,y` in the type section for an initial guard cell. During play, select a friendly `Guard` building and use the normal rally point control to assign a cell shared by its spawn slots. For a ground guard, choose a cell its type can reach.

Lua can call `unit:setDroneGuardCell({x=84, y=71})` or pass `ctx.point`. The method accepts integer coordinates in the playable map. It returns `true` if the unit accepts the order and `false` otherwise. The most recent rally or Lua order wins. A Lua order updates the guard cell while normal factory production uses the factory's rally point. Guard cells survive save/load.

### Advanced garrison

<a id="actsp-bettergarrison"></a>

Set `BetterGarrison.Enabled=yes` in `[General]` to enable the feature for buildings that allow infantry to garrison and fire. Set `BetterGarrison.Enabled=no` in a building type's section to keep its original garrison weapons.

Set any of the three adjustment fields in a building type's section to override that field for all buildings of the type. Omitted fields inherit from `[General]`. An invalid damage or fire-rate multiplier falls back to the corresponding global value; an out-of-range range bonus is clamped to `-128..128`. A building-type `BetterGarrison.Enabled=yes` takes effect when the global switch is enabled. The following example gives Battle Bunker occupants 50% more damage, two extra cells of range, and a 1.25× fire rate.

```ini
[General]
BetterGarrison.Enabled=yes

[NABNKR]
BetterGarrison.DamageMultiplier=1.50
BetterGarrison.RangeBonus=2
BetterGarrison.FireRateMultiplier=1.25
```

Occupants use their transport weapons according to `OpenTransportWeapon` and `FireInTransport` and reload independently. The longest-serving current occupant's weapon determines which targets can be ordered. Each occupant prioritizes the ordered target when able and otherwise acquires eligible hostile targets. Click an eligible neutral object to attack it manually. Hold Ctrl and left-click eligible empty ground to force-fire. The pointer shows a stationary attack when a garrison can fire at the target. Range is measured from the building's center, and projectiles launch from the assigned garrison ports.

```ini
[General]
BetterGarrison.Enabled=(boolean)
; Enables advanced garrison fire.
; Default: no

BetterGarrison.DamageMultiplier=(decimal multiplier)
; Multiplies occupant weapon damage.
; Default: 1.20. Accepted range: 0..100.

BetterGarrison.RangeBonus=(integer cells)
; Adds this many cells to weapon range for acquisition and firing.
; Default: 1. Clamped to -128..128.

BetterGarrison.FireRateMultiplier=(number)
; Divides full reload time by this multiplier, rounded up to at least one game frame.
; Default: 1.0. Accepted range: 0.01..100.

```

### Raised platform

Set `HighPlatform.Enabled=yes` on the building type. In its art section, use `Foundation=Custom`, set `Foundation.X` and `Foundation.Y` to the outer width and length in cells, and list the complete one-cell perimeter under `Foundation.n`. Each dimension must be at least 3. For example, an 8×5 foundation has a 6×3 deck and 22 perimeter cells. The concrete platform example uses a 6×6 foundation with a 4×4 deck.

Run `powershell -File tools/art/Write-HighPlatformFoundation.ps1 -Width 8 -Height 5` from the repository root to generate the foundation and placement outline. Paste the generated fields into the building's art section and use a building SHP that matches the footprint.

ActSp raises the deck by four terrain levels. Every cell of the deck and perimeter must be flat, at the same elevation, and on buildable land. With `HighPlatform.AllowWater=yes`, a footprint entirely on open water is also valid. Set `HighPlatform.AllowLand=no` on the same type for a sea-only platform. Disabling both settings prevents placement. Beach cells and mixed land/water footprints are rejected. A footprint can span several buildable land types, including `Rough`. Bridges, overlays, tunnels, rubble, and occupied cells still block placement. The base terrain level must be at most 10. On water, the raised deck supports land units and ordinary land buildings. Selling or destroying the platform restores the original water tiles, including after loading a save.

```ini
[PlatformBuildingTypeID]
HighPlatform.Enabled=(boolean)
; Enables raised-platform behavior for a building with a supported foundation.
; Default: no

HighPlatform.AllowLand=(boolean)
; Allows a platform to be placed entirely on buildable land. Default: yes.

HighPlatform.AllowWater=(boolean)
; Allows a platform to be placed entirely on open water. Default: no.

HighPlatform.Enter=(boolean)
; Allows eligible friendly infantry and vehicles to enter this platform.
; Default: yes

[InfantryOrVehicleTypeID]
HighPlatform.Enter=(boolean)
; Allows this type to enter a friendly platform whose entrance is enabled.
; Default: yes
```

The ring foundation must include every perimeter cell of the configured rectangle and leave the interior empty. The engine also accepts a legacy single-cell foundation, with a different deck offset used by older saves. Use the example's ring foundation when configuring a new platform.

Other buildings can be placed with their entire foundation on the deck, subject to the normal build radius. Units placed on the deck can travel across it. The perimeter is closed to ground movement and construction. A ground attacker below the deck cannot acquire or fire on a grounded deck target with a projectile that has `SubjectToCliffs=yes`. Force-firing at a deck cell follows the same rule. Selling or destroying the platform reopens the perimeter, restores the original terrain, destroys the buildings on the deck, and drops ground units to their deaths.

Click a visible retaining wall to select the high platform or issue an attack against it. Click occupants on the deck to select or target them individually. Wall attacks target the platform building. A projectile with `Arcing=yes` uses the nearest wall cell for its range check and aim. If it reaches the wall below deck height, it detonates on contact. Weapon scatter still applies.

Select your ground infantry or vehicles and click a wall of your high platform with the Enter cursor. They walk to a reachable cell outside the perimeter and transfer to free space on the deck. They wait outside while the deck is full. Stop or give them another order to cancel. To bring down your deck units, select the platform and press the Deploy key (D by default) or use the Deploy cursor on the platform. Units wait on the deck if all suitable ground cells are occupied.

`HighPlatform.Enter` defaults to `yes`. Set it to `no` in the platform's building type section to close its entrance, or in an infantry or vehicle type section to keep that type from entering any high platform. The setting only controls entry. Deploy can still bring down units on the deck.

The platform uses the building's normal SHP and palette. Set `CanHideThings=no` in its art section to keep the platform from marking deck occupants as hidden behind a building. The [concrete platform example](../../examples/HighPlatform/README.md) supplies the art and rules for a buildable Soviet platform.

### Mobile build nodes

Set `BuildNode.Enabled=yes` on an infantry or vehicle type in `rulesmd.ini`. The default is `no`. The owner's completed buildings can be placed near the unit using the building's native `Adjacent` distance. For airborne Jumpjet or Hover units and rotor aircraft, the anchor is the map cell directly underneath. Moving the unit moves the anchor. Transported, garrisoned and falling units cannot provide a node. Terrain and foundation checks still apply. An explicit Phobos building whitelist requires a matching building, and prohibited areas take precedence.

```ini
[AMCV]
BuildNode.Enabled=yes

[ZEP]
BuildNode.Enabled=yes
```

### Entity walls

Register posts and segments as standard 1×1 buildings with `Wall=no`. Use `BaseNormal=no` to keep the wall from extending normal base construction range. The [Soviet wall example](../../examples/EntityWalls/EntityWalls.rules.ini) supplies two sidebar entries and uses stock art.

```ini
[BuildingTypeID]
EntityWall.Enabled=(boolean)
; Enables this building as a wall endpoint. Reusing stock wall art retains the SHP and cameo while the object remains a building.
; Default: no

EntityWall.SegmentType=(registered building type ID)
; Building generated in each gap cell. Its type must also be enabled and resolve to itself.
; Default: This type

EntityWall.Group=(group ID)
; Buildings of the same owner and group connect on the four map axes at the same ground level.
; Default: Segment type ID

EntityWall.MaxGap=(integer cells)
; Number of empty cells allowed between endpoints, from 0 to 32. Zero joins immediate neighbors.
; Default: 4

EntityWall.BlocksProjectiles=(boolean)
; Applies the normal wall targeting rule to SubjectToWalls=yes, including allied transparency, downhill fire and wall-breaching warheads. Legacy walls also apply the wall rule during flight.
; Default: yes for legacy walls, no with ProjectileBlocker.Enabled=yes

EntityWall.HighWall=(boolean)
; Blocks aiming across the wall with SubjectToCliffs=yes in either direction, including friendly fire. Legacy high walls also stop these rounds during flight.
; Default: no

EntityWall.ConnectionFrames=(boolean)
; Select a body frame from the four neighbor bits. Use 16 connected shapes followed by their 16 shadows. Optional yellow and red damage banks each add 16 body frames and 16 matching shadows.
; Default: Yes for segments, no for posts

```

Place a post to establish an anchor. To extend from a post or a segment, produce the segment and choose an empty endpoint. Preview outlines show the intermediate cells. Only the chosen endpoint uses a production order and credits. Each clear direction can connect once. A blocked direction adds zero segments. Explicitly placing a post retains the post at that endpoint. Existing map buildings can act as anchors. Each segment takes damage independently. Destroying it opens that line of fire.

### Projectile interception by buildings

Configure an ordinary building, wall segment or post in its own building type section in `rulesmd.ini`. For a connected wall, put the keys on the generated segment type. Posts may have a separate height.

```ini
[MyCoverBuilding]
ProjectileBlocker.Enabled=yes
ProjectileBlocker.Height=4
ProjectileBlocker.BlockAllied=no
```

```ini
[BuildingTypeID]
ProjectileBlocker.Enabled=(boolean)
; Intercepts traveling projectiles that enter the building's occupied footprint between its base and roof.
; Default: no

ProjectileBlocker.Height=(integer terrain levels)
; Roof height above the building's base, in terrain levels (104 leptons each). Values below 1 use 1.
; Default: 4

ProjectileBlocker.BlockAllied=(boolean)
; Enables interception of the owner's and allies' rounds. The firing building can always shoot out.
; Default: no

```

A shooter can aim at a unit behind cover. A low shot strikes the first blocking face. A high arc clears the roof, and a descending missile can hit the roof. The original warhead detonates at the contact point with the building as its target. Its armor, damage and splash rules still apply. Instant projectiles, lasers and electric bolts bypass this collision check.

On an entity wall with interception enabled, omitting both wall flags permits aiming past that segment. An explicit `EntityWall.BlocksProjectiles=yes` or `EntityWall.HighWall=yes` still applies its targeting restriction before firing. Once a shot is in flight, that segment uses its configured height and allegiance filter. Ordinary terrain, other walls and weapon restrictions still apply.

### Lua projectile motion

Set `ProjectileScript=ClimbMissile.lua` on a conventional projectile type in the merged rules and put the Lua file beside `gamemd.exe`. `ProjectileScript.Life` defaults to 90 game frames (1–1,000,000). The weapon supplies its own `Speed`, damage and warhead. Each live round has an independent state. Lua motion supports `Piercing.Enabled=yes`. It requires a visible projectile with no Phobos `Trajectory` controller. Instant beam effects use their existing rendering path.

```ini
[ProjectileTypeID]
ProjectileScript=(Lua filename)
; Selects the projectile motion script in the game directory.

ProjectileScript.Life=(integer frames)
; Limits the lifetime of each scripted projectile.
; Default: 90; range: 1..1000000.
```

The module returns a table with any of `onLaunch(p)`, `onTick(p)` and `onTravel(p, from, to)`. The launch callback runs at age 0 immediately before the first simulated step. Tick ages start at 1. A launch or tick callback may return a positive integer to schedule its next tick that many frames later. Physics, contact checks and age still advance every game frame. Store persistent data in `p.state`, a plain table with the same serializable values and limits as Lua timer data. Fresh module locals are created for each invocation. Give all multiplayer clients identical scripts and rules.

| Query or command | Meaning |
| --- | --- |
| `p.age`, `p.life` | Elapsed and maximum game frames. The final movement step still checks contacts before the flight ends. |
| `p:weaponSpeed()` | The firing weapon’s raw INI `Speed`. Scripts choose how to convert this number to cells/frame. A round without a weapon uses the engine bullet speed. |
| `p:speed()`, `p:launchSpeed()` | Magnitude of the current velocity command and the original launch velocity in cells/frame. |
| `p:position()`, `p:velocity()` | Tables with `x,y,z`. Horizontal integer coordinates denote cell centers. Position Z is absolute world elevation in cells. Velocity is in cells/frame. |
| `p:heightAboveGround()` | Local height in cells. |
| `p:target()`, `p:targetPoint()` | Live on-map target unit handle (supports `id/alive/position/type`) or nil; 3D aim point for a unit or ground shot. Losing the unit retains its last known aim point. |
| `p:firer()` | Original shooter handle while alive, otherwise `nil`. The firing house remains available to projectile actions after the shooter is lost. |
| `p:groundPoint(point)` | Returns `{x,y,z}` on the terrain or bridge deck at fractional X/Y. Omit the point to project the current position. |
| `p:nearbyUnits(point,radius,filter)` | Horizontal radius in cells (0–128), live on-map units sorted by object ID. Accepts `houses`, `types`, `visibleOnly` and `includeSelf` as in `ctx:nearbyUnits`. Here `includeSelf` includes the shooter. House relations use the firing house. |
| `p:action(name,fields)` | Execute one of the five actions below. Returns a success boolean. |
| `p:setVelocity(x,y,z)` | Change the continuing velocity starting this frame. Zero on all axes hovers. |
| `p:moveTo(x,y,z)` | Set a swept endpoint for this frame. The continuing velocity resumes on the next frame. |
| `p:steerTo(point,speed,maxTurn)` | Turn toward a 3D point, at most `maxTurn` degrees (0–180) per call. From rest, take the requested heading immediately. |
| `p:detonate()` | End the flight at its current position. Conventional rounds use their own warhead. Piercing rounds use their optional `Piercing.EndWarhead`. |

Conventional rounds detonate within 32 leptons of the aim point. Both modes check configured entity walls, physical blockers and spherical projectile shields along the movement segment. A scripted round follows the requested 3D path between these contacts. Keep ground clearance in the script when designing terrain-following paths. Leaving the playable world or a callback error removes the round. The engine still draws the projectile image, native smoke and trail effects. For smoke every two frames, set `Trailer=SMOKEY2` and `SpawnDelay=2` on `[DRAGON]` in art. Other rounds using this image share that art configuration. [Example weapon and rules](../../examples/ProjectileScript/ClimbMissile.rules.ini) · [Example flight controller](../../examples/ProjectileScript/ClimbMissile.lua)

With `Piercing.Enabled=yes`, each frame sweeps the actual 3D segment requested by the script, including a `moveTo` step. The projectile can pass through its aim point. On a return pass it can hit a new target, and previously hit objects stay recorded through save/load. `ProjectileScript.Life` controls the entire flight. Expiry, interception and `p:detonate()` use `Piercing.EndWarhead` when configured. Without it there is no end explosion. [Returning wave rules](../../examples/ProjectileScript/ReturnWave.rules.ini) · [Returning wave script](../../examples/ProjectileScript/ReturnWave.lua)

**Effects along the path.** `onTravel(p, from, to)` runs for every nonzero movement step in the playable world, even while `onTick` is sleeping. The endpoint is already clipped to the first stopping contact. Inside this callback `p:position()` returns `to`. Use it for queries and actions, return no value, and control motion in `onLaunch` or `onTick`. Measure the segment and save unused distance in `p.state` to place multiple blast points in a fast step and maintain spacing through save/load. [Ground blast script](../../examples/ProjectileScript/GroundBlastLine.lua) · [Weapon and projectile rules](../../examples/ProjectileScript/GroundBlastLine.rules.ini)

| Action | Example fields |
| --- | --- |
| `DetonateWarhead` | `{Warhead="HE", Amount=40, Target=p:groundPoint()}` creates a separate impact and the parent keeps flying. |
| `PlayAnim` | `{Anim="PIFFPIFF", Target=p:position()}` |
| `PlaySound` | `{Sound="Explosion1", Target=p:position()}` |
| `SpawnParticleSystem` | `{ParticleSystem="SmallGreySSys", Target=p:position()}`. Directed systems use the projectile position as origin and need a distinct destination. |
| `FireWeapon` | `{Weapon="120mm", Target=enemy}` launches a conventional weapon from the projectile position. Optional `Origin` overrides it for one shot. |

`Target` accepts a live unit handle or map point. Points use fractional X/Y and optional absolute Z in cells. Omit Z to use the ground or bridge surface. By default effects occur at the current position and child weapons aim at the parent's target or its last aim point. Child weapons inherit the available shooter and firing house.

### Target-relative weapon origin

Place these keys on the **weapon** in the rules INI. Keep its existing projectile, warhead and damage settings.

```ini
[PrismShot]
FireOrigin=Target
FireOrigin.Offset=0,0,1536
```

`FireOrigin` defaults to `Firer` (normal muzzle). With `Target`, the shot starts at the target's aim point plus `FireOrigin.Offset` (default `0,0,0`). X and Y use fixed map axes, Z points upward, and one horizontal cell is 256 leptons. For a diagonal incoming shot use e.g. `-512,0,768`. A ground order uses the selected cell's surface, including bridges. An offset outside the map grid falls back to the normal muzzle.

```ini
[WeaponID]
FireOrigin=(Firer or Target)
; Selects the normal muzzle or the target-relative firing position.
; Default: Firer

FireOrigin.Offset=(x,y,z integer leptons)
; Offsets a target-relative firing position using fixed map axes.
; Default: 0,0,0
```

Lua can override the launch point per shot using the [FireWeapon Origin argument](lua.md#weapon-firing-scripts).

Range, fire permission, ownership and target tracking follow the original weapon and projectile. Physical rounds and instant hits use the new launch point, including for shield intersection. Ordinary lasers, electric bolts and radiation beams use it for their visual source as well. Each burst round and each multishot branch uses its own target. A continuous beam follows that target while connected. Railguns, sonic and magnetic waves, temporal beams, disk lasers and spawner weapons use their own firing systems and are outside this option.

### Piercing projectiles

Configure these keys on a visible, physical projectile type in the rules INI. Assign it to a normal weapon through its Projectile key. The weapon supplies Speed, Damage and Warhead. Range governs target acquisition. By default, the launch position and initial aim point define a fixed three-dimensional direction. With `ProjectileScript`, Lua sets the path and `ProjectileScript.Life` supplies the lifetime. Losing the original target does not shorten the flight.

```ini
[PiercingWave]
Image=120MM
AG=yes
AA=no
Piercing.Enabled=yes
Piercing.Lifetime=30
Piercing.Radius=0.5
Piercing.DamageFalloff=0.8
Piercing.MinDamageMultiplier=0.3
; Optional: use a registered warhead on expiry or interception.
; Piercing.EndWarhead=HE
```

```ini
[ProjectileTypeID]
Piercing.Enabled=(boolean)
; Sweep the flight path for at most one direct hit per eligible object per projectile.
; Default: no

Piercing.Lifetime=(positive integer frames)
; Positive number of simulation frames for built-in straight flight. Scripted flight uses ProjectileScript.Life.
; Default: 30

Piercing.Radius=(decimal cells)
; Sweep radius in map cells, from 0 to 512. Buildings use their occupied foundation cells and height.
; Default: 0.5

Piercing.DamageFalloff=(decimal multiplier)
; Factor applied after each effective hit, from 0 to 1.
; Default: 1

Piercing.MinDamageMultiplier=(decimal multiplier)
; Lower bound on the remaining damage multiplier, from 0 to 1.
; Default: 0

Piercing.EndWarhead=(registered warhead ID)
; Registered warhead ID for a single explosion at the final flight position or the first blocking wall or shield contact.
; Default: none

```

The first hit uses full damage. Each subsequent multiplier is the larger of the configured floor and the previous multiplier times the falloff. The game applies armor separately to each victim. The projectile tests hits in travel order and uses object ID to break ties. Warhead house filters and projectile AA/AG flags select eligible targets. Filtered targets and fully immune targets do not consume falloff. Each pass-through hit deals direct damage. A blocking wall or sphere ends flight at the first interception and receives the remaining damage. A sphere also applies its interception multiplier. By default expiry is silent. With Piercing.EndWarhead, the explosion uses the damage remaining after the last effective hit. A shield also scales this explosion. At zero damage only the end warhead impact animation plays. The blocker receives its direct hit once. Other nearby objects, including earlier pass-through victims, can receive area damage according to the end warhead. Leaving the playable map removes the round silently. Saving the game preserves the original path, elapsed lifetime, remaining damage multiplier and hit history.

### ActionSequence superweapons

See [ActionSequence superweapons](miscellaneous.md#actionsequence-superweapons).
