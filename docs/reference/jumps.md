# Jumps and the tuning block

How the Prince jumps, recovered from `data/pop3dreal.exe`. The port is `jump_start` and
`jump_update` in `src/main.cpp`.

## States

| state | handler | role |
|---|---|---|
| `StStartStandJump` | `0x4B689D` | starts the jump |
| `StIdleJump` | `0x4B6976` | waits for "preparing done"; on hitting a wall, returns to standing |
| `StContinueJump` | `0x4B6A0F` | picks the direction from the input relative to the Prince and plays its antic |
| `StStartActualJump` | `0x4B6C40` | the launch: velocity, airborne clip |

| input | antic | airborne | landing |
|---|---|---|---|
| none | `JumpUpAntic_G` | `JumpingUp_G` | `LandVertSafe_G` |
| forward | `JumpForwardAntic_G` | `JumpingForward_G` | `LandJumpForward_G` |
| back | `JumpBackAntic_G` | `JumpingBack_G` | `LandBackSafe_G` |
| left / right | `JumpLeftAntic_G` / `JumpRightAntic_G` | `JumpingLeft_G` / `JumpingRight_G` | `LandLeftSafe_G` / `LandRightSafe_G` |
| running | `RunJumpAnticLeft_L` | `RunJumpingLeft_G` | `RunJumpLand_L` |

The clips' root tracks have **no vertical motion**: the flight is ballistic in code, and the
clips only pose the body.

## The tuning block (`dword_801C2C`)

Defaults are set in the constructor `0x4F8547`. Keys are parsed by `0x4F8667`, which reads
`config.dat` and would read `CONFIG.ENT`. This install has no `CONFIG.ENT`, and `config.dat`
sets only the fall-damage values.

| offset | key | default |
|---|---|---:|
| `+0x24` | MinFallHeight | 6.25 |
| `+0x28` | FallDeathHeight | 12.0 |
| `+0x2C` | FallConstDamage | 6.0 |
| `+0x30` | FallLinearDamage | 0.5 |
| `+0x34` | FallSquaredDamage | 1.5 |
| `+0x38` | FallGravity | **−17.0** |
| `+0x3C` | fWalkTurnRadius | 1.0 |
| `+0x40` | fRunTurnRadius | 1.75 |
| `+0x44` / `+0x48` | fRunJumpY / Z | 5.1 / 7.2 |
| `+0x4C` / `+0x50` | fStandJumpForwardY / Z | 2.75 / 5.7 |
| `+0x54` / `+0x58` | fStandJumpUpY / Z | 8.7 / 0.07 |
| `+0x5C` / `+0x60` | fStandJumpBackY / Z | 3.75 / −4.2 |
| `+0x64` / `+0x68` | fStandJumpSideY / Z | 3.28 / 3.28 |
| `+0x6C` | fMinRunClimbHeight | 0.3 |
| `+0x70` | fMaxRunNormalHeight | 0.3 |
| `+0x74` | fMaxRunHeightDiff | 0.2 |

The launch velocity is forward × Z + up × Y. Side jumps use up × Y ± side × Z. So a jump straight
up peaks at 8.7² / (2·17) ≈ 2.2 units, and a running jump covers about 4.3 units.

Measured in the port, from the cell floor: a jump up reaches y 2.01 at 1.2 s and lands at 1.75 s.
A forward jump while walking crosses about 1.8 units.

## Not traced

How the original detects the landing: it uses a predictive planner (`StStartPredictiveStandJump`,
`StIdlePredictJump`). In the port, the floor probe ends the flight. Ledge grabs, hanging and
fall damage are also not traced.

## Ledges

From the binary: `StStart1MClimb` (`0x4B46C2`) chooses the climb by the ledge's height above
the Prince:

| height | clip |
|---|---|
| > 1.75 (`flt_7461A4`) | `HopUpOnto2m_M` |
| > 1.3 (`flt_74AB68`) | `HopUpOnto1point5m_M` |
| > 0.9 (`flt_743F9C`) | `HopUpOnto1m_M` |
| otherwise | `HopUpOntoSmall_M` |

The climb and hang clips carry no vertical root motion either: the engine moves the body.
`"Reachable Ledge YAHOO! Start Hanging!"` (`0x4B4CC8`) gates the hang through two probes
(`0x4CF65C`, `0x4D0980`). They cast with mask 6 from points near the hands, using offsets 0.17 and
0.37 ahead, 0.15 up, and a height tolerance of 1.525. Those probes are not fully traced.

The port finds a ledge with the pick primitive: a downward cast a little ahead finds an
up-facing top, a horizontal cast finds its face (and the yaw to square up), and an upward cast
from the top checks for headroom. The hang pose is placed from the skeleton:
`LedgeHangBase_M` is posed at t = 0, and its `LGrip` / `RGrip` joints (1.83 above the root for
the Prince) are put on the edge.

| action | how |
|---|---|
| climb | jump facing a ledge 0.3–2.25 above the feet |
| catch | jump under a ledge within grip reach of the jump's peak (stand-jump up peaks at 2.2, so about 4 units), or reach it in any jump |
| hang | `LedgeHangBase_M`. Forward or jump pulls up (`LedgePullUpOnto_M1`, or `LedgeHangPullUpFail_M` without headroom), back drops, left/right shimmy 0.6 per step (the clips' root motion) while the ledge continues |

Measured in prisonfix region 0:

- At (−69.07, 20.25, 26.25), a 1.40 ledge is climbed with `HopUpOnto1point5m_M` and the Prince
  ends on top at 21.65.
- At (−66.07, 20.25, 29.25), a jump catches a beam at 24.23. The Prince hangs at root height
  22.40, pulls up to 24.23, and walks on along it. Holding left shimmies him from x −66.07 to
  −64.67.

`--findledges` lists standing spots that face a climbable ledge.
