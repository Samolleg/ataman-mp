# Object property terminology

This is the English UI specification for MOB object properties. It covers the
values currently exposed by Endurance and confirmed additions from the Object
Editor manuals and runtime audit.

## Tooltip rules

- Describe only confirmed behavior that helps the user edit the value.
- Use plain language and no engine implementation terminology.
- Keep tooltips to one or two short sentences.
- Do not speculate about historical intent or unknown behavior.
- If useful behavior is not confirmed, retain the field and omit its tooltip.
- Display-name corrections never change serialized tags, enum values, database
  keys, scripts, prototypes, model names, textures, or other resource names.

## Common world-object values

These apply to scenery, units, levers, traps, and torches unless noted.

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Map ID | Unsigned integer | Exposed | Object identifier used by the map and scripts. |
| Object type | Numeric object type | Exposed read-only for ordinary world objects | Determines how the game handles this object. |
| Player group | `0`-`31` | Exposed for units and traps only | Ownership and diplomacy group used by this object. |
| Name | Text | Exposed | Name stored with this object in the MOB. |
| Model | Model resource name | Exposed | Model used to render this object. |
| Model parts | Ordered list of part names | Exposed except on traps | Model parts stored for this object. WARNING: An empty list enables every model part; incompatible combinations can crash the game, especially on units. |
| Parent template | Text | Exposed | Preserved object-template value. Normal units use Database prototype instead. |
| Primary texture | Texture resource name | Exposed | Texture used by the model's primary texture set. |
| Secondary texture | Texture resource name | Exposed | Texture used by the model's second texture set, when the model has one. |
| Position | X, Y, Z | Exposed | Object position on the map. |
| Rotation | X, Y, Z editor angles; quaternion in MOB | Exposed | Object orientation. |
| Scale | X, Y, Z | Exposed | Object size along each axis. `1, 1, 1` is the model's original size. |
| Available to scripts | On/off | Exposed | Keep this object available to scripts that modify it during play. |
| Cast shadow | On/off | Exposed | Allow this object to cast a shadow. |
| Parent ID | Signed integer | Exposed | — |
| Attached quest variable | Text | Exposed | Zone-local quest variable. Value `1` shows the active quest marker. |
| Comments | Text | Exposed | Editor notes. The game does not use this text. |

`Parent ID` remains visible and preserved, but receives no behavioral tooltip
until its runtime meaning is confirmed.

## Units

Units also contain all applicable common world-object values.

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Unit category | `50` Human, `51` Animal, `52` Monster | Exposed as Human (50), Animal (51), or Monster (52) | Serialized unit category. |
| Use MOB unit values | On/off | Exposed | On uses the equipment, inventory, spells, and parameters stored in this MOB. Off uses the Database prototype. |
| Database prototype | Unit database name | Exposed | Unit definition used by the game. |
| Equipped armor | Ordered item list | Exposed | Armor stored directly in this MOB. Used when Use MOB unit values is on. |
| Equipped weapons | Ordered item list | Exposed | Weapons stored directly in this MOB. Used when Use MOB unit values is on. |
| Spells | Ordered spell list | Exposed | Spells stored directly in this MOB. Used when Use MOB unit values is on. |
| Quick items | Ordered item list | Exposed | Quick-use items stored directly in this MOB. Used when Use MOB unit values is on. |
| Quest items | Ordered item list | Exposed | Quest inventory stored directly in this MOB. Used when Use MOB unit values is on. |
| Unit parameters | 51 serialized values | Exposed in a separate editor | Unit parameters stored directly in this MOB. Used when Use MOB unit values is on. |

The internal enum name `Monstr` may be displayed as `Monster`; the stored value
remains `52`. The individual 51-value parameter labels must not be renamed until
their exact serialized ordering has been verified.

## Unit logic slots

Each unit has five serialized logic slots.

