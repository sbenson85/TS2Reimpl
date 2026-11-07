# Startup: the framework, the COM registry, the directors and the application (subsystem notes)

Structure pass by lane EC (2026-10-18). The step-by-step chart of the run (CRT entry, WinMain, the
startup state machine, the bands, the main loop) is `docs/startup-map.md`; this page is the class
view: what the objects are, how they are built, what is done, and what the standalone build has to
own. The implementation split is the "## Handoff" in `notes/feedback/EC.md`. RVAs are 1.18.0.187.

> **Correction (2026-10-18, lane FG, checked by the coordinator):** the host installs the hooks before it calls the
> image's entry point (host/main.cpp: InstallHooks, then `entry()`), so WinMain's statics run *after* the hooks. Since
> lane EX the live framework is built by our cCTGFramework constructor and carries our vtables (host log
> `cCTGFramework_Construct=1`), and since FG the application too (`cTSAppFW_Construct=1`). Notes below that say the
> framework or cGZCOM is "built before the hooks" (EN, ER, EX) are wrong: in game our vtables are the route, the
> adapters serve only objects the game builds through other paths.

## Why this subsystem

Roadmap stage 4 / milestone E: "the CRT entry, WinMain, RunApp and GZFrameWork_Startup state 0 run
from our source, with the framework object constructed by our constructor". Everything the game does
hangs off five objects built here, and the hook-shaped code for them is already mostly verified
(below), so the remaining work is mostly *shape* (our classes, our vtables) plus a handful of large
app functions.

## The classes

All of them are real C++ classes in the original (RTTI). The standalone-shape headers (declarations
and layouts only, checked by `tests/startup_shape_layout.cpp`) are new in this pass; the hook-shaped
ones are what the verified functions use today.

| Class | Size | Faces (offset: interface, game vtable, slots) | Ctor / dtor | Shape header | Hook-shaped header |
|---|---|---|---|---|---|
| `cRZUnknownMT` | 8 | +0 own (0xda2fec, 6) | 0x1b9078 / deleting 0x1b912e | `rz/RZSystemServiceBase.h` (rz::) | - |
| `cRZSystemService` | 0x1c | +0 cIGZSystemService (0xda3058, 13), +4 cRZUnknownMT (0xda3090, 6) | 0x1b9584 (id, prio), 0x1b95bf (id, prio, tickPrio) / 0x1b95fe, deleting 0x1b961a | `rz/RZSystemServiceBase.h` (rz::) | `rz/cGZAllocatorService.h` |
| `cGZCOM` | 0x70 | +0 cIGZCOM (0xd9b84c, 14), +4 cRZSystemService (0xd9b888, 13; cRZUnknownMT 0xd9b8c0 at +8), cRZThreadSafeObject +0x20 (no vtable) | 0x144e52 / 0x145075, deleting 0x145114 | `gz/FrameworkShape.h` (gz::) | `gz/GZCOM.h` |
| `cGZFramework` | 0x180 | +0 cIGZFrameWork (0xd9b544, 40 + own 9), +4 cIGZFrameWorkW32 (0xd9b60c, 7) | 0x141d76 / 0x141f5c, deleting 0x142207 | `gz/FrameworkShape.h` | `gz/Framework.h` |
| `cCTGFramework` | 0x198 | the above (0xe995cc, 0xe99694) + cIGZMessageTarget2 +0x180 (0xe996b4, 4) | 0xc2ccdb / 0xc2ce90, deleting 0xc2cec9 | `gz/FrameworkShape.h` | `gz/Framework.h` |
| `cRZCOMDllDirector` | 0x64 | +0 cIGZCOMDirector (0xda265c, 16 + deleting dtor), +4 cIGZFrameWorkHooks (0xda26a4, 11) | 0x1adffa / 0x1ae048, deleting 0x1448ca | `rz/RZCOMDllDirectorShape.h` (rz::) | `rz/StaticGetters.h`, `rz/COMDirectors.h` |
| `cGZCOMDirector` (root) | 0x64 | as the base (0xd9b754, 18; 0xd9b7a0) | 0x144897 | same | `rz/StaticGetters.h` |
| `cGZApp` | 0x40 | cRZSystemService +0 (0xe9d118, 0xe9d150), cIGZApp +0x1c (0xe9d16c, 15) | 0xc59f5a / 0xc59f91, deleting 0xc59fbf | `ts/cTSAppFWShape.h` (gz::) | `app/AppBase.h` |
| `cTSAppCommonImpl` | 0x18 | cITSAppCommon +0 (0xeb5468, 18; slots 1, 2, 13 pure) | 0xca4cb0 / 0xca4de0, deleting 0xca4f50 | `ts/cTSAppFWShape.h` (ts::) | `app/AppBase.h` |
| `cTSAppFW` | 0x268 | cGZApp +0 (0xe98464, 0xe9849c, 0xe984b8), cITSAppFW +0x40 (0xe984f8, 5), cTSAppCommonImpl +0x44 (0xe98510), cIGZMessageTarget2 +0x5c (0xe9855c) | 0xc26147 / 0xc266b9, deleting 0xc267d9 | `ts/cTSAppFWShape.h` (ts::) | `app/AppCtor.h` |

Interfaces (new shared headers, global names as their RTTI): `gz/cIGZSystemService.h`,
`gz/cIGZFrameWork.h` (cIGZFrameWork, cIGZFrameWorkW32, cIGZUnknownEnumerator),
`gz/cIGZFrameWorkHooks.h`, `gz/cIGZMessageTarget2.h`, `gz/cIGZApp.h`, `gz/cIGZCOM.h`,
`gz/cIGZCOMDirector.h`, `ts/cITSAppFW.h` (cITSAppFW, cITSAppCommon); cIGZCmdLine is in
`rz/cRZCmdLine.h`, cIGZString in `gz/cIGZString.h`. The slot facts are in the Ghidra program and
`db/methods/{Framework,GZCOM,cRZSystemService,cRZCOMDllDirector,cTSAppFW}.jsonl`.

Shape notes (for the implementation lanes):

* MSVC puts bases without a vfptr after those with one: cGZCOM's cRZThreadSafeObject lands at +0x20
  although it is declared second. The static_asserts check it.
* One override covers every face that declares the slot, which is what the original's vtables show:
  cGZCOM's SetServiceRunning fills cIGZCOM slot 11 and cIGZSystemService slot 7 (thunk 0x145c24);
  cTSAppFW's destructor fills cRZUnknownMT slot 3 (thunk 0xc26780) and cITSAppCommon slot 10 (thunk
  0xc26788).
* cITSAppCommon's virtual destructor sits at slot 10, in the middle of the interface: it is declared
  in place.
* cGZFramework's slots 40..48 are its own virtuals (destructor, the six band loops, an empty slot 47,
  MainLoop). cCTGFramework overrides all but 47.
* The class shapes hold still-hook-shaped members (cRZCriticalSection, cRZCmdLine, cRZString,
  cRZRandom, the exception reporter): their constructors install the game's vtables. They keep the
  layout right, but each must be taken to the shape before the build can run without the image
  (handoff S0).
* cGZApp's slots 11 and 12 are 0x140fe0, a bare `ret` (al undefined). cTSAppFW overrides both, so the
  value is never read.

## Creation order

1. Static initialisers before WinMain: the cGZMadCowCOMDirector static (0x1408d0) and ~800 others.
2. WinMain 0xc2b87c: crash reporter, Steam init, analytics, handlers (all `replaced` or
   replaceable), the cRZCmdLine on its stack, then two function-local statics: the **framework**
   (cCTGFramework at 0x10fdb50, ctor 0xc2ccdb -> cGZFramework 0x141d76 -> the embedded **cGZCOM**
   0x144e52 and cRZCmdLine) and the **application** (cTSAppFW at 0x10fdcf0, ctor 0xc26147 -> cGZApp
   0xc59f5a -> cRZSystemService 0x1b9584, cTSAppCommonImpl 0xca4cb0, the exception reporter
   0x1ba914). Each registers its destructor with atexit (0xd988fa, 0xd988f0).
3. RunApp 0xc2d23b -> RunFramework 0xc2d0a8: g_Framework (0x10c9378/0x10c937c), g_App (0x10c9380,
   the cIGZApp face), g_AppForMsgHook (0x10fdfe8).
4. GZFrameWork_Startup 0x144514, state 0: the command line, `cGZCOM::RealInit` (which builds the
   **root director** on first use: GetCOMDirector 0x14491b, ctor 0x144897) and registers cGZCOM as
   the first service; then the app's AddApplicationServicesHere (12 services), AddCOMDirectorsHere
   (the app director cTSAppFWCOMDirector and its 40-odd sub-directors, then their OnStarts, which
   create most engine services), AddDynamicLibrariesHere. Then PreFrameworkInit, the hooks, and the
   three init bands.

## The services

