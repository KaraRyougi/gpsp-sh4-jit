# gpSP Upstream Snapshot

The upstream emulator source is currently cloned at `vendor/gpsp`.

- Repository: <https://github.com/libretro/gpsp>
- Snapshot: `2db57b11a437c4432ab69823bdcd951181de6213`
- Commit subject: `webOS: set arch params for 64-bit (#284)`

## Why This Base

gpSP already has the structure we need for a low-power target:

- `cpu.cc` contains the interpreter and shared ARM/Thumb decode machinery.
- `cpu_threaded.c` contains block translation, lookup, cache management, and
  the host-emitter interface.
- `arm/`, `mips/`, and `x86/` contain existing host emitters/stubs.
- `gpsp_config.h` exposes translation-cache sizing knobs.

The SH4 port should initially avoid broad rewrites. The most valuable first
path is to add an SH4 host emitter that satisfies the same interface used by
the MIPS and AArch64 emitters.

## Repository Shape

The upstream clone still has its own `.git` directory. This is convenient while
we are reading and comparing upstream code, but before committing long-lived
project history we should choose one of these ownership models:

- keep gpSP as a Git submodule,
- vendor a fixed source snapshot with `.git` removed, or
- maintain a fork and track our port branch there.
