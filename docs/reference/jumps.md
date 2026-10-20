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
