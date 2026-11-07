# TS2Reimpl (snapshot)

A bottom-up C++ reimplementation of The Sims 2 (Legacy Collection). The original game runs under our own host
(`ts2host.exe`), and its functions are replaced class by class, each one checked by a differential test against the
original. The goal is a standalone build: our exe, from our source, running the original assets.

This is a periodic snapshot of the code, the extracted facts and the structural documentation. It has no history,
and the working notes and planning documents are left out, so some documents mention files that aren't here.

Nothing here contains game code, game bytes, decompiler output or game assets. You need your own copy of the game.

Written largely by Claude Opus 5.5 (Anthropic), directed by a human collaborator.

## Where to start
- `docs/subsystems.md`: the map of the game's subsystems, and `docs/subsystems/<name>.md`, what is known about each one.
- `docs/subsystems/persistence.md`: how a neighbourhood is loaded and saved. Which class reads and writes each resource
  type in a neighbourhood package, how a record becomes an object, and the load and save controllers step by step.
- `docs/save-diff.md`: save file formats at field level, and the tools that compare saves.
- `docs/startup-map.md`: what the game does from process start to the main menu.
- `docs/verification.md`: what each status in `db/status/` means and what evidence it needs.
- `docs/contracts/`: infrastructure we replace outright (platform, Steam, crash reporting), each with its written contract.

## Layout
- `target/`: the pinned executable (Steam Legacy Collection `Sims2EP9.exe` 1.18.0.187, an MSVC 14.40 build from 2025).
  Addresses throughout are RVAs in this build; Ultimate Collection-era addresses don't carry over.
- `db/`: facts extracted from the binary, as JSON lines. Function addresses and sizes, names, classes, RTTI, vtables,
  interface slots, call edges, string references and annotations, plus `db/status/`, one entry per function we have
  mapped, reimplemented or verified. Some `db/` files are left out of the snapshot: the library matches (which
  functions are MSVC library code, 108 MB) and the bookkeeping our checks keep for themselves. Several tools read
  those files, so not every tool runs from the snapshot.
- `src/`: our C++. Classes are written to the original's memory layout, so they can replace the original functions in
  the running game: read them as a precise reference for what each function does, not as idiomatic standalone code.
- `tests/`: the differential tests, which run the original function and ours on the same inputs and compare results and
  side effects.
- `host/`, `harness/`, `standalone/`, `record/`: the game host, the test harness, standalone build pieces, record/replay.
- `tools/`: Python and PowerShell tools (status, checks, package reading, game runs). `tools/status.py` prints progress
  from `db/status/`.

## Related work
- [LazyDuchess/TS2-Extender](https://github.com/LazyDuchess/TS2-Extender): fixes and a Lua API for Legacy/UC.
- [LazyDuchess/OpenTS2](https://github.com/LazyDuchess/OpenTS2): an engine reimplementation from the file formats.
- [OmniBlade/LasMarionetas](https://github.com/OmniBlade/LasMarionetas): an earlier hook-based reimplementation (UC).
- [spockthewok](https://github.com/spockthewok?tab=repositories): native fix patches.

## License
MIT (`LICENSE`).
