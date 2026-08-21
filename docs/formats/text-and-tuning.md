# Text, tuning and configuration files

All of these are plain ASCII, all are shipped unencrypted, and several are **self-documenting** —
the developers left the format spec in comments. This is the cheapest information in the whole
project; read these before touching the disassembler.

Comment conventions vary by file: `;` (INI-style), `//` (C++-style), and in some files both.

---

## `data/GameText.txt` — localizable string table

18 199 bytes. Parsed by `UserTextFileParser`.

```
// English text strings.
// All strings from Prince of Persia that may potentially be seen by the end user
// must come from this file

// name of the program
// this string MUST be the first in this file
#textIDPrince_of_Persia_3D
"Prince of Persia 3D",

#textIDNone
"None",
#textIDSimple
"Simple",
#textIDComplex
"Complex",
```

Grammar:

```
directive := "#textID" <identifier>
value     := '"' <text> '"' ','
comment   := "//" <to end of line>
```

The identifier immediately follows `#textID` with **no separator**. Order matters for at least the
first entry ("this string MUST be the first in this file"), which implies the parser assigns
sequential IDs rather than hashing the name — so inserting a line renumbers everything after it.
Confirm before editing.

---

## `data/Traps.txt` — trap parameter reference (design doc)

4 881 bytes. Not read by the game; it is the design team's own documentation of every trap type's
parameter slots, left in the shipped build. It opens with:

> `NOTE: I don't know the details myself to all these; if we want this to be complete, we need
> others to fill it in... (Matt? Ben? Todd??)`

Structure: `<TrapType>:` followed by numbered parameter slots.

```
Almost all traps have the initial argument, but not all traps actually respect the fields:
	InitialState, ResetEnabled (t/f), Visible (t/f), Collideable (t/f)

Action:
	1	mPrinceReach
	2	mActionReach
	3	mPrinceUse
	4	mActionUse
	5	mDoIt
	6	mPrinceFinish
	7	mActionFinish

ArrowCharger:
	1	mArrowType
	2	mArrowDuration
	3	mOn
	types are: #("Fire", "Charm", "Swarm", "Possession", "Freeze",
	             "Exploding", "Death", "LifeStealer")

CollapsingFloor:
	1	Type actor
	2	Wobble skill
	3	Fall skill
	4	Crash skill

Door:
	1	Actor
	2	Transparent (t/f)
```

**Why this matters for RE:** the slot numbers are the parameter array indices the `Trap`
constructors read, and the `m`-prefixed names are the actual C++ member names. When you decompile
`Builder@Action::Build`, you can label its locals directly from this file. The `#("Fire",
"Charm", ...)` syntax is Piccolo array literal syntax, confirming trap parameters are authored in
script.

Traps documented (some with empty bodies — the "fill it in" TODO): `Abyss`, `Action`,
`ArrowCharger`, `ArrowTarget`, `AudioMessage`, `Bar`, `CollapsingFloor`, `Crate`, `CrateTarget`,
`DelayMessage`, `Door`, and more further down the file.

---

## `data/Geometry/{prince,Enemies/*}/master.dat` — character descriptor

**The single most valuable text file in the install.** Fully self-documenting; it explains the
character system's data model without any reversing.

First line is a command line: `-v 5 -n Prince // MASTER for loading prince`

| flag | meaning |
|---|---|
| `-v <n>` | format version (5 for prince, 4 for PG1) |
| `-n <name>` | body name |
| `-p` | load a **PROTO** file instead of an ACTOR |
| `-d <name>` | force a debug file to be created |

Then ordered required lines, then bracketed sections:

