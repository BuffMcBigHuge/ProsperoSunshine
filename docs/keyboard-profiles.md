# Keyboard and mouse profiles

A device without a controller plays with its keyboard and mouse as a controller of its own
([README](../README.md#keyboard-and-mouse-as-a-controller)). How the keys and mouse map depends on the profile. Everything
here is optional: without any setting, the `standard` profile applies and nothing else changes. The profiles are for
those who want them, and for now they live in `sunshine.conf`, not in the app.

## Why these keys

The profiles follow the keyboard layouts that PC games of each kind settled on, so someone who already plays with keyboard
and mouse doesn't have to learn anything new: W A S D move, the mouse turns the camera, the mouse buttons are the
triggers, Space jumps, Shift runs or dodges, Ctrl crouches or walks, R reloads, Esc pauses, Tab opens the map. Where a
profile differs from `standard`, it is because games of that kind differ on a PC too: racing games put throttle and brake
on W and S, fighting games want exact diagonals on the D-pad, 2D games are played with the arrows and Z X C.

They were chosen from the PC controls of the PS4 and PS5 games of each kind (Gran Turismo 7 and F1 by way of their PC
peers, Street Fighter 6 and Tekken 8, God of War, Elden Ring, Horizon, Call of Duty, Destiny 2, Helldivers 2, Hollow
Knight, EA FC and NBA 2K) and from the mappers people use for them (Steam Input, reWASD). No profile uses Alt, so none can
form Moonlight's own shortcuts (Ctrl+Alt+Shift and a key), and the key right of the space bar is for menus, below.

## Every profile

| Keys | Controller |
|---|---|
| Enter | ✕ |
| Backspace | ○ |
| Esc | Options (pause) |
| Tab | touchpad (the map or inventory in most games); L1 in `platformer` |
| F1 / F2 | PS / Create, the host only unless `ps_button_all_players = 1` |

A key gives full deflection, two keys on one stick a diagonal, and opposite keys cancel out, also on the D-pad.

**Mouse sensitivity:** Ctrl+Alt and the wheel turn it up or down a step while you play. Nothing shows on the console,
since everyone streaming would see it: turn until the camera feels right. A device starts at 100 %, with 24 steps either
side, from 10 % to 1000 % of the base speed, each about 21 % faster than the last, so a step feels the same anywhere. The
host keeps it per device and profile in `keyboard_sensitivity.txt` (a device is a hash of its pairing, not its address),
so a laptop can keep shooters faster than action games. `keyboard_mouse_sensitivity` (in percent) in `sunshine.conf` moves
where a new device starts. A game's own dead zone and top turning speed still apply: once the stick is at full, the camera
turns no faster (GTA III and Elden Ring cap it), so raise the game's own camera sensitivity first.

**Menus:** the arrows, Enter, Backspace and Esc work the same in every profile, as in the menus of PC games, so a game's
own menus are walked the same way whatever the profile. While you hold **the key right of the space bar** (right Alt or
AltGr, the right ⌘ on a Mac, 変換 or 한/영 on Japanese and Korean keyboards), every profile gives the menu keys: `standard`'s,
with W A S D on the D-pad, as some menus (the console's own among them) ignore the stick. Letting go brings the profile
back; keys held across it have to be pressed again.

## The profiles

`keyboard_mouse_profile` in `sunshine.conf` picks one; `standard` unless set.

### standard: menus and most games

| Keys | Controller | | Mouse | Controller |
|---|---|---|---|---|
| W A S D | left stick | | movement | right stick |
| I J K L | right stick | | left button | R2 |
| arrows | D-pad | | right button | L2 |
| Space | ✕ | | middle button | R3 |
| Q / E | □ / △ | | back / forward | L1 / R1 |
| Z / C | L1 / R1 | | wheel | D-pad taps |
| 1 / 3 | L2 / R2 | | | |
| F / G | L3 / R3 | | | |
| T | touchpad | | | |

### action: third-person action, open worlds, souls-likes (God of War, Elden Ring, Horizon)

| Keys | Controller | | Mouse | Controller |
|---|---|---|---|---|
| W A S D | left stick | | movement | right stick |
| Ctrl (hold) | walk: half the left stick | | left button | R1 (attack) |
| I J K L | right stick | | right button | L1 (guard) |
| arrows | D-pad | | middle button | R3 (lock on) |
| Space | ✕ (jump) | | back / forward | L2 / R2 |
| Shift | ○ (dodge, run) | | wheel | D-pad taps |
| Q or R | □ (item) | | | |
| E | △ | | | |
| Z / C | L1 / R1 | | | |
| 1 / 3 | L2 / R2 | | | |
| F / G | L3 / R3 | | | |

### shooter: shooters (Call of Duty, Battlefield, Destiny 2, Apex, Far Cry 6, Cyberpunk)

| Keys | Controller | | Mouse | Controller |
|---|---|---|---|---|
| W A S D | left stick | | movement | right stick (aim) |
| I J K L | right stick | | left button | R2 (fire) |
| arrows | D-pad | | right button | L2 (aim) |
| Space | ✕ (jump) | | middle button | R3 |
| C or Ctrl | ○ (crouch) | | back / forward | L1 / R1 |
| E, F or R | □ (interact, reload) | | wheel | △ taps (weapon) |
| Q / G | L1 / R1 (grenade, tactical) | | wheel tilt | D-pad left / right |
| Shift | L3 (run) | | | |
| V | R3 (melee) | | | |
| 1 / 3 | L2 / R2 | | | |

These games share one layout on PlayStation: L3 runs, ✕ jumps, ○ crouches, □ interacts and reloads, △ cycles weapons.
Rockstar's open worlds (GTA V, Red Dead Redemption 2, the GTA Trilogy) use another one: ✕ runs, □ jumps, △ gets in and
out of vehicles, ○ reloads, L1 changes weapon. In those `shooter` gets the vehicle layer right, but on foot Space sprints
and R jumps.

### driving: racing (Gran Turismo 7, F1, Need for Speed)

| Keys | Controller | | Mouse | Controller |
|---|---|---|---|---|
| W / S | R2 / L2: throttle / brake | | movement | nothing |
| A / D | left stick: steering | | left button | R2 |
| Ctrl (hold) | half throttle, brake and steering | | right button | L2 |
| Space | ○ (handbrake) | | middle button | R3 |
| Shift | △ (nitro, DRS) | | back / forward | L1 / R1 |
| E / Q | ✕ / □ (gears) | | wheel | D-pad taps |
| F or Z | L1 (look back) | | | |
| R or C | R1 | | | |
| I J K L | right stick | | | |
| arrows | D-pad | | | |
| 1 / 3 | L2 / R2 | | | |

W, S, A and D (and 1 and 3) ease in: they start at 5 % and reach full in 0.2 s, and let go at once, so the wheels don't
spin and the car doesn't jerk as with a key at full straight away.

### fighting: fighting games (Street Fighter 6, Tekken 8, Mortal Kombat 1)

| Keys | Controller |
|---|---|
| W A S D, Space (up) | D-pad: exact diagonals, opposite sides neutral |
| arrows | D-pad |
| U / I / O | □ / △ / R1 |
| J / K / L | ✕ / ○ / R2 |
| P / ; | L1 / L2 |
| mouse | nothing |

U I O / J K L is Tekken 8's layout on a PC; in Street Fighter 6's classic controls the third column is the heavy attacks.

### platformer: 2D platformers and metroidvanias (Hollow Knight, Celeste)

| Keys | Controller |
|---|---|
| arrows | left stick |
| Z / X / C | ✕ / □ / R2 |
| A / S / D | ○ / L2 / △ |
| E | R1 |
| Tab | L1 |
| I | touchpad |
| mouse | nothing |

This is Hollow Knight's keyboard layout on a PC, on its PS4 buttons.

### sports: football and basketball (EA FC, NBA 2K)

| Keys | Controller |
|---|---|
| W A S D | left stick |
| I J K L or numeric keypad 8 4 6 2 | right stick (skill moves, Pro Stick) |
| arrows | D-pad (tactics) |
| Space | ✕ (pass) |
| F or Num 5 | □ (shoot) |
| R / E | △ / ○ |
| Shift | R2 (sprint) |
| Ctrl | L2 |
| Q or Z / C | L1 (switch player) / R1 |
| mouse | nothing |

## Vehicles: Caps Lock

In `standard`, `action` and `shooter`, Caps Lock switches a vehicle layer on and off, for games where you walk and
drive. Its light on your keyboard shows when it is on. Only these keys change, easing in as in `driving`:

| Keys | Controller |
|---|---|
| W / S | R2 / L2: throttle / brake |
| A / D | left stick: steering |
| Space | R1 (handbrake, as in GTA) |
| Ctrl (hold) | half throttle, brake and steering |

Every other key and the mouse stay as on foot, so getting out, shooting from the car and the camera are where they were.
The host can't see whether you are in a car, so switch it yourself; if the light is the wrong way round, press Caps Lock
twice. Throttle on R2, brake on L2 and steering on the left stick hold in GTA V and the GTA Trilogy (Modern controls),
Cyberpunk 2077, Far Cry 5 and 6, Watch Dogs, Days Gone, Death Stranding and Fortnite; the handbrake differs (○ in Far Cry 6
and Days Gone, L1 in Fortnite). Leave the layer off for horses (Red Dead
Redemption 2, Ghost of Tsushima, Horizon's mounts, Assassin's Creed): they ride like walking. Planes and helicopters have no
layer of their own.