Endurance exposes all five slots as Logic 0 through Logic 4 through the selector
shown throughout Behavior mode. Logic 0 is the default setup;
Logic 1 through Logic 4 correspond directly to world alarms 1 through 4.
Property editing, visualization, patrol/look-point editing,
and undo/redo follow the selected slot.

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Logic enabled | On/off | Exposed for the selected slot | For Logic 1-4, allow this setup to activate when its corresponding world alarm is raised. Logic 0 is the default enabled slot and ignores this setting. |
| Behaviour | See values below | Exposed for the selected slot | Controls how the unit moves and guards while this logic is active. |
| Call-for-help radius | Distance | Exposed for the selected slot | Distance within which allied units can respond for help. |
| Guard radius | Distance | Exposed for Guard area in the selected slot | Area the unit guards around the guard position. |
| Guard position | X, Y, Z | Exposed for the selected slot | Center used by Guard area. Alarm guard uses the active alarm coordinates instead. |
| Always active | On/off | Exposed for the selected slot | Keep this unit's AI running regardless of distance. Otherwise it activates within twice the player's maximum vision range plus 20 metres. |
| Aggression mode | See values below | Exposed for the selected slot | Controls how the unit reacts to enemies. |
| Alarm condition | `0` Enemy seen, `1` Attacked, `2` Suspicion | Exposed for the selected slot | Event that makes this unit raise its configured alarm. |
| Patrol route repeats | On/off | Exposed for the selected slot | On returns from the last patrol point to the first. Off follows the route back in reverse. |
| Guard idle time | Float | Exposed for the selected slot | — |
| Alarm raised | Unsigned integer | Exposed for the selected slot | World alarm this unit raises when its alarm condition occurs. `0` raises no alarm. |

Behaviour values:

| Stored value | Display value | Meaning |
|---:|---|---|
| `0` | Stay | Remain in place and look in one direction. |
| `1` | Guard area | Guard the configured area. |
| `2` | Patrol | Follow the configured patrol points. |
| `3` | Sentry | Remain in place and look around. |
| `4` | Player/script controlled | Controlled by the player or map script. |
| `5` | Alarm guard | Guard the area where the alarm was raised. |

Aggression values:

| Stored value | Display value | Meaning |
|---:|---|---|
| `0` | Attack | Attacks enemies. |
| `1` | Retaliate | Attacks in response to aggression. |
| `2` | Flee enemies | Avoids enemies. |
| `3` | Flee enemies and player | Avoids enemies and the player's party. |

Display corrections such as `Briffing` to `Player/script controlled`,
`Guard Alaram` to `Alarm guard`, and `Agression` to `Aggression` are visual
only.

The game stores the Guard idle time value but the audited runtime logic does not
use it. Keep it preserved and omit its user-facing tooltip.

## Patrol and look points

| Category | UI label | Value | Current property UI | Tooltip |
|---|---|---|---|---|
| Patrol point | Position | X, Y, Z | Exposed for the selected slot | Position the unit travels to on this patrol route. |
| Look point | Look-at position | X, Y, Z | Exposed for the selected slot | Position the unit faces while waiting at the patrol point. |
| Look point | Wait time | Game ticks; `15` = 1 second | Exposed for the selected slot | Time spent looking toward this point. |
| Look point | Turn speed | Unsigned 32-bit speed | Exposed for the selected slot | How quickly the unit turns toward this point. `0` turns immediately. |
| Look point | Action flags | Bitmask | Exposed for the selected slot | `0` uses normal behaviour. `1` rests at the point. |

`Turn speed` is serialized as an unsigned 32-bit value.

## Levers and doors

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Allowed interaction | See values below | Exposed as Type open | Ways the player may operate this lever. |
| Required key number | Unsigned integer | Exposed as Key ID | Key number required when Key is an allowed interaction. |
| Required Science skill | Unsigned integer | Exposed as Hands skill | Minimum Science skill required for manual interaction. |
| Current state | State index | Exposed | State active when the map loads. |
| Number of states | Positive integer | Exposed as Total state | Number of states available to this lever. |
| Repeatable | On/off | Exposed as Is cycled? | Allow the lever to be operated repeatedly. |
| Door interaction | On/off | Exposed as Is door? | Treat this lever as a door approached from its usable sides. |
| Recalculate navigation graph | On/off | Exposed as Is recalc graph? | Update AI navigation when this object's state changes. Enable it when states change passability. |

Allowed-interaction values:

| Stored value | Display value |
|---:|---|
| `0` | Disabled |
| `1` | Hand |
| `2` | Picklock |
| `4` | Crowbar |
| `5` | Hand or crowbar |
| `8` | Key |
| `9` | Hand or key |

These are stored as a bitmask. Unrecognized combinations must remain visible as
their raw numeric value.

