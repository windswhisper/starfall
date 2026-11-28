# Active Skills

[Manual home](../manual.md) · [Lua guide](lua.md)

## Active skills

Active skills share `[Starfall.PassiveTypes]` registration and passive ownership sources. Scripted active skills use `Trigger=Active` and exactly one `Action=RunScript`. A ground vehicle can also use the native [JumpTo active skill](passives.md#vehicle-jump) or [EjectPassengers transport skill](passives.md#passenger-ejection). The accepted parent keys are `Trigger`, `Cooldown`, `Action`, `Action.Script`, `Action.Function`, `Script.Param.*`, the `Active.*` keys below, `GainAnim`, `Tint.Color`, and `Tint.Strength`. Do not add ordinary filters, Chance, Actions, state conditions, or Stack.Max to an active definition.

```ini
[ActiveSkillID]
Active.Name=(text or ID)
; Display label.
; Default: Skill ID

Active.Icon=(text or ID)
; PNG/PCX resource path.
; Default: built-in fallback

Active.Indicator.Texture=(PNG file path)
; Transparent PNG for the ground target preview. Area uses a ring; Unit/Point/Direction use a reticle.
; Default: built-in ring/reticle

Active.Indicator.Opacity=(number)
; Integer 0..100, applied to the texture alpha. Zero hides textured previews.
; Default: 90

Active.Indicator.Arrow.Texture=(PNG file path)
; Transparent PNG pointing upward, used by Direction skills.
; Default: built-in arrow

Active.Indicator.Arrow.Width=(number)
; Arrow width in world cells, decimal 0.1..8.
; Default: 0.75

Active.Indicator.Arrow.Head=(number)
; Top cap as an integer percentage of the arrow's trimmed image height; 1..98.
; Default: 25

Active.Indicator.Arrow.Tail=(number)
; Bottom cap percentage, 1..98. Head plus Tail must be less than 100.
; Default: 37

Active.CastMode=(Single or All)
; Single or All. Single picks the closest eligible selected caster, with unit ID as tie-breaker.
; Default: Single

Active.InterruptOrders=(boolean)
; yes / no. Accepted casts may stop current movement/attack orders.
; Default: yes

Active.Target=(None, Unit, Point, Area, or Direction)
; None,Unit,Point,Area,Direction.
; Default: None

Active.Range=(number)
; Integer 0..128; zero unlimited for Unit/Point/Area. Must be zero for None/Direction.
; Default: 0

Active.Houses=(list)
; List Self,Allies,Enemies,Neutral,All; Unit target only.
; Default: all

Active.Radius=(integer cells)
; Required positive 1..128 for Area; otherwise must be zero.
; Default: 0

Active.Shape=(Circle)
; If supplied, must be Circle with Area.
; Default: circle behavior

Active.Distance=(integer cells)
; Required positive 1..128 for Direction; otherwise must be zero.
; Default: 0

Cooldown=(number)
; Nonnegative frames, independently tracked per holder.
; Default: 0

```

Cooling skill buttons show remaining game seconds rounded upward (`ceil(remainingFrames / 15)`), and the hover tooltip shows the same value. Positive fractions display at least `1`; the countdown disappears at zero. For a shared button, hide the countdown whenever any selected holder is ready; otherwise display the shortest positive remaining cooldown among selected holders. The countdown follows simulation frames, including game speed and pauses, and preserves PNG transparency.

Casters must belong to the issuing player, hold the skill, be alive/on-map/outside transports, and be free of incompatible EMP, forced movement, sinking, or removal states. Invalid/duplicate/foreign commands are rejected. Unit targets obey relevant cloak/disguise visibility rules. Direction clicks cannot equal the caster's cell; the endpoint is shortened at the map edge.

Indicator PNGs are top-down source artwork, not pre-flattened isometric images. Drawing rotates them in world space and then applies the game's 45-degree orthographic map projection, including bridge/ground height. They are local overlays clipped to the tactical viewport, not terrain decals with per-pixel terrain occlusion. Transparent borders are trimmed using an alpha threshold of 16/255; keep any intentional padding inside the visible artwork. The default images are embedded in the DLL and can be overridden by `sfindicator_ring.png`, `sfindicator_target.png`, and `sfindicator_arrow.png` in the game directory, or by the per-skill paths above. Missing/empty custom images fall back to the embedded defaults; drawing failure falls back to lines. Invalid targets are tinted red. Images use the existing bounded presentation cache and are refreshed with that cache on reload.

The upward arrow is split vertically into head, middle, and tail after trimming. Width determines the uniform scale of both caps; only the middle stretches to cover the remaining distance. Short arrows omit the middle and crop the overlapping tail, then the bottom of the head if necessary, preserving cap proportions and the endpoint. Supply straight parallel rails in the middle band so extension does not elongate decorative chevrons. These settings do not change skill range, hit geometry, commands, or network state.

Same-ID skills share a UI button in mixed selection. All mode issues separate casts for eligible selected holders. Left click confirms a target; right click cancels. A successful native action, persistent-state change, or task scheduling can count as callback success; returning `true` from Lua alone does not. Cooldown starts on reported success. `Active.InterruptOrders=yes` interrupts after caster/aim validation and before running the script, so a script that ultimately makes no change can still have interrupted orders.

Range/caster rings rotate once every 6 seconds in the world plane before projection, keeping the ground footprint and radius fixed. Target reticles loop a 1.5-second elastic scale sequence (90% contraction, 112% overshoot, then settling at 100%) around their existing center. A local monotonic clock drives both animations independently of game speed; they do not alter targeting, simulation state, random numbers, or network commands. Valid indicator textures, including custom PNGs and arrows, are tinted bright blue (`#40A8FF`) while retaining alpha and brightness detail. Invalid targets remain red. Arrow head/tail geometry is not animated.

**Complete active example**, assuming `MTNK` exists:

```ini
[Starfall.PassiveTypes]
0=SF_Recall

[MTNK]
Starfall.Passives=SF_Recall

[SF_Recall]
Trigger=Active
Active.Name=Recall
Active.Target=Point
Active.Range=12
Active.CastMode=Single
Cooldown=300
Action=RunScript
Action.Script=Recall.lua
Action.Function=onCast
```

`Recall.lua`:

```lua
return {
    onCast = function(ctx)
        if ctx.self and ctx.self:alive() and ctx.point then
            ctx:action("ChronoTeleport", {
                Target = ctx.self, Destination = ctx.point, SearchRadius = 3,
            })
        end
    end,
}
```

For Area, `ctx.area={center=point,radius=...}`. For Direction, `ctx.point` is the projected endpoint and `ctx.direction={x=...,y=...}` is the unit direction vector. Copy any of these values into task data if delayed callbacks need the original aim.

