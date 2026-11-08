# Unknown-function triage A

Skim-level labels from pseudocode, string names and callers; no function was traced in depth. "documented" means the address already appears in docs/reference; none were found in src. Status of 135 functions: 17 documented, rest NEW.

## NEW, high importance

- 0x4470CB: main per-frame world update (CameraStuff, UpdatePrinceState, NpcUpdate, ManageFloors, SetNeighbors, PurgeChars). Defines the frame order.
- 0x44A8E3: simulation phase sequence (weather, traps, geometry, forces, actions).
- 0x41B7D6: world update phases (visibility, TrapUpdates, PerformUpdate, DetermineFloor).
- 0x411ADC: foot-cast collision ("CastFoot"), core of ground contact.
- 0x411F91: swept movement and slide against geometry.
- 0x4679AB: collapsing-floor trap loader (Wobble, Fall, Crash).
- 0x471BD0: ForceTrans loader (volume, radial, linear, generic forces).
- 0x47CD41 and 0x47D495: MoverTrans loader and mover command executor (Stop, Stall, Rotate, Pause, Move, Loop, Continue).
- 0x48B3A9: WallBlade trap loader.
- Locomotion activity family: 0x48FF84, 0x492B21, 0x494BF6 (ChooseNextStep variants, siblings of documented 0x49023F), and the loco-contour stepping at 0x497E62 and 0x49858E (with 0x49921A "LocoContourFail", 0x4994A7, 0x49984E, 0x499A41). Twins and triplets (48FE2D/4929CA/4949B0, 490A48/49307E/494E0D) are likely per-activity copies of the same code.

Notes: 17 STL tree/map instantiations (455E89, 4568C2, 46A58D, ...) are library. The 0x4Dxxxx-0x45xxxx UI/menu blocks are init/teardown of global resources.

