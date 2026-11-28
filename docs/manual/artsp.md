# ArtSp visual effects

[Manual home](../manual.md)

## Unit labels

Set `TipsLabel` on an infantry, vehicle, aircraft or building type in `rulesmd.ini`. A map can override the label in the same type section. Each visible instance displays the text above its health bar, even when unselected. The label has a blue-black background and light lettering. Leave it empty or omit it to hide the label.

```ini
[MTNK]
TipsLabel=灰熊坦克
```

## Continuous beams

Add one line to the existing weapon section in `rulesmd.ini`. For example, the Tesla coil's regular electric weapon uses this setting.

```ini
[CoilBolt]
ContinuousBeam=yes
```

ArtSp is included in `Starfall.dll`. Configure each weapon ID separately, including elite or overcharged weapons.

```ini
[WeaponID]
ContinuousBeam=(boolean)
; Keeps a supported beam visible between shots at the locked target.
; Default: no
```

The weapon must have `IsLaser=yes`, `IsElectricBolt=yes`, or `IsRadBeam=yes`. When multiple flags are enabled, ArtSp selects laser first, then electric bolt, then radiation beam. An unsupported weapon is reported in `Starfall.log`. ArtSp reads the weapon's existing flags and colors directly.

| Existing weapon flag | Appearance during reload |
| --- | --- |
| `IsLaser=yes` | A steady line using the laser's color or the firing house's color. |
| `IsElectricBolt=yes` | A flickering arc that follows both endpoints and uses the bolt colors. |
| `IsRadBeam=yes` | A traveling wave using the beam color and amplitude. Its phase continues across shots at the same target. |

The first actual shot at the firer's locked unit or building establishes the beam. The muzzle and aim point follow their units between shots. Losing detection of the target, leaving range, switching targets, deactivation or stopping ends the connection. A new target requires a new shot. Each successful `MultiShot` branch also holds a beam on its own target during reload. Losing one secondary target breaks only that beam. Its replacement connects on the next actual volley. Ground fire keeps its normal shot visuals. The weapon continues to spend ammunition and deal damage on its usual firing schedule. After loading a save, the next shot establishes the visual lock again.

## Weapon tracers

ArtSp is included in `Starfall.dll`. A tracer draws a short, moving streak between the muzzle and the point aimed at when a weapon fires. Set the weapon and firing interval in `rulesmd.ini`. Define the visual style in `artmd.ini`. Several weapons can reference the same section directly.

For a golden tracer on the first, fourth and seventh M60 rounds, add this to `rulesmd.ini`.

```ini
[M60]
Tracer=RifleGold
Tracer.Every=3
```

Add the matching section to `artmd.ini`.

```ini
[RifleGold]
Gradient=no
Color=255,205,96
TailColor=210,92,28
Length=96
Width=2
Speed=192
```

### Weapon settings in rulesmd.ini

```ini
[WeaponID]
Tracer=(art section ID)
; Names the tracer style. Omit to disable the effect.

Tracer.Every=(positive integer)
; Distance in shots between visible tracers.
; Default: 1; range: 1..2147483647
```

Each unit counts its shots separately for each weapon. Each round in a native burst counts once. When a shot produces extra targets through `MultiShot`, all successfully launched extra rounds share the original shot's tracer decision.

### Style settings in artmd.ini

```ini
[StyleID]
Gradient=(yes or no)
; Set yes to blend from TailColor to Color and fade the tail into the background.
; Default: no, use the opaque Color value for the entire streak.

Color=(red,green,blue)
; Solid line color, or head color with Gradient=yes.
; Default: 255,205,96; each channel: 0..255.

TailColor=(red,green,blue)
; Tail color with Gradient=yes.
; Default: 210,92,28; each channel: 0..255. Validated when Gradient=yes.

Length=(integer, leptons)
; Maximum visible length. One cell is 256 leptons.
; Default: 96; range: 1..4096.

Width=(integer, pixels)
; Line width.
; Default: 2; range: 1..8.

Speed=(positive integer, leptons per game frame)
; Visual travel speed.
; Default: 192; range: 1..2147483647.
```

The endpoint stays fixed if the target moves. For an instant `Inviso` projectile with `FlakScatter`, the endpoint uses the projectile's scattered location. The streak ends at its recorded point even when the weapon uses a curved or tracking projectile. Damage and impact timing follow the weapon. Only currently visible cells show the effect. Up to 512 streaks can be active at once. Loading a save clears existing streaks and restarts the visual shot counters. A missing style or an invalid setting is reported with its weapon and style ID in `Starfall.log`. The weapon continues to fire.