```
Geometry\prince\prince.k2a   ; FIRST: geometry (.GUY or .K2A)
Prince                       ; SECOND: actor directory name, or .ACT path if -p

[Attributes]                 ; THIRD: additive bitmask
;  CanWalk = 1   CanCrawl = 2   PreferToCrawl = 4   HasWeapon = 8
;  CanFight = 16 OnlyRunsAmbient = 32  UsesUpSword = 64  ImmuneToArrows = 128
CanWalk
CanCrawl
HasWeapon
CanFight
UsesUpSword

[Weapons]
RGrip	0.02, -0.07, 0.02    ; primary hand node + offset
LGrip	-0.05, 0.0, -0.02    ; alternate hand (may be the same)
4 = number of weapons
; per weapon: name, L,C,R ranges, counter L&R, special L,C,R, damage, style
PrinceSword&A1  2.0,2.0,2.0,2.0,2.0,2.1,2.0,1.9  2.0, 0   // normal sword
-B geometry\utils\SwordBlur_Prince1.tga 0.9 0.9 0.9 0.4 10
StaffR   2.1,2.0,2.1,2.1,2.1,2.0,0.0,2.0  1.0, 1          // staff
PrinceDoubleBlade  1.9,...  1.0, 2                        // double blades
LongBow 2.1,...  0.0, 3                                   // bow

[BodyParts]                  ; collision nodes, order is fixed
LGrip RGrip LHeel RHeel      ; hands & feet
LLeg2 RLeg2 LHand RHand      ; knees & elbows (crawling)
LToes RToes Head Pelvis      ; toes, head, hips

[Animations]                 ; exactly 2 required
CombatDrawWeaponBack_G2      - used to draw the weapon
CombatStoreWeaponBack_G2     - used to sheath the weapon

[GrabPoints]                 ; 2 floats: yOffset, xzOffset
-1.9513	; WRONG VALUES!
-0.0611	; WRONG VALUES!

[ArcheryLimits]              ; optional section, must be last
50                           ; mMaxRange (meters)
10 5 25                      ; mAimSpeed mAimAcceleration mAimSpeedLimit
-35 0 60                     ; mMinVertical mInitialVertical mMaxVertical (max is down)
-35 0 35                     ; mMinHorizontal mInitialHorizontal mMaxHorizontal
```

Weapon **style** codes: `0` one-handed, `1` double-weapon, `2` staff, `3` bow.
Weapon geometry indices documented in comments: swords `12,13,2`; staff `4`; spear `0,10,18`;
axe `7,14,15`; little double blade `18`; big double blade `17`; bows `19–23` =
`"Arrow","LongBow","ShortBow","Reticle","ZanAxe"`.

`-B` blur parameters: `texture weaponLength tipOpacity baseOpacity fallOff uvRepeat`
(the comment says fallOff ≈ 0.1–0.2 and uv-repeat ≈ 20 work well — the shipped prince sword uses
fallOff 0.4, uvRepeat 10).

The `; WRONG VALUES!` annotation on `[GrabPoints]` is a shipped known bug. The comment block above
it derives the intended formula:

```
GoalRoot = GrabPt + (GrabPelvis - GrabNode) + (JumpRoot - JumpPelvis)
```

Enemy roster from `data/Geometry/Enemies/` (30 dirs): `A1 A2 A2ARCHER A3 A4 BELLY BM1 BM2 BM3 D1 D2
D2Possess D4 D4Archer DC GOLDPAW GREASE ManRat MEGAMUCK MUCKCREATURE PG1 PG2 PG3 PG3ARCHER PG4
RUGNOR W1 W2 W3 Zan`.

---

## `data/fight.ini` — combat timing table

13 332 bytes. `[Fight Params]` section, `Key = float` pairs. Header comment:

> `all times are multiples of 0.03333333 (1/30) and carried to 7 significant digits`

i.e. everything is a frame count at 30 fps expressed in seconds.

```ini
[Fight Params]
Sword Left Attack Begin = 0.3
Sword Center Attack Begin = 0.4
Sword Right Attack Begin = 0.2666667
Blades Left Attack Begin = 0.2666667
Staff Right Attack Begin = 0.4333333
Super Attack Begin = 1.233333
Left Spin Attack Begin = 0.6
Sword Left Counter Attack Begin = 0.4666667
Super Counter Attack = 0.2
Sword Blocked Left Attack Recover = 0.3666667
```

Key naming is `<Weapon> <Direction> <Phase>` where Weapon ∈ {Sword, Blades, Staff}, Direction ∈
{Left, Center, Right}, Phase ∈ {Attack Begin, Counter Attack Begin, Blocked … Attack Recover, …}.
Keys contain spaces, so the parser splits on `=` only. Consumed by `WeaponParameters` /
`CharacterStateTable`.

---

## `data/enemy.dat` — AI distance tuning

2 120 bytes. Format is **value-first**, with the original C++ line preserved as a comment:

```
25	 distFarFromAnywhere = 20.0f;		// disappear if you want
10	 distFarFromHome = 8.0f;		// come back home!!
12	 distFarFromEnemy = 10.0f;		// watch enemies from here
10	 distApproachEnemy = 5.0f;		// charge within this distance

; relative to shortest attack range
0.15	= distFightWithEnemy = 0.15f;		// stop and fight here
; relative to longest attack range
1.1	= distTooCloseToEnemy = 0.6f;		// back off if you're this close

1.5	distCamoflauge - how far can he see when you're in camo?
15	swing-while-running damage
30	peripheral vision angle
```