| Address | Subsystem | Purpose | Status | Importance |
|---|---|---|---|---|
| 004029F8 | animation | Loads a skeleton/anim by name ("Fail to load skel"); variant 1 of a duplicate pair | NEW | low |
| 00402B52 | animation | Same skeleton-load failure path, sibling of 4029F8 | NEW | low |
| 00402E6C | math/physics | Vector-heavy per-frame helper using actor transform (553070/553D20); likely bone/orientation blend | NEW | low |
| 004032AD | math/physics | Three-vector transform/normalise helper (612916 = sqrt/normalise) | NEW | low |
| 004037E1 | prince | Prince actor init/reset using GblPrinc* ambient parameter; reads many globals and floats | NEW | med |
| 00403DED | debug | Dumps vectors with printf format ("%s %2f %2f ...") | NEW | none |
| 00406A39 | save | Serialises a pair of object refs (index in parent list) with 40A34B/40A366 stream writers | NEW | low |
| 004074FD | camera | Camera "PosIsNot..." check/handler | documented | med |
| 004077DD | camera | Camera state machine entry (CameraPointing, StFixedCamera, StFollowTarget, StMonitorActive) | documented | high |
| 00407B21 | camera | Builds visibility camera ("VisCam"/"VisHub"), prints "Cam %s at ..." (geometry utils) | NEW | low |
| 00407DD1 | camera | Camera update step (calls 407FC7) | documented | med |
| 00407FC7 | camera | Builds the camera's look-at matrix from eye and target (not an obstruction probe; ours casts its own arm at ~10392) | NEW | none |
| 0040E71F | camera | Look mode ("Can't look with this") | documented | med |
| 0040EDE9 | camera | Camera save serialiser | documented | low |
| 0040EFFC | camera | Camera load (read counterpart of 40EDE9) | NEW | low |
| 00410BF8 | locomotion/collision | Pole/rope collision check ("Colliding with", "Clearing a pole") | documented | med |
| 00410E0E | locomotion/collision | Scene/opponent collision move with pole clearing | documented | high |
| 00411ADC | locomotion/collision | Foot cast collision ("CastFoot", "Colliding with") | NEW | high |
| 00411F91 | locomotion/collision | Swept movement/slide against geometry (vec add/sub chain, dword_8A0CC8) | NEW | high |
| 00417BBF | world | Phase dispatch over actor lists: calls vfunc +100 on each registered object for phase 0..3 | NEW | med |
| 0041B7D6 | world | Per-frame world update phases (Visibility, TrapUpdates, PerformUpdate, DetermineFloor) | NEW | high |
| 0041CBF2 | level-load | Scene/geometry node builder (node registration, 8A0CE8 scene) | NEW | low |
| 0041DD2E | level-load | Loads neighborList.txt (room adjacency) | NEW | med |
| 00423A38 | level-load | Scene node parser ("Unnamed node", "Pivot", "Not a TriShape") | NEW | low |
| 004241DE | rendering | Billboard geometry update | NEW | low |
| 0042DCA5 | input/ui | Mouse/cursor tracking with timeGetTime (double-click/repeat timing) | NEW | low |
| 0042E59F | input/ui | Cursor/mouse button event handler | NEW | low |
| 0042EAC2 | ui/menu | Main UI state switch (global menu state machine, word_7EC944) | NEW | low |
| 0042F23F | ui/menu | UI resource reset/teardown | NEW | none |
| 0042FEAC | ui/menu | UI resource release (many globals freed) | NEW | none |
| 00431DA9 | debug | Writes a 24-bit BMP screenshot | NEW | none |
| 00432CEB | ui/menu | Menu screen setup/draw | NEW | low |
| 004333BC | library | iostream Fputc/streambuf (std) | NEW | none |
| 004336A8 | library | iostream Fgetc/streambuf (std) | NEW | none |
| 0043682A | ui/menu | Frees menu resources (dword_7F2Dxx block) | NEW | none |
| 00436ECB | ui/menu | Menu resource load/cleanup | NEW | none |
| 00437F0E | ui/menu | Loads a menu asset file by path | NEW | none |
| 004383EF | level-load | Asset load via 604827/602E20 loader (model/texture), sets dword_7F2E7x | NEW | low |
| 0043876D | level-load | Asset loader dispatch (type switch over 606xxx loaders) | NEW | low |
| 00439578 | debug | Writes "Time %.2f" plus vectors to a log file | NEW | none |
| 00439D78 | locomotion | Ledge/ground probe returning float (4C93BE, 445241), uses prince position | NEW | med |
| 00439F55 | locomotion | Companion probe returning float (vec ops, 512339) | NEW | med |
| 0043BCBB | ui/menu | Table of menu string slots (21x helper) | NEW | none |
| 0043C6DB | ui/menu | Menu entry init (dword_7F34xx) | NEW | none |
| 0043CA67 | ui/menu | Menu entry layout (dword_7F36xx) | NEW | none |
| 0043CFB5 | ui/menu | Big menu build (off_7BF784 x37 entries) | NEW | low |
| 0043E092 | ui/menu | Deep equality compare of two string lists/objects (strcmp) | NEW | none |
| 0043E53C | ui/menu | Wrapper comparing two menu/option objects | NEW | none |
| 004454FA | world | Room neighbour attach ("No neighbors for ...") | NEW | med |
| 004469C6 | world | Room modify report ("Modify report for ...") | NEW | low |
| 004470CB | world | Main per-frame world update: CameraStuff, UpdatePrinceState, NpcUpdate, ManageFloors, SetNeighbors, PurgeChars | NEW | high |
| 0044A4D4 | world | Applies a delta to all actors attached to a room (iterates +74 list) | NEW | med |
| 0044A8E3 | world | Simulation phases: UpdateWeather, UpdateTraps, UpdateGeometry, UpdateForces, UpdateActions | NEW | high |
| 0044D43F | particles | Blood spurt emit (Spurt) | NEW | med |
| 0044D6ED | particles | Blood spurt update | NEW | med |
| 0044DA29 | particles | Spurt/SpurtHub/SpurtBbd node creation | NEW | med |
| 004502D9 | ui/menu | Global teardown of UI/engine resources (dword_7F5xxx) | NEW | none |
| 004514AD | ui/menu | UI resource init (dword_7F5xxx) | NEW | none |
| 004516F6 | ui/menu | UI resource init part 2 | NEW | none |
| 00451BF7 | ui/menu | UI resource refresh | NEW | none |
| 00452C06 | ui/menu | UI state query | NEW | none |
| 00453E34 | input | Reads input.ini (device GUID/offset/comment) and builds key bindings | NEW | med |
| 00455E89 | library | STL map/tree insert instantiation | NEW | none |
| 004568C2 | library | STL tree insert helper instantiation | NEW | none |
| 0045B550 | ui/menu | GDI blit/draw | NEW | none |
| 0045F3E4 | level-load | Level unload / engine cleanup of globals | NEW | low |
| 0045F56F | rendering | DynaLight node creation | NEW | low |
| 0046374C | actor-load | Argument lookup by name for trap loaders (Trap__GetArgv family) | NEW | low |
| 00463DDA | doors | Aperture actor loader | documented | med |
| 004679AB | traps | TrapCollapsing floor loader (Wobble, Fall, Crash, CollapseShape) | NEW | high |
| 0046A58D | library | STL map instantiation | NEW | none |
| 0046AF18 | library | STL tree insert instantiation | NEW | none |
| 0046BB61 | doors | TrapDoor loader (DoorTrans, DoorForce, DoorProto) | documented | high |
| 0046C16E | doors | Door-related destructor/teardown | documented | low |
| 0046F689 | traps | PrincessTrap loader (StartPosition, Loop, FailedPrincess, BadPrincessTrap) | NEW | med |
| 00470041 | traps | PrincessTrap StartPosition handling | NEW | med |
| 004702D9 | traps | PrincessTrap Loop / FailedPrincess handler | NEW | med |
| 00470497 | actor-load | Argument lookup helper (twin of 46374C) | NEW | low |
| 00470D60 | traps | FlameThrower loader | documented | med |
| 00471BD0 | traps | ForceTrans loader (Volume, Radial, Linear, Generic forces) | NEW | high |
| 00473161 | traps | Small-trap geometry setup | documented | low |
| 0047344F | traps | Small-trap per-frame helper | documented | low |
| 00473D5D | level | Level loader (Skydome, Near, Exit, Clip) | documented | med |
| 004779CC | input | Input binding loader ("Duplicate input") | NEW | low |
| 00478873 | library | STL map instantiation | NEW | none |
| 00478DE9 | library | STL map instantiation | NEW | none |
| 00478F53 | library | STL map big insert | NEW | none |
| 0047970F | library | STL tree insert | NEW | none |
| 00479ACC | library | STL tree insert | NEW | none |
| 0047B1F5 | traps | Mover step toward target (speed/accel) | NEW | med |
| 0047C0DF | traps | Mover move/stop step | NEW | med |
| 0047CD41 | traps | MoverTrans loader (TrapChildren) | NEW | high |
| 0047D495 | traps | Mover command executor (Stop, Stall, Rotate, Pause, Move, Loop, Continue) | NEW | high |
| 0047E631 | pickup | Pickup actor loader (pickup args, GotAPickup) | documented | high |
| 004814C8 | library | STL map instantiation | NEW | none |
| 004816EE | library | STL tree insert | NEW | none |
| 0048233E | library | STL map big insert | NEW | none |
| 00485C6E | library | STL map instantiation | NEW | none |
| 00485DD8 | library | STL map big insert | NEW | none |
| 0048651C | library | STL tree insert | NEW | none |
| 004878D2 | actor-load | RoomAttributes trap loader | NEW | med |
| 00488D07 | actor-load | SpecialEffect trap loader | NEW | med |
| 0048B3A9 | traps | WallBlade trap loader | NEW | high |
| 0048BF22 | traps | WaterLevel actor loader | NEW | med |
| 0048D276 | locomotion | Activity step-position helper (dword_89F968) | NEW | med |
| 0048E488 | locomotion | Activity step blend helper | NEW | med |
| 0048ED4D | locomotion | Activity step helper with time arg | NEW | med |
| 0048EFA8 | locomotion | Activity vector/blend helper | NEW | med |
| 0048FE2D | locomotion | Activity duration/weight calc returning float | NEW | med |
| 0048FF84 | locomotion | Activity ChooseNextStep variant (dword_89F900), sibling of 49023F | NEW | high |
| 00490A48 | locomotion | Activity step-clip setup (triplet with 49307E, 494E0D) | NEW | med |
| 00492090 | locomotion | Foot node creation ("Creation failed", "Foot node in S don...") | NEW | med |
| 004929CA | locomotion | Activity duration/weight calc (twin of 48FE2D) | NEW | med |
| 00492B21 | locomotion | Activity ChooseNextStep variant (twin of 48FF84) | NEW | high |
| 00492D97 | locomotion | K2ActivityRunStop ChooseNextStep ("NextSkillIsSF", FindIntersection) | documented | high |
| 0049307E | locomotion | Activity step-clip setup (twin of 490A48) | NEW | med |
| 004931F6 | locomotion | Activity step-end blend helper | NEW | med |
| 00493B23 | locomotion | Activity step-end foot placement | NEW | med |
| 00493F1D | locomotion | Activity frame advance ("FrameOverrun" check) | NEW | med |
| 0049471D | locomotion | Activity frame advance variant with FrameOverrun/Illegal frame | NEW | med |
| 004949B0 | locomotion | Activity duration/weight calc (third twin) | NEW | med |
| 00494BF6 | locomotion | Activity ChooseNextStep variant (third twin) | NEW | high |
| 00494E0D | locomotion | Activity step-clip setup (third twin) | NEW | med |
| 004951AA | locomotion | Falling activity frame check ("Illegal falling ...") | NEW | med |
| 00495893 | locomotion | Gesture activity step ("... was totally lost") | NEW | med |
| 00496367 | locomotion | Activity create wrapper ("Creation failed") | NEW | low |
| 00496474 | locomotion | K2Gest activity update ("K2Gest", "was totally lost") | NEW | med |
| 00496B31 | locomotion | K2GestActivity foot contact check | documented | high |
| 00497C1E | locomotion | Loco contour step helper | NEW | med |
| 00497E62 | locomotion | Loco contour step (big) | NEW | high |
| 0049858E | locomotion | Loco contour step main (485 lines) | NEW | high |
| 0049921A | locomotion | Loco contour failure ("LocoContourFail") | NEW | med |
| 004994A7 | locomotion | Loco contour end step | NEW | med |
| 0049984E | locomotion | Loco contour helper (foot probe) | NEW | med |
| 00499A41 | locomotion | Loco contour helper (sibling of 49984E) | NEW | med |
