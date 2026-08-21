# `.sfx` — sound effect

Location: `data/Audio/sfx/*.sfx` (1255 files, ~150 MB).
Owning class: `SFXFile` (`C:\k2\K2boy\Source\K2Audio\SFX.cpp`).

**Status: confirmed.** Header size and the data-length field were validated against every
sampled file (`fileSize - 0x20 == dataSize` held for 100% of samples).

## Layout

```c
struct SFXHeader {          // 0x20 bytes
    // --- standard WAVEFORMATEX, 18 bytes, no RIFF wrapper ---
    uint16_t wFormatTag;        // 0x0000  always 1 (WAVE_FORMAT_PCM)
    uint16_t nChannels;         // 0x0002  1 (mono) — 1 stereo file exists in the set
    uint32_t nSamplesPerSec;    // 0x0004  22050
    uint32_t nAvgBytesPerSec;   // 0x0008  44100  (= rate * blockAlign)
    uint16_t nBlockAlign;       // 0x000C  2      (= channels * bits/8)
    uint16_t wBitsPerSample;    // 0x000E  16
    uint16_t cbSize;            // 0x0010  0
    // --- POP3D extension ---
    char     magic[2];          // 0x0012  "WI"
    uint32_t dataSize;          // 0x0014  == fileSize - 0x20
    uint32_t loopStart;         // 0x0018  byte offset into data, 0 = no loop
    uint32_t loopEnd;           // 0x001C  byte offset into data, 0 = no loop
};
// followed by dataSize bytes of raw signed 16-bit LE PCM
```

## Observed values

Of 400 sampled files, 399 are `(PCM, 1ch, 22050 Hz, 44100 B/s, align 2, 16 bit)` and one is
`(PCM, 2ch, 22050 Hz, 88200 B/s, align 4, 16 bit)`. The header is fully general, so do not
hardcode mono/22050.

Loop fields are `0/0` for one-shots. Example of a looping asset:

```
L01cranehoist001.sfx  size=65994  dataSize=65962  loopStart=14556  loopEnd=65962
```

i.e. an attack from 0..14556 then a sustain loop to the end. `loopEnd == dataSize` is the common
looping case.

## Converting to WAV

Emit a 44-byte canonical RIFF header, copy the 18-byte `WAVEFORMATEX` verbatim into the `fmt `
chunk (or rebuild it as 16 bytes), then append the payload:

```python
import struct, sys

def sfx_to_wav(src, dst):
    d = open(src, 'rb').read()
    fmt_tag, ch, rate, bps, align, bits, cbsz = struct.unpack_from('<HHIIHHH', d, 0)
    assert d[0x12:0x14] == b'WI', 'not an SFX file'
    n = struct.unpack_from('<I', d, 0x14)[0]
    pcm = d[0x20:0x20 + n]
    hdr = (b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' +
           struct.pack('<IHHIIHH', 16, fmt_tag, ch, rate, bps, align, bits) +
           b'data' + struct.pack('<I', len(pcm)))
    open(dst, 'wb').write(hdr + pcm)
```

Loop points are lost in plain WAV; write them into a `smpl` chunk if round-tripping matters.

## Naming convention

Filenames encode speaker/context, e.g. `A1deathv001` = actor class `A1`, event `death`, variant
`v001`. Prefixes seen: `A1..A4` (assassins), `PG1..PG4` (palace guards), `D1..D4`, `W1..W3`,
`BM1..BM3`, `PRN` (prince), `L01..` (level-scoped ambience). The same prefixes appear as
subdirectories in `data/Geometry/Enemies/`, so the enemy roster is directly recoverable.

## Verification hooks for IDA

Search `.rdata` for the literal `"WI"` compared as a 16-bit constant `0x4957`, and for the format
string `"Bad audio data in %s line %d"` (that one belongs to the `.dat` audio parser, not this
loader). `SFXFile` has RTTI, so locate its vtable first and work backwards to the constructor.