Note the leading number is the **live** value; the `= 20.0f;` text is a stale comment showing what
the C++ default used to be. Do not confuse the two. The file continues with an archer section:
arrow speed, damage levels, magic dropoff rate, then a count of difficulty levels followed by one
row per level.

---

## `data/camera.dat` — camera configuration

5 089 bytes, versioned via a leading command line: `-v 2  /// camera data`

```
; Version 2 dat file: put in five spring values:
;	MF_CAMERA_ORBIT_ELEV	50.0f
;	MF_CAMERA_ORBIT_Y	30.0f
;	MF_CAMERA_PAN_X		50.0f
;	MF_CAMERA_PAN_Y		50.0f
;	MF_CAMERA_DISTANCE	10.0f
5
5
8
30
10

; specify cameras from [0] to [8], [X] = death, [F] = fight, [D] = default, [L] = look
;	format:
;		[index]
;		count
;		x,y,z	[lx,ly,lz]   // one line per 'count'
```

The five spring constants are followed by up to nine indexed camera definitions plus four named
ones (`[X]` death, `[F]` fight, `[D]` default, `[L]` look). Each is a count followed by that many
position (+ optional look-at) triples — i.e. a camera spline. Parsed by `CameraFileParser`;
evaluated by `CameraLogic` / `CameraStateTable` / `MfSplineCameraActivity`.

Again note the shipped values differ from the documented defaults (5, 5, 8, 30, 10 vs the
50/30/50/50/10 in the comment).

---

## Command-line `.dat` files

`data/cmdline.dat` (used in dev builds) and `data/distribution.dat` (used in retail) are argv files read by
`CommandLineParser`. `;` starts a comment.

```
; distribution.dat
-x
;-f0
;-lx-
-warn 1
-h
;-s8
-zp
-b 24,24,24
-i menu.dat
-cmd console.dat
```

**Important:** the parser matches only the **first character** after `-`; the rest of each token
is consumed by that character's handler. So `-cmd` is flag `c`, `-warn` is flag `w`, `-zp` is
flag `z`. The table below lists the tokens as they appear in the files, not as distinct flags.
See [`../reference/command-line.md`](../reference/command-line.md) for the real dispatch table.

Flags seen (meanings unconfirmed — a Phase 2 target):

| flag | in cmdline.dat | in distribution.dat | guess |
|---|---|---|---|
| `-x` | yes | yes | — |
| `-f0` / `-f1` / `-f2` | `-f1` | commented `-f0` | fullscreen mode / frame limiter |
| `-h` | yes | yes | — |
| `-zp` | yes | yes | z-buffer precision? |
| `-b 24,24,24` | yes | yes | bit depths (colour, z, stencil?) |
| `-warn 1` | — | yes | warning level |
| `-i <file>` | `data/menu.dat` | `data/menu.dat` | init/menu params file |
| `-cmd <file>` | `data/console.dat` | `data/console.dat` | console command script |
| `-s1` / `-s8` | commented | commented | start level / skill? |
| `-xmtn`, `-lx-` | commented | commented | — |

Also present in `master.dat`: `-v`, `-n`, `-p`, `-d`, `-B`. And in the savegame, the level compiler
line `prisonfix Test -a 2 -n prisonfix.nbr -b -v3.0 -k -w -t`.

`data/console.dat` is a one-line script: `minrate 15`. **The string `minrate` does not occur
anywhere in `data/pop3dreal.exe`**, so this file is inert in the shipped build — it is a leftover
from another tool or an earlier revision. There is no native console command table; the console
API is reached from Piccolo script (`Console.Print`, `Console.PrintNl`, `Console.Clear`). See
[`../reference/command-line.md`](../reference/command-line.md).

`data/menu.dat` is `5 3 1` — three integers, format unknown (`sMenuModuleClass`).

`data/config.dat` is fall-damage tuning, with a comment that betrays it is a temporary override:

```
// Values need to be rolled into CONFIG.ENT!
// Get blanks from CONFIG.ENT as well...
// Fooling with fall values to be not so harsh initially....
FallConstDamage = 6
FallLinearDamage = .5
FallSquaredDamage = 1.5
```

`CONFIG.ENT` is referenced but not present — another CD-side or dev-side file.

`data/mohawk.w32` is INI-syntax audio driver config (Broderbund's Mohawk engine heritage):

```ini
[WaveMix]
fEnable = TRUE
ulFrameSize=16

[DirectSound]
fEnable = TRUE
```

The string `\\.\MOHAWK95.VXD` in the binary confirms a Win95 VxD path for the WaveMix mixer.