## Magic traps

Magic traps also use the common `Player group`. During normal map loading, the
game takes that player's row from the containing MOB's 32-player diplomacy
table and creates a target bit for every group marked `War`. This calculated
mask replaces the serialized `MT_DIPLOMACY` value; the stored value is neither
an override nor an additional filter.

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Will attack (derived) | Player-group names and numbers | Read-only in Logic mode | Groups marked War in this trap's Player group row of the containing MOB's diplomacy table. This is the target mask the game calculates when the map loads. |
| Stored target mask (legacy) | 32-bit player-group bitmask | Preserved but omitted from the normal property UI | Serialized mask replaced during normal map loading. |
| Spell | Spell expression | Exposed in Logic mode | Spell cast when the trap activates. |
| Cast interval | Game ticks; `15` = 1 second | Exposed in Logic mode | Delay before the trap can cast again. |
| Nearest unit only | On/off | Exposed in Logic mode as Is cast once? | For unit-targeted spells, affect only the nearest eligible unit. Off affects all eligible units. |
| Activation area center | X, Y | Exposed through activation-zone selection in Logic mode | Center of an area checked for eligible hostile units. |
| Activation radius | Distance | Exposed through activation-zone selection in Logic mode | Radius checked around this activation center. |
| Spell target point | X, Y | Exposed through cast-point selection in Logic mode | Position targeted by a ground or point spell. |

Activation areas decide which units trigger or receive a unit-targeted spell.
Explicit spell target points are destinations for ground/point spells. Trap area
and target arrays store X/Y only; a separately editable Z value would not be
serialized.

## Torches

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Light and effect offset | X, Y, Z | Exposed as Torch point | Position of the torch's light and particle effect relative to the object. |
| Light power | Float | Exposed as Torch power | Strength of the torch light and effect. |
| Sound | Sound resource | Exposed as Torch sound | Sound played by this torch. |

## Point lights

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Map ID | Unsigned integer | Exposed | Light identifier stored in the MOB. |
| Position | X, Y, Z | Exposed | Light position on the map. |
| Cast shadows | On/off | Exposed | Allow this light to cast shadows. |
| Color | Red, green, blue; `0`-`1` | Exposed | Light color. |
| Radius | Float | Exposed as Range | Distance affected by the light. Negative values darken instead. |
| Name | Text | Exposed | Name stored with this light in the MOB. |
| Comments | Text | Exposed | Editor notes. The game does not use this text. |

## Sounds

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Map ID | Unsigned integer | Exposed | Sound identifier stored in the MOB. |
| Position | X, Y, Z | Exposed | Sound position on the map. |
| Inner radius | Distance | Exposed as Range | Area where the sound plays at full volume. |
| Outer radius | Distance | Exposed as Sound distance | Distance where the sound fades to silence. |
| Sound files | Ordered resource list | Exposed as Sound filepath | Sound resources used by this source. |
| Minimum delay | Game ticks; `15` = 1 second | Exposed as Sound min | Shortest delay between ambient playbacks. |
| Maximum delay | Game ticks; `15` = 1 second | Exposed as Sound max | Longest delay between ambient playbacks. |
| Ambient | On/off | Exposed | Play this source repeatedly using its delay range. |
| Music | On/off | Exposed | Treat this source as music. |
| Name | Text | Exposed | Name stored with this sound in the MOB. |
| Comments | Text | Exposed | Editor notes. The game does not use this text. |

The optional `SOUND_VOLUME` record remains preserved but receives no editable
label or tooltip until its behavior is confirmed.

## Particles

| UI label | Value | Current property UI | Tooltip |
|---|---|---|---|
| Map ID | Unsigned integer | Exposed | Particle-source identifier stored in the MOB. |
| Position | X, Y, Z | Exposed | Particle emitter position on the map. |
| Particle type | `8192`-`8274`, with raw fallback | Exposed | Effect generated by this particle source. |
| Scale | Float | Exposed | Size multiplier for the particle effect. |
| Name | Text | Exposed | Name stored with this particle source in the MOB. |
| Comments | Text | Exposed | Editor notes. The game does not use this text. |

The complete named particle-value inventory and reconstruction status is kept in
`docs/PARTICLE_RENDERING_STATUS.md`. Internal spellings in particle identifiers
remain unchanged.
