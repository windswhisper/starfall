# Animation, Icons, and Audio

[Manual home](../manual.md) · [Configuration reference](reference.md)

## Contents

- [Animation, icons, and audio](#animation-icons-and-audio)

## Animation, icons, and audio

### Passive visual fields

See [Passive visual fields](passives.md#passive-visual-fields) in the passive guide.


### Icons, text, and audio

Research and active icons accept PNG and engine-loadable PCX. PNG transparency is supported; invalid/missing resources use the appropriate built-in fallback. Research rarity colors: Common preserves original colors, Rare uses blue/purple, Epic gold, Insane red. Keep UI resource names separate from animation IDs.

Active buttons retain a 64x64 hit area and use a 64-pixel horizontal stride. Ready/total count badges are hidden; cooldown countdowns remain visible. Icon artwork is scaled to 54x54 at offset (5,4), then clipped to the 44x44 opening at offset (10,9), underneath the solid rim of `sfframe.png`. Clipping preserves the larger subject scale while keeping opaque corners out of the frame's transparent outer margins. Place essential artwork centrally; the outer image edges are cropped. The name and countdown also fit inside the frame. Replacement frames should retain a compatible inner opening.

PNG alpha is preserved by active button artwork (including cooldown dimming) and the cached `sfrcard.png` card skin. Card scale/fade animations composite the UI layer over its destination background without copying terrain from the original rectangle. Active button artwork has no implicit opaque backing plate; names and tooltips retain their own readability backgrounds. Opaque dark pixels in a source PNG remain opaque: the built-in default active icon, tornado icon, and card panel include dark artwork. To create holes or translucent panels, author an alpha channel; black is not treated as a transparency key.

For generated single-passive research details, supported placeholders include `{amount}`, `{count}`, `{chance}`, `{cooldown}`. A multi-action passive's custom amount placeholders refer to its first action. GiveCredits supports `{credits}`. Custom WhileState details and a combined research project's custom Detail are displayed literally rather than expanded per child effect.

Fields in `[Starfall.ResearchSounds]`: `Button`, `Cards`, `Refresh`, `Select`, `Available`. When a building or `GrantFreeResearch` grants a free choice, an omitted `Available` uses `builtin:available`. An explicit empty value in `Starfall.ini` silences the cue. Other uses default to empty. Accepted built-ins are `builtin:cards`, `builtin:select`, and `builtin:available`, or an external WAV path. Empty Button uses native click audio; other empty fields are silent. Failed Button audio falls back to native click audio.

External WAV: uncompressed PCM, 8/16-bit, mono/stereo, `8000..48000` Hz, at most 16 MiB. Paths can be absolute or relative to the game root. Action PlaySound instead uses a native sound ID or `wav:<path>`. Custom action WAV plays only for a local visible/unshrouded position, at the configured effects volume; missing local audio must not control gameplay. Local shake respects the viewport and applicable `HideShakeEffects` setting. Fire/railgun particle systems require distinct source and destination positions.

Cache behavior is internal, not additional INI configuration:

| Cache | Bound |
| --- | --- |
| Decoded icon/card images | 256 entries and 64 MiB of accounted data. |
| WAV samples | 64 entries and 32 MiB of accounted data. |
| Current scaled card surface | One surface, at most 1,048,576 16-bit pixels (2 MiB). |
| Rendered text | 256 entries and 4 MiB. |
| Simultaneously managed audio voices | 8. |

Image/audio caches use bounded LRU behavior; failed loads also occupy bounded entries. Oversized valid resources can be used transiently. Reloading research configuration clears image/card/audio caches; in-progress sounds retain independent playback buffers. Scenario/load reset stops old voices. These limits do not cap total process memory, temporary decoding, or the game's own PCX cache. There is no documented user-facing hot-reload command; do not invent one.

### Native animation references

Use an animation type already registered by the game or mod, with a valid native image. `Anim`, `Action.Anim`, and `ctx:spawnAnim` reference the animation type ID, not an image filename. Register custom animation types through the game's normal rules and art configuration before using them.

The Tornado example uses the native `PIFF` animation as a placeholder. Replace `Script.Param.Anim` with your mod's registered animation ID to supply its artwork; the movement, damage, and Lua animation APIs work independently of that choice.
