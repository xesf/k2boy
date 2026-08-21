# Engine structure — the module system

First pass at the engine's top level, recovered the same way as the format work:
find the interface vtable, diff each subclass against it, then read the overrides.

## The `module` interface

`module` (vtbl `0x744D44`) is a **six-virtual interface** with five subclasses:

| Class | vtable | Role |
|---|---|---|
| `gameModule` | `0x74402C` | in-game |
| `sMenuModuleClass` | `0x744BC8` | menus |
| `sTitleModuleClass` | `0x74D1BC` | title screen |
| `sNISModuleClass` | `0x74D178` | non-interactive sequence (cutscene) |
| `sFinaleModuleClass` | `0x74D10C` | end sequence |

Override matrix — `*` means the subclass replaces the base:

```
slot     module    gameModule   sMenuModule  sTitleModule  sNISModule  sFinaleModule
  0     0x52DFBA        *            *            *            *            *
  1     0x434806        *            *            *            *            *
  2     0x434810        *            *            *            *            *
  3     0x434813        *            *            *            *            *
  4     0x434818        *            *          (base)          *            *
  5     0x434819        *          (base)       (base)       (base)          *
```

Two things fall out of this immediately.

**Slots 0–3 are mandatory, 4–5 are optional.** Slot 0 points at `0x52DFBA` — the same
pure-virtual stub `MfStreamObject` uses, so it must be overridden. Slots 4 and 5 have real no-op
bases (`retn`) and are skipped by the simpler modules.

**`sFinaleModuleClass` is a `gameModule`, not a sibling.** Its slots 0, 1, 2, 4 and 5 are the
*same addresses* as `gameModule`'s (`0x4208C1`, `0x4208EF`, `0x420932`, `0x420935`, `0x42093A`);
only slot 3 differs. The finale runs the game module with one behaviour swapped.

## Slot roles

Established:

| Slot | Base | Evidence |
|---|---|---|
| 0 | pure virtual | stub shared with `MfStreamObject` |
| 3 | `xor eax,eax; retn 18h` | **event handler.** Takes six dwords, returns 0 (= unhandled) in the base. `gameModule__on_event` compares an id against `0x50E` and writes a "handled" flag through an out-pointer. |
| 2 | `retn 0Ch` | three dwords, returns nothing |
| 4, 5 | `retn` | no-ops |

Not yet established: which of slots 0/1/2/4 are enter, update, render and exit. Sizes hint at
where the work is — `sMenuModule` slot 1 is 325 bytes, slot 3 is 300, slot 2 is 248, slot 4 is 152
— but `gameModule`'s slot 2 is a `nullsub` while the menu's is 248 bytes, so the split is not
uniform. Naming them requires reading the bodies, which has not been done.

The C++ interface in `src/engine/module.h` therefore uses `vf1`/`vf2`/`vf4` rather than inventing
names that later turn out wrong.

## The character system

The next layer down, for reference:

| Class | vtable | Slots |
|---|---|---|
| `CharacterStateTable` | `0x74395C` | **51** |
| `CameraStateTable` | `0x7437F8` | 51 |
| `PrinceObject` | `0x74AC74` | 51 |
| `K2Activity` | `0x7491E8` | 21 |
| `EnemyAIObject` | `0x749BF8` | 7 |
| `MfLocoPlanner` | `0x75308C` | 1 |

`PrinceObject` and `CameraStateTable` both present 51 slots, matching `CharacterStateTable` —
consistent with the inheritance chain recovered from RTTI, where both derive from it.

> **Correction.** An earlier version of this section called that 51-slot interface "the gameplay
> state machine". It is not. `NiRTTI` recovery (`docs/reference/nirtti-hierarchy.md`) places
> `CharacterStateTable` under `CharacterObject <- NiCollisionSwitch <- NiNode <- NiAVObject <-
> NiObject`, so the 51 slots are the **inherited `NiNode` scene-graph interface**. `PrinceObject`
> and `CameraStateTable` override only **7** of them, and the same 7: slots 0, 6, 45, 46, 47, 48
> and 50. Slot 0 is `GetRTTI`; slots 45-48 and 50 are pure virtual in the base; slot 47 is empty in
> both subclasses. The customisation surface per character class is 7 methods, not 51.
>
> `CharacterStateTable` is still central — it is the common base of `PrinceObject`,
> `CameraStateTable` and `Trap` (39 subclasses) — but the states are not in this vtable. Where they
> actually live is **open**.

## Method

The same three steps that worked for every format in `docs/formats/`:

1. Find the base interface vtable and count its slots.
2. Diff each subclass's vtable against it; identical entries are inherited, differing ones are
   overrides worth reading.
3. Read the base implementations first — stubs, no-ops and trivial returns reveal which slots are
   mandatory and what the default answers are, before touching any large override.

Step 3 is what makes this cheap: a `retn 18h` in the base gives the argument size, and a
`xor eax,eax` gives the default return, without decompiling anything.
