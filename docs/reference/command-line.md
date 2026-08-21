# Command-line flags and the "console"

**Status: dispatcher structure confirmed; individual handlers partially decoded.**

## There is no console command table

An earlier revision of these notes asserted that `minrate` was a real console command, on the
strength of `data/console.dat` containing the line `minrate 15`.

**That was wrong.** The string `minrate` does not appear anywhere in `data/pop3dreal.exe` — not as
a literal, not as a substring. Neither does `maxrate`. So whatever wrote `console.dat` was a
different tool or an earlier build; its contents are **inert** in the shipped game.

What *does* exist:

| Thing | Where | Notes |
|---|---|---|
| `AConsole` | vtable `0x74351C` (3 slots: `0x401340`, `0x401367`, `0x401445`) | a debug console class, constructed at `0x40133B` |
| `"Console (debug)"` | `0x7BB704` | a menu/option label, **no code references** |
| `"I@CONSOLE "` | `0x745162` | log prefix, **no code references** |
| `Console` script class | `Console.Print()`, `Console.PrintNl()`, `Console.Clear()` | the real console API — see [`script-api.md`](script-api.md) |
| `MfScConsoleManager` | vtable `0x7572E4`, refs `0x6D608F`, `0x6D64D3` | Piccolo's console manager |

So the console surface is reached from **Piccolo script**, not from a native name→handler table.
There is nothing to dump.

## Flags are matched on the first character only

`CommandLineParser::Parse` (`0x45DA6D`, vtable slot 2) reads lines and hands each to the
dispatcher at **`0x45D0A7`**, which:

1. scans forward to a `-`
2. takes the **single next character**, `tolower`s it
3. switches on that character through a compare tree
4. each handler then inspects further characters itself

This is the same style as `LevelFileParser` — see [`../formats/lvl.md`](../formats/lvl.md).

**Consequence, and a correction:** the multi-letter flags listed in
[`../formats/text-and-tuning.md`](../formats/text-and-tuning.md) are not distinct flags. Only the
first letter selects the handler:

| As written in the `.dat` | Actually |
|---|---|
| `-cmd console.dat` | flag `c`, with `md` consumed by the `c` handler |
| `-zp` | flag `z`, with `p` as a sub-selector |
| `-warn 1` | flag `w`, with `arn` following |
| `-xmtn` | flag `x`, with `mtn` following |
| `-lx-` | flag `l`, with `x-` following |

This is why almost none of them appear as string literals in the binary: there are no `strcmp`s
against `"-zp"` or `"-warn"` to hold such literals. The only flag-shaped strings that *do* exist
(`-x` `0x782DC9`, `-b` `0x7CD1C0`, `-i` `0x755EE5`, `-v` `0x762E09`) belong to other parsers.

## Dispatch table

Handler addresses, all inside `0x45D0A7`:

| Flag | Handler | Decoded behaviour |
|---|---|---|
| `-2` | `0x45D285` | — |
| `-a` | `0x45D267` | — |
| `-b` | `0x45D254` | — |
| `-c` | `0x45D1F4` | sub-checks next char: `d` sets `byte_7EAC74 = 1`; `m` then `d` takes a further path. `-cmd` lands here. |
| `-d` | `0x45D131` | **debug flags.** `-dm<n>` → `dword_801CAC`; `-dx` → `dword_7FADD0 \|= 0x3E`; otherwise optional `+`, then `strtoul` OR-ed into `dword_7FADD0`, accepting chains like `-d1+2+4` |
| `-e` | `0x45D2EB` | — |
| `-f` | `0x45D497` | — |
| `-g` | `0x45D424` | — |
| `-h` | `0x45D3C7` | — |
| `-i` | `0x45D37E` | — |
| `-k` | `0x45D327` | reads a filename; **defaults to `cmdline.dat`** if absent |
| `-m` | `0x45D6F1` | — |
| `-o` | `0x45D6E5` | — |
| `-r` | `0x45D6A4` | `strtoul` → `dword_7EAC9C` |
| `-s` | `0x45D689` | `strtoul` → `word_7EABE0` (16-bit) |
| `-t` | `0x45D66C` | sets `byte_7EAB3C = 1`, then `sub_45CFEB` → `dword_7C1854` |
| `-v` | `0x45D73E` | — |
| `w`–`z` | subtree at `0x45D7EC` | not yet enumerated |

Letters with no handler fall through to the default at `0x45DA0F`.

`-d` is the interesting one: a bitmask of debug flags built with `+`-separated numbers. Combined
with the `PMW_*` inspector windows and `MfLogger*` UI still linked into the retail binary (see
[`../ARCHITECTURE.md`](../ARCHITECTURE.md)), this is the most plausible route to re-enabling the
developer tooling. `dword_7FADD0` is the mask to study; find its readers.

## To finish this

1. Disassemble the `w`–`z` subtree at `0x45D7EC`.
2. For each handler above, note which global it writes; then xref those globals to learn what the
   flag actually switches on.
3. `dword_7FADD0` (debug mask) and `dword_801CAC` are the highest-value globals.