Registration is `cIGZFrameWork::AddSystemService` (slot 3, 0x14234f): the service goes into the
priority multimap (+0x84, key = GetServicePriority) and the id hash map (+0x8c). Init and shutdown
run in three bands (cCTGFramework slots 41..46): high (priority > -2,000,000; Init in descending
priority), the app band ([-2,999,999, -2,000,000]: only cTSAppFW), low (< -2,999,999: the CAS
catalogs and UI script). Shutdown runs the bands in reverse, ascending within each. Lookup is slot 5
`GetSystemService(id, iid, out)`. Who registers what: `docs/startup-map.md` 4.4 (directors' OnStart)
and 4.5 (the application's twelve, with the table of ids and priorities).

### Milestone I: the services still of the image's class (lane PU sizing, 2026-10-29)

At the main menu 55 of 66 services were ours before lane PU (56 expected after it, 58 after lane PW, 59 after lane PZ) (`tools/services.py <dump> --todo`). The small standalone ones among the 11 left,
sized by `tools/classinfo.py <rtti> --todo` (open bytes: functions of the class not yet ours; "slots": the class's distinct
vtable functions). Every one still carries the game's vtables, so the creator must build our class before any slot counts.

| Class (service id) | Open | Slot functions | Still original, besides its own code | Order |
|---|---|---|---|---|
| cGZPropertyManagerT<string traits> (0x0ac1887e) | 1,074 B | 40 (19 already verified: the cGZPropertySetString slots it inherits, cRZSystemService's) | nothing: its constructor (AppInit, hook-shaped) and cGZPropertySetString / cRZSystemService / cRZMessage2Standard are ours; two cGZPropertySetString helpers (SetDefault 0x1bf595 / 0x1bf539) only it calls | done (lane PU): src/gz/cGZPropertyManager.h, 20 entries verified in db/status/cGZPropertyManager.jsonl, the constructor hook 0xc26045 builds our class |
| cRZNetSSLManager (0x2c805c54) | 1,590 B | 36 | OpenSSL 0.9.7d (third party: SSL_CTX_free, RAND_seed, SSL_SESSION_free; called through OriginalFunction), `_time64`; constructor ours but hook-shaped (AppInit) | done (lane PW): src/rz/cRZNetSSLManager.h, 32 entries verified in db/status/cRZNetSSLManager.jsonl, the constructor hook 0xc644aa builds our class; OpenSSL stays the image's (original_calls); the verify and DH callbacks (0xc64f6f, 0xc64ee9) are handed to OpenSSL as game addresses and are not ours yet |
| nGZLog::cService (0x09b69c4e) | 23 B | 49 (47 verified) | only the two `shared_vf_*` slots 0x14a260 [8], 0x470710 [26]; the class is hook-shaped (three faces 0xeb4620 / 0xeb4658 / 0xeb4674 at +0x1c), so the retrofit is the whole class's shape, not new behaviour | done (lane PW): class nGZLog::cLogService (rz::cRZSystemService + cIService, src/rz/nGZLog.h) with our vtables on all three faces, built by cLogServiceAdapters::Construct (hook 0xc9c309, and cTSAppFW::AddApplicationServicesHere); the two folded slots are our overrides, never hooked; the Service* bodies keep their hooks and the faces forward to them |
| cRZNetManager (0x654383c9) | 2,689 B | 42 | registry (RegOpenKeyExA / RegQueryValueExA / RegSetValueExA), GlobalAlloc, the winsock thunks 0xd1800d / 0xd18013 / 0xd18019, strncpy, `_time64`; built by cTSAppFW's NewObject (hook-shaped Construct) | done (lane PZ): src/rz/cRZNetManager.h, 31 entries verified in db/status/cRZNetManager.jsonl (the 0xc65a40 clear, the shared 0x530460 GetDialTarget included), the constructor hook 0xc65932 builds our class through cRZNetManagerAdapters::Construct; the two socket constructors 0x9998e2 / 0x999970 stay the image's (original_calls). Untested because the original faults there: a null dial-params / hang-up function in Dial, a null projection function in Disconnect, a failed reallocation in EnumEntries, the RAS callback's null global |
| cTSCheatSystem | 1,441 B | 85 (67 open) | nGZCommandParser::cBaseParser / cParserIsState / cState bases with virtual bases (vtordisp thunks); five faces, one of 51 command-parser thunks: larger than its byte count | 5 |
| cTSWinCheatConsole | 3,907 B | 63 | a UI window (cGZWinGen-family bases) | 6 |
| cTSWebService (0x8bfc2f67) | 8,218 B | 53 | HTTP work, the nested cCheckIfFilesExistOnServerList; faces +0, +4, cIGZMessageTarget2 +0x1c, cITSWebService +0x20 (31 slots) | 7 |
| cGZWinDragAndDropManager (0x0c0874c6) | 14,489 B | 201 | the window base class (most of its 201 functions) | 8 |

Also of the image's class at the main menu, on other campaigns: nGZSceneDBAnim::cSceneDBAnimSystem (5,258 B), cHitMan
(4,763 B), nTSSG::cTSSGSystem (110 B).

## What is verified

See `docs/startup-map.md` section 6 for the per-group table. In short: the framework 70/76, cGZCOM
38/38, cRZCmdLine 34/34, cRZString 38/38, the directors 196/197, the application constructor and
its services' constructors all verified, WinMain's helpers 21/23 plus four `replaced`. Not done:
the 16 mapped cTSAppFW / cTSAppCommonImpl functions (Init 3375 B, PreFrameworkInit 1500 B,
AddDynamicLibrariesHere 1680 B, AddApplicationServicesHere 1018 B, the option parser 4414 B, ...),
seven director OnStarts, the cGZFramework base slots 40..48, the faces' forwards and thunks (the
shape makes them the compiler's), WinMain (blocked on the CRT seam), and every class's shape.

### S1 done: rz::cRZUnknownMT and rz::cRZSystemService in the shape (lane EI)

* Bodies in `src/rz/RZSystemServiceBase.size.cpp` (QueryInterface 0x1b9650 in `RZSystemServiceBase.cpp`: the game built
  it for speed), adapters `rz::cRZUnknownMTAdapters` / `rz::cRZSystemServiceAdapters`, table
  `src/hook/tables/RZSystemService.cpp`, status `db/status/RZSystemService.jsonl`, tests `tests/rz_system_service.cpp`
  (Construct, Destructors, RefCount, Slots, Adapters).
* Hooks moved here from cGZAllocatorService (0x1b9584, 0x1b95fe), cRZRefCount (0x1b9517, 0x1b9544, 0x1b912e, 0x1b90ab)
  and QueryInterfaces (0x1b9650), now adapters; new: 0x1b9078 (cRZUnknownMT constructor), 0x1b95bf (the three-argument
  constructor), 0x1b961a (the deleting destructor; db/names had a std::pair name for it by shape).
* The hook-shaped structs stay for our own hook-shaped code: `::cRZSystemService::Construct/Destruct`
  (rz/cGZAllocatorService.h, ~60 services build their base with it, game vtables), `::cRZUnknownMT` (rz/cRZRefCount.h;
  its Release / RemoveRef / DeletingDestructor members are no longer hooked, only tested by tests/rz_refcount.cpp), and
  `qi::SystemServiceQueryInterface` (the services' QueryInterface tail calls). When a service class takes the shape it
  derives from rz::cRZSystemService and stops using them; the last one deletes them.
* A class deriving from rz::cRZSystemService gets QueryInterface / AddRef / Release from it by overriding (forward to
  `rz::cRZSystemService::QueryInterface(riid, out)` for the default case, as the originals' `jmp 0x1b9650`); its +4
  (cRZUnknownMT) face's slots 4 / 5 stay the base's (0x1b9544 / 0x147a30 in every game vtable checked).
* Interface aliases (lane EI): `ComFramework`, `NbrFramework` = cIGZCOM (slot 3 is `GetClassObject`, the callers were
  renamed from `CreateInstance`), `SimServiceHub` = cIGZFrameWork (slot 5 `GetSystemService`, was `GetInterface`).
  cIGZCOM::AddLibrary's fact now takes `cIGZString *` (the generated view could not name `const cIGZString &`).

### F1 done: cGZCOM and cGZCOMLibrary in the shape (lane EN)

* `::cGZCOM : cIGZCOM, rz::cRZThreadSafeObject, rz::cRZSystemService` (0x70 B) and `::cGZCOMLibrary : cIGZCOMLibrary`
  (0x34 B, new interface header `src/gz/cIGZCOMLibrary.h`) in `src/gz/GZCOM.h`; the hook-shaped `gz::cGZCOM` is gone from
  FrameworkShape.h. Bodies in `src/gz/GZCOM.size.cpp`, adapters `cGZCOMAdapters` / `cGZCOMLibraryAdapters`, table
  `src/hook/tables/GZCOM.cpp`, status `db/status/GZCOM.jsonl`, tests `tests/gz_com_*.cpp` (new: `gz_com_guid.cpp`,
  `gz_com_adapters.cpp`).
* New: CreateGuid 0x145325 and CreateGuids 0x1453ab (both byte-match; the quarter-second wait 0x1b9f95 stays an original
  call). The six adjustor-thunk hooks (0x14510c, 0x145b36, 0x145b3e, 0x145c04, 0x145c0c, 0x145c24) were dropped: the game's
  thunks jump into the hooked primaries, ours are the compiler's.
* Creation: the game builds the framework (and cGZCOM at fw+8) before the hooks go in, so in game the live object keeps the
  game's three vtables and is served by the adapters; our vtables are only used by objects we build (the tests, and
  `fw_lifecycle::ConstructBase` / the 0x144e52 hook, which now placement-new our class). The libraries in the set are ours from
  AddLibrary on (the set's nodes are built by our std::set), so in game they carry our vtable, and the game's callers reach them
  through it (EnumerateLibraries hands them out).
* Bisect switch: the GZCOM table (all adapters) together with the fw_lifecycle construct/destruct hooks: with them off, the
  game's own cGZCOM code runs and nothing of ours is reached.

### D1 done: the directors in the shape (lane EO)

* `rz::cRZCOMDllDirector` (base, every slot ours; slot 13 is `_purecall` as in the game's vtable), `rz::cGZCOMDirector`
  (root: id 0x1448f1, slot 17 0x1448f7 `StoreFrameWork`: **no argument**, stores GetServiceObject() at +0x34 and
  returns whether it is non-null; EC's plan had an argument) and `rz::cCOMDirectorT<N>` for the 41 game directors.
  Bodies: `src/rz/RZCOMDllDirector.cpp` (slots), `RZCOMDllDirector.size.cpp` (constructors, destructor, GetCOMDirector),
  `src/rz/COMDirectors.cpp` (the 15 verified OnStarts, the getters, InitMadCow). Adapters in the shape header; table
  `src/hook/tables/COMDirectors.cpp` (the base / root constructors, base destructor and GetCOMDirector moved here from
  StaticGetters).
* Creation: all 42 statics are built by our getters / GetCOMDirector / InitMadCow with placement new; the base
  constructor 0x1adffa has no other callers than those (now hooked) functions and the audio constructor. The audio
  director (cTSAudioCOMDirector, three faces) stays hook-shaped, deriving from a thin `comdir::COMDirector`.
* Left for D2: seven OnStarts (TSPersist 0x1d3e20, TSSG 0x2454c0, TSSim 0x416370, TSUI 0x5b72e7, TSAppFW 0xc2beec,
  TSCommon 0xcb5110, TSCAS 0xcb54a0) and six hooks-face slots (TSUI 0x5b7722 / 0x5b763c, Sound 0xb224d9 / 0xb2252e,
  HitMan 0xb2a4c3 / 0xb2a568): our vtable slots forward to them through OriginalFunction (listed in the getters'
  `original_calls`); they are not tracked in db/status yet.
* Bisect switch: the getters, GetCOMDirector (0x14491b), the constructors (0x1adffa, 0x144897, the 41 directors') and
  InitMadCow (0x1408d0) together decide whose vtables the directors carry; the slot adapters only matter for game
  vtables, which no director carries once those are on.

### A1 (part): ts::cTSAppCommonImpl, the option store, in the shape (lane EP)

* Class `src/ts/cTSAppCommonImpl.{h,cpp}` (moved out of `cTSAppFWShape.h`, which includes it), adapters
  `ts::cTSAppCommonImplAdapters`, table `src/hook/tables/cTSAppCommonImpl.cpp` (13 entries; bisect switch), status
  `db/status/cTSAppCommonImpl.jsonl`, tests `tests/ts_app_common_impl.cpp`.
* **Open question 4 answered.** Both maps are MSVC 14.40 `std::map<uint32_t, OptionValue>` (node 0x20: the key at +0x10,
  not an 8-byte key; the next word is the value's alias), `OptionValue{uint32_t alias; std::list<OptionEntry>}`,
  `OptionEntry{cRZString text; uint32_t mask}` (list node 0x2c). +4 holds the defaults, +0xc the overrides. The
  difftests compare the trees node for node (shape, colours, links) after every AddOption, so the type is settled.
  Library helpers by shape: 0xca9b00 list push_front, 0xca4b10 list copy, 0xca4e60 map operator[], 0xca49a0 the value's
  move assignment, 0xca9990 map erase(key), 0xca4ac0 std::find over entries.
* **Arguments.** AddOption(key, alias, text, toOverrides (low byte), mask): null text erases; an existing key keeps its
  alias; an equal entry (mask, then case-sensitive IsEqual) is not added again; new entries go to the front; the result
  is undefined. Readers (slots 11 / 12, and 4 / 5 which clear the output first): (id, out, a, defaultsOnly, mask): `a`
  is only passed on; the overrides are searched first unless the low byte of defaultsOnly is set. An alias makes the
  aliased option's value (read with entry mask & mask) a prefix. GetOptionList's list is a `std::list<std::string>` on
  the allocator-service allocator.
* **Names corrected.** "ParseOptions" 0xca75c0 finds the install and product directories (registry, ini 'Directories',
  file system; options 0x6bcfce93 and 0x4bcfce5e); "ClearOptions" 0xca6c10 reads the resource-configuration ini
  (sections ResourceFactories, DBSegments, DBSegmentDirectoryFiles). These two and 0xca8710 (base
  SetupUserDataDirectories, overridden by cTSAppFW) are `mapped`: our slots call the originals.
* The only creation site, cTSAppFW's constructor, is still hook-shaped (game vtable 0xe98510), so in game the adapters
  serve that object; our vtable is exercised by the difftests. Our class gives the game's `_purecall` slots 1, 2 and 13
  bodies that call `_purecall`, so it is not abstract (`tests/startup_shape_layout.cpp`).

### F2 done: gz::cGZFramework, the base, in the shape (lane ER)

* `gz::cGZFramework : cIGZFrameWork, cIGZFrameWorkW32` (0x180 B) in `src/gz/FrameworkShape.h`, bodies in
  `src/gz/GZFramework.size.cpp`, adapters `gz::cGZFrameworkAdapters` (one statement each, written out; the two +4-face ones in
  the `__assume` form), table `src/hook/tables/Framework.cpp` (the constructor 0x141d76 and destructor 0x141f5c moved here from
  the GZCOM table). Slots 0..40 and the W32 face are ours; slots 41..46 and 48 (cCTGFramework's overrides in game) call the
  originals through `OriginalFunction` until F3, 47 is empty as in the original.
* New: the deleting destructor 0x142207, StdOut / StdErr / StdIn 0x143887 / 0x1437e7 / 0x143837 (the stream constructors
  0x1b4dbe / 0x1b4da6 stay original calls until cRZOStream / cRZIStream are done), SetCommandLine 0x143660 and
  CommitCommandLine 0x1427a7 (byte match). AbortiveQuit 0x142270 is written and difftested but not hooked (it ends in `exit`, the
  game's CRT; ours would skip the game's atexit list): `cGZFramework::sExit` is the seam the tests redirect.
* The hook-shaped `::cGZFramework` (src/gz/Framework.h) keeps the layout and forwards its non-virtuals to ours, for the
  hook-shaped cCTGFramework code (Framework.size.cpp, FrameworkLifecycle.size.cpp); `fw_lifecycle::ConstructBase` /
  `DestructBase` call our adapters. (Retired by F3, below.)
* Creation: the only caller of 0x141d76 is cCTGFramework's constructor 0xc2ccdb, which WinMain's static runs before the hooks go
  in. In game the live object keeps the game's vtables (0xd9b544 / 0xd9b60c are overwritten by cCTGFramework's anyway) and every
  call reaches us through an adapter; our vtables are only used by objects we build (the tests). The folded one-liners
  (shared_vf_*) are not hooked: the game's shared bodies serve the live object.
* F3 (cCTGFramework) needs: derive `gz::cCTGFramework : gz::cGZFramework, cIGZMessageTarget2`, override 41..46 and 48 (then
  delete the `OriginalFunction` placeholders and their `original_calls` on 0x141d76), turn the hook-shaped `::cGZFramework`
  forwards and `fw_lifecycle` into calls on the class, and keep the creation order (built by WinMain's static before the hooks).
* Bisect switch: the Framework table's base entries (all `gz::cGZFrameworkAdapters` and `CommitCommandLine`); with them off the
  game's own base code runs. The CTG entries in the same table are separate.

### F3 done: gz::cCTGFramework, the live framework, in the shape (lane EX)

* `gz::cCTGFramework : gz::cGZFramework, cIGZMessageTarget2` (0x198 B; vtables 0xe995cc / 0xe99694 / 0xe996b4) in
  `src/gz/FrameworkShape.h`, bodies in `src/gz/Framework.size.cpp`: constructor (the two message ids, then the fifteen
  manual service ids in the original's order), defaulted destructor, the class's own cIGZUnknown entries (forwards to the
  base), OnTick (running services only), the six bands (the +0x18c set initialised but not started; InitHigh's persist
  message, the message server's subscriptions and service), MainLoop (tick flag through slot 17, the application's
  message hook) and DoMessage. Adapters `gz::cCTGFrameworkAdapters` (one statement each; Construct, Destruct and the
  deleting destructor written out) in the Framework table; the constructor and destructors moved there from the GZCOM
  table. `FrameworkLifecycle.*` and the hook-shaped members are gone: `gz/Framework.h` is now layout views only (tests,
  WinMain's code). GZFrameWork_Run / Shutdown / ReleaseFrameworkGlobals and GZFrameWork_Startup call the class's members
  on g_Framework (slots through the object's vtable, as the original).
* The base's slots 41..46 and 48 (0x14307f, 0x1425c7, 0x142f87, 0x143133, 0x142662, 0x142ffb, 0x1435c2) are our own bodies
  now, hooked through `gz::cGZFrameworkAdapters` though never run in game (overridden); the `OriginalFunction` placeholders
  and 0x141d76's `original_calls` are gone. The base framework calls no original code except the stream constructors
  0x1b4dbe / 0x1b4da6.
* Not hooked: 0xc2d995 / 0xc2ceeb / 0xc2d9ad (5-byte jumps to the base's QI / AddRef / Release; hookspec refuses them as
  thunks) and the six adjustor thunks of the +4 and +0x180 faces; verified through our vtables in FW_Basics.
* Creation order unchanged: WinMain's static (0x10fdb50) runs 0xc2ccdb before the hooks go in, so the live object keeps the
  game's vtables and every call reaches us through an adapter. Milestone E: our constructor can build it now, and WinMain's
  candidate (`src/app/WinMainInfra.cpp`, unhooked until E1) does (`::new (storage) gz::cCTGFramework`).
* Tests (`tests/framework.cpp`, `tests/framework_startup.cpp`, `tests/gz_com_framework.cpp`): three routes (adapters on
  original-built objects with trap vtables; base vtable on base objects; cCTGFramework vtable on objects from our
  constructor). FW_BaseBands and FW_MainLoopScripted (a scripted queue through the patched user32 import slots) cover the
  base's slots; FrameworkStartup_Startup runs the bands for real on fake services (on side B through the band adapters or our
  vtable). 34 mutants, all killed.
* Byte matches: matchcheck v7 = v6 = 14/72 (324 B; F2 had 13/65): ER's matches kept, plus GZFrameWork_Run and
  ReleaseFrameworkGlobals (one of them new). The seven new base bodies and the cCTGFramework bodies don't match (EH
  prologue / register allocation). Every adapter that is a `jmp` shows "size 5 vs
  original N": v7's vtable tie keys on the RTTI name, which carries the gz:: namespace.
* Bisect switch: the Framework table's cCTGFramework entries (`gz::cCTGFrameworkAdapters`: OnTick, the bands, MainLoop,
  DoMessage, the constructor and destructors) plus GZFrameWork_Run / Shutdown / ReleaseFrameworkGlobals; the base entries
  (`gz::cGZFrameworkAdapters`) are separate (F2's switch). The seven base band entries never run in game.

### D2 done (partly): the directors' own overrides (lane ET)

* Ours now, in the explicit specialisations of `rz::cCOMDirectorT<N>` (src/rz/COMDirectors.cpp), hooked through
  one-statement adapters (new: the hooks-face adapters PreAppInit / PostAppInit / PreAppShutdown in
  `rz::cCOMDirectorAdapters<N>`): OnStart of TSPersist 0x1d3e20, TSSG 0x2454c0, TSSim 0x416370, TSAppFW 0xc2beec, TSCommon
  0xcb5110, TSCAS 0xcb54a0; StartEngineDirectors 0x14107c (`comdir::StartEngineDirectors`, cdecl, hooked directly); Sound
  PostAppInit 0xb224d9 / PreAppShutdown 0xb2252e; HitMan PreAppInit 0xb2a4c3 / PreAppShutdown 0xb2a568; TSUI PreFrameWorkInit 0x5b7722. Tests:
  `tests/com_directors_start.cpp` (fake world, three routes, exceptions at every call; see its header).
* Original callees left (the services they build; their own lanes): the constructors of the persist system 0x1d8b90
  (0x268 B), the scene-graph system 0x246110 (0x3b8 B, added through its +0xc face), the sim system 0x416810 (0x170 B), the
  realtime debug display 0x9704bb (0x34 B), and the cached sound-service getter 0x9c5b7e, and TSUI's 0x5b7a9c (builds a small game object, 0xdd87d8 vtables, and hands it
  to a service's slot 40).
* Byte matches: none (v7 = v6: 0/12). Seven of the originals are size-built (EH prologue `_EH_prolog`); the bodies sit in
  the speed-built COMDirectors.cpp. Moving them to a .size.cpp is a cheap follow-up but EO saw no gain for this group.
* The 37-byte "GetSystemService into a fresh holder" functions (0x20db8e, 0x20db69, 0x212e8c, 0x22b0b2, 0xb29d53, ...: one
  per id pair, `ref = 0; if (service) service->slot5(id, iid, &ref)`) are one local helper (`QueryService`); they are not
  hooked (many copies, shared by unrelated callers).
* The hooks-face overrides have no EH frame: an exception leaves their holders unreleased. Ours use plain holders
  (ScopedRef, RZAutoRef, AllocatorServiceHolder, released by our replacements) so they do the same.
* Still forwarding (OriginalFunction, listed in the TSUI getter 0x5b6847's `original_calls`): TSUI OnStart 0x5b72e7 (853 B: the
  XBreak option parse with cRZString tokens and _wtoi, cheat tool registrations), TSUI PostAppInit 0x5b763c (an AnyBoolean
  local carrying a game vtable, a std::string on the service allocator, RZMemory::Malloc and a message object sent to the
  message server); cTSAudio DoMessage 0x20e7c7 (2 KB) not started. They need cIGZAny / cRZString / message classes in the
  shape first.
* Test fix: `COMDirectors_Construct_TSAppFW` (and any Construct_ test run first) failed in a fresh image: `IsBuilt` took
  the MadCow static's zero word for "built", so EnsureMadCow never built it and the application director's constructor
  added a director with a null vtable (fault at 0x1ae0f1 in AddDirector). IsBuilt now treats a zero word as not built.
* Bisect switch: the eleven ET entries in src/hook/tables/COMDirectors.cpp only matter for game vtables; since every
  director carries our vtables (D1), the real switch for our bodies is the getters / constructors (D1's switch). To
  isolate ET alone, restore the specialisations' OriginalFunction forwards (git history) or disable the specialisations.

### A1 (rest): gz::cGZApp and two of the directory slots (lane EV)

* `gz::cGZApp` in `src/gz/cGZApp.{h,size.cpp}` (moved out of `cTSAppFWShape.h`, which includes it), adapters
  `gz::cGZAppAdapters`, table `src/hook/tables/cGZApp.cpp` (5 entries: constructor, destructor body, deleting destructor
  0xc59fc7 (new), QueryInterface, AddApplicationService; moved from WinMain / AppCtor / TSApp), status `db/status/cGZApp.jsonl`
  (12 entries incl. the five compiler thunks 0xc59fbf, 0xc5a01e, 0xc5a026, 0xc5a02e, 0xc5a036), tests `tests/gz_app.cpp`
  (ConstructDestruct, Slots: all 34 slots of the three faces, Adapters). AddRef is the folded 0x1cc320 (an override calling
  the base's), Release the folded 0xb359c9 (`return RemoveRef();` through the +4 vtable); the name stays a hook-shaped
  cRZString in an anonymous union. Creation: only cTSAppFW's constructor 0xc26147 calls 0xc59f5a, and the three vtables are
  stored only by 0xc59f5a / 0xc59f91, so until A2 the live application keeps the game's vtables and the adapters serve it.
  The hook-shaped `appbase::cGZApp::Construct`, `appctor::DestructGZApp` and `tsapp::GZApp*` stay (cTSAppFW's hook-shaped
  code calls them directly); they go with A2.
* **cITSAppCommon slots 13-16 return bool** (al): 0xc2a7bb (InitOptions) tail-calls slot 14; 0xca75c0, 0xca8710 and 0xca6c10
  end in `mov al, 1` (0xca8710 `xor al, al` when slot 13 fails). Renamed: slot 14 `FindProductDirectories`, slot 16
  `ReadResourceConfiguration` (cITSAppFW.h, cTSAppFWShape.h's two declarations follow).
* Ported, `src/ts/cTSAppCommonImplDirs.cpp`, tests `tests/ts_app_common_impl_dirs.cpp`: **0xca8710** SetupUserDataDirectories
  (the base; reached in game: cTSAppFW's 0xc2a7de calls it directly before its own options) and **0xca6c10**
  ReadResourceConfiguration with its three section callbacks 0xca74b0 / 0xca7020 / 0xca7130 (hooked; only 0xca6c10 uses them).
  The tests read nothing real: g_Framework is a fake (command line switches, the registry service), the registry reader
  0xcb4690, `cRZFile::PathExists` (0x1c1242 and ours) and the ini's ProcessSection 0x9c5ef2 are redirected to fakes on both
  sides, the forwarders' cached cITSAppCommon (0x10cc040) is the side's own object, filled with synthesised paths.
  Left as original calls: 0xcb4690 (registry reader), 0x1c2732 (MakePath), 0xb39360 (product mask, `mov eax, 0x20000`),
  0x9c5df3 / 0x9c5ef2 (cRZIniFile constructor and ProcessSection).
* **Ported (lane FC): 0xca75c0** FindProductDirectories (4.4 KB, slot 14) and its flag test **0xca6910** (`HasFlags`,
  the word at +0x14, now `mFlags`; bit 0 = development layout), both hooked through adapters, in
  `src/ts/cTSAppCommonImplDirs.cpp`. What it does: the file-system service's application directory (0x9c59d9 = the cached
  cIGZFileSystem face 0x10f51c4; slot 4), '/'-normalised -> option 0x6bcfce93. Development layout: '../TSData' made absolute
  if DirectoryExists, else switch 'ep0dir' + 'TSData'; masks 1 and 0x20000. Installed layout: 0xcb4690('Sims2.exe') writes
  the products' registry key ('Software\Electronic Arts\The Sims 2 Ultimate Collection 25\') and the base game's directory
  (the executable's, three levels up + product name; it reads no registry itself, only GetModuleFileNameA); 0xcb4bb0 reads
  the installed products' directories from the registry (malloc'ed array, freed by the caller). The 'ep' switch lists the
  enabled products (',' tokens through atoi 0x830972, 1..31; db/ calls it `_wtoi`, whose wrapper is byte-identical, but
  its parser 0x82fdbf reads chars, so it is atoi: the difftest settles it); product 0
  always, all without the switch. Per product n the switch **and** ini key is `ep<n>dir` (format 'ep%ddir'): switch, else
  the 'Directories' section of the ini at option 0x6c253729, else the base directory (n 0) / the product list entry n-1 /
  for n 17 (this executable's product, mask 0x20000) the executable's directory cut at its last '/out/' or its absolute
  '../'. Non-empty: + 'TSData' -> option 0x4bcfce5e, mask 1 << n, if PathExists; n 17 also 'TSDataNonShip' (mask
  0x20000) and the directory always goes to the file system's SetDataDirectory (slot 19).
  Test `CommonImplDirs_FindProductDirectories` fakes on both sides: the file-system face, 0xcb4690, 0xcb4bb0, PathExists,
  DirectoryExists, AbsolutePathString, the cTSIniFile constructor / GetString / destructor (original 0xcb2a20 / 0xcb2e90 /
  0xcb2cc0 and ours), the command line; the product list's frees are logged through the game's free 0x831a54 and our
  CRT's `free`. Left as original calls: 0xcb4690, 0xcb4bb0 (registry, GetModuleFileNameA, _access), 0x9c59d9.
* Bisect switch: the cGZApp table (all adapters; with it off the game's own cGZApp code runs) and, for the directory
  slots, the five EV entries and the two FC entries at the end of the cTSAppCommonImpl table. A wrong directory shows at startup as missing
  packages, a missing Logs / Downloads folder or the resource ini not found.

### D2 rest: audio DoMessage, TSUI OnStart and PostAppInit (lane FB)

* Every director slot is now ours (D2 done): cTSAudioCOMDirector::DoMessage 0x20e7c7 is
  `comdir::AudioMessageTarget::DoMessage` (the audio director is still hook-shaped: hooked straight to the member, no
  adapter); TSUI OnStart 0x5b72e7 and PostAppInit 0x5b763c are bodies of the `rz::cCOMDirectorT<kDir_TSUI>`
  specialisation behind the existing adapters. Tests: `tests/com_directors_msg.cpp` (DoMessage, its own fake world) and
  two cases in `tests/com_directors_start.cpp` (ET's world, extended: an options object, a real message object with the
  image's vtable slots recorded, the class map's keys compared).
* DoMessage: 0xca creates nine services into the director's references (+0x68..+0x8c), registers two cheats
  (cTSAudioEventCheat / cTSAudioEventHandlerCheat: 12-byte objects carrying the game's vtables 0xda7e10 / 0xda7ddc, built
  by us and handed to 0xb39640) and hands reference 9 to the sound service; 0xcb shuts the references down;
  0xeded3807 runs 0x20f60e and 0x20f028. The cheat objects are a creation site storing game vtables (they go with the
  cheat classes' own lane).
* TSUI OnStart: no cheat registration there; it erases five tool classes from its own class map (+0x44) when the
  dispatchers 0x5b68ba / 0x5b69c2 say so (find then erase, end() not tested: kept, ORIGINAL-BUG). 0x830972 is `atoi`
  (named `_wtoi`; it parses bytes). The 37-byte 0x5b7b88 is inlined as a local helper (not hooked); its third argument,
  a service name, is unused.
* Original callees left: 0x20f60e, 0x20f028, 0x20f805 (the audio director's own helpers, 0.5-1 KB each), 0xb39640 (cheat
  registration), 0x9c5b7e, 0x9c5a89 (cached sound service / message server), and TSUI's eight dispatchers 0x5b696a,
  0x5b6a72, 0x5b68ba, 0x5b69c2, 0x5b6aca, 0x5b6912, 0x5b6b22, 0x5b6a1a (each calls a hook pointer at 0x103c364 or a default).

### A2a done: ts::cTSAppFW, the application, in the shape (lane FG)

* `ts::cTSAppFW : gz::cGZApp, cITSAppFW, cTSAppCommonImpl, cIGZMessageTarget2` in `src/ts/cTSAppFW.{h,size.cpp}`
  (`cTSAppFWShape.h` now only includes the three class headers), adapters `ts::cTSAppFWAdapters`, table
  `src/hook/tables/cTSAppFW.cpp` (9 entries: constructor 0xc26147, destructor body 0xc266b9, deleting destructor 0xc267d9,
  QueryInterface 0xc297fa, InitOptions 0xc2a7bb, AddCOMDirectorsHere 0xc26bf5, and new: AddApplicationServicesHere 0xc267fb,
  PreTranslateMessage 0xc296b4, DoMessage 0xc27605), status `db/status/cTSAppFW.jsonl` (36 entries: AppCtor's five,
  every TSApp entry, AppCOMDirectors' 0xc26bf5; `TSApp.jsonl` is gone). The hook-shaped `appctor::cTSAppFW`, `src/app/TSApp.*`,
  `appcom::AppFace` and their tests are removed; `src/app/AppCtor.*` keeps only the container teardown helpers 0x1d8450,
  0x1d5e50, 0xca49f0. The reporter (+0x98) and the string at +0x244 stay hook-shaped members in anonymous unions.
* The compiler's thunks replace the game's (recorded as DK / DW / DZ did: verified, not hooked, test named): QueryInterface
  0xc29852..0xc29872, AddRef 0xc274bb..0xc274db, Release 0xc29897..0xc298b7, the deleting destructor's 0xc26780 (+4 slot 3)
  and 0xc26788 (+0x44 slot 10). AddRef / Release are cGZApp's (0xc274b6 / 0xc29892 are 5-byte jumps, 0xb359c9 is folded).
* **Creation site**: only WinMain 0xc2b87c (the game's, not hooked) builds the application, in its function-local static at
  0x10fdcf0, through 0xc26147; the six vtables are referenced only by 0xc26147 and 0xc266b9 (atexit 0xd988f0 destroys the
  static). **The host installs the hooks before the image's entry point** (`host/main.cpp`; the last in-game log shows
  `cTSAppFW_Construct=1` and `cCTGFramework_Construct=1`), so WinMain's call reaches our adapter and **the live application
  is our class with our vtables** from this lane on (the hook-shaped constructor stored the game's): the own-vtable route
  of the tests is the in-game route, the adapters serve direct calls to the RVAs. (The F2 / F3 notes above, "built before
  the hooks go in", are contradicted by the same log for the framework: its live object carries our vtables too.)
  `src/app/WinMainInfra.cpp`'s candidate (unhooked until E1) placement-news `ts::cTSAppFW` into the static's storage and
  destroys it with our destructor.
* **Tests** (`tests/app_ctor.cpp`, all on both routes: own vtable, i.e. each side's object built by its own constructor and
  called through its own vtables; adapter, i.e. game-built objects on both sides, the adapter's own slot trapped in a copy
  of the game's vtable): AppCtor_Construct (the six face words compared as codes, the only words normalised),
  AppCtor_Destruct / _DeletingDestructor / the two thunks, cTSAppFW_Slots (QueryInterface / AddRef / Release from every face,
  SetAppName, InitOptions, GetWord70 and the six forwards), cTSAppFW_PreTranslateMessage, cTSAppFW_DoMessage (the
  string-table case with the resource-manager fake of `tests/ts_string_fake.h`), cTSAppFW_AddApplicationServicesHere (the
  12 constructors, both helpers and the options reader redirected at the original RVA and at ours; failing allocations).
  Filesystem and registry: nothing real is reached (imports and helpers are fakes on both sides; the crash reporter is only
  constructed / destroyed, never driven: `replaced` scope).
* **Left for A2b** (our members forward through OriginalFunction; not hooked, which would turn the forward into a loop;
  listed on 0xc26147's original_calls; cTSAppFW_Slots checks each gets the face and arguments the game's vtable gives):
  Init 0xc27c3e (3.4 KB: control%d.dat, the registry, many services, the reporter), PreFrameworkInit 0xc290d8 (1.5 KB:
  config, locale, file-system roots), SetupUserDataDirectories 0xc2a7de (2.2 KB; calls the base 0xca8710 directly, then
  string / map code), Shutdown 0xc2a1e3 (a scoped reporter 0x1baa57 / 0x1bbf19 / 0x1bab4e, a 0x2c-byte message, framework
  slot 10, the key map at +0x64 via 0x1c081e, the object at +0x60 via 0xcb28b0 / 0xcb0ad0, 0xc28b7d), OnTick 0xc28fcc
  (+0x6c skip byte, 0x1cbaf3, Sleep, the cached singletons 0x20dde0 / 0x20df9a / 0x20de51 / 0x211da3, 0x18ae10), and
  **AddDynamicLibrariesHere 0xc26c30** (1.7 KB; not ported here: seven unverified methods of a local list class 0xcb3640,
  0xcb3e10, 0xcb3870, 0xcb3740, 0xcb3b70, 0xcb3a80, 0xcb36f0, plus cRZVersion, 0x1bdb55, 0x140fd0 / 0x140fe0). The new
  bodies' own original calls: DoMessage 0xc2a3d6 (state query apply), 0xcb28b0 (object at +0x60), 0xc28de0 (fatal message),
  0xc28e32 (crash data; reporter side), 0xc27961 (text expansion); PreTranslateMessage 0x9c5a89 (message server getter).
* Byte matches (9 hooked entries): v8, v7 and v6 each 1/9, InitOptions 0xc2a7bb (35 B; its adapter uses `__assume` + a
  reference cast; the one-statement form matched only through v8's tie). Before the lane the group's one match was the same
  function in hook-shaped form. Mutants: 60, 56 killed, 4 equivalent (notes/feedback/FG.md).
* **Bisect switch**: the cTSAppFW table. With it off, the game's constructor builds the object with the game's vtables and
  the game's slots run (the bases still reach the cGZApp / cTSAppCommonImpl adapters). With it on, every slot the game calls
  on the application runs through our vtables: our members, the compiler's thunks, and the A2b forwards (game bodies on our
  object). New bodies: AddApplicationServicesHere (startup: a missing or misregistered service shows at once),
  PreTranslateMessage (every window message; the launcher's shutdown request) and DoMessage (the application's messages:
  string tables on language load, 0xce, the crash-report messages). Risk: game code that takes the application's RTTI
  (`dynamic_cast` / `typeid` on it) would see our type names (`ts::cTSAppFW`); none was found in this lane, not searched
  exhaustively.

### A2b done (part): four of cTSAppFW's six forwards have bodies (lane FM)

* **Bodies, verified by difftest** (4343 B, all four hooked through new cTSAppFWAdapters entries; in game the live
  application runs them through our own vtables): OnTick 0xc28fcc (268 B), Shutdown 0xc2a1e3 (373 B), PreFrameworkInit
  0xc290d8 (1500 B), SetupUserDataDirectories 0xc2a7de (2202 B). What each does is in its status entry; the main facts:
  * OnTick: +0x6c is a skip-one-tick flag; the sleep is the settings service's (0x1cbaf3 getter) `onTickSleepTime`; the
    state machine 0x20dde0 slot 5 == 0xbea28c2 steps 0x20df9a; then 0x20de51 slot 36 -> slot 10, 0x211da3 slot 17, and the
    view service (0x18ae10 getter) slot 25 unless its slot 31 has bit 8.
  * Shutdown: 0x1baa57 / 0x1bbf19 / 0x1bab4e are a psapi memory snapshot (vtable 0xda31e0, 0x18 bytes), not a reporter; the
    0x2c-byte object is a cRZMessage2Standard with id 0xb466ab3 sent to the server from 0x9c5a89; the configuration at +0x60
    is saved by 0xcb28b0 **without a null test** (it faults on null: Shutdown without PreFrameworkInit crashes), then
    destroyed and freed. The EsperSession switch runs the esper service (0xc260e1 getter) slot 4 and 0xc28b7d(this, 0, 1, 1).
  * PreFrameworkInit: the package directories option 0xcc9f6e91 is walked last to first; eCAS.package / uitext.package are
    opened and kept in the +0x78 list; the reporter gets the Logs directory and the +0x5c face as listener; 0xc28b7d gets
    `noExceptionUpload`'s raw al (not a bool); Locale = "Locale/<language service slot 15>/" under the resources; the IME
    services 0x2ac4516b / 0x2ac4516d are added unless IME is "off" / "disabled" (any case); the 0xf0-byte configuration
    object is built and loaded without null tests; the message ids at +0x64 are registered with the +0x5c face (0x1c0458).
  * SetupUserDataDirectories: **+0x70 (cITSAppFW slot 4, GetWord70) is the develop-mode flag**: 1 for
    `userDataMode=develop` (case-insensitive), else 0. Install mode registers the 12 user-data folders under "UserData/"
    and roots the user data at `<Documents>/EA Games/<product>/` (product "The Sims 2", or the registry's DisplayName via
    0xc27a16), unless `activeUserDataDir` (made absolute, '/') differs from the UserData option; `templatedev` / `nhdev`
    redirect the neighbourhoods; `activeAllUsersDataDir` or `<all users>/EA Games/<product>/`. Develop mode keeps the
    user data under the resources ("UserData/", "AllUsersData/"), falling back to the Documents / all-users roots when
    those directories do not exist.
* Tests: tests/app_lifecycle.cpp (OnTick, Shutdown) and tests/app_dirs.cpp (PreFrameworkInit, SetupUserDataDirectories),
  300 runs on each route (own vtable and adapter); the shared fixture moved from tests/app_ctor.cpp to tests/app_fixture.h.
  Callees are recording fakes on both sides (and our own versions redirected where the body calls ours); compared: al, the
  log, the whole object with heap words masked. Mutants in notes/feedback/FM.md.
* **Still forwarded** after FM (both have bodies since A2c, below; OriginalFunction from our members, not hooked): Init 0xc27c3e (3375 B: control%d.dat, registry
  reads and writes, the disk-space check, string-table messages, the fatal-message dialog 0xc2dbf1 / 0xc2da5a, a message
  box, the property set) and AddDynamicLibrariesHere 0xc26c30 (1680 B: debug DLL names "GZLogMFCWinD.dll" ...
  "TSAutomation.dll" through the local list class 0xcb3640 / 0xcb3e10 / 0xcb3870 / 0xcb3740 / 0xcb3b70 / 0xcb3a80 /
  0xcb36f0 and 0x1bdb55, the bench / metrics / perfTrackExecution switches, cRZVersion and 0x140fd0 / 0x140fe0). Both are
  next for this group; AddDynamicLibrariesHere first wants the list class mapped.
* Original calls the new bodies keep: 0x20df9a, 0x211da3 (OnTick); 0x1bbf19, 0x9c5a89, 0xcb28b0, 0xcb0ad0, 0xc28b7d
  (Shutdown); 0xc28b7d, 0xcb0890, 0xcb0f40, 0xcb18a0, 0xcb1790 (PreFrameworkInit); 0xc27a16, 0x1c50d6, 0x1c5031
  (SetupUserDataDirectories). 0xc28b7d starts ExceptionUploader.exe (crash-reporting scope) and stays faked in tests.
* **In game**: the four hooks are new; the bodies run at every start (PreFrameworkInit, SetupUserDataDirectories from
  AddApplicationServicesHere), every frame (OnTick) and at exit (Shutdown). A smoke run that reaches a loaded lot and quits
  cleanly covers all four; a wrong user-data root shows as missing neighbourhoods or a new empty Documents tree.

### A2c done: the last two forwards have bodies (lane FW)

* **Bodies, verified by difftest** (5055 B, both hooked through new cTSAppFWAdapters entries): AddDynamicLibrariesHere
  0xc26c30 (1680 B) and Init 0xc27c3e (3375 B). cTSAppFW has no forwarded slot left; the constructor's original_calls
  are empty. What each does is in its status entry; the main facts:
  * AddDynamicLibrariesHere: the local list (0x1c bytes) is a struct in our file with its seven members as original calls
    (0xcb3640 constructor, 0xcb3e10 version requirement, 0xcb3870 read the configuration, 0xcb3740 add, 0xcb3b70 load all,
    0xcb3a80 report, 0xcb36f0 destructor). 0x140fd0 is the text "monolithic"; 0x140fe0 is a bare `ret` (not called). The
    al is the list destructor's (the caller ignores it).
  * Init: 0xcb4690 fills the product key ("Software\Electronic Arts\The Sims 2 Ultimate Collection 25\") and the install
    directory and ignores its first argument; 0xcb4cc0 (a newer product) answers 0 for EP9, so the DisplayName / 0xb5/9 branch
    is dead in practice; 0xc2896d is an elevation check whose result is unused; 0xc2dbf1 is the yes/no box (the disk-space
    question below 2,000,000,000 free bytes), 0xc2da5a the fatal box (winmain::ShowFatalMessage, ours). The second half
    (our private StartUp) registers the .Sims2Skin file type in HKEY_CLASSES_ROOT on every start where the install
    directory is known (writes: the user's machine, as the original), adds the application to the tick, takes the +0x80
    delegate (service 0x9b3798f7), posts the start message (installUserData / load / default) and hands a string property
    set to the configuration (0xcb1780) and to the globals' target (slot 21, its slot 17).
* Tests: tests/app_dlls.cpp and tests/app_init.cpp, both routes; every import and every callee with effects outside the
  process is a fake on both sides (the fatal box and the question are never shown). Mutants in notes/feedback/FW.md.
* Original calls kept: the list's seven members (AddDynamicLibrariesHere); 0xcb4cc0, 0xcb4690, 0xc2896d, 0x1c48da,
  0x1c49eb, 0xb417c0, 0xc2dbf1, 0x9c5a89, 0x1c9e49, 0xcb1780 (Init).
* **In game**: both run once at every start (AddDynamicLibrariesHere from FrameworkStartup, Init as the application's
  system-service Init). A start that reaches the main menu covers both; a regression in Init shows as the fatal box
  (wrong control-file path or user-data check) or as no neighbourhood loading (wrong start message). Bisect switch: the
  hook table entries cTSAppFW_Init and cTSAppFW_AddDynamicLibrariesHere.

## E1: taking over the process start

Design by lane GC (2026-10-18) for milestone E. Facts first (disassembly, the PE file, xrefs), then the options, the
recommendation, the step done in this lane and the acceptance text proposed for the coordinator.

### What the original start does

* `entry` 0x8248a3 (10 B): `__security_init_cookie` 0x8258a9 (the image's /GS cookie at 0x104f540, complement 0x104f580),
  then a jump to `__scrt_common_main_seh` 0x824727 (318 B, an SEH4 frame whose filter is `_seh_filter_exe` 0x842b14).
* 0x824727, in order (try level 0 from after the CRT initialisation to the returns):
  1. `__scrt_initialize_crt(1)` 0x824af8; false -> `__scrt_fastfail(7)` 0x82595c (outside the try).
  2. `__scrt_acquire_startup_lock` 0x824ac6 (al = "nested", kept for step 4).
  3. The native start-up state 0x10df7e8: 1 -> fastfail(7); 0 -> state 1, `_initterm_e` over the XI table 0xd9b3ac..0xd9b3d8
     (11 slots, 10 entries: section 1 of `docs/startup-map.md`), a non-zero result returns **255** at once (no exit, no
     lock release); then `_initterm` over the XC table 0xd9a724..0xd9b3a8 (801 slots, 800 entries: section 2), state 2;
     2 -> "already initialised" (a flag in bl, read on the managed exit paths).
  4. `__scrt_release_startup_lock(nested)` 0x824c4c.
  5. The dynamic TLS callbacks: slots 0x110a388 (init, called `(0, DLL_THREAD_ATTACH, 0)` after the CFG check through
     0xd9a71c, which points at the bare `ret` 0x140fe0) and 0x110a384 (dtor, `_register_thread_local_exe_atexit_callback`
     0x82f699), each only when non-null and `__scrt_is_nonwritable_in_current_image` 0x824bb8 says so. **Both slots are in
     .data and zero in the file: never called in this game.**
  6. `WinMain(0xA0000000, 0, __get_narrow_winmain_command_line() 0x8434c1, __scrt_get_show_window_mode() 0x825a71)`
     (WinMain ignores all four).
  7. `__scrt_is_managed_app` 0x825aa2: false for this native exe -> **`exit(code)` 0x82f6bf** (db name `_loaddll` is
     wrong), which runs the game CRT's atexit list and ends the process. The managed branch (`_cexit` 0x82f674 unless
     "already initialised", `__scrt_uninitialize_crt(1, 0)` 0x824c69, return the code) is dead here.
  8. Exception path: the filter `_seh_filter_exe(code, info)`; if it selects the handler: not managed -> `_exit(code)`
     0x82f683 (db `_Exit`); managed -> `_c_exit` 0x82f665 unless already initialised, return the code.
* What WinMain itself takes from the game's CRT: `atexit` 0x824cbe for the two statics' destructors (thunks 0xd988fa,
  0xd988f0), the thread-safe-statics protocol for the two guards (0x10fdce8, 0x10fdf58, compared with the thread's
  epoch in the image's TLS slot), and **`exit(0)` 0x82f6bf** on the Steam-failure path (not `_exit`: the atexit list
  runs). The storage 0x10fdb50 / 0x10fdcf0 and the guards are referenced only by WinMain and the two thunks (xrefs).
* Two CRTs are in the process today. The host is an MSVC 14.40 `/MT` exe whose own CRT started first (our code, our
  statics, our `std::atexit`); the image's CRT (libcmt / libucrt of the same family: `atexit`, `_Init_thread_header`
  0x824d4f, `setlocale` 0x837dcc, `rand` 0x83c90a / `srand` 0x83c92b are byte-matched) is started by `entry`. Both
  allocate from the process heap, which is why memory crosses freely between our code and the game's. Everything
  else is per CRT: the atexit list, the statics epoch, stdio, locale (game code calls `setlocale`: 0x1b18b5, 0x32d62,
  0x33014, 0x448c4, 0x449b1), `errno`, the `rand` state (`srand` from 0x766a92), the terminate / purecall / new
  handlers, `exit`. The host's own atexit list never runs: the game's `exit` ends the process.

### The options

| | (a) keep the game's start, hook WinMain | (b) our start sequencer drives the image's CRT | (c) one CRT: ours for the image too |
|---|---|---|---|
| What runs | `entry`, 0x824727, XI, XC: the game's; WinMain 0xc2b87c hooked to `WinMainBody` | the host calls our sequencer instead of `entry`; it calls the image's CRT steps (cookie, `__scrt_initialize_crt`, the lock, `exit`) as a library, runs XI / XC with our toolchain's `_initterm_e` / `_initterm`, then `WinMainBody` directly | every game-CRT entry point game code reaches is hooked to our toolchain's function; the image's CRT is never initialised; XC run by our start; `exit` is ours |
| atexit | the game CRT's (the seam bound to the image's `atexit`) | the game CRT's (same binding); order as the original: framework and application registered last, destroyed first | ours: one list, our statics, the image's ~610 destructors and the two statics in true LIFO order |
| statics epoch | the game's for game code; WinMain's two guards written as "done" (1) by ours, read by nobody else | same | ours |
| exit | the game's `exit` (Steam failure, return of WinMain) | the game's `exit` (bound), the original's path; the host's own atexit list still never runs (unchanged) | ours: our list runs, then ExitProcess |
| What breaks | nothing new if the seams are bound to the image's CRT; **but a hook is on by default** (hook tables have no default-off entry), so it changes the default start at once | nothing by construction: the sequencer is difftested against `entry` with every callee recorded; the risk is `WinMainBody`'s first run in game, isolated by the switch's two values | all or nothing per state family: one missed function (one of the 31 `crt-equivalents`, an inlined access, `_purecall` in game vtables, the EH runtime's per-thread data, `FILE` / locale objects handed between functions) works on a CRT nobody initialised; only testable in game |
| Tests | WinMainInfra_WinMain (exists); in game: hook counts | WinMainStart_Entry (new: our sequencer vs the original `entry`, both driving the same recorders) + WinMainInfra_WinMain; in game: `[Startup] OwnStart=1` (our sequencer, the game's WinMain), then `=2` (ours) | per family contract tests against our CRT; in game for everything |
| Milestone E | not met (the CRT entry is the game's) | met: the start sequence, WinMain, RunApp, Startup state 0 and the framework constructor are ours; the image's CRT is a library we call, as stage 4 allows | met |
| Left for stage 5 | everything about the start | the sequencer's image bindings (cookie, CRT init, XI, lock, `exit`), deleted with the image, and the XC entries not yet owned (stage 3, entry by entry, through a skip list, without touching the sequencer). What remains is our exe's own CRT start calling `WinMainBody`, which the toolchain provides | the hooks vanish with the image; same XC work. Least to undo, but stage 5 doesn't need it: once no game code runs there is no second CRT |

**Recommendation: (b).** It meets milestone E's text, changes nothing until the switch is on, has a real difftest
for the new code (the sequencer against `entry` / 0x824727 with every callee recorded), splits its in-game risk
into two switch values, and leaves for stage 5 only bindings that disappear with the image. (c) is not needed for
E or for stage 5; keep it as a per-family fix for the day a CRT split bites (locale-dependent CRT calls in our
replacements while game code has called `setlocale`; a replacement that must share `rand`'s state with game
code: those call the image's function through a binding, or move the family to one CRT, decided case by case).
(a) is (b) with `OwnStart=1`'s sequencer removed: no reason to do it separately.

Rules that follow, for this and later lanes:
* src/ never names an image-CRT function: the host binds them (`winmainstart::ImageCrt`, `winmaininfra::Seams()`
  `registerAtExit` / `exit`). The bindings are the stage-5 removal list. (kImageCrt's two data writes in the
  handler installer predate this and stay as they are.)
* Exit-time work registered by our code that must run at the game's exit goes through the bound `atexit`, not
  `std::atexit` (the host's list never runs). Code that ends the process goes through the bound `exit`
  (`cGZFramework::sExit` for AbortiveQuit can be bound the same way, which would unblock hooking 0x142270).
* The two statics keep the game's storage for now (nothing else reads it); in the standalone build they become
  ordinary function-local statics of `WinMainBody`.

### The switch and the in-game runbook (for the coordinator)

`ts2reimpl.ini` next to the host: `[Startup] OwnStart=0` (or absent) is today's start, byte for byte (`entry()`).
`OwnStart=1`: our sequencer with the game's WinMain 0xc2b87c bound as the WinMain step. `OwnStart=2`: our sequencer
with `winmaininfra::WinMainBody`, its `registerAtExit` / `exit` seams bound to the image's `atexit` 0x824cbe / `exit`
0x82f6bf. The host logs `own start: mode N`, the sequencer's steps (`XI` / `XC` counts, `WinMain`), and for mode 2
the framework and application constructions and their destructors at exit (logging wrappers around the seams).
Runbook: mode 1 to the main menu and quit; then mode 2, the usual smoke (load a lot, quit from the menu). Look for: the
`own start` lines (`mode N`, `10 XI initialisers`, `800 XC initialisers`, `XC initialisers done`, `calling ... WinMain`),
no fault-log entry, the same hook counts as a default run in mode 1; in mode 2 `WinMain_RunApp`, `cCTGFramework_Construct`
and `cTSAppFW_Construct` stay 0 (our WinMain calls our RunApp and constructs both directly, not through the hooked
RVAs), the two `constructing ... (ours)` lines, and at exit `exit(0) through the image's CRT` followed by the
destructor lines (application first, then framework); exit code 0. A failure in mode 1 is the
sequencer or a binding; one only in mode 2 is `WinMainBody` (its callees are verified, it has never run in game).

### Done in this lane (first step of (b))

* `src/app/WinMainStart.{h,cpp}`: `winmainstart::RunImageStart` (entry) and `CommonMain` (0x824727) on the
  `ImageCrt` binding table. Difftest `WinMainStart_Entry` (`tests/winmain_start.cpp`): the original `entry` against
  ours, all nineteen callees recorders on both sides (redirected in the original, bound in ours), 600 scenarios over
  the CRT-init answer, the lock's answer, the start-up state (0, 1, 2, other), XI failing, both TLS slots filled or not
  with either section answer, junk in the upper bytes of every bool / `ax` answer, WinMain returning, raising an SEH
  exception or ending in a nested exit, managed or not, the filter handling or passing on; compares the call log with
  arguments, the result, how the run ended and the state variable; fails if any of 14 paths is never reached.
* `host/main.cpp`: `[Startup] OwnStart` (above); default unchanged. Builds (`ts2host`); not run (lanes don't run the game).
* `winmaininfra::WinMainSeams::exit` (was `exitProcess`, default `_exit`): the original's Steam-failure path calls
  `exit` 0x82f6bf, so the default is now `std::exit`, and mode 2 binds the image's.
* Next: the coordinator's runs (mode 1, mode 2); RunApp 0xc2d23b to `verified` (done, lane GH: below);
  then, as stage 3 work, a skip list in the host's XC runner for initialisers whose objects our build defines.

### Milestone E acceptance (draft for the coordinator to propose)

> **E. The startup path to the framework's first state.** Run: `local/game` with `[Startup] OwnStart=2`, the
> standard smoke scenario (main menu, load a neighbourhood lot, quit from the menu). Evidence: (1) the host log shows
> our start (`own start: mode 2`, 10 XI and 800 XC entries run by our sequencer, our WinMain called; the image's
> `entry` and WinMain 0xc2b87c are not run), the framework and application constructed by our constructors at the
> game's static storage, the game reaching the main menu and a lot, and the two destructors run from the image CRT's
> atexit list at exit; (2) the process exits
> with code 0 and no fault-log entry; (3) the same scenario with `OwnStart=0` is unchanged; (4) status: the
> sequencer (entry 0x8248a3, `__scrt_common_main_seh` 0x824727) and WinMain 0xc2b87c verified by difftest, RunApp
> 0xc2d23b verified (done by lane GH: the unwind difference was gone since the /GS- build, see below), RunFramework 0xc2d0a8
> and GZFrameWork_Startup verified. Not part of E: the static initialisers from our source (stage 3), our CRT for
> image code (option (c)). After acceptance the switch's default becomes 2.

### Milestone E: the exact run (lane GH, for the coordinator)

Evidence item (4) as of lane GH (2026-10-18):
* RunApp 0xc2d23b: `verified` (difftest, hooked). The "SEH3 vs SEH4" hold was obsolete: since the /GS- build
  (c943df9) our `__try` is `__SEH_prolog` + `_except_handler3` from our toolchain, byte-identical library code to the
  original's (0x824dc0, 0x826a30, 0x829a60, 0x829ae0, 0x829a19), so the crash path unwinds the same way (thrown object
  destroyed, then RtlUnwind without a record: no /EHs destructors, `__finally` blocks run). The difftest now shows
  each of these in its log and kills a /GS (SEH4) build of WinMain.size.cpp. Keep game code at /GS-: the
  TS2_ESPCHECK build is SEH4 here and fails WinMain_RunApp.
* RunFramework 0xc2d0a8 and GZFrameWork_Startup 0x144514: `verified` and hooked; under `OwnStart=2` ours call
  each other directly (RunApp -> RunFramework -> `fw_startup::GZFrameWork_Startup`), so their hooks only matter
  for modes 0/1. Nothing missing.
* Framework and application constructors (0xc2ccdb, 0xc26147): `verified`, hooked; mode 2 builds both through the
  seams instead (the hooks then count 0).
* entry 0x8248a3, `__scrt_common_main_seh` 0x824727, WinMain 0xc2b87c: `mapped`, difftested (WinMainStart_Entry,
  WinMainInfra_WinMain). They move to `verified` with `"in_effect": "callers"` only after: GE's trap and
  callers check for these three, and a passing mode-2 run below. WinMain keeps one known difference (its two
  statics' guard words get 1, not the game CRT's epoch; nothing else reads them, xrefs).

Before the run counts: lane GE's host trap on the image's `entry` 0x8248a3, `__scrt_common_main_seh` 0x824727 and
WinMain 0xc2b87c (a log line and a stop if reached) and its `tools/callers.py` check of those three are merged; RunApp
is `verified` (lane GH). The two tool gaps lane GH found are closed (lane GI): the scenario step `quit` asks the game to
quit through its own request, and game.ps1 records the process exit code (`stop` is still `Stop-Process -Force`, and
says so instead of a code).

**How `quit` quits** (host control `quit`, host/control.cpp; lane GI, read from the binary): it posts the game's quit
request, a cRZMessage2Standard with id 0x4beb45bb and only value 0 = 0x40000, to the message server. The same message is
posted by the options panel's Quit button once its "quit?" dialog is answered yes (cTSWinProcOptionsPanel slot 25
0x649eef, control 0xa220) and by the launcher's shutdown request (cTSAppFW::PreTranslateMessage 0xc296b4).
cTSGameStateController::DoMessage 0xc31a9e (case at 0xc32d26) passes value 0 to 0xc3be33 when the controller's input
count (+0x7c, the edi face's +0x78; 0xcd91ece9 moves it) is above 0, otherwise 0x40001. 0xc3be33 by state: bit 0 would
ask (0xc2f1e8 shows the "quit?"/"save first?" dialogs), bits 0x1800 would save; 0x40000 has neither. From a lot
(0xbea28c2) it posts 0x6d03d837 to leave the lot first; from the neighbourhood/main menu it runs the shutdown state
0xc3a866 ("inShutdown"), which ends with the framework's Quit(0) (cIGZFrameWork slot 19, 0x1431fc: PostQuitMessage),
so MainLoop sees WM_QUIT, RunApp returns g_ExitCode, WinMain returns and the CRT's exit runs the atexit list. Nothing is
called through `OriginalFunction`: the host posts the message with the game's own allocator and constructor, as its
`post`/`loadlot` commands do. A count at or below 0 (input disabled, e.g. mid-load) turns it into 0x40001, which shows
the "quit?" dialog and waits: send `quit` once the lot runs (after the scenario's `until:`), not during a load.

1. **Switch.** `local/game/EP9/TSBin/ts2reimpl.ini`, section `[Startup]`, key `OwnStart` (deploy does not touch the
   ini; an instance has its own in `local/game-N/EP9/TSBin`). Runs in this order: `OwnStart=0` (reference), `1`, `2`.
2. **Scenario** (one file for all three runs; `-ResetUserData` each time): `load-check` up to `shot:lot; log`
   (boot, Belladonna Cove, lot 6, 600 simulator frames), then `quit:300` (the game's own quit, no dialog, no save;
   the step waits up to 300 s for the process to end by itself and fails, killing it, otherwise); the `log` step
   before it gives the hook counts of item 4, and the rest is in `ts2host.log`. For modes 1/2 add
   `until:own start: atexit destroys the framework,5` after `quit` (it reads the finished log). No `stop` step.
   Record it once as e.g. `tools/scenarios/own-start.txt`.
3. **Exit code.** The scenario prints one line from `<bin>\ts2exit.txt` (game.ps1's watcher, beside `ts2host.log`)
   after the quit step: expected **`exit code 0 (0x00000000)`** in all three modes. `killed: ...` means `stop` (or a
   failed step) force-closed it; `exit code 1414676816 (0x54524150): trap fired` is lane GE's trap (the log has a
   `trap: original` line); `exit code 1 (0x00000001): trap not armed` is GE's arming failure. Modes 1/2 also log it:
   `own start: exit(0) through the image's CRT` is the code WinMain returned, passed to the image CRT's `exit` (which
   ends the process with it).
4. **Log lines** (`ts2host.log`), in order:
   * mode 0: `calling the image's entry point`; no `own start:` line.
   * mode 1: `own start: mode 1 (the game's WinMain)`, `own start: 10 XI initialisers`, `own start: 800 XC
     initialisers`, `own start: XC initialisers done`, `own start: calling the game's WinMain`, the usual game
     run, `own start: exit(0) through the image's CRT`. No destructor lines (the game's WinMain registers its own
     thunks 0xd988fa / 0xd988f0).
   * mode 2: as mode 1 with `mode 2 (our WinMain)` and `calling our WinMain`, then `own start: constructing the
     framework (ours) at <0x10fdb50 + image base>`, `own start: constructing the application (ours) at <0x10fdcf0
     + base>`; at the end `own start: exit(0) through the image's CRT`, `own start: atexit destroys the
     application`, `own start: atexit destroys the framework` (that order: LIFO).
   * all modes: no trap line from GE's guard, no `exception 0x...` / `c++ exception` fault-log lines that the mode-0
     run does not have too, and the last `log` line's hook counts: mode 1 the same hooks called as mode 0;
     mode 2 additionally `WinMain_RunApp`, `cCTGFramework_Construct`, `cTSAppFW_Construct` at 0 (our WinMain calls
     our RunApp and builds both objects itself), `GZFrameWork_Startup` non-zero only if a caller still reaches
     the game's address (ours calls `fw_startup::GZFrameWork_Startup` directly).
5. **Reading a failure.** Mode 1 failing: the sequencer or an image-CRT binding (host/main.cpp `ownstart`). Only
   mode 2: `WinMainBody` (its callees are verified; it has never run in game). A missing destructor line after
   `exit(0)`: the atexit binding (`ImageAtExit`). Exit code -1 with a message box: RunFrameworkGuarded's handlers
   (a cError / D3D exception: a real graphics failure, also in mode 0?); exit code 0 without `exit(0)` logged and
   with a reporter dump: RunApp's crash path (ExitProcess(0) after the report).

Note for the status rules: entry, 0x824727 and WinMain are not hooked under (b) (a hook is on by default, and the
host calls ours instead of the image's start). CLAUDE.md's `verified` asks for a hook; the coordinator should agree
with the user that "called by the host instead of the original, under the accepted switch" stands in for it here.

## Stage 3: the static initialisers

Structure pass by lane GL (2026-10-18). Facts: `db/initializers.jsonl` (one line per XC entry, written by
`tools/initializers.py`, which also still writes `db/static-initializers.jsonl`; `--kind K` lists one kind, `--rva`
one entry). Each line: slot, rva, kind, the globals it writes (direct stores and constructed objects), the globals it
reads, its constructors / other calls, its atexit registrations (thunk, the globals the thunk touches, its calls),
the class (from a vtable it stores, or a constructor's / destructor's name) with `class_slots` (done/total), the
subsystem (by the functions that reference what it writes), the status of what it calls, `ours`, and `after` /
`before` (order dependencies, below).

### The facts

The XC table 0xd9a724..0xd9b3a8 has 801 slots, 800 initialisers; between them they make 610 atexit registrations
(the destructors of the statics, run LIFO by the image CRT's `exit`). The entries are grouped by translation unit
(0x136e60..0x140ee0, then the CRT's own). By kind (`ours`: every game function it reaches has a status entry above
`mapped`; for inlined classes see `class_slots`):

| kind | n | atexit | ours | what it is | example |
|---|---|---|---|---|---|
| crt | 7 | 5 | - | the CRT's / STL's own state: `_set_new_mode`, `std::_Init_locks` (2), `_Fac_tidy_reg_t`, `_Init_atexit`, `__tzset`, ATL's module | slot 1 0x824715 |
| key-hash | 111 | 0 | 111 | a key from a string constant: `g = Crc24(name, 1) \| 0xff000000` (81, UI sound names, slots 384-464) or the pair `{Crc24(name, 0) \| 0xff000000, Crc32(name, 1)}` (30, audio and the four game modes) | 0x13d9ab, 0x13852c |
| std-string | 85 | 85 | 0 | a `std::string` constant: the library constructor, or inlined (the characters copied in; size and capacity are constant-initialised in .data; `operator new` first when longer than 15) | 0x1384bb |
| class-inline | 262 | 262 | 72 | an object whose constructor is inlined (it stores a known vtable): cRZString 135 (`class_slots` 31/33), cIGZAny 36, the pixel formats 56 (7/7: ours), cRZFastFixedAllocator2 21 (2/4), a few allocators | 0x138479 |
| container | 24 | 24 | 0 | an empty container: `std::map`/`set` head node (`_Buyheadnode`), hash buckets, pooled list heads (`RZBlockPoolAlloc`) | 0x13e293 |
| object | 147 | 107 | 20 | a class static by its constructor (ecx = the global): 55 by 0xaf6560 (a list of 28-byte strings from a literal), 27 by 0x57c3c0 (a 16-byte math value, then a constant stored), cRZCriticalSection 10, cRZRandom 5, ... | 0x138342 |
| atexit-only | 122 | 122 | 21 | a constant- or zero-initialised object: only its destructor is registered (73 free one buffer: empty vectors / strings; RZMemory 21) | 0x13e046 |
| value | 26 | 1 | 0 | a value from other constants, no calls (copies, float products) | 0x13cd08 |
| computed | 5 | 0 | 0 | a global set from a call's result | 0x1395b0 |
| registration | 9 | 4 | 0 | a call that registers something elsewhere (no global written here) | 0x13c790 |
| other | 2 | 0 | 0 | 0x138440, 0x138450 | |

By subsystem (top): framework-gz 143, rendering-gfx 89, ui-game 70, objects 57, command-parser 56, scenegraph 47,
cas 37, world-lot 27, animation 23; 152 have no subsystem (what they write is referenced only from functions without
one, mostly string constants and the 73 buffer destructors), 32 `unassigned`. Key-hash is ui-game 46, cas 36,
world-lot and audio the rest; std-string is objects 50 and unknown 34; class-inline framework-gz 86 (cRZString,
allocators) and rendering-gfx 83 (pixel formats); object command-parser 54 (the 0xaf6560 lists).

### Owning them, by kind

What "owned" means while the image is hosted: game code reads these statics at the image's addresses, so ours write
the image's storage (`GameAddress`), in the entry's own slot, with the same values; destructors go through the
bound image `atexit` (as WinMainBody's do), registered in the same slot so the LIFO order is unchanged. In the
standalone build the same objects become ordinary C++ statics of the code that reads them, and the compiler does the
registration.

* **Plain C++ statics (constants) in the standalone build:** key-hash (`const uint32_t`; constexpr once a constexpr
  CRC exists), std-string (`const std::string`), value (constexpr), and the constant parts of class-inline cRZString
  and cIGZAny. Hosted: our initialiser writes the value; strings are constructed in place by our STL (the same
  library code and layout: the STL is byte-matched) and destroyed by our thunk through the bound atexit. Strings
  allocate from the process heap on both sides (E1 facts), so a game-side assignment that frees ours is fine.
* **Statics of our classes:** class-inline and object, when the class is ours in the shape (`class_slots` full;
  ctor and dtor verified): our constructor at the image's storage, so the object carries **our** vtable. That is
  the one behavioural change owning brings (game code then calls our slots for that object), so only classes whose
  slots are all ours go first: the pixel formats (56 entries, 7/7), cRZCriticalSection (10, ctor 0x1af64e / dtor
  0x1af68a verified), cRZRandom (5), StringKeyParser::CrcTables (1), cRZString (135, lane GY: still hook-shaped, so
  its statics carry the game's vtable, every slot of which is ours or a verified folded body).
* **Containers and atexit-only:** our container type, empty, in place (layout = the library's); atexit-only becomes a
  `constinit` static whose destructor is ours (hosted: register our destructor thunk in the slot; the object's
  bytes are the image's file contents and stay). The pooled ones wait for the RZ pool allocator.
* **crt:** never owned. Hosted they initialise the image CRT's state, which image code uses; standalone our
  toolchain has its own. The host runs them as now.
* **computed / registration / other (16):** one at a time, when what they call is ours.

### The skip list (done: pilot below)

`winmainstart::RunInitialisersOwned(first, last, imageBase, owned, count)` (src/app/WinMainStart.cpp) is
`_initterm`'s loop with one change: a slot named in the owned list runs `run(arg)` instead of the image's pointer,
**only if** the slot holds `imageBase + imageRva` (another image build runs its own entry and counts as
mismatched). Every other slot runs as before, in order. The host's XC step under `[Startup] OwnStart=1/2` calls it
with `staticinit::OwnedInitialisers()` (src/app/StaticInit.cpp); `[Startup] OwnStatics=0` passes an empty list (to
bisect). Mode 0 is untouched (the image's own `_initterm`). The host logs `own start: 800 XC initialisers (N owned
by us)` and `own start: XC initialisers done (I image, O ours, M mismatched, U unused)`; expected now (after lane
GY) `800 XC initialisers (428 owned by us)` and `done (372 image, 428 ours, 0 mismatched, 0 unused)`.

How a test proves a skipped entry's effect is reproduced (the pattern for every kind):
1. The slot check: the image's XC slot holds the initialiser our table names (a wrong slot or RVA fails).
2. Per entry, on the mapped image: fill the entry's `writes` (and a margin around them) and every state its callees
   touch with junk, run the image's initialiser, capture; restore, run ours, capture; compare. The callees' state
   comes from their own difftests (for the keys: the CRC table block). Junk rather than zero, so a skipped store shows.
   With destructors: also run the image's atexit thunk and ours on the constructed object and compare (the image's
   `atexit` call itself needs a redirected callee in the original, as WinMainStart_Entry does for the CRT's).
3. The runner on a copy of the real XC table with only the owned slots kept: counts O = owned, 0 mismatched / unused,
   and the same globals as the image's entries run in slot order.
4. The runner against the image's `_initterm` 0x843514 on recorder tables (null holes; owned entries matching,
   mismatched, out of range, listed twice).

### Order constraints

* Slot order is kept by construction: an owned entry runs in its own slot, so everything before and after sees the
  same state as with the image's entry, and its atexit registration lands at the same point of the LIFO list.
* Direct data dependencies are rare: 2 entries load a global another entry stores (slots 364 and 799, each a value
  copy from the slot before it); none loads one a later entry writes (`after` / `before` in the facts). The walk sees
  only the entry's own loads.
* Indirect ones go through callees: the CRC tables (built lazily by the first Crc24 / Crc32, idempotent; slot 533
  `CrcTables::Initialize` builds them too), the allocators (RZMemory, the cRZFastFixedAllocator2 statics feed the
  pooled containers: own the allocator entries before their users, or with them), cRZCriticalSection objects used by
  later constructors. Check an entry's constructor for reads of other statics before owning it.
* Destructors: LIFO over XC plus WinMain's two statics (registered last, destroyed first). Owning keeps the order as
  long as each registration stays in its slot.
* Standalone: across our translation units C++ orders dynamic initialisation by link order only. Keep dependent
  statics in one translation unit (definition order) or behind a function-local static, and make constants
  `constexpr` / `constinit` so they have no runtime order at all.

### Done in this lane (pilot: the key-hash kind, 111 entries)

* `src/app/StaticInit.{h,cpp}`: the 81 name keys and 30 key pairs (slot, the image's initialiser, the storage, the
  name; each entry's code was checked against the single instruction pattern of its form), `InitNameKey` /
  `InitNameKeyPair` (our verified `StringKeyParser::Crc24` / `Crc32`, which build the tables on first use as the
  original's do), and `OwnedInitialisers()`, the skip list (both tables merged by slot at compile time; a
  static_assert keeps it one entry per slot, in order).
* `src/app/WinMainStart.{h,cpp}`: `OwnedInitialiser`, `XcRunCounts`, `RunInitialisersOwned`. `host/main.cpp`: the XC
  step uses it (`OwnStatics`, default 1). Not run in game (lanes don't); mode 0 unchanged.
* Tests (`tests/static_init.cpp`): `StaticInit_NameKeys` and `StaticInit_NameKeyPairs` (each entry against the
  image's initialiser in its slot, 9 start states each: tables unbuilt / built / junk with the flag set, junk around
  the storage; compares the storage window and the table block), `StaticInit_SkipListOnImageTable` (the runner on
  the real table's owned slots), `StaticInit_Runner` (against the image's `_initterm`, 400 rounds).
* Status: the 111 initialisers are `mapped` in `db/status/StaticInit.jsonl` (difftested, owned only under
  OwnStart=1/2): like entry / WinMain they move to `verified` with `"in_effect": "callers"` after a passing mode-1/2
  run and the rule for host-run start code; the only reference to each is its XC slot.

### Done in lane GQ: pixel formats, locks, randoms, values, std-strings (182 more entries)

* Owned (all `mapped` in `db/status/StaticInit.jsonl`, promoted with GL's after the mode-1/2 run): the 57 pixel
  format entries (slots 19-75: 51 constructed by our constructors, the five constant-initialised DXT objects whose
  entry only move-assigns the name "DXT"+n, the unknown format whose entry copies only the seven characters
  "UNKNOWN"), cRZCriticalSection x10, cRZRandom x5 (`src/app/StaticInitObjects.cpp`); 25 of the 26 `value` entries
  (`StaticInitValues.cpp`: a small op table, sources read from the image's .data, .rdata constants as literals;
  slot 104 0x1383b4 is misclassified: it calls five imports (a named kernel object from a process-specific name, then
  mapped) and registers a destructor; left to the image); all 85 `std-string` entries (`StaticInitStrings.cpp`).
* The std-string kind is three forms, not one: 31 library constructions of " \t\n\r" (`basic_string(const char *)`
  0x141136; thunk `_Tidy_deallocate` 0x141455), 32 constant-initialised short strings whose entry copies only the
  characters (size and capacity 15 are in .data, the terminator is .data's zero), 22 long strings (17-31
  characters) whose entry allocates capacity + 1 = 32 bytes (0x141344), copies the characters and terminator and
  stores only pointer, size and capacity 31. The forms and texts came from running each image entry on junk with
  two patterns (which bytes it writes), then are checked per entry by the difftest. Allocator: both sides use the
  process heap (E1), so the game freeing or reassigning one of our strings is fine.
* Destructors: our own thunk per entry (a template per table index), registered through
  `staticinit::BindAtExit` / `BoundAtExit()`: default our CRT's `std::atexit` (standalone); the host binds the
  image CRT's atexit (`ImageAtExit`) under OwnStart=1/2, so the image's `exit` runs them in the original slots' LIFO
  order. Pixel formats: the class's deleting destructor with flags 0 (BaseT vtable, name emptied); locks:
  `cRZCriticalSection::Destruct`; strings: `std::string`'s destructor.
* Behaviour change by design: owned pixel formats carry our vtables from the start (as with mode 0, where the
  image's entries call our hooked constructors); the DXT and unknown objects keep the image's vtable until
  ImageStore adopts them, as before. The cRZRandom entries call `ConstructAt(-1, site)` with the image's call site
  (entry + 12), so the host's seed control (TS2HOST_SEED) covers them.
* Tests: `tests/static_init_objects.cpp` (PixelFormats, Locks, Randoms, Values, Strings: each entry against the
  image's in its slot, junk start states, construction + registration + the thunk run + frees; OwnedList: one per
  slot, 293 in all; ObjectsOnImageTable: the runner over the real table's GQ slots, 152 registrations, values equal,
  the LIFO exit frees the same blocks). GL's `StaticInit_SkipListOnImageTable` now covers the key kind only.
* Left: `class-inline` cRZString (135, needs its last two slots: done by lane GY below), cIGZAny 36, the
  cRZFastFixedAllocator2 / pooled containers (wait for the RZ pool allocator), `object` 0xaf6560 lists (55) and
  0x57c3c0 values (27), `atexit-only` (122), container (24), computed / registration / other, value slot 104.

### Done in lane GY: cRZString's last two slots and its 135 statics

* **The slots.** cRZString is now 33/33: slot 11 (0x1ad030, `mov eax, [ecx]; jmp [eax + 0x30]`, the reference form of
  IsEqual forwarding to slot 12 through the string's own vtable; folded with five other classes' slot 11, so not hooked,
  like the class's folded slots 1, 5/6, 7) as `cRZString::IsEqualForward`, and slot 33 (0x1cbea8, the vector deleting
  destructor: bit 1 = a `new[]` block, elements destroyed last first, bit 0 frees) as `cRZString::DeletingDestruct`,
  hooked. Tests `tests/rz_string_slots.cpp` (slot 12 recorded through a copied vtable; single strings and blocks of 0-5
  with every flag combination, frees in order). Byte match of 0x1cbea8: 0.375 (the EH frame around the CRT's vector
  iterator call), so difftest.
* **Not the standalone shape.** The class is still hook-shaped (non-virtual members on the game's vtable). Taking it to
  the shape is a project of its own: cRZString is used as a raw struct in ~180 source files (`Construct()` on embedded
  members, unions), and the image inlines its constructor at a few thousand sites (`tools/xrefs.py 0xd9b470`), so most
  game-built strings keep the game's vtable whatever our constructor stores; many groups' tests compare objects with
  embedded strings byte for byte, vtable word included. Owning the statics needs no shape: our constructor stores the
  game's vtable exactly as the image's entries do, so owning them changes nothing the game can see.
* **The statics** (`src/app/StaticInitRZStrings.cpp`, `staticinit::InitRZString`): 135 entries, 219 strings: 128
  single strings, 5 arrays of cRZString (13, 30, 23, 8, 4) and 2 arrays of 36-byte {cRZString, dword} pairs (9, 4; the
  pair's destructor 0x1d99b0 is cRZString's inlined, the dword is a constant: 0/1/2 or a float). Four element forms
  (`staticinit::RZStringForm`), found by running each image entry on junk with two patterns: **Construct** 139 (the
  library constructor's result: union zeroed, characters, terminator, a long text in a block of `size | 15` + 1;
  vtable; refcount 0: ours is `cRZString::Construct(text)`), **Heap** 52 (a long text inlined without zeroing the
  union: block of capacity + 1, pointer / size / capacity, vtable, refcount 0; union bytes 4-15 left), **Characters**
  17 (constant-initialised in .data but for the characters: only those are copied), **None** 11 (constant-initialised
  whole, an empty string: nothing written; 9 entries only register the destructor). Slots 643 and 644 also store the object's
  address in a pointer global (0x10fae58, 0x10fae5c), and register their thunk before constructing. Our thunk per
  entry runs our `cRZString::Destruct` on each element, last first, registered through `BoundAtExit()` in the slot.
* **Tests**: `StaticInit_RZStrings` (each entry against the image's in its slot: the file's bytes and three junk
  states with only the None / Characters capacity kept; built bytes, long texts by content, the alias, one
  registration, the thunk's result and its frees in order by element), `StaticInit_OwnedList` (428 in all),
  `StaticInit_ObjectsOnImageTable` (now with the cRZString slots: 287 registrations, the same frees). 13 mutants
  killed (forms, sizes, refcount, alias, the pair's dword, thunk order and index, the merged list).
* **Status**: the 135 are `verified`, `"in_effect": "callers"`, trap `Startup.OwnStart=1,2&Startup.OwnStatics=1`
  (803 traps). **For the coordinator**: a mode-1/2 run should log `800 XC initialisers (428 owned by us)` and
  `done (372 image, 428 ours, 0 mismatched, 0 unused)`; mode 0 is unchanged except the new hook on 0x1cbea8.
* **Left** in class-inline: cIGZAny (36), cRZFastFixedAllocator2 (21) and the few allocators.

### Done in lane GZ: the cIGZAny statics (36 entries)

* The form (each image entry run on two junk patterns, `StaticInitAny_Form`): all 36 are `push thunk; call atexit;
  pop ecx; ret`. The objects are constant-initialised in .data (vtable and value in the file); the entry writes
  nothing, the thunk (`mov [object], 0xd9c654; ret`, 11 bytes) is the inlined destructor storing cIGZAny's vtable.
  initializers.py calls them `class-inline` (cIGZAny, `class_slots` 1/1) because the thunk stores that vtable; by
  their effect they are `atexit-only`. The classes come from the vtable word in .data: AnyBoolean 20 (slots 171,
  174-184, 190, 337-338, 694, 696-697, 780-781), AnyUint32 11 (326-328, 339-342, 690, 692-693, 770), AnyFloat32 5
  (687-689, 691, 695). Most sit in 12-byte rows {pointer, value} (174-184, 687-697), read by the light-map manager,
  the effects manager, the pattern code and cTSCommonSystem::OnTick.
* The three classes in the standalone shape first (`src/gz/cGZAny.h`, namespace gz; `src/gz/cIGZAny.h` the interface,
  global as its RTTI): real classes deriving from cIGZAny, every slot an override, slot 0 the compiler's deleting
  destructor (the folded 0x14c5f0 / 0x1bedcb in the image), AnyFloat32's Assign the folded 0x1bf068. Hooks point at
  `gz::AnyAdapters<V>`. The global structs AnyUint32 / AnyBoolean / AnyFloat32 stay as the layout of objects the
  game builds inline with its vtables (about 700 code sites store these vtables; none compares them: every
  reference is a store or a register load for one), used by cTSSaveGameInfo and the property-set serializers. Our
  Clone returns our class, so clones of game-built values carry our vtables too.
  Pitfalls: the classes are `struct`s, as the original's RTTI (`.?AUAnyBoolean@@`): matchcheck v8 ties our vtable
  to the original's only by the full name (AU vs AV). `virtual ~cIGZAny() {}`, not `= default`: with a defaulted
  destructor MSVC drops the store of cIGZAny's vtable that the originals leave in a destroyed object (the slots test
  and the statics' thunk test catch it). Clone is `new X(*this)` (the compiler's null test after operator new).
  Byte matches: 11 of the 14 hooked slots MATCH through their adapters (v8); AnyFloat32::Clone 0x1bef35 lost its
  match (/O1 joins the null path through edx), Compare 0x177b70 / 0x14e5b0 never matched: difftested. The folded
  0x14c5f0 / 0x1bedcb / 0x1bf068 are `verified` unhooked (our vtables' slots, as other folded slots in the shape);
  AnySint32 and AnyAny, which share 0x1bedcb / 0x1bf068, are still hook-shaped.
* Owned (`src/app/StaticInitAny.{h,cpp}`): a table {slot, entry, object, thunk, kind, value}; `InitAny` constructs
  our class in place (our vtable, the same value; an AnyBoolean's three bytes after the value untouched) and
  registers its thunk (our destructor, qualified: our cIGZAny vtable stored) through `BoundAtExit()` in the slot.
  Behaviour change by design: the 36 objects carry our vtables (all six slots ours). Standalone, each becomes a
  `constinit` static of its class.
* Tests: `tests/static_init_any.cpp` (Form; Entries: each against the image's in its slot, the file's object with
  junk margins and junk padding, built and destroyed windows with vtable words as codes, no frees; OnImageTable: the
  runner over the 36 slots, LIFO exit); `StaticInit_OwnedList` counts them (36); `tests/gz_any_shape.cpp` (the
  classes through our vtables, the adapters on trap vtables).
* Host log expected after GZ alone: `800 XC initialisers (329 owned by us)`, `done (471 image, 329 ours, 0
  mismatched, 0 unused)` (lane GY's cRZString entries add to both).

### Done in lane HC: the atexit-only entries (109 of 122)

* **The form** (each image entry run on two junk patterns, `StaticInitAtExit_Form`): all 122 are `push thunk; call
  atexit; pop ecx; ret` and write nothing; the object is the image's file contents, and the thunk is its destructor,
  inlined or a tail jump to a function. Grouped by what the thunk destroys (the file's state checked too):
  * **std::string** 72: `_Tidy_deallocate` inlined (free when the capacity is above 15, then size 0, capacity 15, the
    first character zeroed). In the file: capacity 15, 70 hold `" \t\n\r"` (the same per-unit whitespace constant
    lane GQ found constructed by 31 other entries) and 2 `"\/"`. initializers.py's "73 free one buffer" counted slot
    225 with them: that one is a vector.
  * **std::vector** 3, trivially destructible elements: 4 bytes (slot 225, the neighbourhood code's), 8 bytes (730,
    731, the scenegraph's): zero in the file. Our `std::vector<uint32_t / uint64_t>` destructor: the same `_Tidy`.
  * **A fixed-size pool's chunk list** 21 (initializers.py's "RZMemory"): the word at the object heads a list of
    RZMalloc'd chunks linked through their first word; the pool also keeps its element size and chunk bytes at -8 / -4
    and its free list at +4 (the allocation, e.g. 0x9e0bb0: a chunk of `chunk bytes + 8` when the free list is empty).
    The thunk unlinks each chunk and frees it with RZFree (verified; ours `RZMemory::Free`). Zero in the file.
  * **References** 12: Release (slot 2) of a held cIGZUnknown when not null, 5 (366, 367, 383, the terrain paint
    tool's; 635, 636, cGZSndEa's); 6 (537-542, the animation code's) whose class has cIGZUnknown as a virtual base
    (the pointer adjusted by its vbtable's second word first); 1 owning pointer (762, the crash reporter's annotations
    object from StartCrashReporter, replaced): its deleting destructor (slot 0, flags 1). Null in the file.
  * **comutil's `vtMissing`** (764, 0x1083278): a VARIANT {VT_ERROR, DISP_E_PARAMNOTFOUND} in the file; the thunk is
    `_variant_t`'s destructor, VariantClear (oleaut32, the IAT entry 0xd9a474).
* **Owned** (`src/app/StaticInitAtExit.{h,cpp}`): a table {slot, entry, object, thunk, kind}; `InitAtExit` writes
  nothing and registers our thunk (one per row, by index) through `BoundAtExit()` in the slot; the thunk runs our
  destructor of the kind on the image's object (`std::destroy_at` of our STL's string / vector, the chunk loop with
  `RZMemory::Free`, `cIGZUnknown::Release` through the object's vtable (a `virtual cIGZUnknown` view class for the
  vbtable adjustment), slot 0 through `VirtualCall`, `VariantClear`). Standalone: constinit statics of those types.
* **Tests** (`tests/static_init_atexit.cpp`): Form (above, plus the file's object per kind and the string texts);
  Thunks (the entry and its registered thunk against ours, per kind 5-8 start states with junk margins: short strings,
  heap strings and vectors of both block schemes (below and above 4096 bytes, the aligned big-block form), chunk lists
  of 0-6 chunks, fake objects logging every vtable call (thiscall, and stdcall COM objects in VT_UNKNOWN /
  VT_DISPATCH variants), vtMissing, VT_I4 / VT_R8; compared: the window, each side's frees (exactly its own block),
  the calls; the pools: the image's RZFree redirected to a logging HeapFree, ours into a real allocator whose next
  allocations give back exactly the chunks); OnImageTable (the runner over the 109 slots: 109 ours, 109 registrations
  each, the same objects after the LIFO exit). `StaticInit_OwnedList`: 573 in all.
* **Status**: the 109 are `verified`, `"in_effect": "callers"`, trap `Startup.OwnStart=1,2&Startup.OwnStatics=1`
  (948 traps). callers.py saw one unconfirmed byte match, a "call" to slot 133's entry 0x138870 at 0xcf035c: the e8 is
  the displacement byte of `cmp edi, [ebp - 0x18]` at 0xcf035a; recorded in its `callers_handled`.
  Mutants (filter StaticInit): 9 killed (a string row as a vector, a virtual-base reference as a plain one, a string's
  storage off by 4, only the first pool chunk freed, the deleting destructor's flags 0, VariantInit for VariantClear,
  AddRef for Release, every entry registering thunk 0, the kind missing from the merged skip list); an 8-byte vector
  row as a 4-byte one survived, equivalent (both free the same block with the same byte count whenever the span is a
  multiple of 8, as a valid vector's always is).
  **For the coordinator**: a mode-1/2 run should log `800 XC initialisers (573 owned by us)` and `done (227 image, 573
  ours, 0 mismatched, 0 unused)`; mode 0 is unchanged.
* **Left: 13 atexit-only entries whose destructor isn't ours yet** (none has a status entry):
  * slot 11 (0x13e046): 0x1b5324 on 0x10df6cc: `[this+4]`'s slot 2, then the result's deleting destructor (an
    owning holder of some kind; class unknown).
  * slot 12 (0x14094a): 0xc6c66a on 0x1103054: a {first, last, end} buffer freed through 0x1be435 (an allocator's
    free(pointer, count), the 0xc6c... module).
  * slots 99, 100 (0x138317, 0x13830b): 0x1bd611 / 0x1baaa6 on 0x10cb000 / 0x10caffe: library-module holders
    (FreeLibrary through the IAT entry 0xd9a120 on a cached module handle, which is then zeroed), framework-gz.
  * slots 136, 137 (0x1388b0, 0x1388c0): `vector<cRZString>::_Tidy` 0x1441eb (our cRZString destructor is verified;
    the vector instantiation isn't in db/library-matches.jsonl).
  * slots 166, 220, 224: vectors with destructible elements (12-byte elements through 0x2b3600; 0x468fd0; 0x4ac010:
    the `_Destroy_range` of their element types).
  * slots 502, 523, 525, 529: 0x87d6e2, 0x98742c, 0x992516 (FreeLibrary again), 0x99936e (with a flag byte set first):
    framework-gz / third-party module code.

### Done in lane HD: the object entries (97 of 132)

* **The forms** (each entry's code and its thunk read; the values and arguments from the pushes, stores and movaps
  sources; every row then checked against the image's entry by the difftest). initializers.py's 147 `object` entries were
  132 not ours (lane GQ owned the 15 locks and randoms); grouped by constructor:
  * **Name tables** 55 (the "0xaf6560 lists"): `push array; mov ecx, object; call 0xaf6560` (NameValueTable::Construct,
    verified): a `vector<pair<std::string, uint32_t>>` from a null-name-terminated {name, value} array (1-43 names; 50
    arrays in .rdata, e.g. the light type names at 0xdbd4c0, 5 in .data: 699, 706, 712, 717, 719), sorted; thunk the
    vector's destructor, `jmp 0x3c6bf0` (Tidy, verified) in 46, inlined in 9. Mostly the command parser's (582-620).
  * **Floats** 27 (the "0x57c3c0 values", slots 289-316 but 297): `call 0x57c3c0` (StaticObjectStub, touches nothing),
    then three dword stores (19) or a 16-byte movaps copy from .rdata (7) or xorps zero (298): axes and colours (0, 0.5,
    1, -1). Constants: the table holds them as float literals.
  * **cRZDate** 2 (96: the epoch, read by cRZTime; 97: day 0xc22d): `ConstructDays(days, 0)` (the class's three slots
    verified; it keeps the game's vtable, as the image's entries' hooked constructor stores). No destructor.
  * **cRZFileName** 2 (197, 680): the default constructor 0x1c7f30 is five `cRZString()` (0x144f5b, verified); thunk
    `jmp 0x1c0eb2` (Destruct, verified). 0x1c7f30 itself has no status entry: ours constructs the five strings in place.
  * **A flag byte** 2 (507, 508; initializers.py's "cBlockCommand", from the shared empty destructor): 0x90a915 is
    `mov byte [ecx], 1`; thunk `jmp 0x140fe0` (`ret`). **cRZStopwatch** 1 (132): `Construct(4)`, thunk 0x140fe0 too.
  * **CrcTables** 1 (533): `jmp 0x9cbb60` (Initialize, verified). No destructor.
  * **Slot 212**: 0xc0a664 zeroes four dwords: a vector of 20-byte trivially destructible elements (thunk 0xd92580, its
    destructor inlined) and a word after it.
  * **Fixed-size pools** 5 (509-511, 637, 638, the windowing code's): 0x945bd6 (ret 4) stores max(size, 4) at +0 and
    that times 128 / 32 / 16 (below 0x200 / 0x800 / above) at +4; the chunk list at +8 is the file's zero. Thunk `jmp
    0x940e4e`: each chunk unlinked and RZFree'd (as lane HC's PoolChunks, at +8).
  * **std::list + cRZCriticalSection** 1 (110, network-web): 0x1ce3f3 = the library `list<uint32_t>` constructor and
    cRZCriticalSection at +8; thunk `jmp 0x1ce437`: the lock's destructor, then the list's `_Tidy` (library).
* **Owned** (`src/app/StaticInitConstructed.{h,cpp}`): a table {slot, entry, object, thunk, kind, argument, floats};
  `InitConstructed` builds the object with our code (the verified constructors, our STL, the few stores of 0x90a915 /
  0x945bd6 / 0xc0a664 written out) and registers our thunk (one per row) through `BoundAtExit()` in the slot when the
  image's entry registers one (67 of 97). Behaviour: the same objects; cRZDate and the cRZStrings carry the game's
  vtables, as with the image's entries. The name tables read the image's {name, value} arrays (GameAddress): standalone
  they become our own arrays.
* **Tests** (`tests/static_init_constructed.cpp`): Entries (each against the image's in its slot, 3-7 start states:
  the file's and junk with 8-byte margins; the CRC block with the built flag clear and set; pools with 0-6 chunks;
  slot 212 also destroyed live in both block schemes; compared: the registration (none / exactly the named thunk), the
  object as built (name tables by count, capacity and element texts and values; the list-and-lock by its ring and the
  lock block's fields), after the thunk, the blocks freed; pools as lane HC's), OnImageTable (the runner over the 97
  slots, per object after the run and after the LIFO exit). `StaticInit_OwnedList`: 670 in all.
* **Status**: the 97 are `verified`, `"in_effect": "callers"`, trap `Startup.OwnStart=1,2&Startup.OwnStatics=1`.
  Mutants (filter StaticInit): 17 killed: a name table's array off by one entry, the last float not stored, one float
  value, the date's year, a file name's fifth string not constructed, the flag's value, the stopwatch's mode, slot
  212's extra word left, a pool's chunk bytes above 0x800, the pool thunk freeing only the first chunk, the list-lock
  thunk leaving the lock, the lock not constructed, every row registering, every row registering thunk 0, a name table
  not destroyed, the CRC tables not built, the kind missing from the merged skip list. **For the coordinator**: a mode-1/2 run should log `800 XC initialisers (670 owned
  by us)` and `done (130 image, 670 ours, 0 mismatched, 0 unused)` (with the other lanes merged since, more); mode 0
  is unchanged.
* **Left: 35 object entries whose constructor or destructor isn't ours yet**:
  * slot 3 GeneralAllocator (constructor 0x1c655d verified, destructor 0x1c66c9 not); slot 536 cRZCallbackThread
    (destructor 0x9cfac2 not); slot 8 cRZStd (constructor verified, its three slots unmapped).
  * pooled containers 15 (RZBlockPoolAlloc: 0x177350 x6 565/570/571/578/621/622, 0x17acf0 x5 566/568/572/579/580,
    0x15b7e0 573/574, 0x54e250 569, 0x16de90 576) and 6 on cRZFastFixedAllocator2 pages (0xa368d0 560-563, 0x2051d0
    112, 0xaa4930 575): wait for the pool allocators.
  * imports 5: 93 / 94 (0x1af60a / 0x1afb32: kernel32 entry points looked up, thunk FreeLibrary), 102 / 103 (0x1c0d9b
    / 0x1c4830: more lookups), 105 (0x1c8d8e: the stopwatch calibration, QueryPerformance*, thread priority).
  * one each: 134 (0x26bbb0: memset and slot 33 over a vector at 0x10d8ca0), 675 (0xb094b0: a vector of 27 pairs
    through 0xb08f50 / 0xb09010), 754 / 755 (0xc696d9 / 0xc696ce: a class with vtable 0xe9e7ac), 769 (0xc94c78), 773
    (0xcac2b0: objects with virtual bases).

### Done in lane HF: containers, registrations, computed, other, value slot 104 (36 of 41)

* **The forms** (each entry and thunk read, then checked per entry by the difftest from junk). initializers.py's kinds
  are rough here: 5 of the 9 "registration" entries are an `eh vector constructor iterator` over an array (3 std::pair
  of strings, 401 / 14 game objects, 12 cRZString), 5 parse a GUID text (0x4f8eb0); 2 "container" entries write
  nothing (atexit-only vectors); 2 "other" are empty functions.
  * **Trees** 17 (std::map / std::set): the entry stores only the head node (`_Buyheadnode`, or inlined), the count is
    the file's zero; the thunk is `_Erase_head` / `~_Tree`. Element types by the node size and the node free: int,
    pair<int,int>, pair<float, 12 / 4 bytes>; slot 526's 44-byte and 645's 36-byte elements hold a **cRZString** (at +0 /
    +4) whose teardown the node free runs (0x9626b2 / 0x1baba2: vtable stored, string tidied): the library matches name
    these `Blob4<N>` instantiations, but the reloc targets (the element destructor) differ, so a library name does not
    say the element is trivial. Read the node free before choosing a type.
  * **Hashes** 2 (std::unordered_*: {max load 1.0f, list, buckets x16, mask 7, max index 8}): slot 512 calls the
    `_Hash` constructor (every word written), elements {cRZString, dword} torn down by 0x1d8450; slot 729's constructor
    is inlined and leaves the list's count to the file, elements {std::string, cIGZUnknown *} (0xb0d300: Release, then
    the string).
  * The NameValueTable (478, 0xaf6560: ours) whose thunk 0x731ee4 (the library match says vector<Blob4<28>>) does
    destroy the strings (`_Destroy_range` 0x72e80d). Arrays: 109 (3 pair<const string, string>), 733 (12 cRZString,
    `Construct()` / `Destruct()`), destroyed last first.
  * Computed: 363 / 798 `(float)tan(c)`, 524 `1.0f / (float)log(2.0)` (0x83a4d0 is the CRT's SSE2 log: bit-exact for
    this input by the test; not yet in crt-equivalents.jsonl), 765 the CPU feature flags (0xee0d1: GetVersionExA,
    CPUID; only called from its entry). Value slot 104: the cRZTimer section {mapping, view} at 0x10cb638 (a named
    8-byte mapping "cRZTimer<pid>"; read by cRZStopwatch::MeasureFrequency; ORIGINAL-BUG: a failed MapViewOfFile is
    not checked before the view is zeroed). The GUID entries 317-319, 323, 324.
* **Owned** (`src/app/StaticInitContainers.{h,cpp}`, `staticinit::InitHF`, one table): containers are our STL's types
  built in a local and stored word by word (only the words the image's entry writes; the nodes and buckets point at
  the head node, never at the container, so the words can move); thunks by table index through `BoundAtExit()`. The
  elements with a cRZString use our verified `appctor::DestroyElement` as their destructor. New: `src/misc/HexParse`
  (0x1b0e2c ParseHex32, 0x4f8eb0 ParseGuidText, both hooked, verified by difftest; 60 callers reach them).
* **Tests**: `tests/static_init_containers.cpp` (Entries: junk and file states, pointers compared by what they point
  at, the thunk on live objects with elements, long strings and fake referenced objects: frees in order, calls;
  Empty; CpuFeatures with GetVersionExA faked; TimerSection with the imports bound to the real kernel32: fresh,
  existing and name-taken cases; OnImageTable), `tests/misc_hex_parse.cpp`. `StaticInit_OwnedList`: 609. Mutants: 18 +
  1 + 7 killed (lists in the status notes).
* **For the coordinator**: a mode-1/2 run should log `800 XC initialisers (609 owned by us)` and `done (191 image, 609
  ours, 0 mismatched, 0 unused)` with HF alone on top of HC (lanes HD / HE add to both); mode 0 gains the two
  hexparse hooks.
* **Left (5)**, each waiting on a destructor or constructor that isn't ours:
  * slot 135 (0x138880): 401 objects of 0x7c, ctor 0x26bc20, dtor 0x257860 (`mapped`: two std::strings and a cRZString
    vtable at +0x20; owner unknown).
  * slot 218 (0x13a970): 14 objects of 0x14 of a class with vtable 0xdc3adc (base 0xdca568: ctor 0x47e000 reserves
    0x10 through 0x47eb80; dtor 0x416c60 -> 0x47e260).
  * slot 223 (0x13aa30): an unordered map of 0x80-byte values whose destructor 0x4ad0f0 holds two cRZStrings and two
    nested containers (0x218cf4 at +0x48, 0x4acfe0 at +0x60).
  * slot 527 (0x13e405): a std::list (node 0x40) of 0x38-byte objects destroyed by 0x999001 (a class's scalar deleting
    destructor, flags 0: a cRZString at +8).
  * slot 193 (0x1395b0, computed): two axis-angle quaternions (sqrt, sin, cos) multiplied and normalised by 0x1a7020
    (not ours; a 383-byte float routine whose operation order the result depends on).

### Done in lane HE: the RZ pool allocators and their statics (50 entries)

* **The classes in the standalone shape first** (`src/rz/RZFixedAllocator.h`, `src/rz/RZFastFixedAllocators.h`):
  * the interface `rz::cIGZFastFixedAllocator2`: 4 slots (Alloc, Free, NewPage, FreePage) and no destructor slot;
  * `rz::cRZFastFixedAllocator2Common`: Alloc, shared by every instance (0x14a2d0), and the page-ring members
    0x1be2bc / 0x1be31b / 0x1be02b plus FreeRings (0x1be23b on both rings);
  * `template <N> rz::cRZFastFixedAllocator2`: Free, NewPage, FreePage, the constructor and the destructor. The page
    size is the smallest 2^L (L ≥ 11) holding 64 blocks after the 16-byte header, and FullMark is 0x40000000 + 3/4 of
    the blocks;
  * `template <T> rz::cRZFastAllocator`: derives from it, with the block size from a trait.

  The game's 18 sizes and 8 pooled classes (AudioEvent, AudioSoundInstance, BehaviorNode, WantInstance,
  TreeStackElem, cGZSndEventStd, cGenericDataTableContainer, XPrimParam) are instances of these two templates.
* **Hooks.** They point at adapters: the hook-shaped views' members, which stay for other groups' direct calls and
  make qualified, forced-inline calls into the classes. New in this lane:
  * Alloc 0x14a2d0;
  * the five folded FreePage copies, one per page size: 0x15aa70 (2^11), 0x14a380 and 0x20fef0 (2^12), 0x163170
    (2^13), 0xa35050 (2^14);
  * 15 out-of-line constructors and 13 destructors (`rz::FastAllocatorAdapters<X>`).

  The tools name 0x14a2d0 and the FreePage copies `shared_vf_*`, but tools/xrefs.py shows only the allocator family's
  vtables, its jump stubs and the pooled classes' operator new referencing them. They are one template body per page
  size, not folded with unrelated classes. hookspec refuses `shared_vf`, so their CRCs come from hookcrc.py by hand.
  **The coordinator should agree to hooking them.**

  Byte matches (matchcheck v8): Alloc, the four 2^11 / 2^12 / 2^13 / 2^14 FreePage copies, every Free, and
  0x1be31b / 0x1be02b. 0x20fef0 (pop ecx; pop ecx for add esp, 8), the NewPages, 0x1be2bc and the constructors and
  destructors are difftested.
* **Tests.** `RZFastFixedAllocators_Shape*`: one per class (26), each running the class's whole life on real block
  pools, every slot through the object's own vtable.
  * Route A compares the image's object, from its constructor and with the game's vtable, against ours, built by our
    constructor or the hooked constructor's adapter.
  * Route B runs the image-built object on a copy of the game's vtable whose slots are the hooks' adapters.
  * Each run calls slots 2 and 3 by number, then four phases of Alloc / Free. The third run instead fills another page,
    so the destructor gives back a full ring.
  * Compared: each step's block and both rings, the page bytes, the destructor's vtable word by class, and the pool's
    next 12 pages.
  * The block-pool fixture moved to `tests/rz_block_pool_sides.h`.
* **Owned** (`src/app/StaticInitAllocators.{h,cpp}`): a table `{slot, entry, storage, thunk, block size, game
  vtable}` with 50 rows:
  * 21 FFA2 class-inline entries;
  * 8 cRZFastAllocator<T> (113, 125, 219, 285, 288, 641, 681, 732);
  * 21 that initializers.py calls `object` with no class (each calls an allocator's out-of-line constructor, e.g.
    0x17acf0).

  `InitAllocator` constructs our class in the image's storage (our vtable and the first page from the block pool) and
  registers its thunk through `BoundAtExit()` in the slot. The thunk runs our destructor on the storage: both rings
  are given back, and the base's vtable is left, as the image's thunk leaves cRZFastFixedAllocator2<N>'s.
  Standalone, each becomes a static of its class.
* **Tests** (`tests/static_init_allocators.cpp`):
  * Form: each image entry on junk patterns. The class-inline entries rely on the vtable being constant-initialised
    in .data; 219 / 285 / 288 store the derived vtable last.
  * Entries: each against the image's in its slot from junk states, including the thunk and the pools after it.
  * OnImageTable: the runner over the 50 slots, with the LIFO exit.
  * `StaticInit_OwnedList`: 623 in all.
* **Behaviour change, by design.**
  * The owned pools carry our vtables (every slot ours).
  * With the constructor and destructor hooks, pools built by the image's out-of-line constructors carry our
    vtables in mode 0 as well.
  * Pools built by inlined constructors elsewhere keep the game's vtables, whose slots are hooked.
* **For the coordinator:**
  * A mode-1/2 run should log `800 XC initialisers (623 owned by us)` and `done (177 image, 623 ours, 0
    mismatched, 0 unused)`.
  * Mode 0 needs a smoke run for the new hooks: Alloc, the folded FreePage copies, and the constructors and
    destructors.
* **Left: cGZDefaultAllocator** (slot 101, class-inline). It has 7 slots; QueryInterface 0x1be4fa, 0x1be540 and slot
  6 0x1be557 are unmapped.

## What the standalone build must own

* **The CRT seam.** Our own entry: `entry` 0x8248a3 / `__scrt_common_main_seh` 0x824727 are the
  game's CRT; while it runs WinMain, the atexit list, the thread-safe-statics epoch (WinMain's two
  statics, GetCOMDirector's) and `exit` belong to it, which is what blocks the WinMain candidate.
  Milestone E needs our CRT start (MSVC 14.40 `/MT`), our WinMain, our statics. **Design and first step: "E1" above**
  (our start sequencer drives the image's CRT as a library; WinMain's two statics stay in the game's storage with
  their destructors on the image CRT's atexit list; the 800 XC statics are stage 3).
* **Our vtables for the five startup objects** (framework, cGZCOM, root and app directors,
  application): the shape headers of this pass. A shape framework calling `OnStart` on an
  original director is fine (it calls through the interface), so the order of the shape lanes is
  free once the interfaces exist.
* **The application's own policy**: cTSAppFW::Init, PreFrameworkInit, the option store
  (cTSAppCommonImpl), the user-data directories. These are game behaviour, not infrastructure:
  verified by difftest, never `replaced`.
* **Inside the `replaced` scope** (roadmap list; eligible, contracts in `docs/contracts/`): crash
  reporting (WinMain steps 1-3, the reporter at app+0x98, already replaced: 0xc2b206), analytics
  (0xc2df67, replaced), the terminate / purecall / unhandled-exception handlers (0xcac1f0, 0xcac170,
  replaced), Steam glue (SteamInit 0xc2a358, the SteamID query, SteamAPI_RunCallbacks in OnIdle,
  SteamAPI_Shutdown), process setup (IME 0xc2a3a4, the single-instance mutexes and the launcher
  shutdown message in the app constructor), and from 2026-10-17 the window (`cCanvasW32::Init`
  0x155e90, RegisterClassExA / CreateWindowExA), the Direct3D device and device-level input. The
  framework, the COM registry, the directors, the service bands and the app's options are not in
  that scope.

## Milestone H: work packages

Planning pass by lane H0 (2026-10-18), on main at 6a6cbba. Milestone H (docs/roadmap.md): the `tools/status.py`
"reachable" line at 100%. **The raw list comes from the tool, the grouping and order live here**:

```
tools/py -I tools/status.py --reachable-todo            # what the line counts as not done, largest first
tools/py -I tools/status.py --reachable-todo --json     # one object per line: callers (done / not done / library), callees
tools/py -I tools/status.py --reachable-todo --live     # only what still runs under our replacements (below)
tools/py -I tools/status.py --rva 0x4f8eb0,0x1b0e2c     # per function: status, group, class, original_calls, line facts
tools/py -I tools/status.py --metrics                   # just the totals and the metric lines, the reachable line last
```

Each row has a `kind` (a first cut: `static-init` for a root, `import-thunk`, `third-party` from db/subsystems.jsonl,
`near-library` for unmatched code linked among library functions, else `game`) and `live`: still reached by direct
calls when our functions are in effect, i.e. the walk follows only the declared `original_calls` out of a function of
ours (with OwnStart=2, OwnStatics=1, the defaults now).

### The numbers (2026-10-18)

The line: 1,440 functions, 165,280 bytes, 56,618 done (34.26%; the roadmap's 33.5% was a few merges earlier). Not
done: **693 functions, 108,662 bytes**, of which only **362 functions, 27,486 bytes are live**. The rest is code the
original's call graph still reaches but nothing runs any more: callees our verified or replaced functions reproduce
themselves or no longer need. Two policy points follow, for the coordinator to settle with the user before lanes
spend time on dead code:

1. **`replaced` and the line.** The roadmap says "verified (or library / `replaced` per its rules)", but the line
   counts only verified and library, and it walks on through a replaced function into callees its contract
   replacement never calls. 36.0 KB of the 108.7 KB is that (P0 below): crashpad under StartCrashReporter 0xc2b206
   (31.6 KB, third-party kind "drop"), the HTTPS client under SendAnalyticsEvent 0xc2df67 (2.1 KB), the handler
   installer 0xcac1f0. Proposal: the line counts `replaced` as done and does not descend from it except through its
   `original_calls`. Without that, H means verifying crashpad, which the third-party policy says to drop.
2. **Dead callees of verified code.** 9.0 KB (H6 and the dead part of H7) is only reached through verified functions that do the
   work themselves (NameValueTable's std::sort under 0xaf6560, small helpers under the framework constructor). The
   line needs them verified anyway. Options: verify them (cheap, they are STL or small), or let the line follow the
   live walk (descend only through `original_calls` of our functions). The live walk is what the standalone build
   needs; the plain walk is stricter (every original on the path has a checked twin).

With both changes the line would be 165,280 bytes minus what nothing runs, and the remaining work the 27.5 KB live
set (H1-H5, the live parts of H7 and H8). The table below covers the full 108.7 KB either way.

**Decided (coordinator, per the roadmap text): policy 1 yes, policy 2 no (the plain walk stays for verified code).
Done in lane HO** (tools/status.py, same tree as the numbers above): the line counts `replaced` as done and descends
from it only through its `original_calls` (also in `--reachable-todo`), and import thunks (`jmp [IAT]`, 6 bytes, found
from the image's import directory: 62 in the image) count as done by rule, reported as their own number:

| | old line | new line |
|---|---|---|
| reachable game functions | 1,440 | 1,185 |
| reachable bytes | 165,280 | 99,596 |
| done bytes | 56,618 (34.26%) | 58,903 (59.14%): verified 56,579, replaced 2,287, import thunks 18 (3), stubs/tiny leaves 19 |
| not done | 693 functions, 108,662 bytes | 436 functions, 40,693 bytes |
| of which live | 362 functions, 27,486 bytes | 362 functions, 27,486 bytes (unchanged: nothing live was dropped) |

The 68.0 KB that left the line is everything only the four replaced functions reached: P0 (crashpad, the HTTPS
client, mini_chromium and its iostreams) and, below it, CRT code nothing else calls: H8a falls from ~10.5 KB to 729
bytes (717 live; the printf/scanf processors were crashpad's), H8b to 5,553 (4,011 live), H8c to 8,781 (5,609 live);
the 6 import thunks in the old list are done or unreached. What remains dead on the line (13.2 KB, policy 2 keeps it):
NameValueTable's STL 4,861 (H7), dead CRT 4,726 (H8), dead game code 2,789 (H6 and others), near-library 791. The
P0 row below is done by this rule; the other rows' byte counts are from before it (their live bytes still hold).

### The packages

Ordered: first what removes original calls from verified code (H1), then what the image still runs at start (the
XC initialisers still the image's, 227 after lane HC, and their callees: H2-H5, which also finishes milestone G), then the dead
remainder and the identification work. Bytes are the functions' sizes; "live" is the part still executed.

| package | functions | bytes (live) | prerequisites | notes |
|---|---|---|---|---|
| H1 registration and TextToNumber | 0x4f8eb0, TextToNumber 0x1b0e2c, the 5 registration statics calling 0x4f8eb0 (slots 317-319, 323, 324) | 389 (389) | - | Removes 10 `original_calls` declarations: TextToNumber from cGZPropertySetSerializerXml (7), 0x4f8eb0 from cTSCASBodyPartSystem, cTSSimDNA, SimCall. Then the five slots join the skip list. |
| H2 ready statics | 139 XC entries whose direct callees are all done: 55 NameValueTable lists (ctor 0xaf6560), 27 by StaticObjectStub 0x57c3c0, 23 containers, 13 atexit-only, cRZDate 2, GeneralAllocator, cRZStopwatch, cRZCallbackThread, cRZStd, CrcTables, 4 registrations, 2 computed, value, other 2, a few class-inline (SimDoNodeActionLog, cGZDefaultAllocator, cCompletionObj, cBranchNodesNoFilter) | 4,457 (4,457) | - | Stage-3 mechanics only (owned initialiser in its slot, difftest per "The skip list"); no new bodies. The 13 atexit-only need their destructors (list under lane HC), which the line doesn't see (atexit pointers aren't calls) but G does. Two lanes if one can't finish (objects; the rest). |
| H3 fast allocators | the 14 cRZFastFixedAllocator2 / cRZFastAllocator constructors (0x160840, 0x1608a0, 0x165870, 0x177350, 0x17acf0, 0x15b7e0, 0x16de90, 0x54e250 (83-85 B each, one template), 0xa368d0, 0xaa4930, 0x20ff00, 0x22ee16, 0xb286b0, 0x14ba20) and their 49 statics | 3,085 (3,085) | - | A class lane: the template in the shape (db/status RZFastFixedAllocators: 8 slots still mapped), the constructors as its members, then the statics own them. Allocator statics before their users (Order constraints). |
| H4 command parser static | slot 773 (0x1409f0): its constructor 0xcac2b0, nGZCommandParser::cBaseParser 0xafd800 (0x1c0 bytes, virtual bases), 0xafdc40, cLogFileCheat registration 0xcaf620, 0x14a800, 0x14a3a0; slot 675 (0x13ff80); cBlockCommand 0x90a915 and slots 507, 508 | 2,825 (2,825) | H7's sort for slot 675 (0xb094b0) | cBaseParser is the one real class body on the start-up path left; the parser's vtables are virtual calls (not in the line) but its constructor is. |
| H5 other statics | the rest of the image-run XC entries and what they call outside the CRT: file-system set-up (InitWideFileFunctions 0x1c0d9b, InitWideDirectoryFunctions 0x1c4830, 0x1c8d8e with cRZStopwatch::ReadTsc 0x1c9244, cRZFileName 0x1c7f30), scenegraph computed slot 193 (0x1395b0 with 0x1a7020), audio slot 769 (0xc94c78, 0xc94db1, 0xc98e00), 0xc696ce / 0xc696d9, slot 765 (0xd8e6d1 with 0xee0d1, 0xee07f, 0xee0a9), 0x945bd6 (5 slots), 0x97df6d, 0x1af60a, 0x1afb32, 0x1ce3f3, 0x2051d0, 0x26bbb0, 0xc0a664, the XI entry 0xd17f1a (0x144845, 0x242e9a) | 3,532 (3,532) | - | Mixed: about 15 small owners. ReadTsc is `reimplemented` (cpuid/rdtsc); it needs a difftest route (intrinsics, a fake TSC) to be verified. Slot 524 (0x13e3a0) calls CRT code (0x83a4d0, in H8b). **Done in lane HQ** (below; 0x2051d0 left to H3). |
| H6 callees of verified code | 24 small functions only verified code calls (0x1416fa family under 0x142c2a, 0x141c65 / 0x141d39 / 0x1c8c84 under 0x141d76, 0x144e3b, 0x15aa90, 0x1adf65 / 0x1adf14 / 0x1aafcb, 0x1af5d6 / 0x1ae3a4, 0x1c01b9 / 0x1c0158 and 0x5cb103 / 0x5cb09f under the framework constructor 0xc2ccdb, 0x1be496, 0x1bdd37, 0x1adba0, 0x1adb28, 0x418be0, 0x1be435, shared_vf_4dc940) | 1,661 (0) | policy 2 | Dead under our code. If the line keeps the plain walk: write and difftest them (most under 150 bytes). shared_vf_4dc940 is 5 bytes and folded: it can't be hooked (CLAUDE.md), so it needs the callers route or a rule. |
| H7 STL instantiations | NameValueTable's vector growth and std::sort (0xaf4fd0-0xaf6800, 9 functions, 5.0 KB, dead), the 27-entry name table of slot 675 with its sort (0xb094b0, 0xb08f50, 0xb09010, 0xb08a30, 0xb08e30, 0xb08930; 2.7 KB, live), game-looking helpers only library code calls (0x141xxx-0x145xxx vector / cRZStringVector internals, 0x152c50, 0x1ac5ec, 0x1af86f, 0x1b6dee, 0x1bdc30, 0xc25b3b, ...), std::string ctor 0x173790 | 10,131 (2,760) | - | Library matching (tools/libmatch.py, tools/stlprobe): compile the instantiation with the element type at /O1 and record it in db/library-matches.jsonl; reimplement only what doesn't match. |
| H8a CRT: start-up and stdio | 0x821000-0x830000 plus the CRT's own static initialisers (the 7 `crt` kind XC entries, slot 1 0x824715 among them, and the XI entries in the CRT range, 0x824662 the largest): printf / scanf processors (0x82b560-0x82f58a) | 10,640 (885) | - | EA's CRT version differs from MSVC 14.40, so these don't byte-match: identify each by structure and record it in db/crt-equivalents.jsonl (no code; counts as library). Or find the exact UCRT version and match. |
| H8b CRT | 0x830000-0x845000: strftime-like 0x8416fe (2.5 KB), locale (0x835c0c, 0x836521), 0x830006, fp conversion | 13,469 (3,972) | - | As H8a. |
| H8c CRT | 0x845000-0x85a000: fp conversion and math (0x846401..., 0x8540ac 5.6 KB), locale names, stdio streams, the VERSION.DLL import thunks | 22,498 (4,794) | - | As H8a; split at 0x84e000 if one lane can't finish. Import thunks (`jmp [IAT]`, 6 bytes) should count as done by rule (our exe imports the same DLLs): a status.py change. **Done in lane HO** (the VERSION.DLL thunks are done; the bytes above are from before policy 1, under which H8c is 8,781 bytes, 5,609 live). |
| P0 replaced subtrees | StartCrashReporter 0xc2b206 (crashpad 0xc4d400-0xc5a000, mini_chromium, its iostream instantiations 0x1b4xxx-0x1b8xxx), SendAnalyticsEvent 0xc2df67 with 0xc2de96 / 0xc2e1c8 ..., handlers 0xcac1f0 | 35,975 (787) | policy 1 | No lane: a rule change. The 787 live bytes (0x852b10 in the CRT range, filed third-party, and a few shared helpers) are also reached from live code and move to H8 / H7 once the rule applies. **Done in lane HO**: the rule is in tools/status.py; of P0 only the live part is still on the line (0x852b10, 645 bytes, now counted under H8c, and the shared helpers). |

Bytes add up to the line's 108,662. Each package's statics also need what Stage 3 says for owning them (slot check,
destructor in the same slot), and its run is part of the next G/H smoke batch.

### Done in lane HK: H1 and H6 (all but three)

- **H1**: the two bodies are lane HF's (hexparse::ParseHex32 0x1b0e2c, hexparse::ParseGuidText 0x4f8eb0, src/misc/HexParse).
  Lane HK switched every verified caller to them and dropped the 10 `original_calls` entries (cGZPropertySetSerializerXml
  x4 entries, cTSCASBodyPartSystem 0xcf5ca0, cTSSimDNA 0x4fc8a0, SimCall 0x58bc50, PrimT24 0x5879a0, cTSFamily 0x49c1a0,
  cTSPersistSystem 0x1e7230). Note: the original 0x4f8eb0 stores words without a bound, so a text of more than 32
  significant characters writes over its own return address.
- **H6**: 21 of the 24 are verified in group StdGameTypes (src/std/StdGameTypes*.cpp, tests/std_game_types.cpp): they are
  all standard-library members instantiated with game element types (list<FwRef> emplace and its guard, the framework's
  and cGZCOM's container constructors, set<uint32_t>::_Emplace in both copies with their temp nodes, the COM director's
  class-map insert, the block pools' region-map erase and process-heap deallocate, string +char / find / find_last_of,
  allocator<cRZString>::deallocate). Each replacement is that member of our STL through a thin accessor; four byte-match
  (0x141d39, 0x144e3b, 0x1aafcb, 0x1adb28), the rest are difftested (18 mutants).
- **Left in H6**: 
  - 0x1c8c84, cRZExceptionNotification's constructor (13 B), stores the class's vtable: it needs the class in the shape (5
    slots: QI 0x1c8d3f, Release 0x1c8d77, Notify 0x1c8cb7 with _snprintf, MessageBoxA and a catch(...), deleting destructor
    0x1c8c98 / 0x1c8c91), and then the Framework group's constructor (src/gz/GZFramework.size.cpp) should create it
    instead of writing the game's vtable by hand. A class lane's job.
  - shared_vf_418be0 (7 B, `lea eax, [ecx+0x98]`, called by RunApp 0xc2d23b) and shared_vf_4dc940 (5 B, `mov byte [ecx+0x10],
    0`, called by WinMain 0xc2b87c) are folded with unrelated classes' slots (nTSUI::cAlbumEntry, cRZMemoryReport, ...), so
    they can't be hooked, and the callers route fails while those vtables are live. They need a rule: e.g. a folded
    trivial function counts as done when every direct call to it is from verified code that does the same thing inline
    (our RunApp / WinMain do), checked by status.py. Coordinator's call.

**H8c: done in lane HN** (2026-10-18). All 104 non-thunk functions of 0x845000-0x85a000 (23,759 bytes, 45 live /
5,609 B) are in db/crt-equivalents.jsonl (rows 0x8451c9..0x859ac3); `--reachable-todo` lists only the three VERSION.DLL
import thunks there (left for the import-thunk rule). Nothing in the range is EA code: 0x852b10 (filed third-party)
is UCRT `expand_argument_wildcards<char>`, and the other third-party filings (0x849a5d, 0x84d628, 0x852640/0x8527e8,
0x855cf1) are wcscat_s, the x87 log10 body, signal and _87except; db/subsystems.jsonl could drop them. 32 rows are
`"evidence": "bytes"`: a whole 10.0.26100 libucrt body matches at the address but Ghidra sized the function
differently (cut before a `__finally` funclet or tail, stack-argument libm entries inside `_CIxxx_default`, XI
initialisers missing from db/functions.jsonl), so libmatch's exact-size rule missed them. The other 72 are
identified by structure (source file, callees, callers, size within a few percent of 26100's). No exact CRT found:
only MSVC 14.40 and 14.44 are installed (14.44 adds no match of 16+ bytes anywhere in 0x821000-0x85a000) and the UCRT
is the Windows SDK's (only 10.0.26100 here). EA's UCRT is older than 26100 (getptd without the slow-path split,
try_get_function in one piece, different winapi thunk ids) but has `__crt_cached_ptd_host`, so roughly
10.0.19041-10.0.22621; an older SDK's libucrt.lib (e.g. the Microsoft.Windows.SDK.CPP.x86 NuGet packages) is the
thing to try for exact matches. Pitfalls found on the way: libmatch names in winapi_thunks.obj (0x847c83-0x8484a8)
can be off by thunk id (0x847c9d `try_get_FlsFree` is EA's id 1, CompareStringEx; 0x84835b `__acrt_CompareStringEx`
is used as LCMapStringEx), the `acos` names on 0xc4afa0/0xc4b090/0xc4b0e0/0xc4b130 are the identical 67-byte
`_cos`/`_log`/`_sin`/`_tan` stubs, and 0x840d37 (`__acrt_lock` in tool output) is `__acrt_unlock` (both names are in its
library-matches row).

**H8a / H8b: done in lane HM** (2026-10-18). All 93 functions `--reachable-todo` listed in 0x821000-0x845000
(24,087 bytes, 34 of them live) are rows in db/crt-equivalents.jsonl (`"lane": "HM"`, contiguous), so the range is
off the list; no code. All are CRT (libucrt, libcmt start-up, libcpmt); none is EA code (0x836096, filed
third-party, is `_wsetlocale_get_all`). Evidence per row: `bytes` (27 rows: the image equals the 10.0.26100 /
14.40 library body with relocations masked, but libmatch missed it because Ghidra's size differs from the body's,
the function is under 16 bytes and ambiguous, or it is a `__finally` funclet Ghidra split off), `relocation` (28: the
callee's symbol named by the relocation inside a byte-matched library caller, e.g. `_wcsnlen` from
`__acrt_LCMapStringW`), `structure` (38: side-by-side with the 10.0.26100 body plus callers/callees). Why the rest don't
byte-match: EA's UCRT is a different SDK build compiled by a different compiler (same source, different register
allocation, cmov vs branches; a few older shapes: `_handle_exc` calls `_set_statfp` per flag,
`exit_or_terminate_process` checks `should_call_terminate_process` itself, `_wcsdup` without the 0x55 cap). Only
MSVC 14.40 / 14.44 and SDK 10.0.26100 are installed; the UCRT comes from the SDK, not the toolset, so an exact match
needs the matching older SDK's `libucrt.lib` (not installed; the SDK or the `Microsoft.Windows.SDK.CPP.x86` NuGet
package would have it). Scripts used (scratch, not committed): prefix/containment search of library bodies at each
RVA, relocation-name lookup in byte-matched callers, difflib similarity ranking.

### Done in lane HL: H7 (STL instantiations)

* **Library matches (probe widened, db not regenerated on the branch).** `tools/stlprobe/probe.cpp` now also
  instantiates member templates through small user functions (vector::resize for trivial element sizes,
  string::append(n, ch), map operator[], unordered multi containers) and two of the game's own sorts:
  NameValueTable's (pair<string, uint32_t>, out-of-line comparator) and the expression evaluator's (pair<const
  char*, T*>, inlined strcmp; `#pragma intrinsic(strcmp)` stands in for the game's /Oi). A scratch build at /O1,
  /O2, /O2 /Ob1 matched 80 functions not yet in db/library-matches.jsonl: in H7 0x152c50 (vector<uint8_t> resize),
  0xaf4fd0 / 0xaf6280 (NameValueTable's push_back growth), 0xb08930 / 0xb08e30 (the evaluator sort's _Med3 and
  _Pop_heap_hole_by_index); in H4 0x14a3a0 (_Allocate_at_least_helper). Three of the 80 are `verified` entries now
  (0x141c15 StdInstances, 0x5757a0 SimAnticsTreeSim, 0x9b3eb0 cGZPersistResourceManager): as in 2887911, their
  hooks and status lines go when the db is regenerated. **Coordinator:** run `tools/stlprobe/stlprobe.ps1` and
  `tools/libmatch.py` from the main checkout after merging (the lane could not compile the probe).
* **Reimplemented, hooked, difftested** (the rest of the speed-profile instantiations differ from the probe only in
  registers and inlining: whole-program optimisation; each forwards to the same MSVC 14.40 template):
  * `src/gz/EvalFunctionTable.{h,cpp}`: the evaluator's built-in name table (27 names: "true"/"on", "false"/"off",
    "pi", the math functions, "strcmp", "strstr", angle conversions; each with its cEval* object), constructor
    0xb094b0 (what lane H4's cBaseParser and slot 675 construct; callable as `nGZCommandParser::EvalFunctionTable`),
    and 0xb09010 / 0xb08a30 / 0xb08f50. No original calls left.
  * `src/std/NameValueTableSort.{h,cpp}`: 0xaf5fe0, 0xaf51e0, 0xaf55c0, 0xaf54f0, 0xaf5ce0, 0xaf5e80, and the
    comparator 0xaf6800 hooked to lane GK's `ByLengthThenReverseText::operator()`.
  * `src/std/StdStringHelpers.{h,cpp}` (removed by lane WM, library matches): string copy constructor 0x173790, compare 0x145d1f, pair<const string,
    string> deleting destructor 0x1cbf38.
* **Left** (all dead: only library code calls them): 0x14155c (vector<cRZString> _Assign_counted_range), 0x1419c3 /
  0x1419de / 0x1419f9 (list-node frees calling element destructors 0x1421bb, 0x142196, 0x1421e1), 0x143dc2 /
  0x14424b (unordered multi-container rehash / range erase with a uint32 key; not matched by the probe's
  multimap<uint32_t, uint32_t>), 0x1440b8 (_Min_load_factor_buckets, x87 code), 0x1443bb (string _Xran, 10 bytes:
  libmatch drops it as not unique), 0x1bdc30 (map insert), 0x1ac5ec (string append(n, ch) growth), 0x1af86f /
  0x1b6dee / 0xc25b3b (`_EH_prolog` functions: db/functions.jsonl sizes them 10 bytes, so libmatch can't see them),
  and the iostream pieces 0x1b4xxx (P0, crashpad's).

### Done in lane HP: H4 (the command parser statics)

* **Slots 675 and 773 owned** (`src/app/StaticInitParser.{h,cpp}`, merged into `OwnedInitialisers` as `OwnedParsers`;
  StaticInit_OwnedList now counts 708). 675 builds the evaluator's name table at 0x10fb30c with lane HL's
  EvalFunctionTable and registers our vector destructor (the image's 0xd97450); 773 builds cGZLogParser at 0x1107510
  with our constructor and registers our destructor (the image's 0xd98bc0 -> 0xcac4c0). Difftests
  StaticInitParser_EvalTable / _LogParser run each against the image's entry in its slot from junk (registration,
  object, thunk, frees). In effect through callers, behind `Startup.OwnStart=1,2&Startup.OwnStatics=1` as the other
  lanes' rows.
* **cBaseParser** (`src/gz/cBaseParser.{h,cpp}`, hook-shaped): the 0x1c0-byte layout (field names where other verified
  code uses them: +0x30 commands, +0x104 open blocks, +0x110 condition stack, +0xec trace, +0xf0 version, +0x5c..+0x64
  version range), the constructor 0xafd800 for the whole object and as the base of a derived parser (the derived
  vbtable at +8 decides where the virtual bases and vtordisps go), ParserNamespace::Construct 0xafdc40 (two hash
  tables, nodes 0x28 and 0x14, a NamePool), and the 8-byte element `vector::_Emplace_reallocate` 0x14a800 (8 callers;
  ours is our STL's emplace under the callers' "vector full" precondition). The constructor still installs the game's
  vtables: the class's 59 virtual functions (51 on the cIBaseParser face) are the game's. That, not the constructor,
  is what the standalone build needs next for this class.
* **cGZLogParser** (`src/gz/cGZLogParser.{h,cpp}`): constructor 0xcac2b0, destructor 0xcac4c0, RegisterCommands
  0xcaf620 (the 11 log cheat commands: cCommandT<cGZLogParserState> with the game's vtables 0xeb6fcc ... 0xeb709c).
* **0x90a915** (StaticFlag::Construct, the flag byte of slots 507 / 508): hooked, difftest StaticInitParser_FlagConstruct.
* **Left:** 0x14a3a0 (_Allocate_at_least_helper) is library code (lane HL's probe); it goes when the coordinator reruns
  stlprobe / libmatch. 0x14a800 might match too once the probe instantiates `vector<8-byte POD>::_Emplace_reallocate`
  (its private member: the probe would need an emplace-into-full-vector wrapper); until then it is ours, difftested.

### Done in lane HU: cBaseParser's virtual functions (most of them)

* **Verified** (`db/status/cBaseParser.jsonl`, 62 entries of lane HU, 11,128 bytes; with HP's three the group has 65,
  12,727 bytes): the cIGZUnknown face (3), the primary vtable's slot 0 (the deleting destructor 0xaff070) and slot 4,
  44 of the cIBaseParser face's 51 slots (1-10, 16-44, 46-50; slot 33 shares EvalInt's body), the destructor 0xafe1b0
  (for whole objects and as the base of a derived parser: the vtordisps come from the vbtable), and the helpers they
  reach: the namespace tree (ParserNamespace Clear / Destruct / FindPath, the child table's insert, rehash and erase,
  the variable table, the name pool reset), the pending calls (0xafc470, 0xafe000), MoveCountedPointers 0xafcc10 and
  the evaluator table's sorted insert 0xb09550. Code: `src/gz/cParserSlots.cpp`, `cParserNamespaces.cpp`,
  `cParserVariables.cpp`; adapters in `cBaseParserAdapters` turn the face in ecx into the object. Difftests:
  `tests/gz_base_parser_face.cpp`, `_eval.cpp`, `_namespaces.cpp` (fixture `gz_base_parser_slots.h`: the whole parser
  dumped as text, child namespaces recursive, fakes logging every call). 123 mutants, all killed but one equivalent
  (in the status notes).
* **Original bugs kept** (`// ORIGINAL-BUG:`): Clear(false) keeps an open child but resets the pool its key lives in
  (a use after free; the tests avoid it); destroying a parser with namespaces still open leaks them; UnregisterCommand
  folds into a 128-byte buffer; ResetBlocks with no namespace open runs below the vector; the pool reset frees with
  size 1.
* **Original calls left**: 0xb0a870 (the evaluator, from the Eval slots) and 0xb0bf40 (cArguments' destructor, from
  the pending calls). `src/gz/CommandParser.cpp:1367` can switch to our MoveCountedPointers (status.py says so).
* **Left for the class** (7,670 bytes): cIBaseParser slot 0 0xb02880 (the run loop, 3,347), 12 0xb04490 (464),
  13 0xb04940 (1,604), 14 0xb046b0 (497), 15 0xb03fa0 (993), and primary slot 1 0xb06450 (765). Primary slots 2 / 3
  (0x960960), face slots 11 (0x140fe0) and 45 (0x9d6630) are bodies folded with other classes (not hookable; the
  standalone vtable needs our own trivial bodies there). The vtordisp thunks of derived parsers are not done.
* **Standalone shape: not reached.** The constructor still installs the game's vtables (all slots must be ours
  first), and the class reads the game's character class table (0xe7d528) and some strings from the image.

### Done in lane IH: the run path (cBaseParser's last six slots) and cArguments

* **Verified**:
  * `db/status/cParserRun.jsonl`: slots 0 (Initialise), 12 (RunFile), 13 (RunFiles, the wildcard include), 14
    (RunText), 15 (RunLine) and primary slot 1 (SetVariable), with AppendUncommented 0xb02450,
    VariableTable::Insert 0xafd050 and cExpressionFunction::Construct 0xaf7840.
  * `db/status/cArguments.jsonl`: cArguments, 20 functions, in the standalone shape.
  * ThrowIfNull 0xafc340.
  * Code is in `src/gz/cParserRun.cpp` and `src/gz/cArguments.cpp`. Tests are in `tests/gz_parser_run.cpp`,
    `tests/gz_parser_run_text.cpp` and `tests/gz_arguments.cpp`.
* **Every cBaseParser slot is now ours** except the three folded bodies (primary 2 / 3 0x960960, face 11 0x140fe0 and
  45 0x9d6630).
* **Standalone shape: still not reached.** 10 of the constructor's 11 callers are game-owned derived parsers, whose
  vtables reach the base bodies through vtordisp thunks at the base RVAs.
* **Original calls left**:
  * RunFiles calls the directory scan (0x1c48da, 0x1c63d4, 0x1c5bbd, 0x1c5bcb, 0x1c49eb).
  * CommandParser 0xaffe10 calls 0xafd520 and 0xb06920.

### Done in lane IM: cFileSource, cFileParser, cBaseParser's vtordisp thunks

* **cFileSource** (`src/gz/cFileSource.*`, `db/status/cFileSource.jsonl`, 10 verified): standalone shape on the new real
  base `rz::cRZRefCount` (`src/rz/RZRefCountBase.*`); cBaseParser's NewFileSource is `new cFileSource`, so every source
  carries our vtables. Tests `tests/gz_file_source.cpp`; the parser suite compares the source's words as codes
  (`tests/gz_file_source.h`).
* **cFileParser** (`src/gz/cFileParser.*`, `db/status/cFileParser.jsonl`, 17 verified): standalone shape. The factory
  0xb0dc30 (registered for class 0x5cf9459a in the COM director data) is its only creation path and is hooked to
  `cFileParser::Create`. It runs a list of command files through a parser and watches their directories with
  cGZPersistDirectoryWatcher (class 0xe953da68); a watcher's 0x6ae76406 message marks the list dirty, and RunFiles
  posts 0x4b3bc675. The catch in RunFiles is `catch (const cIGZExceptionBase &)` (FuncInfo 0xfcae88). Original call
  left: 0x9c5a89 (the message server). Also cRZFileName's lazy SplitDirectory 0x1c8554 / SplitDrive 0x1c85c2 and
  cRZStopwatch::Reset 0x1c95dd. Tests `tests/gz_file_parser.cpp`.
* **cBaseParser's vtordisp thunks** (`src/gz/cParserThunks.h`, `db/status/cBaseParserThunks.jsonl`, 51 verified): the
  8-byte `sub ecx, [ecx-4]; jmp body` thunks of the +0x1b0 and +0x1b8 faces, each hooked to `VtordispThunk<adapter>`.
  Slots 11 and 45 hold thunks folded with other classes; they are not hooked. Test `tests/gz_parser_thunks.cpp`.

### Done in lane HQ: H5 (the other image-run statics and their callees)

* **Classes** (each constructor a member of its class, hooked, difftested against the image's, mutants in the status
  notes; size or speed file by `tools/flagprofile.py`):
  * `src/rz/WinApiTables`: `rz::WideFileFunctions` (InitWideFileFunctions 0x1c0d9b), `rz::WideDirectoryFunctions`
    (InitWideDirectoryFunctions 0x1c4830), `rz::TryEnterCriticalSectionLoader` (0x1af60a, the Windows 95 test) and
    `rz::ShellExecuteLoader` (0x1afb32), with `Unload` for their exit thunks. They call Windows through the image's
    import entries (`*GameAddress<Fn>(iat)`), so the tests bind those entries to logging fakes for both sides.
  * `src/rz/cRZStopwatchCalibration`: the timebases 0x1c8d8e (frequencies at realtime priority, five unit scales and
    their inverses, the least of 8 read gaps per counter). **cRZStopwatch::ReadTsc 0x1c9244 is `verified`**: a
    single-step seam (`tests/cpu_trace.h`: trap flag and a vectored handler; each cpuid / rdtsc the code executes is
    answered from a script, emms recorded) runs the original and ours on the same scripted TSC, exact; the same seam
    verifies DetectMmx.
  * `src/rz/cRZMemoryReport`: `cRZMemoryHook` (0xc696ce, destructor 0xc69881 as `Destruct`), `cRZMemoryReport`
    (0xc696d9), `CountFromOne` (0x1baa70), `LoadGlobalDbgHelp` (0x1bcc62).
  * `src/audio/MmxResampleTables` (slot 769): Construct 0xc94c78, DetectMmx 0xc94db1, BuildTables 0xc98e00.
  * `src/misc/StartupObjects` (`startupobj::`): ZeroedVector20 0xc0a664, FixedPool 0x945bd6 (the pool-size
    constructor lane HD had inlined: hooked now, HD's entries call it), ListAndLock 0x1ce3f3, NamedWordHashSet 0x97df6d,
    RecordPoolReset 0x26bbb0. HD's `StaticInitConstructed.cpp` now uses these types and `cRZFileName::ConstructEmpty`
    (0x1c7f30, hooked) instead of its local copies.
  * `src/misc/Quaternion`: `math::Quaternion::PreMultiplyNormalised` 0x1a7020 (the original's operation order; NaN
    inputs excluded from the test: payload order of commutative SSE ops, as in tests/simantics_geometry.cpp).
* **Owned XC entries** (`src/app/StaticInitH5.{h,cpp}`, `staticinit::InitH5`, 10 rows): slots 93, 94, 102, 103, 105,
  134, 193 (the rotation: two axis / half-angle quaternions multiplied and normalised, computed for real), 754, 755,
  769; thunks by table index through `BoundAtExit()`. Slot 754's thunk calls the image's ~cRZMemoryReport 0xc69888
  through `OriginalFunction` (its `original_calls`). Tests: `tests/static_init_h5.cpp` (Entries: constructors stood in
  and logged on both sides, registrations, each side's thunk with FreeLibrary and 0xc69888 logged; slot 193 bit for
  bit from the file's inputs and 399 random ones; OnImageTable: the runner, 10 ours, LIFO exit).
  `StaticInit_OwnedList`: 716 on this branch (HQ adds 10).
* **XI entry 0xd17f1a** and its callees 0x144845 / 0x242e9a: library code, byte-identical (relocations masked) to
  `initialize_legacy_wide_specifiers`, `__local_stdio_printf_options` and `__local_stdio_scanf_options` of MSVC 14.40's
  `legacy_stdio_wide_specifiers.lib`; recorded in `db/crt-equivalents.jsonl`. The standalone exe must link that lib
  (not on our link line yet), or `%s` in the wide printf / scanf functions changes meaning.
* **For the coordinator**: a mode-1/2 run should log 10 more owned initialisers than before this merge (716 on this
  branch's base: `800 XC initialisers (716 owned by us)`, `done (84 image, 716 ours, 0 mismatched, 0 unused)` if no
  other lane's entries land first); mode 0 gains 19 hooks (WinApiTables 4, cRZStopwatchCalibration 1, cRZMemoryReport
  4, MmxResampleTables 3, StartupObjects 5, Quaternion 1, cRZFileName 1; ReadTsc was hooked already).
* **Left**: 0x2051d0 (`cRZFastFixedAllocator2<0x74>`'s constructor, slot 112's entry 0x1384e0): one of the
  cRZFastFixedAllocator2 family (0xa368d0, 0xaa4930, ... the same 39 bytes) that lane HE's H3 owns; 0xc69888
  (~cRZMemoryReport: calls 0xc6981b and tidies the lock; reached only from slot 754's thunk).

### Done in lane HT: the last image-run XC entries (milestone G's remainder)

After lanes HC-HF, HP and HQ the image still ran 34 XC slots: the 7 `crt` ones (1, 5, 6, 7, 10, 13, 98) and 27 others.
Lane HT owns 25 of them (`src/app/StaticInitHT.{h,cpp}`, `staticinit::InitHT`, the table `kHT`); slots 675 and 773 were
lane HP's already (a regex over `src/app/StaticInit*.cpp` that misses `Fixed<..>(slot, ...)` rows counts them wrongly).
The "21 pooled containers lane HD left" of the brief were lane HE's allocators (done); the 18 unassigned leftovers were
taken instead. With this lane every XC slot but the `crt` ones is ours: `StaticInit_OwnedList` 793 (768 + 25).

* **Entries** (slot: what the image's entry does; ours does the same on the image's storage and registers one thunk of
  its own per row through `BoundAtExit()`, as the other lanes):
  * built objects: 3 `GeneralAllocator::Construct(no core, 0, can free)` (thunk: `Shutdown`); 8 `cRZStd::Construct(7,
    0, 0)` (no thunk); 135 401 two-string records at 0x10cca58 (`Record7c`: 0x26bc20 / 0x257860, destroyed last
    first); 218 14 `cTSMotiveCurveArray<16>` at 0x10da808 (`MotiveCurveSet`: a vector of 16 `MotiveCurve` built by
    resize; the curves destroyed through their own slot 14); 223 an `unordered_map<uint32_t, MapRecord>` (0x7c-byte
    values: two cRZStrings, a vector of 20-byte rows, an object with a byte vector; teardown order from 0x4ad0f0);
    233 SimDoNodeActionLog's hash, string vector and name (thunk: its destructor, with `ActionLog::Flush`); 527 a
    `std::list<ListRecord>` (0x38-byte elements, a cRZString at +8); 536 `cRZCallbackThread::Construct` (thunk:
    `Destruct`); 766 `comdir::InitMadCow` with `gRegisterExitHandler` bound to `BoundAtExit()` for the call.
  * atexit-only (the entry writes nothing): 11 the STL's classic `std::locale` (0x10df6cc; our STL's `~locale`, both
    calls virtual); 12 a `vector<void *, ProcessHeapAllocator>`; 99 `DbgHelpTable::Unload`; 100, 502, 525 a cached
    module freed (FreeLibrary through the image's import entry) and zeroed; 101, 564, 640 a base vtable stored (an
    inlined destructor); 136, 137 `cRZStringVector::Tidy`; 166 a vector of vectors of 8-byte elements; 220, 224 vectors
    of 40- and 0x828-byte rows with a cRZString at +0; 523 each 16-byte entry's reference (+0xc) released and cleared,
    the span emptied (nothing freed); 529 the shutdown flag set and `WorkerThreads::Shutdown(-1)`.
  * A container the image's entry builds keeps the file's size word (it writes the rest): `BuildWords` builds ours in a
    local and copies only the words the image's entry writes.
* **Members** (hooked, `db/status/StaticInitHT.jsonl`): `cRZCallbackThread::Destruct` 0x9cfac2 and `DeletingDestruct`
  0x9cfb3e (slot 8), `appbase::DbgHelpTable::Unload` 0x1bd611, `staticinit::ActionLog::Flush` 0x526c30,
  `staticinit::WorkerThreads::Shutdown` 0x99936e (calls 0x1b9f95, the quarter-second sleep, through `OriginalFunction`;
  the clock is `nGZLog::Time64`). Windows calls go through the image's import entries, so the tests bind them to fakes.
* **Tests** `tests/static_init_ht.cpp`: `StaticInitHT_Entries` (each entry against the image's from junk, six rounds of
  live objects: windows with heap blocks described, registrations, frees in order, calls into fakes; the verified
  constructors stood in), `StaticInitHT_MadCow`, `StaticInitHT_OnImageTable` (the runner over the 25 slots, LIFO exit),
  and one per member. Slot 766's entry is 0x1408d0 itself, already `verified` and hooked in
  db/status/COMDirectors.jsonl: it has no second row in StaticInit.jsonl (24 rows there).
* **For the coordinator**: a mode-1/2 run should log `800 XC initialisers (793 owned by us)` and `done (7 image, 793
  ours, 0 mismatched, 0 unused)`, which is milestone G's acceptance line (with the trap batch and own-start-menu exit 0).
  Mode 0 gains 5 hooks (the members above). Slot 112's entry (0x1384e0, constructor 0x2051d0) was already lane HE's.

### Done in lane II: the last 25 reachable functions (milestone H's remainder)

`status.py --reachable-todo` listed 25 functions (2,137 bytes). What each turned out to be:

* **Library code libmatch missed** (rows in `db/crt-equivalents.jsonl`, lane II): `std::_String_val::_Xran` 0x1443bb
  (bytes: libcpmt's 10-byte body, dropped by libmatch's uniqueness rule, 41 identical bodies); the CRT's own XC entries
  slot 7 (0x13e074, `init_atexit`, iosptrs.obj) and slot 10 (0x13e052, `_Fac_tidy_reg`, locale0.obj) with their
  `??__F` thunks 0xd950ae / 0xd9509a (relocation); ATL's `_AtlBaseModule` slot 13 (0x140f1b) with its thunk 0xd99774
  and `ATL::_AtlInitializeCriticalSectionEx` 0xc603a0 (structure: atlmfc is not installed). Two STL instantiations
  that match only in a small translation unit: `vector<char>::_Resize_reallocate<_Value_init_tag>` 0x152c50 (`/O2`)
  and `_Hash::_Min_load_factor_buckets` for 4-byte keys 0x1440b8 (`/O1 /Oi`): `tools/stlprobe/probe_tu.cpp`, built by
  `stlprobe.ps1` (`$TuFlagSets` adds `/O1 /Oi`); `db/library-matches.jsonl` regenerated with them: 6 rows added
  (these two and four `unordered_map<int, int8_t>::clear` copies), 3 family labels resolved, no row removed.
* **Slot 98** 0x138306 (`jmp _tzset`, in cRZDate's translation unit, so game code although its kind is `crt`):
  `H5Kind::TimeZone` in `src/app/StaticInitH5.cpp` calls our CRT's `_tzset()`, which our cRZTime reads. The image's
  CRT sets its own state lazily (`__tzset` in `_localtime64_s` / `_mktime64`), and every image-code read of its
  `_timezone` (0x267ee0, 0x3a6bf0, 0x9914ee, 0xcd2190) comes after one of those calls. With it the owned list is 794;
  a mode-1/2 run should log `done (6 image, 794 ours, 0 mismatched, 0 unused)`.
* **The CPU feature detection** slot 765's entry calls (hooked, `src/app/StaticInitCpu.size.cpp`,
  `db/status/StaticInitCpu.jsonl`):
  `staticinit::DetectCpuFeatures` 0xee0d1, `CpuidAvailable` 0xee07f, `CpuidQuery` 0xee0a9.
* **Game code in StdGameTypes** (hooked): RZRegionMap's insert 0x1bdc30 (not `_Mutex_count_pair`: a coincidence of a
  35-byte forwarder), its temporary node 0x1bdbc9, `ProcessHeapAllocate` 0x1be421; cGZFramework's
  `unordered_map<uint32_t, FwRef>` `_Forced_rehash` 0x143dc2 and range erase 0x14424b (one instruction scheduled
  differently from every 14.40 build tried), the node frees 0x1419c3 / 0x1419de / 0x1419f9 and the element deleting
  destructors 0x1421bb / 0x1421e1; `vector<char>::_Resize_reallocate` 0x1af86f (an older EH-framed copy).
* **cRZStringVector** (hooked): `CopyN` 0x141633, `UninitializedCopyN` 0x141a91, `AssignCounted` 0x14155c.

### Keeping this current

Rerun `--reachable-todo` (and `--live`) after merges; a package is done when none of its RVAs is listed. New RVAs
appear when Ghidra or tools/missedfuncs.py adds functions or call edges; give them a package here. The grouping was
made with scratch scripts over the `--json` output (static-init roots by `db/initializers.jsonl` kind, subtrees
claimed in table order, the CRT by address); `kind` in the tool is the reproducible first cut of it.

## Open questions

1. The meaning of the framework's +0x18c id set (services whose running flag the 0xcd/0xce messages
   drive; set in the constructor 0xc2ccdb with ids such as the CAS body-part system and the sim).
2. cITSAppFW slot 4 (`GetWord70`, folded 0x1457d0 = +0x70 of the app) and slot 3's message
   semantics beyond "true = consumed" are tentative.
3. cGZCOMDirector slot 17 (0x1448f7): who calls it; it stores its argument at +0x34 (the
   cIGZFrameWork field InitializeCOM also fills).
4. The cITSAppCommon slot arguments (a, b, c of GetOptionText / GetOptionList) and the two option
   maps' std type (keys of 8 bytes, values lists of cRZString-sized nodes).
5. The stream classes behind StdOut / StdErr / StdIn (0x1b4dbe, 0x1b4da6) and the debug-stream
   interface (`void *` in the headers).
6. Whether the base cGZFramework band loops (0x14307f..0x142ffb) and MainLoop 0x1435c2 need a
   difftest at all: they never run in the game (the live object is a cCTGFramework). The handoff
   gives them one, since a C++ base class has to provide them.
7. Alias merges proposed to the coordinator (shared facts, not done here): `ComFramework` and
   `NbrFramework` in `db/methods` are cIGZCOM; `SimServiceHub` slot 5 is cIGZFrameWork slot 5.

## Left (from the backlog, 2026-11-03)

- cRZExceptionNotification (0x1c8c84 ctor + 5 slots) in the standalone shape; then GZFramework.size.cpp creates it (HK).
- status.py problem: COMDirectors.jsonl 0x9b33b0 cGZResManCOMDirector::OnStart has an open reference: StaticInitContainers.obj (kOwned array) holds the original vtable 0xe60044 (RVA 0xe60060 in data); find who added it and give the object our vtable or lower the entry.
- src/rz/COMDirectorData.cpp: rows for 0x229c42 / 0x92f5ed could point at our Create functions (cosmetic); 0x3989c0 (cTSCustomContentSupport factory, :165) to ours when that class is ported.
