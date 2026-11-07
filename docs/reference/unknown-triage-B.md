# Unknown function triage B

Generated from pseudocode skims, documentation greps, and address ranges. Entries not listed in the overrides use a range-based default label, so treat those as low-confidence. Status: documented = address appears in docs/reference/*.md; NEW = not found in docs or src; library/skip = CRT, STL, debug overlay.

## NEW, high importance

- `0049BEFA`: K2LocoActivity update (LocoActivity); creates Mofac motion objects, large state step
- `0049CA96`: K2LocoActivity contact handling (K2locoContactN...)
- `004B0B3B`: JumpingUp state with PredictJumpAbort check
- `004B403F`: Ledge hang end-move and moving states (LedgeHangEndMove, LedgeHangMoving)
- `004B485D`: Climbing / Ledge pull-up / StIdleClimb / StStartClimb states
- `004BC51A`: StChargePrince: guard charges the Prince
- `004F8AAA`: Loads gameplay tuning keys: MinFallHeight, FallDeathHeight, FallConstDamage/Linear/Squared, FallGravity, F*TurnRadius, F*Jump Y/Z, Spurt*, EnemyVisibleDist, ArrowVelocity, FHorizLaunch
- `00501890`: Large force/collision solver with sqrt/fabs
- `00502C54`: Very large force/collision solver (14 KB)

## Table

| Address | Chars | Subsystem | Purpose | Status | Importance |
|---|---|---|---|---|---|
| `00499C88` | 2749 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049A529` | 8713 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049AEB3` | 2008 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049B00A` | 3392 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049B280` | 5207 | locomotion | K2LocoActivity::ChooseNextStep (NextSkillIsSF), picks the next step before each step | documented (locomotion.md) | med |
| `0049B5E2` | 2076 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049BD92` | 2208 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049BEFA` | 9065 | locomotion | K2LocoActivity update (LocoActivity); creates Mofac motion objects, large state step | NEW | high |
| `0049C413` | 3987 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049C68B` | 3502 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049C91E` | 2076 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049CA96` | 4166 | locomotion | K2LocoActivity contact handling (K2locoContactN...) | NEW | high |
| `0049D89C` | 3057 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049DFDB` | 8888 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049E707` | 3365 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049E94F` | 4245 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `0049FC68` | 5347 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A01C6` | 9727 | locomotion | Large Prince/loco per-frame step with sqrt/vector math | NEW | med |
| `004A0F5C` | 2088 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A10AD` | 2477 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A12E0` | 9094 | locomotion | Large loco step with sqrt, calls nullsub hooks | NEW | med |
| `004A1B53` | 6660 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A25D4` | 3866 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A2999` | 2238 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A3305` | 2947 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A393F` | 4879 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A3DE5` | 5782 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A4526` | 2316 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A46F2` | 2937 | pickup | Arrow holder / Arrow_0 object setup (new) | NEW | low |
| `004A4AE4` | 2076 | locomotion | Prince loco/skill step (K2 activity, helper) | NEW | med |
| `004A6C82` | 4920 | guard | EnemyDat__Parse: reads enemy.dat tuning numbers ([Archery] section) | documented (guard-ai.md) | high |
| `004A9C3A` | 2750 | guard | StIdleEnemyLoc / StStartEnemyLoc: guard locomotion state pair | NEW | med |
| `004A9E89` | 1813 | guard | StEnemyStanding / StStartEnemyAmbush-like guard state | NEW | med |
| `004AADEC` | 3350 | locomotion | Static init of float tuning constants (angles 0.4, flag 7) | NEW | low |
| `004AB027` | 3588 | guard/locomotion | Guard or Prince state helper | NEW | med |
| `004B058A` | 2136 | level-load | Builds a floor proto-actor (FloorActor / Vfloor) from MfProto nodes | NEW | med |
| `004B0B3B` | 3555 | locomotion | JumpingUp state with PredictJumpAbort check | NEW | high |
| `004B1845` | 2037 | locomotion | Prince state (StStart/StIdle) for a move | documented (locomotion.md) | med |
| `004B1A32` | 2042 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B27F5` | 2688 | locomotion | Prince state (StStart/StIdle) for a move | documented (locomotion.md) | med |
| `004B2BCD` | 2512 | locomotion | Prince state (StStart/StIdle) for a move | documented (locomotion.md) | med |
| `004B3E86` | 1643 | locomotion | LedgeHangPush start state | NEW | med |
| `004B403F` | 4589 | locomotion | Ledge hang end-move and moving states (LedgeHangEndMove, LedgeHangMoving) | NEW | high |
| `004B485D` | 1887 | locomotion | Climbing / Ledge pull-up / StIdleClimb / StStartClimb states | NEW | high |
| `004B6186` | 2041 | locomotion | Prince state (StStart/StIdle) for a move | documented (locomotion.md) | med |
| `004B747C` | 2054 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B76A9` | 2154 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B78AE` | 2029 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B7A61` | 2115 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B7C52` | 2418 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B80B9` | 2437 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B83F7` | 2224 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B8701` | 2041 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004B88B4` | 2179 | locomotion | Prince state (StStart/StIdle) for a move | NEW | med |
| `004BC51A` | 7296 | guard | StChargePrince: guard charges the Prince | NEW | high |
| `004BD590` | 2593 | guard | StHeadToPursuitWaypoint | documented (guard-ai.md) | med |
| `004BE338` | 3733 | guard/prince | State or effect handler | NEW | med |
| `004C1CF7` | 2347 | pickup | RemovedArrow handler (arrow pickup/despawn) | NEW | low |
| `004C22EE` | 3128 | guard/prince | State or effect handler | NEW | med |
| `004C254E` | 3068 | guard/prince | State or effect handler | NEW | med |
| `004C80FC` | 8134 | input/combat | Prince__HandleCommand / OnInputEvent (fight command storage) | documented (combat-moves.md / locomotion.md) | high |
| `004C9173` | 3400 | prince | Prince state/skill helper | NEW | med |
| `004C95BB` | 2756 | skill | NoDeathSkillFor... (no-death skill) / skill object alloc | documented (level-transitions.md) | med |
| `004CAFEA` | 2143 | prince | Prince state/skill helper | NEW | med |
| `004CC1C4` | 2503 | prince | Prince state/skill helper | NEW | med |
| `004CD551` | 3660 | locomotion | DropActorToGround: snaps an actor down to floor | NEW | med |
| `004CF899` | 3340 | prince | Prince state/skill helper | NEW | med |
| `004CFB3E` | 7354 | collision | Collision response with acos (angle test) | documented (collision.md) | high |
| `004D2C6B` | 3816 | pickup | StIdleAction / StStartAction: go to the pickup | NEW | med |
| `004D3142` | 3717 | trap | StStartArrowCharger / StIdleArrowCharger | documented (small-traps.md) | low |
| `004D3477` | 1538 | trap | StIdleUseCharger / FinishedArrowCharger | documented (small-traps.md) | low |
| `004D35BE` | 5633 | pickup | StStartGrasp: GoPickup / NotAPickupItem | documented (doors-pickups.md) | med |
| `004D392C` | 1506 | trap | StStartManipulation: UseTheTrap | documented (doors-pickups.md) | med |
| `004D6072` | 3527 | crate | CrateGrabStart / StIdleCrate / StStartCrateGrab states | NEW | med |
| `004D727C` | 2183 | library | String/ini parsing helper (strchr) | library/skip | none |
| `004D74AB` | 3170 | library | String compare/copy helper (memicmp) | library/skip | none |
| `004DD658` | 5104 | library | Compression/table routine with memset | library/skip | none |
| `004DDC51` | 2769 | library | Timing helper (timeGetTime) | library/skip | none |
| `004DE001` | 2366 | library | Table free/maintenance | library/skip | none |
| `004DE392` | 2130 | library | Small helper | library/skip | none |
| `004E1147` | 2831 | actor | Game-object update using floats (dynamic cast chain) | NEW | med |
| `004E7067` | 5055 | actor | Game object (sub_4042F0 dynamic cast chain) per-frame step | NEW | med |
| `004E7675` | 3342 | actor | Game object step (new/float) | NEW | med |
| `004E7A62` | 4320 | actor | Game object step with dynamic casts and time constant | NEW | med |
| `004E7D91` | 3282 | actor | Game object step with dynamic casts | NEW | med |
| `004E844F` | 2061 | library | STL _Lockit | library/skip | none |
| `004E85B9` | 8418 | library | STL allocate | library/skip | none |
| `004E8D10` | 4321 | library | STL _Lockit | library/skip | none |
| `004EA8CC` | 3749 | actor | Game object step with dynamic cast | NEW | med |
| `004F03A9` | 4968 | actor | Timed actor update using global time dbl_7F2CC0 | NEW | med |
| `004F585C` | 2124 | library | CRT strtoul/locale | library/skip | none |
| `004F604C` | 4696 | library | CRT memchr/locale helper | library/skip | none |
| `004F6383` | 4560 | actor | Actor helper | NEW | low |
| `004F6891` | 2078 | actor | Actor helper | NEW | low |
| `004F7212` | 5961 | actor | Dynamic-cast dispatch over many object types | NEW | med |
| `004F8AAA` | 2127 | config | Loads gameplay tuning keys: MinFallHeight, FallDeathHeight, FallConstDamage/Linear/Squared, FallGravity, F*TurnRadius, F*Jump Y/Z, Spurt*, EnemyVisibleDist, ArrowVelocity, FHorizLaunch | NEW | high |
| `004FCB65` | 5804 | physics | Contact data copy with qmemcpy | NEW | low |
| `004FEF2E` | 2036 | physics | Buffer resize helper (new/qmemcpy/delete) | library/skip | none |
| `004FFC21` | 10930 | physics | Large contact/force builder (memset32, new/delete) | NEW | med |
| `00501373` | 2110 | physics | Force factory (BadForceObject, UnknownForceType) | documented (doors-pickups.md) | med |
| `005015D5` | 3628 | physics | Force evaluator using fabs | NEW | med |
| `00501890` | 10746 | physics | Large force/collision solver with sqrt/fabs | NEW | high |
| `0050222E` | 4491 | physics | Force body step with sqrt | NEW | med |
| `00502742` | 5206 | physics | PushedByForce / ForceHasDiagonal checks | NEW | med |
| `00502C54` | 14837 | physics | Very large force/collision solver (14 KB) | NEW | high |
| `0050F5A5` | 3744 | debug | Debug overlay: draws box/lines and text via GDI | library/skip | none |
| `0050F8A0` | 3675 | debug | Debug overlay text output (sprintf, TextOutA) | library/skip | none |
| `005100B8` | 2601 | debug | Debug overlay text (bool flags true/false) | library/skip | none |
| `005111B9` | 5205 | debug | Debug dump of tree to file (fprintf, "children") | library/skip | none |
| `0051376F` | 1631 | physics | Force shape ctor | NEW | med |
| `00513AE0` | 7636 | physics | Force shape step with sqrt | NEW | med |
| `0051419B` | 3399 | physics | Force shape geometry math (sin/cos/sqrt) | NEW | med |
| `005146E2` | 13369 | physics | Large force-shape evaluator (13 KB) | NEW | med |
| `005153FE` | 2225 | physics | Force shape | NEW | med |
| `00515588` | 2357 | physics | Force shape angle helper (atan/fmod/sin) | NEW | low |
| `00515780` | 1988 | physics | IllegalEdgeInShape check | NEW | low |
| `00515971` | 4624 | physics | Force shape geometry (cos/sin/sqrt) | NEW | med |
| `00515E83` | 1975 | physics | IllegalEdgeInGeometry check | NEW | low |
| `00516084` | 5183 | physics | RotationForceShape builder | NEW | med |
| `0051648C` | 8427 | physics | Force shape mesh builder (sin/cos, new/delete) | NEW | med |
| `00517ABF` | 2317 | physics | ForceShape ctor | NEW | med |
| `0051825C` | 2833 | physics | ForceShape ctor variant | NEW | med |
| `00518856` | 3131 | physics | MotivateForce ctor (skill-driven collision body) | documented (doors-pickups.md) | med |
| `00518DC4` | 2622 | library | MSVC std::string dtor-style helper | library/skip | none |
| `0051BF52` | 6343 | library | Allocator-heavy resource builder (malloc/free) | library/skip | low |
| `0051C249` | 2590 | library | Allocator-heavy resource builder (malloc/free) | library/skip | low |
| `0051CDEA` | 2838 | sound/3d | Positional sound update (GetMaskId, listener transform) | NEW | low |
| `0051E564` | 3057 | sound | DirectSound CreateSoundBuffer for a sound object | NEW | med |
| `0051F189` | 2011 | library | Small helper (no calls) | library/skip | none |
| `00524802` | 8418 | library | STL | library/skip | none |
| `00528605` | 2061 | library | STL | library/skip | none |
| `0052876F` | 8418 | library | STL | library/skip | none |
| `00528F1A` | 2061 | library | STL | library/skip | none |
| `00529084` | 8418 | library | STL | library/skip | none |
| `0052998E` | 4321 | library | STL | library/skip | none |
| `00529FD9` | 4321 | library | STL | library/skip | none |
