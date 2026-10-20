# Crates: push and pull

How the Prince moves a `Crate` trap, recovered from his state machine in `data/pop3dreal.exe`.
The port is `update_crate` / `crate_input` in `src/main.cpp`.

## The states

The Prince's states are registered by name with their handlers (`sub_44EEB8(handler,
"StName")`). For crates:

| state | handler | role |
|---|---|---|
| `StStartCrateGrab` | (from `sub_4B0F1B`) | begin holding |
| `StIdleCrate` | `0x4D62B4` | holding, waiting for input |
| `StStartCrateMove` | `0x4D648C` | choose and play a move |

`StStartCrateMove` reads the motion vector at `PrinceObject +0x1D0 +0xF4..+0xFC`. It plays
`Push_M` when the vector is +1 and `Pull_M` when it is −1, but only if the move is possible (see
below). Otherwise it plays `PushFail_M` or `PullFail_M`. The other clips are `PushOrPullBase_M`
and `PushOrPullFinish_M`. These names come from the table at `off_74AB28..off_74AB3C`.

## One step, and when it is allowed (`0x4D1719`)

A crate moves one step of `flt_7C1F5C` = **1.5** units. The move is allowed when a probe finds
floor at the far end of the step, at the Prince's own height:

```
dist   = push ? 1.5*0.5 + 1.5 : 1.5*0.5 - 3.0        // +2.25 / -2.25 along his facing
probe  = feet + facing * dist
hit    = pick(mask 6, probe + up * 1.5, down, front-only)
ok     = hit && |hit.y - probe.y| <= 0.01             // dbl_743AD0
```

A wall or a higher floor at the destination puts the hit above the probe. A drop puts it below.
Either one fails the move.

## The clips carry the motion

The clips' root tracks agree with the binary's constant, so the Prince is moved by root motion
and the crate travels with his hands:

| clip | length | root motion |
|---|---:|---|
| `PushOrPullAntic_M` | 264 ms | +0.70 forward (step up to the crate) |
| `Push_M` | 2772 ms | **+1.50** |
| `Pull_M` | 2409 ms | **−1.50** |
| `PushFail_M` / `PullFail_M` | 2046 / 2508 ms | 0 |
| `PushOrPullFinish_M` | 462 ms | −0.70 (step back) |

## In prisonfix

`P1_chamber2_block04` is a Crate set into the north wall of the starting cell. The Prince
pushes it four steps down a hidden corridor (z 3.6 → 10.3). The fifth push fails at the
corridor's end. This is the way out of the cell. Later the `AngryGuardCinema` Area runs
`P1_chamber2_block04,visible:F;P1_chamber2_block04,collide:F`, which removes the block.

Reproduce with
`k2boy --boot --place 23.77,0.01,3.6,0 --fixeddt 0.05 --autopush --frames 450 --shot out.bmp data`.

## Not traced

How a crate is detected and grabbed (`sub_4B0F1B` around `0x4B1789`). The port grabs the
nearest Crate within 1.2 units ahead on the same floor, and squares the Prince up to the
crate's nearest axis. In play, E grabs and holds; W pushes, S pulls. It also skips the room test `sub_4117B7`, which follows the floor probe.
