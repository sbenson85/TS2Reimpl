# Foundations: the GZ/RZ framework's leftovers and the shared utilities (campaign plan)

The first campaign of docs/roadmap.md ("Campaign order"): framework-gz plus shared-utility. Working notes for lanes in
it: scope, the plan, conventions and test patterns, what's done and left. Keep it short and current; the lane that
changes the subsystem updates this file on its branch (facts, not decompiler output). The class view of the startup
path that is already ours (framework, COM registry, directors, application, static initialisers) is
docs/subsystems/startup.md; this plan covers what of the framework still runs from the image. Verification: `verified`
by difftest or byte match (this is infrastructure, not simulation code). The triage of the "unassigned" and "other-ts"
groups into campaigns, and of the groups the coverage tool files here but other campaigns own, is
docs/subsystems.md, "Triage".

## Why this campaign

It sits at the bottom of the call graph: by `coverage.py deps run-1023 --by group`, framework-gz calls 26 KB of
not-done code in later campaigns (all of it callbacks and notifications: event listeners, the canvas, window procs) and
shared-utility 12 KB, while everything else calls into them. Doing it first means the later campaigns' tests call our
strings, buffers, events, threads, streams and logs instead of original-calls or stand-ins, and the shared test
boundary it owns (threads and timers, plus the simple Win32 calls) is built once.

## Scope

**What `queue --whole` says and what is really here.** `tools/py -I tools/coverage.py queue
local/coverage/run-1023.txt --subsystem shared-utility,framework-gz --whole --top 0` gives 184,019 B in 985 functions
(199 groups). About 70 KB of that is other campaigns' code the tools file here:

- *Owner inference*: a group named after the nearest caller's class. ~cTSObjectFolder (11.4 KB, objects), cOverlayCheat
  (9.5 KB, cheats: command-parser; its 4.4 KB body 0xaf90e0 is a variadic option-spec parser, "-flag", "%d/%f/%s",
  "[...]", "<name>", status text "no error"), ~cTSWinProcClothingDialog (4.8 KB: really LoadUIScript 0xb41b50 and its
  reader 0xb40d20, kept here, F5), cTSUserTool* (7.3 KB, ui-game), cTSAudioResourceManager (4.7 KB, audio-video), and
  the rest of the triage in docs/subsystems.md's "filed here, owned elsewhere" table.
- *The class rule*: tools/status.py's best_names gives a function the class whose vftable it stores at `[this]`. A
  constructor of a struct whose first member is a cRZString stores cRZString's vftable there (0x4ec600 builds two
  cRZStrings at +0 and +0x20, then +0x40/+0x44), so the cRZString group (14.3 KB, 51 functions) is the code of ~40
  other classes (ParseFontStyleDefinition 0xb824b0 5.5 KB, the camera and lighting parser states, CAS, persistence);
  only cRZString::TrimBoth (16 B) is a cRZString member. The same is true of rendering.md's P0 item "cRZString members
  (3.6 KB)". Those functions go to their callers' campaigns (the triage in docs/subsystems.md); feedback filed (notes/feedback/LL.md).
- *Library*: ~1.4 KB of unmatched STL instances (and 12 KB more in "unassigned").

Function-level scope, by class (framework classes' own functions, their helpers, and ownerless framework code next to
them; method in the triage in docs/subsystems.md): **about 115 KB in ~670 functions**, plus ~9 KB of ownerless framework code
that `queue` files under "unassigned" (the triage in docs/subsystems.md), so **~125 KB**:

| family | KB | groups (executed not-done bytes; class done %) |
|---|---:|---|
| UI script runtime and legacy controls | 36 | cGZLegacyControl 31.6 (0%: 13 Register*Properties bodies 23 KB, FormatPropertyValue 4 KB), LoadUIScript + reader 4.2, cGZScriptTerp 3.0, cGZScriptableControlSet 1.0, cGZUIScriptService 0.8 (72%), cGZScriptVocab 0.5, nRZSTD::cNotificationHelp1 0.7 |
| app shell, logging, ini | 25 | cTSAppFW 8.0 (86%), ts::cTSAppCommonImpl 3.9 (83%), ObjectSaveTypeTable 1.7, nGZLog::cService 3.0 (48%), cGZLogFormatterText 1.6, cGZLogFilterGroupLevels 1.0, log back ends 0.4, cRZIniFile 3.6 (19%), cRZStd 0.4 |
| values: variants, strings, buffers, language | 19 | cRZVariant 5.2 (7%), cGZBuffer 3.7 (14%), cGZTransientBuffer 0.9, cGZBufferFontRenderer 1.4, cRZLanguageUtility 1.6, cRZLanguageManager 1.5, ownerless near AnyStringSet / the property-set serializers / std::list 3.0, the iterator templates 0.8, cRZStringResource, GZServiceString |
| images, files, xml | 16 | DynamicLibraryList 3.1, cGZGimexFactory 3.1, cGZGimexReader 2.3, cGZGimexWriter 0.7, cGZRESIDtoExtensionMap 0.8, cRZIFStream 1.6, cRZOFStream 1.5, DirectoryScan 1.4, cGZXmlWriter 1.1, text streams 0.3 |
| threads, timers, messages, events, cursor, clipboard | 14 | cRZCallbackThread 1.1, cRZCallbackTimer 0.9, cRZThread 0.5, cRZThreadSignal 0.1, message queues 0.9, nGZEvent::cConnectionManager 2.0, cInputDeviceMap 0.9, cGZEventReflector 0.6, the cAbstractBehaviorTypeT instances 2.8, cRZCursor 1.5, cRZCursorManager 0.8, cGZClipboardManagerClipboard 1.3 |

Rounds (run-1023): most of it runs at start (round 0); UI scripts and legacy controls again at neighbourhood load
(round 2) and lot load (4); threads, timers, messages and events every round.

**What other campaigns' plans assume from it.** rendering.md "Before this campaign" and lane P0: cRZString members
(really the callers' own code, above: they stay in rendering's lanes), cGZBuffer and cGZTransientBuffer (F2),
nGZEvent::cConnectionManager and the behaviour-type templates (F1), cRZIniFile (F4), cGZRESIDtoExtensionMap (F3). Once
F1-F4 are merged, P0 shrinks to the persist-dbpf reader and writer leftovers. Every later campaign uses F1's import
fakes for its own thread, timer and simple Win32 calls (list below).

## Subsystem map (lane LL, 2026-10-23)

Layers, callers above callees (call edges of the code that ran; "ours" = verified or replaced classes in src/gz, src/rz,
src/app):

- **Application shell**: ts::cTSAppFW (ours but forwards and helpers: 8 KB) and ts::cTSAppCommonImpl (the option
  store) on gz::cCTGFramework / cGZFramework, cGZCOM, the directors (ours, startup.md). They start the services
  (milestone I: the service objects are ours, the classes they manage are not all), read the registry (one function,
  0xc27a16, RegQueryValueExA), set up logging (nGZLog::cService -> formatter, filters, back ends: file, debugger, the
  window caption) and load ini files (cRZIniFile, cTSIniFile ours).
- **UI scripts**: cGZUIScriptService (ours, KS) -> LoadUIScript 0xb41b50 (~140 call sites in window procs) -> the reader
  0xb40d20 -> cGZLegacyControl (a property registry per cGZWin* kind: Register*Properties fill name -> id/type maps,
  FormatPropertyValue writes a value back as text) -> cGZScriptTerp / cGZScriptVocab / cGZScriptableControlSet. It
  calls little (0.6 KB not done in ui-windowing and ui-game), so it is a leaf despite feeding the UI.
- **Values**: cRZVariant (typed value with conversions, 0x9c7a04-0x9cbaf0), cIGZAny (ours), cRZString (52% done; of
  what ran, only TrimBoth is left), cGZBuffer / cGZTransientBuffer (pixel buffers; cGZBufferFontRenderer draws text into them), the language
  utilities (code pages, language ids), the indexed and pointer iterator templates.
- **Images and files**: cGZGimexFactory -> cGZGimexReader / Writer (image codecs; the reader loads a library at run
  time: LoadLibraryA, GetProcAddress, FreeLibrary), cGZRESIDtoExtensionMap (resource type -> file extension),
  cRZIFStream / cRZOFStream (on a heap std::ifstream / std::ofstream, not cRZFile: lane LV), DirectoryScan (FindFirstFile/FindNextFile), DynamicLibraryList (the
  plug-in DLL list), cGZXmlWriter / cGZXmlBufferWriter.
- **Threads, time, messages, events**: cRZThread (partly ours; left: vf5 ResumeThread, vf7 QueueUserAPC, vf10 SetThreadPriority,
  vf11 GetExitCodeThread), cRZThreadSignal (events), cRZCallbackThread and cRZCallbackTimer (driven by
  cRZCallbackSystemService and cGZTimerService, both ours), cGZMessageQueue / 2 with cRZMessage2Standard under the
  message servers (ours), nGZEvent::cConnectionManager (event connections: dispatches to listeners in rendering, the
  scenegraph and the UI), cGZEventReflector, cInputDeviceMap, cAbstractBehaviorTypeT<I, cRefSingle/MultiThreaded>
  (the refcounted behaviour base most listeners derive from; one instance per interface).
- **OS objects**: cRZCursor / cRZCursorManager / cRZSystemCursorW95 (LoadImageA, SetCursor, ShowCursor),
  cGZClipboardManagerClipboard (a hidden clipboard-viewer window: CreateViewerWindow 0xc6d2cf, ViewerWindowProc 0xc6e73f).

Calls out of the campaign (into later ones, cut by recording fakes in tests): cConnectionManager's notifications to the
canvas, cGraphicSystemDX, cDeviceDX, cDraw2D listeners (rendering, 2 KB); cGZBuffer -> cGZGraphicSystem; cGZBufferFontRenderer
-> cGZDrawContextAsBuffer; LoadUIScript -> cGZWin* (ui-windowing); log back ends -> the caption window.

## Hard test boundaries this campaign owns

**Threads, timers and the simple Win32 calls: decided here.** Not-done executed functions calling these imports
directly (run-1023, all campaigns; `coverage.py deps` "hard test boundaries" plus QueueUserAPC/GetExitCodeThread):

| campaign | functions (import) |
|---|---|
| foundations (8) | cRZThread vf5 0x1b9ec9 (ResumeThread), vf7 0x1b9ff1 (QueueUserAPC), vf10 0x1b9edc (SetThreadPriority), vf11 0x1b9db2 (GetExitCodeThread); cRZThreadSignal vf1 0x9ccb93 (SetEvent/PulseEvent), vf3 0x9ccbb9 (WaitForSingleObject), vf6 0x9ccbfb (ResetEvent); 0x1b9f95 (SleepEx, under cGZCOM) |
| audio-video (6) | cGZSndEaRenderer 0xea16a (CreateThread), 0xe8f31 (CreateMutexA); 0xe8f4f (WaitForSingleObject); 0xe9e0d, 0xea0dd, 0xe9780 (timeGetTime, timeBegin/EndPeriod, Sleep: the sound thread) |
| rendering (3) | cCanvasW32::Init 0x155e90 (SetTimer, 2 s), cDeviceDX 0x1699b0 (Sleep), cCanvasW32::WindowProc 0x157800 |
| persist-dbpf (1) | cGZPersistDirectoryWatcher::WatchThreadProc 0x9c1a90 (FindFirstChangeNotification, WaitForMultipleObjects) |
| sim-core (1) | cTSConfigurationManager 0x1cb3b0 (QueryPerformanceCounter, thread priority, Sleep: CPU timing) |

Foundations' simple Win32 calls: cursor (6 functions: LoadCursorA, LoadImageA, GetIconInfo/GetDIBits, SetCursor,
ShowCursor, GetCursorInfo, DestroyCursor), the clipboard viewer window (RegisterClassA, CreateWindowExA,
SetClipboardViewer, DefWindowProcA), the caption back end (IsWindow, SetWindowTextA), GetSpecialFolderPath 0x1c50d6
(SHGetMalloc, SHGetPathFromIDListA), the registry read 0xc27a16, DirectoryScan (FindNextFileA), gimex
(LoadLibraryA, GetProcAddress, FreeLibrary), nGZLog (GetProcessTimes).

**The fake: `impfx`, an import-fake table (lane F1 builds it).** The seam exists: our code calls imports through
`ImportFunction<Fn, IatRva>()` (src/game/import.h, decision 2026-10-22) and tests point the slot at a fake with
`IatPatch` (tests/app_fixture.h); today each test hand-writes its fakes (tests/rz_mutex.cpp, services_b_thread.cpp's
scripted clock, gz_win_mgr_w32.cpp). impfx makes that one header: per import (by IAT RVA) a `__stdcall` recording
fake of the right argument count that logs {import, stack words} and returns a programmed value or runs a handler (for
out arguments: write every byte the real API writes); kinds for:
- *threads*: CreateThread records the start routine and parameter and returns a fake handle; the test chooses to run
  the routine inline (same thread, deterministic), never or later; ResumeThread / SetThreadPriority / QueueUserAPC /
  GetExitCodeThread answer from the fake thread's state; Sleep / SleepEx log their argument and return at once.
- *sync*: CreateEvent / CreateMutex / CreateSemaphore hand out fake handles with state; Set / Reset / PulseEvent change
  it; WaitForSingleObject / WaitForMultipleObjects answer from it or from a script (WAIT_OBJECT_0 + n, WAIT_TIMEOUT).
- *clock*: timeGetTime, GetTickCount, QueryPerformanceCounter / Frequency, GetSystemTime / GetLocalTime from one
  scripted clock (services_b_thread.cpp's pattern), advanced by the test or by each read.
- *user32 / shell32 / advapi32 simple calls*: log and return; handle-producing calls (LoadCursorA, CreateWindowExA,
  RegOpenKeyExA) return fake handles that later calls are checked against; RegQueryValueExA and
  SHGetPathFromIDListA fill their out buffers from the test's table.
- *dynamic libraries*: LoadLibraryA / GetProcAddress hand out a fake module whose exports are recording fakes (gimex).

It follows drawfx's conventions (normalised pointers, readable diffs). Lane LM (rendering R0) is adding a stdcall table
kind to drawfx/fx_slots for the Direct3D device: impfx should be built on that kind rather than beside it (open
question 1). **Win32 window and message fakes** (stateful windows, a message queue, dispatch to a WindowProc): not
here. Foundations needs only the call log above (one hidden window, the cursor); the canvas (cCanvasW32's 16 functions
plus its 4.4 KB WindowProc) is rendering R2's, so R2 builds the window and message fake on impfx. **Service fakes**:
tests/ts_services_fixture.h (svcfx) for the listeners and services the framework calls back (connection manager
listeners, the log's output targets, the canvas notified by the event reflector). **File I/O** stays with the cRZFile
level fakes (rz_file.cpp) plus impfx's FindFirstFile/FindNextFile.

## Test strategy per family

- *Threads, timers, messages, events*: impfx; inline thread routines; a real second thread only where the original's
  behaviour depends on a race it guards (cRZMutex's test shows the pattern). Events: build a connection manager with
  svcfx listeners on both sides, compare dispatch order, refcounts and the listener logs.
- *Values*: plain difftests on seeded random inputs (variants: every type pair of the conversion matrix, strings at
  the edges of the 0xf small-string boundary); the existing rz_variant.cpp / gz_buffer.cpp tests extend.
- *Images and files*: synthetic image bytes through a fake codec library (impfx dynamic-library kind) and the cRZFile
  fakes; xml writers compared as output text.
- *App shell, logging, ini*: app_fixture.h and svcfx (the shell calls services through the framework); log output
  compared as text through a recording back end; ini on in-memory files (tests/ini_file_*.cpp's pattern).
- *UI scripts and legacy controls*: the Register*Properties bodies fill maps: compare the maps' contents (names, ids,
  types, order) after each call; FormatPropertyValue on every property type; script reading on synthetic script text
  (no package content), cGZWin* creation through winfx (tests/winfx.h).

## Next lanes (lane VY's closure audit, 2026-11-03)

**Closed 2026-11-04** (coordinator, after lane WG's validation): every row below is done except FM (deferred: it waits
for persist-dbpf's directory files and ui-windowing's file browser), and the F4 (save part, qualified) and F5 exit checks
are done (notes/log.md). What stays filed here but owned elsewhere is listed under "Filed here, owned elsewhere".

Lane LL's F1-F7 plan (2026-10-23) is done apart from what is listed here; its table is in
`docs/archive/foundations-lane-plan-LL.md`. Method: every function filed under framework-gz or shared-utility
(`db/subsystems.jsonl`) that is not `verified` or `replaced`, grouped by class (else `~owner`), each group placed in its
majority subsystem; bytes from `db/functions`, executed bytes from run-1023. 181 KB in 350 groups is filed here;
about 98 KB of it is this campaign's, the rest is handed on (below). Lanes are about 2.5 h each, leaf-first; each takes
its classes whole (all slots, constructors, destructors, helpers, the vtable and RTTI data where a gate needs them).

### What is left, by family (todo bytes / executed todo bytes)

| family | groups (todo / exec) | KB |
|---|---|---:|
| timebase (done, FA) | cRZStopwatch 604 / 604 (0x1c9368, 0x1c948d verified by FA); the calibration object 0x10cb640 and its constructor 0x1c8d8e; MeasureFrequency's cells 0xff2480 / 0xff2488; the stopwatch cells 0x10cb648 / 0x10cb64c | 0.6 |
| streams | cRZIFStream 2,864 / 818; cDebugStream (anonymous namespace) 1,974 / 0; cRZOFStream 360 / 360 and its vtable 0xda2c14 (24 slots; slot 18 GetError 0x1b5941 is `shared_vf_` with cDebugStream 0xdcf704); cRZTextFile 332 / 122 | 5.5 |
| values and containers | cRZVariant 1,060 / 55 (the 0x9c7659..0x9c796e conversions and the 5-byte setter stubs); cRZUnknownList 1,041 / 211; cGZCommandParameterSet 866 / 0; ServiceRefVector 488 / 488; cRZStringVector 403 / 272; cRZNameLookup 388 / 68; unordered_map<cRZString,cRZString> 356 / 356 (library: stlprobe first); cSLTokenField<cRZString> 309 / 0; GZStringSetOps 248 / 248; cRZUnknownAutoPtr<cRZSparseMatrix> 229 / 0 (the legacy grid holders, Left); AnyStringSet 224 / 224 (53 B `reimplemented`); cGZTransientBuffer 220 / 4; RefPtrVector 109; cRZLanguageUtility 104 | 6.1 |
| events and behaviour bases | nGZEvent::cAbstractBehaviorType 430 / 430; cConnectionManager 513 / 513; cInputDeviceMap 243 / 243; cAbstractBehaviorTypeT<cIAbstractBehaviorType, cRefMultiThreaded> 990 / 59; the single-threaded instances (camera controller manager 2,291, configuration change manager 2,225, main toolbar sink 1,521, camera effects support 1,309, screen grabber 1,185, win scenegraph 964, cGenericControl 203); the cVectorEvent<cVector4f> vtable 0xdb07d4 and the other cVectorEvent instantiations (slots 0x154eb0, 0x258ca0 are shared bodies) | 13.4 |
| messages, framework, COM, registry | cGZMessageQueueManager2 833 / 0; cGZMessageServer2 712 / 0; cGZMessageServer 498 / 186; cGZFramework 471 / 410; cGZRegistry 241 / 241; cGZCOMLibrary 210 / 0; cGZCOM 175 / 0; cRZLock 57 / 27 | 3.2 |
| app shell, logging, ini | ts::cTSAppCommonImpl 1,472 / 1,472; DynamicLibraryList 1,479 / 1,282; cRZIniFile 594 / 504; nGZLog::cService 594 / 594; ExceptionReporterObject 501 / 187 (0x1bd39f `reimplemented`, 0x1bd687 `mapped`); nGZLog::cGZLogFilterGroupLevels 412 / 412; cTSAppFW 395 / 37; ts::cTSAppPropertyFiles 221 / 221; the cRZStd vtable 0xda3e6c (its Assert / Report paths); nGZLog's standalone shape (Left) | 4.7 |
| cursor, resources, clipboard | cRZCursorResource 1,404 / 0 (LoadFromSerialRecord 0x9c4af0 1,205 B); cRZCursorResourceFactory 543 / 0; cRZStringResourceFactory 305 / 0; cGZClipboardManager 150 / 150; cGZClipboardViewer 70 / 0 | 2.5 |
| legacy control's script writer | the writer 0x87a9a9 (1,179 B, one caller: GetScript, slot 7 0x86e758) and its tree: 21 per-class property writers (0x873913 7,178 B, 0x87117a 6,585, 0x8792f2 5,815, 0x86ff03 3,156, 0x878762 2,705, 0x8764bf 2,533, 0x86f2b8 2,501, 0x877979 2,229, 0x8730b3 2,144, 0x875ccb 2,036, 0x87551d 1,966, 0x870b57 1,571, 0x87822e 1,296, 0x872b33 1,042, 0x877189, 0x876ea4, 0x87751e, 0x86fc7d, 0x87ae44, 0x8777a6, 0x872f45, 0x8791f3) over about 20 shared formatting helpers 0x86e546..0x86f25b and 0x87b080 / 0x87b0d4 (each under 210 B). Filed as ~cGZLegacyControl (44 functions, 0 executed: the UI editor's save path, reachable through slot 7, so not dead) | 50.8 |
| memory report | cRZMemoryReport 12,626 / 0 (vtable 0xe9e780, 10 slots: vf1 0xc6aca2, vf4 0xc6a573, vf5 0xc69efd 722 B, vf9 0xc6a768; helpers 0xc6ae7d 965 B, 0xc6b4a7 859, 0x1bacbd 827, 0xc6ba27 690, 0xc6b242 613): a debug report whose callees are cGZDBSegmentDirectoryFiles, cTSPersistSystem and cGZWinFileBrowser | 12.6 |
| F6's leftovers (Left) | lane FI: matrix destructors 0x85d901 / 0x85d928, cell constructor 0x85d5f0, copy 0x85d59d and assignment 0x85d8b2 verified (the holders were lane FC's). Left: the font constructor 0x940a6e (ui-windowing) and the key buffer 0x87b2ac 176 B (unreached since FileBrowser's 0x87b143 hook; no replacement of its own) | 0.2 |

Owned or hooked but not verified (`reimplemented` or `mapped` in this campaign's status files; list them with
`status.py`): cRZStopwatch 0x1c9368 / 0x1c948d (FrameworkStartup), AnyStringSet::Assign 0x237679 (cGZAny), ExceptionReporterObject
0x1bd39f (and the filter stub 0x1bd687 `mapped`), cRZOFStream::GetError 0x1b5941 (`mapped`, shared body), GeneralAllocator::DebugString
0x1c7cac, cGZFramework::AbortiveQuit 0x142270 (`mapped`), cGZMessageQueue::ContainsId 0xc756a8 (`mapped`, 1 B: never hooked), the
behaviour base cAbstractBehaviorTypeT<cITSAudioSoundManager> 0x218ca2 / 0x218ee7 (FD), cPatternParsingFilter::QueryInterface
0x72baec (folded: stays as is); Gimex's 0x888e00 (`reimplemented`, lane MV). Library instances `mapped` only (nGZLog's hash
helpers 0x97ca4a / 0x1ac03b / 0x97b19c, UtilitiesB 0x18b860): stlprobe, never reimplemented. Unanalysed `mapped`
entries (UtilitiesB 0x18c0c0, UtilitiesC 0x257860, UtilitiesD 0x85ca6e, MiscHelpersA 0x56c1d0) go with their callers'
classes. Each other item is in its family's lane below. The F5 exit check (Left) is the coordinator's.

### Filed here, owned elsewhere (handed on; re-check of the filing, 2026-11-03)

Each group's owner is the subsystem of its callers (lane VY's scratch scripts over the call edges in db/functions; majority by
bytes) or of its class's vtable. Recorded so the closure lists them as "handed to a named campaign":

| to | groups (todo KB) |
|---|---|
| command-parser | cOverlayCheat (two RTTI faces, 8.4 + 5.3; 0xaf90e0 the option-spec parser), cArgSpecTestCheat 5.3, cDisplayAnimBillboardCheat 1.2, nGZCommandParser::cBaseParser 0.4 |
| ui-game | cTSWinProcCustomObjectDialog 6.3, cTSWinProcRegisterPetDialog 3.9, cTSWinProcEmployeePanel 2.0 |
| network-web | cRZCookieManager 8.9, cRZFTPClientLowLevel 2.1, cRZFTPClient 0.5, cRZHTTPClient 1.2, cRZSocketError 0.8, cRZMessage2NetStandard 0.8, HTTPPing |
| audio-video | cWaveXA 2.3, cGZWave 1.0 |
| persist-dbpf | cGZPersistDirectoryWatcher 1.7 |
| scenegraph | cReflectionViewerVisibilityQueryFilter 0.7, cRenderGroupsVisibilityQueryFilter 0.3, cNodeCloneContext 0.4, cTransformNodeT<cIObjectV> 1.1 |
| rendering | nGZGraphic4::cTextureAtlas 0.4 (lane RB) |
| sim-person / cas / world-lot | WantMapI16 0.8; std::set<SkinKey> 0.2, DNAVector<DNAWeightedGene> 0.1; cHeightField |
| each class's own campaign | the cRZString group (25.1 KB, 15.2 executed: ~40 classes' constructors that build a cRZString first; method in Scope) |
| none (a filing artefact) | about 107 KB of ownerless code with basis `calls-into` framework-gz and 35 KB `called-only-from`: callers' code in the UI and start-up ranges (0x6xx..0x7xx, 0x99x, 0xc4x) that calls the framework. It belongs to its callers' campaigns; not foundations work |

The RZ network classes are RZ-layer code, but nothing below the network layer calls them; network-web is a leaf
campaign, so they go there whole.

### Lane order

Callees first: the timebase and streams have no not-done callees; values and containers call only cRZString (whose
members are done); events, messages and the shell call values; the cursor and resources call the stream and the
resource manager; the writer calls the legacy control (done), the window faces and cRZString::Sprintf; the memory report
calls higher layers and goes last. Not-done callees outside the lane, by lane (call edges weighted by callee bytes, run-1023): FD calls
cDraw2D's behaviour base (4.1 KB, rendering RA/RL), FE cRZString-built helpers only, FF unordered_map<cRZString,
cRZString> (library) and cTSCustomContentSupport (objects, 90 B: original call), FI content_registry (objects, 348 B),
cGZWinGrid (ui-windowing, 141 B), cMaterialParser (scenegraph, 98 B): original calls.

| lane | classes (RVAs: the family table above) | KB | mark |
|---|---|---:|---|
| FA | timebase: cRZStopwatch verified; the calibration object 0x10cb640 owned (data and constructor 0x1c8d8e), MeasureFrequency's cells 0xff2480 / 0xff2488 owned, the stopwatch cells 0x10cb648 / 0x10cb64c; the constructor run in the image-free start-up (XC slot 105); plus the cRZStd vtable 0xda3e6c | 1.5 | Opus: a static initialiser in the image-free start-up and gate references, not a class body |
| FB | streams: cRZIFStream, cRZOFStream (vtable 0xda2c14 whole), cDebugStream, cRZTextFile; GetError 0x1b5941 | 5.5 | Sonnet: typed get/put bodies over a buffer, gz_file_source.h covers the file side |
| FC | values and containers: the values row above | 6.1 | Sonnet: small value classes, conventions known (cRZVariant layout, the 5-byte stubs) |
| FD | events and behaviour bases: the events row, every cAbstractBehaviorTypeT instance once (one template), the cVectorEvent gate | 13.4 | Opus: template instances possibly folded (`shared_vf_`), multi-threaded reference counts, the vtable gate |
| FE | messages, framework, COM, registry, cRZLock | 3.2 | Sonnet: classes known (svcfx fakes exist); join with FG if both are short |
| FF | app shell, logging, ini, ExceptionReporterObject, nGZLog's standalone shape | 4.7 | Opus: 70% of the shell's helpers are owner-inferred (a structure pass decides whose they are), the standalone shape |
| FG | cursor, resources, clipboard | 2.5 | Sonnet: resource factories over a serial record, like cRZStringResource (done) |
| FI | writer part 1: the dispatcher 0x87a9a9, the ~20 formatting helpers, the smallest writers (0x872f45, 0x8791f3, 0x87ae44, 0x86fc7d, 0x87751e, 0x876ea4, 0x877189, 0x872b33), F6's leftovers; adds the writers' getters to `tests/legacy_win_faces.h` (Shared fakes, below) | 8.5 | Opus: the structure pass for the four writer lanes (which writer is which window class) and the fixture |
| FJ | writer part 2: 0x87822e, 0x870b57, 0x87551d, 0x875ccb, 0x8730b3, 0x877979, 0x86f2b8, 0x8777a6 | 14.2 | Sonnet: the same pattern per class, after FI |
| FK | writer part 3: 0x8764bf, 0x878762, 0x86ff03, 0x8792f2 | 14.2 | Sonnet: as FJ |
| FL | writer part 4: 0x87117a, 0x873913 | 13.8 | Sonnet: as FJ; the two biggest bodies (likely the file browser and the text edit) |
| FM | cRZMemoryReport | 12.6 | deferred: its callees are persist-dbpf's directory files, the persist system and ui-windowing's file browser; run it after those (or with their fakes if the campaign must close first). Opus |

FA..FG are independent of each other except FD after FC (behaviour bases use the containers) and FF after FE; FI..FL
are a chain. Two tracks fit: FA, FB, FC, FD and FE, FF, FG, then FI..FL.

### Per-lane recipes

Each lane: `tools/py -I tools/coverage.py queue --group <class> --functions` for its list (no runs: the reference set,
docs/coverage.md),
the structure first (docs/lane/brief.md), then the conventions sections below this one; db/status/<group>.jsonl per
class.
Lane FB found the family table over-counts (2026-11-03): its todo bytes include old-MSVC STL instances (library, never
ours: check with stlprobe) and functions already verified. Each lane first checks its list against db/status and stlprobe and
reports early if little is left (coverage.py finds the runs in the main checkout from a worktree).

- **FA, timebase (Opus).** Calls: QueryPerformanceFrequency / QueryPerformanceCounter and timeGetTime (impfx Clock kind),
  nothing of the image. Ours already: cRZStopwatch's two bodies. Fixture: impfx (tests/impfx.h, Clock). Test:
  cRZStopwatch's start / stop / elapsed over a scripted clock on both sides; the calibration constructor's effect on the
  cells (MeasureFrequency's two doubles and the stopwatch's two words) compared after the call; the start-up slot by the
  noimage gate count (render_object's gate, docs/subsystems/scenegraph.md "Milestone O", drops by these references).
  Pitfalls: x87 vs SSE for the frequency division (compare bits); a static initialiser that the host already runs from the
  image must not run twice (XC slot 105: ours runs in the image-free start-up only); the cRZStd vtable's Assert / Report
  slots reach the message box import (impfx traps it: stand in).
  *Done (lane FA, 2026-11-03):* 0x1c9368 / 0x1c948d verified (mode 0 differentially: the time-stamp helper and the
  QPC slot scripted, tests/stopwatch_tsc.cpp); the calibration object, both timebases (0x10cb648, 0x10cb678: written,
  never read; every mode subtracts 0x10cb648's overhead), 0xff2480 / 0xff2488 and the frequency cache pointer 0x10cb63c
  (the cRZTimer section's view, null image-free) owned, with the stopwatch's import slots; the constructor runs from
  standalone/owned_statics.cpp only, and tests/stopwatch_calibration.cpp's CalibrationWhole runs it whole over impfx's
  Clock. cRZStd's table owned (src/rz/cRZStdTable.cpp) with its slots' cells (debug level 0x10cb5d4, the message-box
  lock 0x10cb5cc and its XC slot 9 lifetime, "ASSERT" 0xda3f88, eight import slots): no stand-in needed, since the
  table only has to link. render_object's gate 7 -> 4 (left: cRZOFStream 0xda2c14, cVectorEvent 0xdb07d4,
  cTextureHandleIterator 0xe7c7f4, and the null address of four scene-graph users).
- **FB, streams (Sonnet).** Calls: the cIGZFile face (ours: cRZFile), cRZString (ours). Fixture: tests/gz_file_source.h
  (file faces), tests/gz_gimex_stream.h (seekable memory stream) as patterns; the cDebugStream sink is
  OutputDebugStringA (impfx). Test: each typed getter / setter on random values and byte orders, compared as the
  stream's bytes and the object's state; error and end-of-stream paths. Pitfalls: GetError 0x1b5941 is folded with
  cDebugStream's slot: hook it only once both families are ours (CLAUDE.md, the `shared_vf_` rule); "Constructors that
  allocate other classes" (cRZOFStream in the file back end, F4 conventions); the typed bodies 0x1b2ffb..0x1b3487, 0x1b5358, 0x1b6c2a are old-MSVC STL instances, library code (FB; not a template of ours).
- **FC, values and containers (Sonnet).** Calls: cRZString (ours), the game allocator. Fixture: plain; svcfx (tests/
  ts_services_fixture.h) for cRZUnknownList's counted elements. Test: each slot on random values; containers compared by
  contents and the elements' reference counts. Pitfalls: "cRZVariant layout" and "Slots as 5-byte jmp stubs" (F2
  conventions: stubs under 5 bytes are never hooked, the shared helpers are); "cRZString held by value in our
  containers" (the one-member union); "The game's STL is ours" (unordered_map<cRZString, cRZString> is library: run
  stlprobe before writing anything); cRZUnknownAutoPtr<cRZSparseMatrix> is F6's grid holder (Left): own the holder vtables
  so the Grid handler stores ours.
- **FD, events and behaviour bases (Opus).** Calls: cConnectionManager's maps (library STL), cRZThreadSignal / the
  critical sections (ours, F1), the listeners' OnQuery slots. Fixture: svcfx; "A behaviour as a target" (hand-made
  vtables, F1 part 4 conventions); impfx Sync for the multi-threaded base. Test: connect / disconnect / dispatch
  sequences, compared as the listeners' logs and the managers' map contents; reference counts. Pitfalls: check every
  template body in db/names.jsonl for `shared_vf_` before hooking (bodies folded across listener classes are hooked only
  when every reference is the family's own); "Original bug: Disconnect of a missing connection" (reproduce,
  ORIGINAL-BUG); the cVectorEvent slots 0x154eb0 / 0x258ca0 are shared with other families: own the vtable data (the
  gate) and leave the bodies unless the rule allows.
  *Done (lane FD, 2026-11-03):* the queue itself was STL instances (library). The 31 TS-side
  cAbstractBehaviorTypeT<I, cRefSingleThreaded> copies are one template (src/ts/TSBehaviorCopies.h, cTSBehaviorCopy:
  SG's cBehaviorCopy asserts more than 15 slots and has no /O1 registry getter), 62 bodies verified (6,464 B), each
  copy's cell owned. cVectorEvent<cVector4f>: 0xdb07d4 owned and cIEventHeader's 0xd9ca58 too (the destructor stores
  it); 0x258ca0, 0x269ba0, 0x25b130 hooked and verified, 0x154eb0 verified through our table (folded). The
  ORIGINAL-BUG was already reproduced (src/gz/cConnectionManager.cpp:119). render_object's gate 3 -> 2 (left: the null
  data reference and cTextureHandleIterator 0xe7c7f4). 0x1bed67 is library (std::string copy assignment). The registry
  cRZNameLookup uses (std::list of 0x38-byte records at 0x10f4ff0, lock g_Lock, static initialiser 0x13e405: lane HT's)
  is the network's asynchronous name-lookup table (users 0x999103 / 0x9994e7 / 0x999556 / the thread 0x999678, called
  from the HTTP client and the sockets): it goes with cRZHTTPClient / cRZNetSocket, not foundations.
- **FE, messages, framework, COM, registry (Sonnet).** Calls: the message queues' std::list (library), cRZLock (in the
  lane), the COM directory (ours). Fixture: svcfx; app_fixture.h for cGZFramework. Test: post / dispatch / pump sequences
  over recording targets; the registry's lookups on a built table. Pitfalls: "Message server and graphics system caches"
  (cells 0x10f5198 and the cached getter 0x9c5a89, F1 conventions); GetMessageServer's cell must be the test's.
  *cRZLock done (lane FD, 2026-11-03):* one class, src/rz/cRZLock.h, for the three copies (SmallVirtuals, the persist
  system's local class, the speculative loader's ScopeLock); the constructor 0x1d8af0 (27 B) and the deleting
  destructor 0x142248 hooked, byte matches plus difftests (tests/rz_lock.cpp); table 0xd9b644 owned. The rest of FE's
  scope was found empty (lane FC).
- **FF, app shell, logging, ini (Opus).** Calls: nGZLog's back ends (ours), cRZIniFile's writer (ours), the
  property files (ours), cTSCustomContentSupport (objects: original call, 90 B). Fixture: app_fixture.h, gzlog_support.h,
  impfx (Libraries for DynamicLibraryList). Test: as F4's (log text through a recording back end, ini on in-memory files).
  Pitfalls: "Back end and service fakes" and "Pointer globals the image reads" (F4 conventions); the structure pass first
  (owner-inferred helpers may be other classes'); nGZLog's standalone shape (Left) is a data item: the service's
  globals from our build.
  *Step 0 (lane FD, 2026-11-03):* nothing left. The classes are 96-100% done; every queued function is owner-inferred
  and either an STL instance over cRZString (0xc9bea0, 0xc9bb9f, 0xc9bb2b, 0x5d67df, 0xc9bf54, 0x1c44f0, 0x6b97db,
  0x1ba27e, 0xcb3250, 0xcb35e0, 0x3f5220: library) or a caller's parser (cRZIniFile's 6.7 KB: the nTSWorld lot tables
  0x4ad450 / 0x4adb50 / 0x4ac1f0 and others, world-lot; 0x2b5290, neighbourhood; 0x22cc28, a begin_group reader).
  nGZLog's standalone shape is still open as a data item.
- **FG, cursor, resources, clipboard (Sonnet).** Calls: the serial record reader (cGZDBSerialRecordAsIGZFile: ours),
  CreateIconIndirect / LoadCursor (impfx traps: stand in), the clipboard imports. Fixture: impfx, the stream fakes of FB.
  Test: LoadFromSerialRecord over synthetic cursor records (not package content), compared as the cursor's fields and
  the import calls. Pitfalls: "Win32 window procedures" and "Fakes that answer true must fill their out argument".
  *Step 0 (lane FD, 2026-11-03):* the cursor resource, its factory, the string resource factory and the clipboard
  viewer are not executed in run-1023 (queue 0 B; LoadFromSerialRecord 0x9c4af0, 1,205 B, has no status). The
  clipboard manager queues a map find-or-insert instance (0x85ca6e, library) and a 5-byte stub. Run FG only if the
  standalone build needs the cursor classes.
- **FI..FL, the script writer.** Calls: the window faces (cIGZWin*: ui-windowing's classes, called through their faces),
  cRZString::Sprintf (ours), std::string (library), content_registry (objects), cGZWinGrid (ui-windowing),
  cMaterialParser (scenegraph): the last three original calls. Fixture: `tests/legacy_win_faces.h` (lane VY moved the 24 face
  tables there from tests/gz_legacy_handlers.cpp, unchanged; the writers read getters the handlers didn't need, so FI
  names and declares those slots there, for both tests) with winfx windows; the formatted script text is the
  result. Test: per writer, a window with the class's face whose getters answer generated values, the writer's text and
  the call log compared. Pitfalls: "Helpers returning in al", "Stack-built serializables" (F4), the std::string reserve
  difference ("The image's std::string::reserve": compare contents, not capacity); exec 0 in run-1023: the in-game check
  needs a UI-editor save (none in the scenarios), so these are difftest-only.
- **FJ..FL recipe (lane FI's structure pass, 2026-11-04).** Which writer is which class (the dispatcher 0x87a9a9
  switches on the interface id the header 0x87ae44 returns, QIs the window for it and hands the writer that face):

  | lane | writer | class (iid) |
  |---|---|---|
  | FJ | 0x87822e | GZWinText (212cdc1f) |
  | FJ | 0x870b57 | GZWinCombo (412ce496) |
  | FJ | 0x87551d | GZWinLineInput (21335c59) |
  | FJ | 0x875ccb | GZWinListBox (4132242b) |
  | FJ | 0x8730b3 | GZWinGen (5386d516) |
  | FJ | 0x877979 | GZWinSpinner (612ce0c3) |
  | FJ | 0x8777a6 | GZWinSlider (21325207) |
  | FJ | 0x86f2b8 | the common attributes, cdecl (window, out): not a class writer, called on the window itself |
  | FK | 0x8764bf | GZWinOptGrp (a1336cc0) |
  | FK | 0x878762 | GZWinTextEdit (231a1c57) |
  | FK | 0x86ff03 | GZWinBtn (8810) |
  | FK | 0x8792f2 | GZWinTreeView (3ae8bad2) |
  | FL | 0x87117a | GZWinFileBrowser (3aa52e64) |
  | FL | 0x873913 | GZWinGrid (daa6b9be) |

  Lane FJ (2026-11-04) did Text, Combo, LineInput, ListBox, Gen, Spinner and Slider (verified, in
  src/gz/cGZLegacyScriptWriterFJ.size.cpp); the dispatcher's `original_calls` is now 0x86f2b8, 0x86ff03, 0x878762,
  0x8792f2, 0x87117a, 0x8764bf, 0x873913. New patterns: a getter whose answer is dropped leaves its out locals
  uninitialised in the original, so its fake must always write; a local used by one getter only but converted
  unconditionally needs a forced write in the shared-bytes fake; float alpha converts through int64 truncation.
  Details and the shape of 0x86f2b8 (tooltip object queried on slot 126, message call on slot 128): notes/feedback/FJ.md.

  Lane WA (2026-11-04) did 0x86f2b8, OptGrp 0x8764bf, TextEdit 0x878762 and Btn 0x86ff03 (verified, hooked); left: FK's
  TreeView 0x8792f2 (analysis in notes/feedback/WA.md) and FL. New patterns: reuse the existing face class (`cIGZWinButton`)
  by naming its getter slots and growing its `Slot` table; `hookspec.py` for the CRC; shared key buffers, native colour
  and bytes, and overlapping locals mirrored with shared fake state (`gRgbForce`, `gRgbSpan`) plus the "answers without
  writing" case; the multi-property slot-126 fake `GetOptProperty` (variants AddRef'd once, released twice at the end);
  `RawSlot<N, R>`, `OriginalAddress` for visitor callbacks, `Settle` for callback slots, `LegacyText` for text out
  parameters; a mutation list is refused whole when one `old` string matches twice.

  Lane WD (2026-11-04) did TreeView 0x8792f2 (verified, hooked; src/gz/cGZLegacyScriptWriterTree.size.cpp, case 17 of
  LegacyScriptWriter_Classes, 12 of 12 mutants killed); left: FL only (the dispatcher's `original_calls` is 0x87117a and
  0x873913). New patterns: the six tree tables are window properties holding addresses of real std::maps
  (src/gz/cGZLegacyTreeTables.h, shared with the applier 0x86c19e), so the test builds real maps and the fake slot 126
  hands out uint32 variants of their addresses; an unguarded original lookup (image resources, parents) means the test
  forces the table present exactly when the loop runs; a face call inside a null guard (GetNodeImage) must stay inside
  it or the call logs differ; fixtures `TreeTables`, `TreeState` and `InstallTreeGetters` in tests/legacy_win_faces.h
  (reusable for FileBrowser and Grid: a state struct of drawn answers plus one install function per face, see notes/feedback/WD.md).

  GZWin and GZWinCustom have no writer. Row FI's writers are all done (verified, db/status/cGZLegacyScriptWriter.jsonl):
  BMP 0x86fc7d, TextTicker 0x8791f3, FlatRect 0x872b33, Outline 0x876ea4, Folders 0x872f45, Scrollbar 0x87751e,
  Scrollbar2 0x877189, with the dispatcher 0x87a9a9 and the header 0x87ae44; the dispatcher's `original_calls` lists
  the writers still to do. Patterns met so far, for the lanes after: an out value shared by several getters (Scrollbar2's
  six value getters write one variable only when they answer true: a getter that answers true without writing repeats
  the value before), one key buffer zeroed once for several key properties, one text reused for a list of elements (an
  element not written repeats the one before), and a node buffer from `operator new[]` that may fail (Outline). The
  fakes must reproduce such cases, or a mutant that makes the local fresh per call survives.
  Per writer, the steps:
  1. Read it with `classinfo.py --disasm`: every writer is cdecl (face, out), and every attribute is one helper call
     from src/gz/cGZLegacyScriptWriter.h (the 23 helpers are verified and hooked, so call ours). The name is a
     stack-built cRZString; in src it's a `Text("name")` local (a LegacyText built from a char string, as in
     src/gz/cGZLegacyScriptWriterClasses.size.cpp). Face getters are reached as `face->slot(N)`: name and declare each
     one in src/gz/cGZLegacyHandlers.h (the class's face) and in its fake in tests/legacy_win_faces.h. A getter
     with an out pointer is an `a` argument; give the fake an `On(slot, handler)` that writes the out values. Check
     that the face's slot count still covers the new slot (kTextTickerFace grew from 10 to 13).
  2. Write it in src/gz/cGZLegacyScriptWriterClasses.size.cpp (or a sibling file per lane). Then switch its case in
     ClassWriter() (src/gz/cGZLegacyScriptWriter.size.cpp) from `OriginalFunction` to ours. Then switch its stand-in in
     tests/gz_legacy_script_dispatch.cpp from `shape::CodePatch` to `shape::StandIn(rva, &Ours, &WriterStandIn<N>)`.
     Add its hook to src/hook/tables/cGZLegacyScriptWriter.cpp (CRC from `tools/hookspec.py`).
  3. Test it as a new case in tests/gz_legacy_script_classes.cpp (LegacyScriptWriter_Classes). Raise kWriters and add
     a case: the class's fake face, its window (its GetWindow slot returns the winfx window, whose GetProperty, slot
     126, hands out the one stored value), the getters' returns or handlers, and the original's RVA in kOriginals.
     The helpers run for real on both sides.
     - What gets compared: the text, the stored value's count, and the log.
     - Edge values: the font property (number, text, other type, absent), key properties (16 bytes, other sizes,
       absent), flags from gRng(), and floats past 0..1.
     - The writers seen so far use ftol for float * 255 → byte. Ours: `uint8_t(int64_t(f * 255.0f))`.
  4. Mutants: 2 to 4 per writer (a wrong slot, a dropped or reordered attribute, a flipped flag test, an off-by-one in a
     list loop). Use a mutation file in build/; never edit src while mutate.py runs.
  5. Status: verified, with test LegacyScriptWriter_Classes (and LegacyScriptWriter_Dispatch, which covers the
     dispatch). The writers run 0 times in game, so the notes say so.
- **FM, memory report (deferred).** Calls persist-dbpf and ui-windowing classes; fixture then: those campaigns' fakes.

### Shared fakes

Existing, to reuse: impfx (imports), svcfx (`tests/ts_services_fixture.h`), app_fixture.h, gzlog_support.h, winfx.h,
drawfx.h, gz_file_source.h, gz_gimex_stream.h, rz_recording_string.h. New (lane VY): `tests/legacy_win_faces.h`, the 24 legacy
window faces' slot tables (cIGZWin* and the graphics / UI-defaults services), moved out of tests/gz_legacy_handlers.cpp
unchanged; the handlers' tests (LegacyHandlers_*) are its test, and FI..FL extend it. Not built: a writer fixture
(FI's structure pass decides its shape) and anything for FA..FG (their recipes reuse the existing fakes above).

## Conventions (lane LO)

- **Import fakes: impfx** (tests/impfx.h; how-to and pitfalls in docs/lane/test.md, "Faking imports"). Built on drawfx's
  stdcall kind; one Imports per test, `BeginSide()` per side, kinds Threads / CrtThreads / Sync / Clock / Libraries.
- **Hook shape for the thread classes.** cRZThread, cRZThreadSignal, cRZCallbackThread and cRZCallbackTimer are value
  members of other groups' classes (built over their memory by our Construct or theirs), so they keep the game's vtable
  and every slot of theirs is hooked at its RVA. Folded slots stay the original's: the signal's slots 0 and 7 (shared
  with cRZMutex), the timer's 2, 4, 8 and 12.
- **Game-global STL containers** (the timer's live set at 0x10f59d8, the callback thread's queue) are used in place
  as our std types by reinterpret_cast: MSVC 14.40's layouts match and the two CRTs share the process heap. Tests
  placement-construct a fresh one over the global and restore its bytes afterwards (rz_callback_timer.cpp, World).
- **A slot that calls its own vtable** (the timer's setters restart through slots 5-7): our side's test object gets a
  copy of the game's vtable with our slots in it, as the hooks make it in the game.
- **For F3-F6:** use `impfx::Imports` for any Win32 call (F3's DirectoryScan FindFirst/NextFile and gimex
  LoadLibrary/GetProcAddress: the Libraries kind; F4's GetSpecialFolderPath and registry read: plain `IMPFX_ADD` fakes
  with handlers that write every out byte; nGZLog's GetProcessTimes likewise). Timers and callbacks (cGZWinBtn,
  cGZWinScrollbar2, cTSEffectsManager, cGZSndSys, cGZDBSegmentDirectoryFiles) can now call our cRZCallbackTimer
  Construct / Destruct instead of OriginalFunction 0x9ccf32 / 0x9ccfa9; StaticInitHT.size.cpp and GZCOM.size.cpp can
  call our cRZThread::SleepMicroseconds instead of 0x1b9f95 (`status.py --check` notes list each site).

## Done

**F2, part 1 (lane LN, 2026-10-23): cRZVariant whole, plus cRZString::TrimBoth.** 117 cRZVariant functions verified,
15.7 KB (from 23 verified / 0.5 KB and 8 reimplemented): the 19 constructors (one, the uint32 0x9c7b3d, stays the
image's: UtilObjectB::Construct's hook, another group's), destructor, deleting destructor, assignment, every unique slot
body of the 133-slot cIGZVariant (src/rz/cIGZVariant.h), the store / clear / text helpers, the string parsers
(ParseScalar 0x9cb4e1, ParseArray 0x9cad97) and the eight `__cdecl` statics (Convert 0x9c986c, ToFloat64, ToInt64,
ToString, FromString, IsTrueText, ParsePointer, FormatPointer). Standalone shape: the game's variants carry our vtable.
cRZString::TrimBoth 0x1ad83c (16 B) hooked to the IniFile group's helper. Tests: tests/rz_variant.cpp (scripted
transcripts on three routes, every static on every tag, the deleting destructor's frees, and cRZVariant_Reach: every
hooked RVA tied to the slot or caller the scripts reach it through, checked against the image),
tests/rz_string_trim_both.cpp. Original calls left: the text parsers 0x1b0ec2 (hex), 0x1b101c (int64), 0x1b1008
(double) and the splitter 0x1b1557 (shared-utility; take them with the property-set serializers).

**F2, part 2 (lane LQ, 2026-10-23): cGZBuffer and cGZTransientBuffer whole; the cRZVariant call sites switched.**
72 status entries, 7.36 KB verified (from 22 entries / 0.8 KB): every slot body of both classes, the 15 adjustor thunks,
the constructor 0x8c5710, both deleting destructors, the colour mask 0x8c7ca0, the bytes-per-pixel table 0x8c76f0 and
the line clipper 0x8c7a50. Standalone shape (src/gz/cIGZBuffer.h, src/gz/cGZBuffer.h): the two creators 0x8c75d0 /
0x8c7620 the cGZDrawCOMDirector getter registers are the only hooks besides FormatMask (the pixel converter, game code,
calls it); everything else is in effect through them. Tests: tests/gz_buffer.cpp (GZBuffer_Run: 3000 seeds, the
original creators against ours, 10-69 random slot calls on every face, fields, pixel memory, the 0x54 pool and every
fake's log compared), tests/gz_buffer_helpers.cpp (the three cdecl helpers). The 8 cRZVariant call sites build our class
(the constructor, copy, uint16 and destructor original calls dropped; lane LQ's first commit). Original calls left: the
pixel converter 0x8c6b10 (BltUnclipped; about 60 per-format routines behind the table 0xe4f0d8); InitPixelFormat calls our graphics
service getter 0x9c5a05 (lane LT). The colour type constructor 0x18a220 is reimplemented inline, not hooked
(other classes call it).
Not done in F2 (next F2 lane, about 5 KB): cGZBufferFontRenderer 1.4 KB, cRZLanguageUtility 1.6 KB, cRZLanguageManager
1.45 KB, cRZStringResource 0.18 KB (its cRZString base and cGZPersistResource face: check which are ours first),
cRZNameLookup 0x998f2c, GZServiceString 0x177110, the iterator templates, the ownerless code near AnyStringSet and the
property-set serializers.

**F2, part 3 (lane LS, 2026-10-23): the text parsers, cRZLanguageUtility and cRZLanguageManager whole,
cRZStringResource, GZServiceString, cGZBufferFontRenderer's non-slot code.** 102 entries verified, 23.6 KB:
- the parsers (db/status/RZTextParse.jsonl, src/rz/rztext: 0x1b0ec2, 0x1b101c, 0x1b1008, 0x1b1557 and two helpers;
  cRZVariant calls them; 1.7 KB);
- cRZLanguageUtility (33, 13.5 KB; constructor and ToDefaultEncoding hooked, slots through callers);
- cRZLanguageManager (49, 6.4 KB; only the creator 0xc6f338 hooked, everything else through callers; the creator is a
  manual entry in db/missed-functions.jsonl);
- cRZStringResource (8, hooked: game code builds it inline);
- GZServiceString::AllocateForCapacity 0x177110 (hooked);
- cGZBufferFontRenderer's constructor 0x90a7b5, blend-table builder 0x90e1aa, glyph blitter 0x90bbba and the 565 / 4444
  blends (src/gz/cGZBufferFontBlit.size.cpp, all hooked; DrawGlyphs calls ours, still a byte match).

Tests: tests/rz_language_manager.cpp, rz_language_utility.cpp, rz_string_resource.cpp, std_service_string.cpp,
gz_buffer_font_blit.cpp. Gaps: the manager's setlocale / _setmbcp go through a seam (`LanguageManagerCrt()`) that
defaults to our CRT; the host should bind it to the image CRT's (0x837dcc, 0x84a8d7) while the image CRT holds the
game's locale.

Not done in F2, with reasons:
- the ownerless code near AnyStringSet (0x237138, 0x2383aa, 0x238159, 0x23709e, 0x237111) is the music playlist's
  (audio campaign);
- cRZNameLookup 0x998f2c is cRZHTTPClient's (network);
- the iterator templates' slots are mostly shared_vf_ bodies of other groups' containers (take them with their owners);
- the property-set serializers.

**F1, part (lane LO, 2026-10-23):** impfx; cRZThread's slots and thread routines, the 0x1b9f95 SleepEx helper,
ExitWith; cRZThreadSignal's signalling slots; cRZCallbackThread's slots and thread body; cRZCallbackTimer whole
(45 functions verified, 2.9 KB; db/status/Threads.jsonl). The rest of F1's row is under Left.

**F1, part 2 (lane LR, 2026-10-23):** cGZMessageQueue and cGZMessageQueue2 whole, standalone (52 verified; ContainsId
0xc756a8 mapped only: its body is split in db/functions.jsonl); cRZMessage2Standard's remaining ten (hooked; the class
is now whole); nGZEvent::cConnectionManager with cConnectionIterator and the null iterator, standalone (25 verified,
src/gz/cConnectionManager.h). Call sites switched to lane LO's sleep and callback timer, and to our queues and
connection manager (the message servers, cGZSndSys, the event system service's static). Tests:
tests/gz_message_queue.cpp, gz_message_queue2.cpp, rz_message2_standard_rest.cpp, gz_connection_manager.cpp
(mutants: 49/49, 26/27 with one equivalent, 30/30). The nine sites that built a cRZMessage2Standard through
OriginalFunction 0x1c033d (cTSAudioListener, cGZMessageServer2, cGZWinMgrW32, PrimS2A, cTSCatalog, cTSObjectFolder,
cTSTradeableRewardsService, cTSWantTreeManager, cTSUIServiceManager) call our Construct; the two tests that stood in for
0x1c033d on the image now stand in for ours too. Gap: the tests of cTSWantTreeManager, cTSUIServiceManager and
cGZWinMgrW32 pass with the constructor call dropped (notes/feedback/LR.md).

**F1, part 3 (lane LT, 2026-10-23):** three groups, standalone, 108 entries verified (9.2 KB):
- nGZEvent::cGZEventReflector with its cAbstractBehaviorTypeT<cIAbstractBehaviorType, cRefSingleThreaded> base, as one
  class (41 entries, 2.4 KB; db/status/cGZEventReflector.jsonl, tests/gz_event_reflector.cpp, mutants 35/37 with two
  equivalent). Factory 0x1a9e0b, constructor and the ownerless helpers (event service acquire, active-bit sync, the
  id range and its release, the event registry) hooked.
- cRZCursor, cRZSystemCursorW95 and cRZCursorManager (36 entries, 3.0 KB; db/status/cRZCursor.jsonl,
  tests/rz_cursor.cpp, mutants 62/63 with one equivalent). The cursor factory 0x9b3460, the constructors and the
  graphics-system helpers 0x7ae7f4 / 0x9c5a05 / 0x5ba211 hooked; cGZBuffer (its test through shape::StandIn) and
  cTSUIPaintingService call ours.
- cGZClipboardManagerClipboard with its system clipboard viewer (32 entries, 4.0 KB, the constructor's entry moved from
  cGZClipboardManager; db/status/cGZClipboardManagerClipboard.jsonl, tests/gz_clipboard.cpp, mutants 80/81 killed, 1 equivalent).
  The constructor, the clipboard data constructor 0xc6d2a5 and the message-server helpers 0x762db4 / 0x9c5ab4 hooked;
  the viewer's window procedure is the one our viewer registers. cGZClipboardManager now builds and calls the class
  through cIGZClipboard (its tests patch our vtable as well as the game's). cGZWinBtn, cTSMainToolbar and
  cGZWinMgrW32 call our GetMessageServer (WinMgr_Run's notifier stand-in moved from a CodePatch to shape::StandIn).
Not taken: cConnectionChangedEvent (the connection manager that builds it is LR's, verified; its vtable 0xda239c stays
the game's), cInputDeviceMap and the other cAbstractBehaviorTypeT instances (Left).

**F1, part 4 (lane LW, 2026-10-23):** nGZEvent::cInputDeviceMap whole, standalone (20 entries verified, 2.2 KB;
src/gz/cInputDeviceMap.h / .cpp, db/status/cInputDeviceMap.jsonl, tests/gz_input_device_map.cpp, mutants: 36, all killed). The
factory 0x1a8d95 (a manual entry in db/missed-functions.jsonl) and the constructor 0x1a8af6 are hooked; the 11 slots,
SendButton / SendMotionAll / SendMotion, the destructors and the stored event's four members are in effect through
callers (trap `Hooks.nGZEvent_cInputDeviceMap_Construct=1`). cConnectionChangedEvent needs nothing more: its slots are
byte matches in the ClassesRound3 group, the rest are a jump stub to one of them and two 4-byte leaves (no entries), and
its vtable 0xda239c is referenced only by verified code (our connection manager builds it). Six original calls switched
to verified callees, each caller's tests rerun and one mutant per switched path killed (6/6): rztext::ParseDouble in
cTSAudioListener and the property-set XML delegates, rztext::ReadDouble in the sound defaults, MakeIteratorRange in
cTSAudioSoundManager::GetRegisteredIds, cTSSimSystem's constructor in the TSSim director's OnStart and
cRZCursorManager's constructor in cGZWinMgrBase::Init.

**F3 (lane LV, 2026-10-23):** six groups, 120 entries verified (about 12.9 KB). Gimex not taken (Left).
- The file and text streams (db/status/RZFileStreams.jsonl, 53 verified, 3.2 KB; tests/rz_file_streams.cpp, mutants
  23, one equivalent): cRZTextOStream, cRZTextODSStream, cRZOFStream, cRZIFStream, hook-shaped (54 hooks) over
  std::ifstream / std::ofstream. GetError 0x1b5941 (shared_vf, also the sim's cDebugStream) mapped, not hooked. Call
  sites switched: MaterialCommands, PrimRDelete, SimTraceLog checktree, cTSWantSimulator dump, GG debug.
- cGZXmlWriter, cGZXmlBufferWriter, cGZXmlFileWriter (31 verified, 1.7 KB; tests/gz_xml_writer.cpp, mutants 30/30), standalone: the
  XmlUtils director's two creators and the two constructors hooked, every slot ours through our vtables.
- DirectoryScan (16 verified, 2.6 KB; tests/rz_directory_scan.cpp, mutants 40, one equivalent): the plain directory
  lister (FindFirstFile / FindNextFile through impfx), 14 hooks; the entry constructors 0x1c4939 / 0x1c4962 in effect
  through callers.
- DynamicLibraryList (9 verified, 4.1 KB; tests/ts_dynamic_library_list.cpp, mutants 28/28): the list
  cTSAppFW::AddDynamicLibrariesHere builds on its stack, 7 hooks; the two ini line callbacks 0xcb3f90 / 0xcb3fd0 in
  effect through ReadConfiguration. The cTSAppFW comment on RequireVersion corrected (a requirement is dropped whatever
  the version).
- cGZRESIDtoExtensionMap (7 verified, 1.1 KB; tests/gz_resid_extension_map.cpp, mutants 27/27): standalone class,
  slot-hooked, because its only objects are embedded at +0x7c of cGZDBSegmentDirectoryFiles and built inline by that
  class's constructors 0x99e4c0 / 0x99e8f0 / 0x99ebf0 (destroyed inline by 0x9a1bb0). The test runs three routes
  (original slots, our vtable, the adapters on a trap-vtable object).
- cNeighborhoodThumbnailFilter (4 verified, 0.1 KB; tests/ui_neighborhood_thumbnail_filter.cpp, mutants 10/10): standalone,
  the constructor 0x725594 hooked (its only caller is cTSUIThumbnailService slot 52), the three own slots in effect
  through it.

**F3b (lane LX, 2026-10-23):** Gimex, three groups, 100 entries verified (9.4 KB). The codec question settled (Risks,
5) and the codecs split for the next lanes (Left).
- The Gimex core (db/status/Gimex.jsonl, 14 verified, 0.6 KB; tests/gz_gimex_core.cpp, 28 mutants: 26 killed,
  2 equivalent): the codec table is ours (src/gz/Gimex.size.cpp; the format rows hold the original codecs' entry points through
  OriginalAddress, the null rows are ours), the current format is ours (the game's 0x10e693c is no longer read), the
  dispatchers, CompareNoCase and FindFormatByExtension; in effect through callers. The dispatcher tests run over
  recording fakes CodePatched onto all 38 codec entry points (tests/gz_gimex_fakes.h), so both tables reach the same
  fakes.
- cGZGimexReader / cGZGimexWriter (db/status/cGZGimexReader.jsonl, 30 verified, 2.0 KB; tests/gz_gimex_reader.cpp,
  31 mutants, all killed): standalone, the creators 0x87b428 / 0x87b43e and constructors 0x87b4b4 / 0x87b4d1
  hooked. Slot 3 (shared_vf_87bbd7) is shared by these two only: one entry here. Slot 1 (0x165e30) is folded across unrelated
  classes: no entry, ours only in our vtables.
- cGZGimexFactory (db/status/cGZGimexFactory.jsonl, 56 verified, 6.8 KB; tests/gz_gimex_factory.cpp, 61 mutants:
  60 killed, 1 equivalent (the palette cache in CopyIndices16 only memoises a pure pack); the row converters in
  src/gz/cGZGimexFactoryRows.cpp, 11 mutants, all killed): standalone, the creator 0x87b412 and constructor 0x87bcdc
  hooked; the +4 face's slots 3, 5, 6 are the base's verified bodies (0x9b60e0 / 0x9b6200 / 0x9b62d0, called
  as ours), slot 8 (0x1f62f0) and the folded 0x192140 / 0x4805dd have no entry. The cGZPersistBufferResource
  constructor 0x9b64a0 stays an original call (CreateInstanceByType).

**F3c (lane MN, 2026-10-24):** the Gimex small-format codecs, partly: BMP and TGA whole, the IJL JPEG's loader, about,
is, open, info and close, and the shared wopen / wclose. 28 functions verified, about 14 KB (db/status/GimexBmp.jsonl 11,
GimexTga.jsonl 10, GimexIjl.jsonl 7, Gimex.jsonl +2); all entry points hooked at their RVAs, the table cells
(src/gz/Gimex.size.cpp) ours.
- BMP (row 5, src/gz/GimexBmp.size.cpp, tests/gz_gimex_bmp.cpp, 34 mutants, 3 equivalent) and TGA (row 6,
  src/gz/GimexTga.size.cpp, tests/gz_gimex_tga.cpp, 49 mutants, 4 equivalent): run on both tables with synthetic
  images through fake cIGZFile streams (tests/gz_gimex_stream.h): decoded pixels, info, bytes written, stream position.
  ORIGINAL-BUGs reproduced or kept out of the comparison are marked in src (RLE rows overflowing the row buffer, a
  header size past the stack buffer, 1-byte streams read past what was given).
- Shared wopen 0x87f3e4 (OpenWriter) and wclose 0x87fe5d (FreeInstance) for rows 2..6 (1 mutant).
- IJL (row 3, src/gz/GimexIjl.size.cpp, tests/gz_gimex_ijl.cpp, 31 mutants, 4 equivalent): the DLL faked with impfx
  Libraries() (ijl15 / ijl11 / ijl10, any subset, each export present or missing) and recording fake ijlInit /
  ijlRead / ijlFree answering from a script. The game's cells 0x10e6948..0x10e695c hold the module and entry points
  (the original read and write still use them). Info reads the comment pointer 4 below the flag word (the operand
  follows a `push`): worth knowing when reading such disassembly.

**F3c part 2 (lane MO, 2026-10-25):** the rest of the Gimex small-format codecs: the IJL read 0x87fc52 and write
0x87fe74, the libjpeg JPEG (row 2) and the PNG (row 4); rows 2..6 are now whole. 15 JPEG (db/status/GimexJpeg.jsonl,
about 6 KB) and 14 PNG functions (GimexPng.jsonl, 3.6 KB) verified, plus the two IJL ones (GimexIjl.jsonl); all hooked
at their RVAs, the table cells (src/gz/Gimex.size.cpp) ours.
- The libjpeg 6a, libpng 1.0.5 and zlib 1.1.3 code in the image runs for real on both sides of the tests (no fake
  library): seeded synthetic images written by the original, corrupted nine ways (truncated, bytes flipped, chunks cut,
  header fields damaged with the checksum fixed, garbage), compared for results, info blocks, decoded pixels, bytes
  written, stream position and the allocator / message log. The allocator (0x87da65 / 0x87da72), the JPEG library's
  output_message and the PNG library's default error function (0x8939f0, then the image's own longjmp 0xc49d03) are
  recording fakes that can fail a scripted call. JPEG tests/gz_gimex_jpeg.cpp: 95 mutants, 92 killed, 3 equivalent;
  PNG tests/gz_gimex_png.cpp: 102 mutants, 95 killed, 7 equivalent or unreachable (listed in the status notes).
  FixColorSpace 0x87f333 is a byte match (matchcheck v8).
- The library callees stay `OriginalFunction` calls (listed in each entry's original_calls); `status.py --check`
  flags them "library code: call the toolchain's function instead" (the image's libjpeg / libpng have an older
  structure layout than any toolchain build, so a standalone build needs the recipes in tools/thirdparty.ps1 and a
  decision on that rule: notes/feedback/MO.md).
- Inputs kept out of the PNG comparison because the original itself crashes or reads garbage there (the png library's
  allocation callback does not check its answer: png_zalloc writes through a null block): a failing allocation made
  by the library (only the glue's own allocations and the library's two structures are failed), a header claiming an
  image no allocator can give, a write stream that stops accepting bytes after the header (the library's error then
  jumps into the row writer's returned setjmp frame, in the original and in ours), a stream under eight bytes in the
  format test (the original compares an uninitialised buffer). The writer's palette copy and transparency array are not
  checked by the original either.

**F3c part 3 (lane MP, 2026-10-25):** Gimex's shared helpers and the FSH glue (row 1); with it every cell of the
codec table is ours. 8 helpers (db/status/Gimex.jsonl) and 9 FSH entries (db/status/GimexFsh.jsonl) verified, 4.5 KB
(328 bytes of helpers, 4,217 of FSH entries, info alone 3,510); all hooked at their RVAs.
- The allocator 0x87da65, free 0x87da72 (returns `uint8_t` 1: the original sets al only), the stream helpers
  0x87da81 / 0x87da8f / 0x87dab6 / 0x87dacd and the readers 0x87daf4 / 0x87db43 (src/gz/Gimex.size.cpp): byte matches
  (matchcheck v8) plus tests/gz_gimex_helpers.cpp (11 mutants, all killed). The other codecs call ours now (their
  local wrappers removed). Allocate and FreeMemory are `__declspec(noinline)`: the codecs' tests patch a recording
  allocator at our address as at the original's, and an inlined copy escapes the patch (heap corruption in
  Gimex_FindFormatScripted).
- FSH row (src/gz/GimexFsh.size.cpp, tests/gz_gimex_fsh.cpp): about, is ('ShpF' 100, 'FntF' 0x62), open, info, read,
  close, wopen, write, wclose. Each entry alone with the back end faked (results, the instance, the logs), info
  through the real back-end helpers over synthetic directories (every record type 0..0xff, every palette type,
  attached blocks, names), and a round trip through the real encoder / decoder (Gimex_FshRoundTrip: written by each
  side, read back as written, longer and with damaged names). 54 mutants, 52 killed, 2 equivalent (argued in the
  status notes). The back end below stays `OriginalFunction` calls (listed in original_calls). Kept out (the top of
  tests/gz_gimex_fsh.cpp): files the directory reader can't read (it hands a null directory on: the original faults
  at 0x888024), cut-short files and damaged offsets (the back end reads past the file).
- The file the writer makes: 'ShpF', the file size (little-endian, from the stream's Length), the count and a word
  (big-endian), then per entry its offset and size (big-endian) and a NUL-terminated name. The directory writer
  0x88c719 first writes a placeholder header from an uninitialised stack buffer and patches it after a seek to 0: a
  test stream for writers must honour seeks (the round trip has its own).
- Facts for the back end (binary, checked against refs/EA-Graphics-Manager): the header words 0x10fb / 0x11fb (and
  0x90fb / 0x91fb) are RefPack (0x895802), 0x18fb / 0x1afb / 0x1cfb / 0x28fb and their 0x80 variants are the block
  coders (0x8955c7; the reference lists 0x18 / 0x1a / 0x20, the binary 0x1c and 0x28), 'GM' is EA's lossy DCT coder
  (0x89694e, header 0x8968dd). The platform argument is 2 for every FSH call; 0x884193 says platforms 5, 7, 0xf and
  0x11 are big-endian. By the decompile (not tested apart): the directory reader 0x887a06 reads the 16-byte
  header and directory for platforms 0..9 (FSH passes 2) and scans chunks only for platforms 10..19 or a 'FntF' head.

**F3c part 11 (lane MZ, 2026-10-25): FSH back end step 7b, the top three of the encode side (the last step).** EncodeShape 0x88905f
(2,193 bytes), CompressShape 0x88aa6e (2,455) and WriteDirectory 0x88c719 (1,287): 5,935 bytes, `verified`, each hooked at its RVA. Tested
alone against the original (tests/gz_gimex_fsh_write_shape.cpp: 12,000 cases each, stack pre-cleared, return value, allocations, every byte
written, the stream's call log, the directory entries; a slack allocator and SEH catch the original's overflows, which are mirrored).
30 mutants, 29 killed; the survivor (`status == 0` break to continue in the block loop) is equivalent, because the status only changes at a
write. The FSH glue calls ours throughout, so no FSH function has an original call left (library code aside), and Entry / Directory are one
struct. Details and the kept-out inputs: the plan below (step 7b).

**F3c part 9b (lane MX, 2026-10-25): FSH back end step 6, block 1's decode side 0x88584d..0x8888e7.** 43 functions, 12,245 bytes,
all hooked at their RVAs and `verified` (db/status/GimexFshDecodeRecords.jsonl 26, GimexFshDecodePixels.jsonl 13,
GimexFshDecodeDirectory.jsonl 4; hook tables src/hook/tables/GimexFshDecode*.cpp). 12 of them (543 bytes) are byte matches
(matchcheck v8: HasCodec, RecordLength, IsSwizzled, IsKnownRecord, IsCode, ReadShapeExtra, ReadShapeMarker, NextLevelOffset,
BlockBits, ImageBytes, BlockIndex, YuvPixel); the rest rest on the difftests. The glue (src/gz/GimexFsh.size.cpp) now calls ours for
the directory reader, the info helpers, the decoder and the directory free; its `original_calls` shrank to 0x88905f, 0x88a105, 0x88c719.
- Sources: src/gz/GimexFshRecords.size.cpp (record classification, sizes, codecs, mip levels, palette entries, the info readers, the
  six-face splitter, `FreeDirectory`), GimexFshRecords.cpp (the two speed-built leaves), GimexFshPixels.size.cpp (copy and block
  reorder, `DecodeBlockImage`, `Untile`, the YUV converters, `UnpackPixel`, `DecodeShape`), GimexFshDirectory.size.cpp (`ExtractRecord`,
  `ParseEntry`, `BuildDirectory`, `ReadDirectoryStream`), header GimexFshDecode.h.
- Facts worth keeping: a directory is `{int32 count; Entry[0x4c]}`; a record has a 0x20-byte header (type at +0, attribute word at +1,
  length at +4, data offset at +8, width/height at +0x18/+0x1c); platforms 5, 7, 0xf and 0x11 are big-endian; the old file layout's
  entry count is the big-endian word at +8 of its 16-byte head (a first draft read +4: found by the reader's difftest), each entry
  being offset, 4 unknown bytes and a name; chunk files ('ShpF', 'FntF') are scanned for platforms 10..19 or a font head. The
  face splitter and the mip builder both read the shape past the record's own size on short records (the tests give the allocator
  64 KB of filler so both sides read the same bytes).
- **Tests** (`GimexFshRecords_*` 26, `GimexFshPixels_*` 13, `GimexFshDirectory_*` 4; tests/gz_gimex_fsh_decode_*.cpp, shared helpers in
  gz_gimex_fsh_decode_common.h): each function alone, the original and ours over seeded and crafted inputs with the stack cleared,
  comparing results, outputs, every block left allocated (with 512 guard bytes) and the allocator's log; the directory tests build
  real record chains (shapes with 1..4 mip levels, block-coded, RefPack-coded, cube-strip faces, palettes, pairs, text, markers) and
  files of the three layouts. Inputs kept out where the original itself crashes or reads wild memory are listed at the top of each
  test file. `Gimex_FshRoundTrip` and every `GimexFsh*` test stay green.
- Mutants: 86 over the three groups (tools/mutate.py), 71 killed, 11 equivalent (argued in the status notes), 4 did not build. Two
  survivors led to test changes (the copier's tiny-record and block-room draws).
- Left: 0x888939 (YuvPixel's colour matrix) and 0x888a19 (YuvPairs's pair converter) were original calls until lane MV's merge (switched to ours at the merge); then
  block 1's encode side (step 7). In-game: anything that loads .fsh / .newfsh shape files (textures, UI shapes, fonts, thumbnails with mip levels) must look as before; the
  smoke test covers the common ones.

**F3c part 9 (lane MV, 2026-10-25): FSH back end lane 5, the GM encoder 0x89d938..0x89e722 and block 1's encode leaves.**
37 functions, all hooked at their RVAs: the GM (DCT) encoder (17 functions, 3,561 bytes, src/gz/GimexFshGmEncode.size.cpp,
db/status/GimexFshGmEncode.jsonl, tests/gz_gimex_fsh_gm_encode.cpp) and 20 leaves of block 1 (4,678 bytes,
src/gz/GimexFshConvert.size.cpp, db/status/GimexFshConvert.jsonl, tests/gz_gimex_fsh_convert.cpp): the float and integer
YCbCr / RGB converters (0x885d30, 0x8885a1, 0x888939, 0x888f97, 0x88d5e8), the YUYV and 2x2 block writers (0x888a19, 0x888b13,
0x888c40, 0x88d212, 0x88d39e, 0x88ce4e, 0x88cc20), the plane reorderers (0x88cf96, 0x888e00), the size helpers (0x88cf36,
0x88d362, 0x88d5c9, 0x88d52e, 0x888d6d) and the pixel format detector 0x888efc. 36 are `verified`; 4 of those are byte
matches (0x888efc, 0x88d5c9, 0x89e102, 0x89e14a).
- **GM encoder:** the encoder state (bit writer, DCT scratch, DC predictors, tables) is the image's global block; ours is one
  struct at the same addresses (offsets static_asserted), the tests compare it bit for bit after every call. The DCT adds in the
  original's order (read from the asm; the decompile printed `(a2+a5)+(a1+a6)` in the wrong grouping). Mutants: 104, 100 killed,
  4 equivalent (argued in the status notes). The warning printf paths are not compared (the counter is).
- **Block 1 leaves:** the float converters keep single and double precision as written; SSE2 conversions go through
  `_mm_cvttsd_si32`. Reproduced originals' bugs (`// ORIGINAL-BUG:` in src): 0x88d39e converts four pixels and uses only the middle
  two; 0x88cf96 for formats 0x16 / 0x1e on a plane whose stride is its width writes 0x21 bytes past the last tile with 0x20 spare
  (a 1-byte heap overflow, found as a heap corruption in the test: RgbToYuv420 / ReorderPlanes are tested with 0x17 / 0x1f instead).
  Mutants: 119, 106 killed, 9 argued equivalent, 3 not killed (see Left).
- **Original calls:** only 0x888efc calls the original 0x88888f (its platform header test, listed in the entry).
- Left: 0x888e00 (ReorderPlanes) stays `reimplemented`: its two failure-path frees are untested (no allocation failure
  injection in the harness); 0x888d6d's truncate-of-sum vs sum-of-truncates differs only for negative heights. The FSH back end's
  lanes 6..7 (block 1's decode side, block 1's encode side). In-game: export a texture through the Gimex FSH encoder with the GM
  and YUV paths and compare with the original's file.

**F3c part 8 (lane MU, 2026-10-25): FSH back end lane 4b, the quantiser 0x89aa95 (6,424 bytes).** `verified`
(db/status/GimexFshCluster.jsonl), hooked at its RVA, in src/gz/GimexFshQuantise.size.cpp; with it block 2's block encoders
(0x896e1e..0x89d7b1) are all ours and the FSH plan's step 4 is fully done. What it does: scales each end point's channel to
0..255, rounds the pair down and up to the nearest 5 / 6 / 5-bit field values (four roundings per channel, kept as the 8-bit
expansions), makes the in-between values (the middle for form 3, the thirds `(2a + 1 + b) / 3` for form 4), tables each colour's
count-weighted squared distance to every level per channel and rounding, scores the 64 combinations of one rounding per channel
(the least distance of a colour is `(x + y) - |y - x|` pairs, rounded as written, colours added from the highest index down,
starting bound `32 * |weights|^2`, strict less), writes the winner's six field values and the nearest level of every colour.
- **Tables:** built from scratch on every call, no flag and no cache (the 12 palettes at 0x10e7eb0, the 12 distance blocks of
  0x200 bytes at 0x10e8030, the best score 0x10e9830 and a scratch 0x10e9838). A scan of the image's relocated references to
  0x10e7eb0..0x10e9840 finds only this function (0x10e9840 on is the single-colour fit's own state), so ours are locals and the
  encode-fit tests zero 0x10e8000..0x10e9840 in their snapshots, citing the scan. The channel bit widths {5, 6, 5} are the
  table at 0xe4c3f0, derived (not read) in ours.
- **Quirk reproduced:** when no rounding beats the starting bound (all-zero weights, or counts so large nothing is below it) the
  original reads rounding 3 for every channel, and for the last channel the palette group *below* its own (the middle channel's
  group 3); ours indexes a flat palette array the same way. Forms other than 3 and 4 make the original read an uninitialised
  stack slot, more than 16 colours overflow its tables: ours take the no-winner path and ignore colours past the sixteenth in the
  search; neither is reached (0x899b1f passes 3 or 4 and at most 16 colours).
- **FP:** SSE2 doubles evaluated as written (the original's `ucomisd` zero test is `weight != 0.0`, `cvttsd2si` of the floor goes
  through `_mm_cvttsd_si32`); the floor is the toolchain's. Compared bit for bit, including ours' working doubles (best score,
  palettes, distance tables via the test-only `QuantiseEndpointsTraced`) against the original's globals.
- **Tests:** tests/gz_gimex_fsh_quantise.cpp (`GimexFshQuantise_Quantise` 40,000 cases, `GimexFshQuantise_Stack` 5,000, the original
  gives the same answer with the stack filled 0x00 and 0xff); `GimexFshPlaceEndpoints_Place`, the `GimexFshEncodeFit_*` tests and
  `Gimex_FshRoundTrip` stay green with ours in place. Mutants: 42, 40 killed, 2 equivalent (argued in the status note).
  matchcheck: no match (the work is in a traced inner function; the original's single 6.4 KB body is not reproducible).
- Left: the FSH back end's lanes 5..7 (the GM encoder with block 1's encode leaves, block 1's decode side, block 1's encode side);
  the position and bit-width tables of lane MS (0x105dea0 / 0x105deac) and the VLC / DC / zigzag tables of lane MR are still read
  from the image. In-game: encode a texture with colour blocks (Gimex FSH export) and compare the file with the original's.

**F3c part 7 (lane MT, 2026-10-25): FSH back end lane 4, block 2's block encoders, second half 0x8995da..0x89d7b1.**
9 functions, 10,694 bytes, all hooked at their RVAs and `verified` (db/status/GimexFshCluster.jsonl): principal axis 0x8995da,
farthest-pair axis 0x899a2f, ordering along an axis 0x89d691, SameOrdering 0x89c42d, cluster fits 0x89c480 and 0x89cacf, alpha
solver 0x899d55, PlaceEndpoints 0x899b1f, record header 0x89d7b1. Each is tested alone over seeded input with the stack cleared on
both sides and doubles compared bit for bit; mutants all killed or argued equivalent (FarthestPair j=i+1 and similar; PlaceEndpoints
one no-op slip). matchcheck: only SameOrdering byte matches. Facts: the double expressions follow the asm's association (Middle(q,a,b)
is inexact and kept as written); the alpha solver's `codes` buffer is also an input (first-pass unchanged flag); the cluster fits keep a
best score across calls in hidden state, found harmless; the original returns doubles in x87 and ours in SSE (the FPU is 53-bit, so
equal). The alpha solver has a test-only trace (lower, upper, cost, score) compared with the original's globals, which killed
association mutants the integer outputs hid. Left then: 0x89aa95 (done in part 8). In-game checks: encode a
texture with colour and fitted-alpha blocks (Gimex FSH export), and a record over 16,000,000 bytes (0x80 flag).

**F3c part 6 (lane MS, 2026-10-25): FSH back end lane 3, block 2's block encoders, first half 0x896e1e..0x8995da.**
10 functions, about 10 KB, all hooked at their RVAs (db/status/GimexFshEncode.jsonl); no byte matches (matchcheck v8 ran on
all of them; the size-built codegen differs in frame layout and spills). Difftested alone over seeded synthetic data
(tests/gz_gimex_fsh_encode_colour.cpp 7 tests, tests/gz_gimex_fsh_encode_fit.cpp 5 tests): outputs, return values, every
byte written and the whole image-global window 0x10e8000..0x10ee000 compared. Mutants (tools/mutate.py): 42 (15 over the colour group, 25 over the fits and the driver, 2 for the search cap), 38 killed, 4 equivalent
(argued in the status notes).
Original calls left: the end-point solver 0x899d55 (FitAlpha) and, in FitColours, 0x8995da, 0x89d691, 0x899a2f, 0x89c42d,
0x89c480, 0x89cacf, 0x899b1f, all the second half's.
- Sources: src/gz/GimexFshColourBlock.size.cpp (0x896e1e colour block, 0x8976f6 strip driver, 0x89779a / 0x8978a7 nearest
  colour of four / three, 0x897987 error, 0x897a29 record header), GimexFshAlphaFit.size.cpp (0x897b56),
  GimexFshColourFit.size.cpp (0x897e3b), GimexFshSingleColour.size.cpp (0x8990a2), GimexFshEncodeBlocks.size.cpp (0x8988f3),
  header GimexFshEncode.h. The fits keep their working tables in the image's globals (lists at 0x10ebb48.., the
  single-colour end-point table at 0x10e9858 built on first use, scratch 0x10eb3b0..0x10ebb18), because the second half
  still reads them; the difftest compares the whole window, so ours leaves the same state.
- **The nondeterminism lane MP saw (kinds 2..5 of the encoder), found.** It is in this range, uninitialised stack:
  1. 0x896e1e (the colour block): its second pass, the 3-colour mode (used for blocks with a pixel of alpha below 0x40),
     classifies for each entry the colour the 4-colour pass *chose* for it (`chosen[k]`), not the palette colour. The
     4-colour pass is skipped when the block has a transparent pixel, so for the first pair of end points `chosen` is
     whatever the stack held; entries past the palette's end (their weights, the count bytes, are read too) are
     unwritten, and a block narrower than 4 pixels reads index slots no pixel wrote (in a multi-block call they hold the
     previous block's data). Probe: 40,000 random 4x4 blocks, the original run three times with the stack filled 0x00 /
     0xff / 0x5a: opaque blocks never differed, blocks with a transparent pixel and more than 2 colours differed in 6,580
     of 15,257, with at most 2 colours never. With the stack zeroed the original equals ours over 60,000 cases, which
     proves the cause. Ours zero-initialises the tables (`// ORIGINAL-BUG:` at the `chosen` read) and reproduces the
     bug that is deterministic (the 3-colour middle is `(a + b) * 0x5555 >> 16`, a third of the sum, not half).
  2. 0x897b56 and 0x8988f3 (the fitted 3-bit alpha, kind 0x200 and the callers' kinds above): a candidate list is empty
     when all the block's alpha values are exactly 1, or exactly 0 or 1; the solver 0x899d55 then writes neither end
     points nor codes for it, yet the candidate can win (error 0). The original reads the end points from its stack,
     stale from the previous call or garbage, so the block's two alpha bytes are random. Ours keeps 0 (`//
     ORIGINAL-BUG:`). The tests clear the stack before each call on both sides, and their multi-block cases of kind
     0x200 use alpha 1..254, so the original is not asked for something it does not define.
  3. Colour fit 0x897e3b and the single-colour fit 0x8990a2 read no uninitialised memory that matters (20,000
     cases each; the single-colour fit's test does not clear the stack).
  So the nondeterministic kinds (transparent colour blocks with 3 or more colours, kind 0x200 blocks whose alpha is all
  0 / 1) cannot be made to match the original, and no test needs them to. Gimex_FshRoundTrip is unchanged: its writer
  reaches these functions through the original block-1 callers (0x88bf12 and the compressor), which the harness does
  not hook, so extending it to kinds 2..5 needs those callers first (lane 7, block 1's encode side).
- One thing left unresolved: the Blocks test (6,000 cases through 0x8988f3) failed twice during development, once
  with a one-byte difference at 0x10ebdc8 (the first byte of the solver's threshold array, the original solver's own
  scratch, `0x899d55` writes [0x10ebdc8 + 8k]) and identical outputs; not seen in the 40+ runs after. Likely cause (not
  proven): the solver builds a double from a stack local it never writes and the stack differs between the sides in
  several-block calls. The test now leaves exactly 0x10ebdc8..0x10ebe00 out of the comparison for several-block cases.
- The original's FP: the code is SSE2 double, evaluated as written; the fits are bit-exact over the seeds (comparing
  `memcmp` of doubles, never a tolerance). 0x8990a2 calls the toolchain's `floor` where the original calls 0x838c10 (the
  same function, a library one).
- Left: the second half, 0x8995da..0x89d7b1 (lane 4), which owns the solver 0x899d55 and the orderings 0x89c480 /
  0x89cacf; the position and bit-width tables at 0x105dea0 / 0x105deac are read from the image (derive them for
  standalone); the tests of kind 0x200 alpha empty lists and of modes above 2 (the colour list is then the previous
  block's, stale stack) stay out.

**F3c part 5 (lane MR, 2026-10-25): FSH back end lane 2, block 2's decoders and the RefPack coder.** 29 functions
verified, about 8 KB (db/status/GimexFshBlocks.jsonl 13, GimexFshGm.jsonl 16), all hooked at their RVAs; 0x896db9 is a
byte match (matchcheck v8), the other 28 are difftest-verified (tests/gz_gimex_fsh_blocks.cpp 13 tests, 95 mutants, 89
killed, 6 equivalent; tests/gz_gimex_fsh_gm.cpp 16 tests, 189 mutants, 176 killed, 13 equivalent; all argued in the status
notes). No original calls left in either group (the allocator calls are ours from part 4; memcpy / memset are the
toolchain's).
- Sources: src/gz/GimexFshBlocks.size.cpp (type tests, alpha, colour and block decoders), GimexFshRefPack.cpp (header +
  decoder, `RefPack::MatchLength`), GimexFshRefPackEncode.size.cpp, GimexFshGm.size.cpp + GimexFshGmBits.cpp (`PeekBits` is
  speed-built in the original, so it has its own file), headers GimexFshBlocks.h / GimexFshGm.h.
- Sharing with cRZFastCompression3 (src/rz/RefPack.size.cpp): the decoder body (`RefPack::DecodeBody`, with no room check
  for FSH), `Hash`, `FlushLiterals` and `MatchLength` are the same code, proved by difftest at 0x895620 / 0x896960. The
  compressor 0x89698a is a variant, not the same function: matches are searched only while at least 8 bytes remain and may
  not reach the last 4; the last 4 bytes (plus pending literals) go out as literal runs plus the end opcode; the tables
  come from the codecs' allocator; the 128 KB window is fixed (the window argument is not read). The leak of `heads` when
  the second allocation fails is reproduced (`// ORIGINAL-BUG:`). 0x896db9 writes the 5- or 6-byte header (0x10fb / 0x90fb,
  wide above 0xffffff).
- GM is EA's MPEG-1-style intra / predicted coder and all integer: a bit reader (4 big-endian bytes at bit >> 3, so a stream
  needs 3 bytes of slack), DC size codes (luma / chroma short and long tables), run / level code tables in eight classes by
  the next 16 bits (A..H; flagged alternates for B and C), escapes (intra: 6-bit run, 8-bit level with 0 / 0x80 / above
  special cases; predicted: 6-bit run, 12-bit level), dequantise `(matrix * quantiser * level) >> 3` clamped to
  +0x7ff / -0x800, the Chen-Wang inverse transform (rows then columns, constants 2841 .. 565, 181), YCbCr to BGRA by
  16.16 fixed point constants, and macroblock address increments (stuffing 0xf, escape 8 adds 33). The original keeps its
  state in image globals (0x10e6c60.. and the clip tables at 0x10e6c70 / 0x10e7078); ours is `gGmState` and the tests sync
  the two. The driver 0x89662e walks column-major with plain intra macroblocks when bit 0 of the flags is clear, and
  row-major reading the predicted (bit 1) and table (bit 2) flags when it is set (the first draft had this wrong; the
  difftest found it).
- The original indexes its two clip tables with no range check (inverse transform out-of-range sums, colour terms). Ours
  clamps the index and counts it in `gGmState.clipOverruns`; the tests skip the data comparison for such cases (the original
  reads whatever global sits next to the table, deterministic in the image but not a behaviour worth copying).
- Nondeterminism: none of the functions here is nondeterministic given its input. What they read that the caller may not
  set: the bit reader's read-ahead up to 3 bytes past the last bit, the RefPack decoder reading past a truncated or damaged
  stream (the tests pad with 256 bytes of 0xfc so both sides stop the same way), and the compressor's `cur[bestLength]` probe
  (always inside the source: bestLength <= remaining). The uninitialised-memory dependence lane MP saw was not found here: the
  compressor memsets `heads` to 0xff and only reads `links` slots it has written, and repeated runs over fresh heaps gave
  identical bytes. Look for it in the block encoders (lanes 3 and 4).
- Left: the VLC, DC and address tables, the zigzag and quantiser matrices are read from the image with `GameAddress`
  (standalone needs them derived: the code tables are canonical-prefix lists, the zigzag is the MPEG zigzag, the
  quantiser matrix the MPEG intra matrix; a follow-up with the formulas checked against the tables); the 0x895e3f..0x895e7b
  part of the missed function 0x895dff (added to db/missed-functions.jsonl) is covered by the address-increment test;
  no function of the block 2 decoders is left.

**F3c part 4 (lane MQ, 2026-10-25): FSH back end lane 1, block 1's leaves.** 61 functions verified, 8.4 KB
(0x88374d..0x885825; db/status/GimexFshLeaves.jsonl), all hooked at their RVAs; 10 by byte match (matchcheck v8:
0x883851, 0x8840a3, 0x8840f5, 0x884193, 0x8843fa, the four tile offsets 0x8845f1..0x884687, 0x8847eb), 51 by difftest
(tests/gz_gimex_fsh_leaves.cpp, 61 tests; 146 mutants, 138 killed, 8 equivalent, argued in the status notes). The FSH
glue's six colour calls (info) are ours now (their original_calls are gone).
- Sources: src/gz/GimexFshLeaves.size.cpp (swizzles), src/gz/GimexFshColour.size.cpp (words, colours, sizes, tiling,
  record types, grey / mipmap tests), header src/gz/GimexFshLeaves.h. Names confirmed in Ghidra (`fsh_*`).
- What they are: the PS2 texture layout: 0x88374d is the 16-bit swizzle bit offset; 0x8837a5 / 0x883851 / 0x883bf0 /
  0x883c9c permute one 0x100-byte block (4-bit or 8-bit pixels), the four page functions 0x883876 / 0x883a33 /
  0x883cf7 / 0x883eb9 move 32 blocks (128 x 128 4-bit or 128 x 64 8-bit) between a linear image and a page, and
  0x884b24..0x88521a are the whole-image forms (two 0x2000-byte stack buffers, pages across and down). Each page
  function allocates 0x80 twice and frees both (the tests compare blocks and frees). The permutation tables
  (0x105c2c0..0x105d240, int32) are the image's data and ours reads them with `GameAddress` (no copy in our source);
  standalone needs them derived: the 32-entry ones are the PSMT4 / PSMT8 block orders and their inverse, the 256-entry
  ones the in-block column permutations (a follow-up, with the formulas checked against the tables).
- Words by platform 0x88407b..0x884193 (platforms 5, 7, 0xf, 0x11 are big-endian). Colour words to B, G, R, A (0x8841b0
  2-10-10-10, 4444, 565, 1555, 655, 6666, 3555 (top bit picks 1555 or 3-4-4-4), 88 grey + alpha, two 1555 forms) and
  the packers back; the three luma functions (0x884781 / 0x88486e / 0x8848ce) are SSE2 doubles, 0.587 G + 0.299 R +
  0.114 B summed left to right, rounded by truncate-then-`>= 0.5`; exhaustive over all 16.7 million B, G, R values.
- Decompiler errors found: 0x884767 returns four bytes `[v, v, v, word >> 8]` (the decompile showed three); the
  functions returning narrower than eax (ushort packers, byte, bool: 0x884193, 0x88570d, 0x88578d) set only ax / al, so
  the tests declare those return types.
- 0x885429 is an original bug (kept, `// ORIGINAL-BUG:`): palette entries 16..23 of each 32 all map to 24.
- Record-type pickers 0x8855c0 / 0x88563c / 0x88568b / 0x8856d9 / 0x885801 / 0x885825 map channel bit counts to the
  FSH record types (0x20..0x39); 0x88570d is the encoder's grey test, 0x88578d the mipmap-name test (0x889a2d).
- 0x885849 (4 bytes, `push 0x2a; pop eax`, called only by 0x88b405) is not hookable: the palette-builder lane inlines it.
- Kept out of the tests: negative image sizes (memcpy of a negative count), the page-count masks' negative branch.

**F4, part 1 (lane LY, 2026-10-23): the log service's back ends and formatter, the special folders, the caller
switches.** 46 functions newly verified, 7.3 KB (nGZLog 43 / 6.4 KB, SpecialFolders 2, cTSAppFW 1), all hooked at their
RVAs:
- The callers of DirectoryScan and DynamicLibraryList now call ours (AppServices, cGZDBSegmentDirectoryFiles,
  cParserRun, cTSAppFW, cTSNeighborhoodManager, cTSObjectFolder, DirectoryListing, cTSPersistSystem,
  cTSCustomContentSupport; their status entries lost those original_calls).
- GetSpecialFolderPath, EnsureDirectory and the registered product name (src/app/SpecialFolders.*, tests/app_special_folders.cpp; registry and shell
  calls through impfx).
- cGZLogFilterGroupLevels' slots, cGZLogFormatterText's slots with cLogTimestamp's texts, LookupAlias and MemoryReport
  (tests/gzlog_filter.cpp, tests/gzlog_formatter.cpp; log output compared as text).
- The output back ends (src/rz/nGZLogOutput.cpp; tests/gzlog_output.cpp, 32 mutants, all killed): the base's
  destructors and WriteConfig, cDefaultOutputBackEnd (stream), cGZLogFileOutputBackEnd (constructor, Output with the
  deferred open, Reopen) and cW32CaptionOutputBackEnd (Output, Flush through SetWindowTextA), and the service slots
  21 / 22 that make them. ORIGINAL-BUG kept: the file back end's constructor never stores its path or append flag.
- Service slots 9, 11, 20, 24 with the presets writer and QuoteText (src/rz/nGZLogService.cpp,
  tests/gzlog_service_config.cpp), and the wildcard matcher RZWildcard::Match 0x9ccc0d / MatchCase 0x9ccea3
  (src/rz/RZWildcard.*, tests/rz_wildcard.cpp); cTSEffectsManager::CopyEffectNames now calls ours.

**F4, parts b and c (lane LZ, 2026-10-23): the log service's rest, cRZStd, cRZIniFile's rest.** 40 functions newly
verified, 7.1 KB, all hooked at their RVAs (nGZLog 17 new + 11 moved, cRZIniFile 11, cRZStd 9, cRZStringSub 3):
- The back end class is declared once: src/gz/GZLog.h (group GZLog) is gone, merged into src/rz/nGZLog.h
  cLogOutputBackEnd; its 11 entries moved to db/status/nGZLog.jsonl (src/rz/nGZLogBackEndSlots.size.cpp,
  tests/gzlog_back_end_slots.cpp, was tests/gz_log.cpp). AppDebugger's duplicate declarations of those slots went too.
- The service's rest (src/rz/nGZLogLifetime.size.cpp; tests/gzlog_write.cpp, tests/gzlog_teardown.cpp): slot 23 (the
  timestamped file name) with cLogTimestamp::CopyFrom / ElapsedMilliseconds and cLogProcessInfo's Sample / CpuTime,
  slot 28 (the write path) with cGZLogRecord::Set, FormatText and ReturnRecord, the destructor 0xc9c843 with its
  deleting form 0xc9ca98 and the +4 thunk 0xc9c9e7, the output thread 0xc9e159 and its entry 0xc9e8ab, and
  cRZStdAdapter (destructor 0xc9c829, deleting 0xc9ca79, SetRZCharStr 0xc9e523). The STL instantiations stay the
  toolchain's (14 `mapped` library entries in nGZLog.jsonl).
- cRZStd (src/rz/cRZStd.*, tests/rz_std.cpp): Printf, Assert (the debug level from the framework or
  GZFRAMEWORK_DEBUG_LEVEL), Report, LogLine, Output, the Abort / Retry / Ignore box and its framework-locked form, and
  the environment / debugger helpers; cRZString's Mid / Left / Right (src/rz/cRZStringSub.*). cGZSndSys and
  cTSObjectFolder call ours.
- cRZIniFile's rest (src/rz/cRZIniFileLoad.size.cpp, the tables in src/rz/cRZIniFileTables.h; tests/ini_file_load.cpp
  on sandboxed files): Clear, Open (the 2 s locked-file retry), ReadLine, Load, GetBinary, GetBinaryAlloc, SetBinary,
  ForEachKey, ForEachSection and the hex helpers 0x1afb5e / 0x1afbd7. cRZIniFile is 0x40 bytes (+0x3c the
  open-for-write flag; cTSIniFile lost its mUnknown3c). cTSIniFile's slot 13 and
  cTSAppCommonImpl::ReadResourceConfiguration call ForEachKey directly. ORIGINAL-BUG not reproduced: GetBinaryAlloc
  decodes an odd-length value one nibble past its buffer; ours skips the decode.

**F4, part d (lane MA, 2026-10-24): cRZIniFile's writer, cTSAppFW's property files, the attribute test.** 16 functions
newly verified, 11.1 KB, all hooked at their RVAs:
- cRZIniFile slot 10 SetString 0x9c6bbc (2.6 KB; src/rz/cRZIniFileWrite.size.cpp, tests/ini_file_load.cpp
  IniFile_SetString on sandboxed files): cRZIniFile is now whole (every slot but the shared AddRef / Release and
  cTSIniFile's HasSection). The test also deletes or cuts the files under a loaded object and moves the path
  (SetPath), which is what the stale-index branches need.
- ts::cTSAppPropertyFiles (src/ts/cTSAppPropertyFiles.*, group cTSAppPropertyFiles, 14 functions, 8.4 KB;
  tests/ts_app_property_files.cpp): the 0xf0-byte object cTSAppFW keeps at +0x60 (no vtable, no RTTI: the name is
  ours). It finds the data / Config / user directories, reads savedProps.txt / userSavedProps.txt (the saved key
  names), globalProps.xml, userProps.xml (only when its propertyRevision matches) and Config/globalProps.xml into the
  framework's property-set service (class 0xac1887e), honours propsNoSave / propsNoLoad / cardConfig / propertyList,
  and writes the saved keys back to userProps.xml. The structure pass corrected one row: 0xcb0f40 was named
  cTSAppFW::LoadGlobalProperties (tentative); its ecx is the +0x60 object, and it sets that object's paths.
  cTSAppFW (Init, PreFrameworkInit, DoMessage 0xcd, Shutdown) calls ours; its member is now typed (mPropertyFiles).
- PathAttributesMatch 0x1c1931 (src/rz/PathAttributes.*, group PathAttributes): the cdecl attribute test (not
  read-only, directory, shortcut, archive, hidden, system, the read-only answer) through the game's wide
  GetFileAttributesW pointer or GetFileAttributesA. cTSObjectFolder and cTSPersistSystem call ours; the rollback
  segment's two callers (0x1d2d50, 0x1d4220) reach it through the hook.

**F4, part e (lane MB, 2026-10-24): cTSAppCommonImpl's and cTSAppFW's helpers, the product info, ObjectSaveTypeTable.**
26 functions newly verified, 11.6 KB (ObjectSaveTypeTable 7 + GUIDTranslation 1, 2.7 KB; cTSAppFW 9, 3.0 KB;
cTSAppCommonImpl 3, 3.6 KB; ProductInfo 5, 1.9 KB; WinMain 1); 83 mutants, 81 killed (two after test fixes), 2
argued equivalent (in the entries):
- cTSAppCommonImpl's registry callbacks (src/ts/cTSAppCommonImplRegistry.cpp, tests/ts_app_common_impl_registry.cpp):
  OnPathRegistryEntry 0xca5170 (the CN_PDBS path-registry walk that builds DB segments: packed file, directory files,
  multi-packed files), OnResourceFactoryEntry 0xca5db0 and AddPathSegment 0xca4f70, all hooked;
  RegisterRegistryHandlers registers ours. ORIGINAL-BUG kept: a directory segment that fails to open always counts as
  added (the DirectoryScan meant to accept an empty directory is never scanned).
- The product info (group ProductInfo, src/app/ProductInfo.*, tests/app_product_info.cpp, registry through impfx):
  GetProductPaths 0xcb4690, ListProductExecutables 0xcb4aa0, InstalledProductDirectories 0xcb4bb0,
  NewestProductExecutable 0xcb4950, FindNewerProduct 0xcb4cc0. The structure pass put them in no class: cdecl helpers
  over the product registry keys, called from cTSAppCommonImpl::FindProductDirectories and cTSAppFW::Init (both
  ours, which now call these by name). 0xcb4bb0 was listed under cTSAppCommonImpl in the row; the code has no this.
- cTSAppFW's helpers (src/ts/cTSAppFW.size.cpp, src/ts/cTSAppFWHelpers.*, tests/app_fw_helpers.cpp): the members
  LaunchExceptionUploader 0xc28b7d, ReportFatal 0xc28de0, AddCrashData 0xc28e32, UnlockNeighborhoodFiles 0xc2a3d6
  (the row's "apply state": unless noIffUnlocking is set or given, it hands each of the neighbourhood's matching files
  to the package service's unlock slot), and the cdecl helpers
  AppendEditionSuffix 0xc27961 (the row's "expand"), BodyShopProductName 0xb417c0, ProcessPagefileUsage 0xc27b49,
  SpawnAndWait 0x1b1a3f and IsUserAdministrator 0xc2896d; WinMain's AskYesNo 0xc2dbf1 (the row's "question box").
  Init, Shutdown, DoMessage and PreFrameworkInit call ours. ORIGINAL-BUG kept: the uploader places its later
  switches at argv[2 + noUpload] with the raw byte.
- ObjectSaveTypeTable (src/ts/ObjectSaveTypeTable.*, tests/ts_object_save_type_table.cpp): the save type table the
  folder builds on its stack to read or write the lot's object type records ('objt', version 0x55): Read 0x4d4c30
  (each record's GUID through the lot's GUIDTranslation, the selector found or a cTSMissingObject added to the
  folder's +0x18 list, the tile flags, string counts and field checks setting the save mismatch), Write 0x4d7040,
  WriteEntry 0x4d7180, Release, GetGZCLSID and both destructors. A real class deriving cIGZSerializable; the folder's
  ReadSaveTable / WriteSaveTable build ours, so the class is in effect through its callers. The destructor body
  0x4cf490 is hooked (an adapter): it is the only code storing the game's vtable 0xdcd0c0 and is reached only from
  the original folder functions' unwind funclets, which callers.py can't attribute, so the slots' traps rest on it
  (`Hooks.ObjectSaveTypeTable_Destructor=1&Hooks.cTSObjectFolder_Create=1`). GUIDTranslation::Resolve 0xc0b617 is
  hooked (its only caller is ours).

**F5, part 1 (lane MC, 2026-10-24): the script interpreter, the control set, the legacy control's frame, two small
helpers.** About 9 KB newly verified, every class in the standalone shape or hooked at its RVAs; mutants 99, 95 killed, 4 argued equivalent (terp and vocab 40/38, set 15/15, ReadScriptNode 6/6, legacy control
23/22, filter 8/7, NotificationHelp 7/7; the reasons are in the status entries):
- cGZScriptTerp and cGZScriptVocab (src/gz/cGZScriptTerp.*, tests/gz_script_terp.cpp): the UI script language's
  interpreter (COM class 0xba2e7953, creator 0x859fe1) and its name table (0xba2e7954, creator 0x859ff7), whole: the
  tokenizer, the parser's state machine, the entity decoder, RunFile / RunResource, the vocabulary stack, the cGZArray
  template both use (an attribute list is a cGZArray of {id, cIGZUnknown *value} pairs). The creators are hooked, so
  every terp and vocabulary is ours. ORIGINAL-BUG kept: a quote opening at the end of the text releases the token
  but leaves it in the out argument.
- cGZScriptableControlSet (src/gz/cGZScriptableControlSet.*, tests/gz_scriptable_control_set.cpp): the service's
  result object (0x5a354595, creator 0x85b2c9), whole, with the control interfaces it calls (cIGZScriptableControl,
  cIGZRenderControl, cIGZLegacyControl) typed in its header.
- cGZUIScriptService::ReadScriptNode 0x85bc39 (tests/gz_ui_script_read_node.cpp): the interpreter made on first use
  with the markup and tag names (read from the image at run time).
- cGZLegacyControl's frame (src/gz/cGZLegacyControl.*, tests/gz_legacy_control.cpp): the class (0xfa450242, creator
  0x85b2b3 hooked, so every control is ours), its 18 own bodies, 7 thunks and 3 folded slots, and
  FindUint32Attribute 0x86401e (hooked: F6's handlers call it). Create is the window builder: see "F6 handoff".
  ORIGINAL-BUG kept: Create drops a window from an earlier Create without its Release.
- cInputEditFilter (src/gz/cInputEditFilter.*, tests/gz_input_edit_filter.cpp) and nRZSTD::cNotificationHelp1
  (src/rz/cNotificationHelp1.*, tests/rz_notification_help.cpp): built inline by code that is not ours (0xcaaed0;
  25 classes), so their bodies are hooked at their RVAs: the filter's QueryInterface, Filter and deleting
  destructor; the helper's Add / RemoveNotifications (every id of its std::set on the message server).

**F5, part 2 (lane MD, 2026-10-24): the properties every window has, and LoadUIScript.** About 9.5 KB newly
verified (12 functions), all hooked at their RVAs (the window classes' handlers and about 150 UI classes call them):
- The legacy controls' attribute readers and common properties (src/gz/cGZLegacyControlProperties.size.cpp,
  tests/gz_legacy_properties.cpp, 4.6 KB): FindTextAttribute 0x863fae, FindPairAttribute 0x863e87,
  FindColorAttribute 0x863e06, HasAttribute 0x864077 (byte match), LoadLocalizedText 0x86dceb, SetResourceKeyProperty
  0x87b35c and ApplyCommonProperties 0x86549d (the row's FormatPropertyValue: flags, id, area / size / position,
  caption and caption key, the tooltip list cGZLegacyTextList (its Release 0x86de0b), typed properties
  "id,type,value"). The legacy control's Create calls ours. Mutants 30, 28 killed, 2 argued equivalent.
- LoadUIScript 0xb41b50 and its helpers (src/gz/LoadUIScript.*, tests/gz_load_ui_script.cpp, 4.9 KB):
  EvaluateWindowCondition 0xb40d20 (a window's condition property: "name=value" / "name!=value" terms joined by and /
  or, settings compared through the cheat console's converter, other names asked of the message server),
  ApplyScriptConditions 0xb42250 (flags 1 / 2 and the notify message; cTSMainToolbar's OnTick calls ours),
  UpdateScriptScreenSize 0xb429b0. Mutants 44 (43 on LoadUIScript.cpp, 1 on the toolbar's switched call): see the
  status entries for the counts and the equivalent ones.
- F6 part 1 (lane ME): the 22 registrations of cGZLegacyControl are ours (src/gz/cGZLegacyControlRegistrations.size.cpp,
  tests/gz_legacy_registrations.cpp, about 25 KB of originals). Data-driven: one `Register` body and 22 constexpr
  tables (a row per name: image-string RVA and id; a header with the counts, the handler RVA and the class / type
  ids). The names are read from the image by RVA; the maps are filled in place with `try_emplace` (an existing key
  keeps its value; the game's nodes and ours share the process heap). The handlers (0x864376 and the region
  0x866410..0x86dbb5) stay original: the tables store their RVAs. 22 difftests plus a sequence test (40 seeds of
  pre-filled maps, a recording vocabulary), 12 mutants all killed. Not byte-matchable: the probe on 0x85da3d reached
  0.485 at best (the original has a plain esp frame with EH-state stores where ours emits __EH_prolog, and a call to
  0x85ca6e where ours inlines `_Try_emplace`). Status: verified, in effect through the constructor (trap
  `Hooks.cGZLegacyControl_Create=1`).
- F6 part 2 (lane MF, Sonnet trial on logic): the Bitmap handler ApplyBitmapProperties 0x864376 (584 B) and its three
  helpers FindRectAttribute 0x863ee9 ("(a,b,c,d)", true on four numbers), LoadImageResource 0x86dc44 (an image
  resource 0x856ddbac by group / instance: the UI script context's slot 3, else the resource manager) and
  SetImagePairProperty 0x86df76 (src/gz/cGZLegacyHandlers.*, tests/gz_legacy_handlers.cpp). The Bitmap registration
  now stores our function (`LegacyRegistration::ours`, handler RVA 0); tests/gz_legacy_registrations.cpp compares
  that one by identity. Face (iid 0xc12cea13, `cIGZWinBitmap`): SetImage 4, SetAlpha 6 (byte, float), SetImageRect 8,
  SetFlag 11 (mask 1 notify, 2 transparent back, 8 edge image, 0x10 image rect); the image's GetWidth 9, GetHeight 10
  (called height first). Quirks kept: the image text is parsed unchecked and the instance keeps the last attribute
  value read (the edge-image flag's, else 0); a text with no number leaves the group as stack garbage in the original
  (ours 0; the tests never feed one).
  **Left for F6:** the other 21 handlers in 0x866410..0x86dbb5 and 0x8645be.. (added to db/missed-functions: Slider
  0x86aa7f, Button 0x8645be, Combo 0x865043, FileBrowser 0x866410, FlatRect 0x8674f6 [500 B, fully read: face iid
  0xc2afa76f, slot 12 takes the OR of the 0xf005 style bits, slot 10 is (index, r, g, b) for colours 0x200..0x203 via
  FindColorAttribute; the original pushes colour dwords whose high bytes are stack garbage, to be masked by the
  callee], Outline 0x86a2a5, Folders 0x8676ea, Scrollbar 0x86a820), and cGZDummyWinProc (built inline at 0x86da0c,
  probably by the Gen handler 0x86d5ee).
- F6 part 3 (lane MG, Sonnet 5.5): ten more handlers, each verified by a difftest against the original with 13-30
  mutants killed: FlatRect 0x8674f6, Text 0x86b2be, Slider 0x86aa7f, Scrollbar 0x86a820, Outline 0x86a2a5, Scrollbar2
  0x86a4a8, Folders 0x8676ea, Combo 0x865043, ListBox 0x868e93, LineInput 0x8688a3, plus the readers FindSint32Attribute
  0x863f55 and LoadImageByKey 0x86dbb5 and the setters SetUint32WindowProperty 0x86df41 / SetTextWindowProperty 0x86df0c.
  Their registrations store ours (handler RVA 0). The UI defaults service (GetCachedServiceObjectB, cell 0x10f51b0:
  slot 5 GetFont, slot 38 FindFontId) is modelled in the test harness as gDefaults. Quirks kept and avoided in tests:
  unchecked sscanf results (Folders, Spinner image) and unchecked null values (0x502 in Folders, Combo, ListBox).
  **Left for F6 after part 3:** Spinner 0x86ac57 (analysed: face iid 0x612ce0c3; its colour chain 0x1006..0x100a
  threads defaults through stack slots), TextTicker 0x86be58, Button 0x8645be (2693 B), TextEdit 0x86b5b0, OptGrp
  0x86942b, FileBrowser 0x866410, TreeView 0x86c19e, Grid 0x867920, Gen 0x86d5ee (with cGZDummyWinProc inline at 0x86da0c).
- F6 part 4 (lane MI, Sonnet 5.5): Spinner 0x86ac57, TextTicker 0x86be58, Button 0x8645be and TextEdit 0x86b5b0, plus its
  flag table reader ApplyFlagTable 0x863aa8, each verified by a difftest (8, 7, 9, 18 mutants killed; the flag table
  shares TextEdit's). Faces cIGZWinSpinner / TextTicker / Button / TextEdit are in src/gz/cGZLegacyHandlers.h. Test
  additions: a kept-text refcount check (TextTicker's message text), a per-test setup hook and attribute fixup.
  Inputs the original reads uninitialised (unchecked sscanf, unchecked FindUint32) are kept out of the test pools.
  **Left for F6 after part 4 (F6 is NOT complete):** OptGrp 0x86942b (decoded: face iid 0xa1336cc0, slots 5, 6, 8, 9, 14,
  22, 23, 25-29, 36; second pass over 0xf0d / 0xf11 / 0xf12 uses SplitOff and sscanf), Gen 0x86d5ee with cGZDummyWinProc
  (vtable 0xe49270, slots AddRef 0x85da25, Release 0x86de9e, slot 4 0x863b3e, deleting dtor 0x85d9e6; the AddRef/Release
  test needs the COM director faked on both sides as FakeStaticDirector does), FileBrowser 0x866410, TreeView 0x86c19e
  (std::map, UtilObjectB), Grid 0x867920 (flags 0x1609..0x1622, heap objects with vtables 0xe49b9c / bac / bbc).
- F6 part 5 (lane MK, Sonnet 5.5): OptGrp 0x86942b, Gen 0x86d5ee with cGZDummyWinProc (HandlesMessage 0x863b3e, deleting
  destructor 0x85d9e6, AddRef / Release through the COM director faked on both sides), FileBrowser 0x866410 (original call
  0x87b143, stood in on both sides) and Grid 0x867920 (original calls 0x863ba3 cell parser, 0x85d3bc matrix constructor,
  0x8640a4 / 0x86422c matrix setters, stood in on both sides; the matrix wrappers carry the original vtables 0xe49b9c /
  0xe49bac / 0xe49bbc). Each hooked and verified by a difftest with mutants (counts in the status entries; Grid 59, all
  killed or argued equivalent). The Grid face is the window's primary interface (its slot 5 returns this + 4, the window's own interface, whose
  slot 121 converts a colour); the test gives the window fake a private slot table with slot 121's bytes as kind 'b'
  (tests/fx_slots.h is generated; lane MM recorded the fact, cIGZWin slot 121 ConvertRGBToNative with three bytes,
  regenerated the header and dropped the override). Three locals the
  Grid handler never sets (the last flag number with no flag attribute, and the first cell's group and instance before a
  scan) are read by the original; ours uses zeros and the test keeps those inputs out (ORIGINAL-BUG notes in
  src/gz/cGZLegacyHandlers5.size.cpp).
  **Left for F6 after part 5 (F6 is NOT complete):** TreeView 0x86c19e (done in part 6, below).
- F6 part 6 (lane MM, Sonnet 5.5): TreeView 0x86c19e (5200 bytes) in src/gz/cGZLegacyHandlers6.size.cpp, the last handler
  of the list: hooked, registered as `ours`, verified by LegacyHandlers_TreeView (12000 seeded cases, apply true and false)
  with mutants (see its status entry). The allocation helpers 0x85a1eb / 0x85d423 / 0x85d403 / 0x85d581 are not a
  private class: they are standard-library instantiations (map and list constructors, the node record's constructor, the
  map insert wrappers 0x85d2c6 / 0x85d280 / 0x85d25d / 0x85d2a3, list insert and erase 0x85cd2a / 0x85d011 / 0x86e35d),
  so ours uses std::map / std::list with a key type laid out as the game's cRZString and the test compares the maps as
  raw trees (colours, parent links, keys, values). The handler keeps six tables on the window as uint32 variants (map
  addresses, never freed); ours allocates them with our CRT: **check in game that nothing frees them with the game's
  allocator**. Original quirks (ORIGINAL-BUG in the source): the position word of a node that is not first / last /
  sorted keeps the previous node's (the first node's is a stack leftover; the test keeps those out).
  **F6 is complete as far as the handler list goes:** every handler the 22 registrations store is ours. Part 6 also
  made Grid's cell parser ParseGridCell 0x863ba3 ours (src/gz/cGZLegacyGridParts.size.cpp, LegacyHandlers_ParseGridCell
  with 15 mutants; the Grid test stands it in on both sides) and recorded the cIGZWin slot 121 fact.
- F6 leftovers (lane NH): the originals' calls the Grid and FileBrowser handlers still made are ours now, so no F6
  handler calls an original function any more (their `original_calls` are empty).
  - **The grid's matrices** (src/gz/cGZLegacyGridMatrix.h / .size.cpp; the original class is a `cRZSparseMatrix`, named in
    the db): constructor 0x85d3bc, cell setter 0x8640a4 and description setter 0x86422c, hooked at their RVAs (the
    constructor has a second original caller, 0x940ae3). Layout (0x20 bytes): a `std::map<int, Line>` at +0 (MSVC 14.40's,
    head node pointer then size), the value count at +8, the accepted line range at +0xc / +0x10 (default
    0x80000001 .. 0x7fffffff), the entry range at +0x14 / +0x18, a flag byte at +0x1c (the constructor writes 0; +0x1d..0x1f
    untouched). A Line (map node 0x20 bytes) is {its index again, `std::map<int, Entry>`}; an Entry is {its index again,
    the value}: a cell (0x44 bytes: group, instance, flags, a cRZString at +0xc, font id, five font words; node 0x5c) or a
    column / row description (three ints; node 0x24). The setters take (entry key, line key, value, previous out): the cell
    setter's entry is the column and its line the row; a column description goes in under (its first number, 0), a row
    description under (0, its first number). They return false when either index is outside its range (signed), else
    create the line if needed, insert the entry (count + 1) or copy the old value to `previous` (if given) and overwrite it.
    Cell copy follows 0x85d59d (the cRZString copy constructor 0x141eb3) and assignment 0x85d8b2 (`assign(data, size)`,
    never to itself). The containers are the toolchain's std::map, so ours are `std::map` with the same node shapes and
    the original's holder destructors (vtables 0xe49b9c / 0xe49bac / 0xe49bbc; matrix destructors 0x85d901 for cells and
    0x85d928 for the descriptions) free ours: one heap. The original builds a line's or a cell's value on its stack
    and copies it into the node, so it allocates and frees one more temporary per call; ours builds in place (the test
    compares what stays allocated, not the number of allocations made).
  - **AppendFileBrowserKey 0x87b143** (cdecl; window, property id, index, 16-byte key): the file browser's entry list is a
    window property holding an untyped byte variant of 16-byte keys. Absent: a new variant with just the key. Present: the
    key is inserted at `index` (0 .. length) by rebuilding the bytes in a 32-entry buffer (a heap block from 32 entries in
    the result, the original's small-buffer helper 0x87b2ac) and SetValVoid; false (variant untouched) for an index outside
    0 .. length or a variant without bytes. Tested over absent, empty, plain-number, whole and ragged byte lists of 0 to 34
    keys, including the heap-buffer sizes freed.
  - Tests: tests/gz_legacy_grid_matrix.cpp (Construct, SetCell, SetEntry: both sides construct with the original, then
    the same run of setters on small and extreme indices with narrowed ranges; raw tree dumps of every node, previous
    values, still-allocated counts, original destructors taking the matrix apart), LegacyProperties_FileBrowserKey, and the
    Grid and FileBrowser handler tests, which no longer stand the helpers in (the Grid test dumps the matrices node by
    node after each run).

**F7 (lane MJ, 2026-10-24): the library pass.** Tooling only. tools/stlprobe builds every source under six flag sets
(adds `/EHsc /O1 /GS-` and `/O1`, `/O2` without exception handling), a new probe_ios.cpp (the iostreams), the size
sweep over a stand-in for the game's allocator-service allocator (`ProbeAlloc`) and std::map's own members apart from
`_Tree`; libmatch reads COFF section numbers unsigned (objects with over 32767 sections lost functions: all the size
probes did), keeps probe deleting destructors only when their calls confirm them, and drops short probe shapes nothing
confirms. db/library-matches.jsonl: 7977 -> 8365 rows, function bytes 1,633,793 -> 1,664,232 (+30.4 KB: 142 named
rows 9.0 KB, 245 family 20.0 KB, 20 family_likely 1.8 KB; 19 short unconfirmed rows, about 370 B, dropped). Of the
brief's groups (by name): crt-glue library-named 1,167 / 1,957 B, foundations 344 / 2,639, unassigned 1,358 / 10,785;
much of the new code was in other groups. What stays unmatched and why: db/README.md (Notable facts); the game-named
groups (`cGZPropertyManagerT`, `cRZRefCountedT`, `c3DArray`, `GUIDTranslation`) are EA code, not library. 15 new
library rows land on status entries (verified game-side entries in StdGameTypes, WinMain, nGZLog, cGZAny,
cGZRegistry, cGZDBSegmentPackedFile; StdStringHelpers was settled by lane WM, the group is gone): libmatch lists them; settling them is the coordinator's.

Already ours before the campaign: the startup classes (startup.md), the
allocators, cRZFile, cRZIStream / cRZOStream, cRZFileName, cRZCmdLine, cRZString's slots, cIGZAny, the message
servers, cGZTimerService, cRZCallbackSystemService, cGZUIScriptService, cGZClipboardManager, cGZRealtimeDebugDisplay,
the command parser (src/gz, src/rz).

## Conventions (found in F2)

- **cRZVariant layout** (src/rz/cRZVariant.h): cIGZVariant vtable at +0, then tagRZVariant (pack 4) at +4: the 8-byte
  payload union (+4; the arrays' pointer), the reference count (+0xc, uint16), the type tag (+0xe, uint16; 0x8000 =
  untyped bytes, 0x8000 | kind = array of the kind, 0x800e text) and the array count `mCount` (+0x10; bytes for
  0x8000). Size 0x14. Old code reading `mBufferSize` now reads
  `mCount` (tests/ts_effects_resource.cpp changed only that).
- **Slots as 5-byte jmp stubs**: most typed setters (slots 12 ... 63) are `jmp` stubs to shared helpers (StoreUint8 ...);
  hookspec refuses the stubs, so hook the helpers. Two of them (0x9cb442, 0x9cb466) are mislabelled in names.jsonl as
  std::_Hash::insert library code: they are cRZVariant's stubs. Check a "library" label on anything a vtable reaches.
- **Classes whose vtable an unowned constructor still installs**: when one constructor stays the image's (here 0x9c7b3d,
  hooked by another group), every slot body must still be hooked to an adapter, or game-built objects run the original
  slots. Once that constructor is ours, the slot hooks become belt and braces.
- **cRZString locals in our code**: wrap them in a union (`LocalString`, src/app/AppServices.size.cpp's precedent):
  cRZString holds a std::string, and Destruct plus the implicit destructor would free it twice.
- **Original bugs reproduced** (`// ORIGINAL-BUG:`): TextOf answers false for most kinds; FromString gives the
  character kinds 0, not the parsed value; FromString into an unknown leaks the old unknown.
- **Original crashes left out of tests**: ToString on an array tag with a null array and a non-zero count reads
  through null (0x9c767c); the tests build arrays with count 0 when null.
- **cGZBuffer layout** (src/gz/cGZBuffer.h, static_asserted): faces cIGZBuffer +0, cIGZBufferShader +4, cIGZAggregate
  +8; ready flag +0xc, colour type +0x10 (format, bits), bounds +0x18 (left, top, right, bottom), transparent colour
  +0x28, flags +0x2c (bit 1 transparency), ref count +0x30, change counter +0x34, shader colour +0x38 (0xffffffff
  unset), 16-bit lock count +0x3c, bytes per pixel +0x3e, pixels +0x40, stride +0x44, lock flags +0x48, hardware cache
  +0x4c, outer +0x50; size 0x54 (pool 0x105f82c). cGZTransientBuffer adds the cIGZTransientBuffer face +0x54, target +0x58, memory owner +0x5c,
  lock total +0x60, external-memory flag +0x64; size 0x68. Formats: 4 / 6 1555, 5 565, 7 4444, 8 / 9 8888; 3 and 1
  ask the display (16 bits: 4444, more: 8888).
- **Original bugs reproduced in cGZBuffer**: ClipRects' clip step compares edges by a wrapping subtraction, so a
  rectangle more than 2^31 from the clip's edge takes an edge outside the buffer and the fill writes past its memory
  (0x8c658e); the test leaves wild rectangles against a clip out. DrawLine's thick lines draw 16-bit squares at any
  depth; once one square near an edge needs the checked path, every later square takes it (the loop stays in that
  branch).
- **Dead original constructors**: the transient constructor 0x8c7190 and the two non-deleting destructors 0x8c5790 /
  0x8c71d0 have no reference in the image (inlined into the creators and deleting destructors). A callers entry needs a
  trap, and a trap must name exactly the hooks its check rests on (none here), so they get no entry; the constructor
  0x8c5710 lists the call from 0x8c7190 in `callers_handled`. Tooling gap for the coordinator: a trap form for
  unreferenced code.
- **Heap checks in difftests**: HeapWalk does not see small (low-fragmentation heap) blocks reliably; check busy / freed
  only for blocks of 0x4400 bytes or more, hash the pixel memory, and reserve the event log up front so its growth never
  reuses a freed block.
- **svcfx::Norm on data is a flake**: CaptureFakes and the fakes' argument log normalise every word, so a random data
  value that falls inside one side's tracked heap block reads as a pointer on that side only (GZBuffer_Run failed about
  one run in six). Keep random fake values out of the user address range (`r() & 0xffff`, `0xff000000 | ...`) and
  normalise only pointer fields in object dumps.
- **Creators registered by address are missed functions** (lane LS): the language manager's creator 0xc6f338 had no
  record, so callers.py saw its call to the constructor as "outside any known function" and the traps rested on no
  hook. `tools/missedfuncs.py --add 0xRVA "how it is known"` records it; check a hooked creator has a record.
- **Jump tables after a function read as code**: a table entry's bytes can decode as a jcc to a nearby function
  (0xc7132e: entry 0xa0c71275 of 0xc711c2's switch "jumps" to 0xc71342). List it in `callers_handled` with the table.
- **cGZBufferFontRenderer's glyph** (0x24 bytes): destination rectangle +0, coverage bitmap pointer +0x10, pitch
  +0x14, width +0x18, height +0x1c, code +0x20. ClassesRound4.h's names for +0, +0x10, +0x14 (mBitmap, mPitch,
  mUnknown14) are wrong. The blend table is game data (0x10ecd10, [colour][destination][alpha], built once: the flag at
  0x10ecd00), shared by the original and ours.

## Conventions (found in F1 part 2)

- **The game's STL is ours**: the queues' std::list, the connection manager's std::unordered_map of std::multimap and
  the trees' node layouts match MSVC 14.40's, and the destructors free the same block sizes (the tests compare them).
  A class built on std containers can use them as members and keep the game's object size.
- **Event connections** (src/gz/cConnectionManager.h): every object is reduced to its behaviour face
  (QueryInterface 0xfb859324, the object itself on refusal; the added reference is never released). Connect and
  Disconnect tell both ends through behaviour slot 3 with (0x5b51e806, 0, the cConnectionChangedEvent, 0). A
  behaviour's slot 5 (cAbstractBehaviorTypeT) walks ConnectionsFrom(this, kind) and calls each target's slot 3 with
  (target event, a, b, this): the next lane's cAbstractBehaviorTypeT instances call our manager.
- **Iterator ranges** ({cursor, begin, end}, each counted; the cursor a clone of begin) come from the template
  constructor 0x1a98e0 shared by eight instantiations: reimplement it locally rather than hook it.
- **Original bug**: Disconnect of a missing connection erases the next connection of each map, or corrupts the tree
  when there is none (ours guards that case; `// ORIGINAL-BUG:` in cConnectionManager.cpp).
- **Slot signatures on the original side**: a test calling the image through a declared interface unbalances the
  stack if a slot's argument count is wrong (the null iterator's slot 9 takes an index); check `ret N` with
  `classinfo.py --disasm` for every slot a test calls.

## Conventions (found in F1 part 3)

- **Message server and graphics system caches**: GetMessageServer (0x9c5ab4, cell 0x10f5198) and GetGraphicSystem
  (0x9c5a05, cell 0x10f51a4) are ours and hooked, with their acquisitions 0x762db4 / 0x7ae7f4: call them directly
  (src/gz/cGZClipboardManagerClipboard.h, src/gz/cRZCursor.h). A test that stands in for 0x9c5ab4 with a code patch
  no longer reaches our callers: set the cell (or the framework) instead.
- **Win32 window procedures**: a class that registers a window class registers our procedure; the test captures the
  procedure from the RegisterClassA fake (its logged word replaced by a marker) and calls each side's with the window
  the CreateWindowExA fake hands out, the user data kept by the SetWindowLongA / GetWindowLongA fakes.
- **Text conversion in tests**: RZText::Convert calls MultiByteToWideChar / WideCharToMultiByte directly, the original
  through its imports, so only the original's call reaches impfx. Fake the two imports as passthroughs that pop their
  own log entry and run the real conversion (tests/gz_clipboard.cpp), then non-ASCII code page 1252 text compares.
- **Dead branches on DefaultEncoding**: RZText::DefaultEncoding is UTF-8, so the original's UTF-16 default-encoding
  branches (the viewer's read and write) never run; leave them out with a comment.

## Conventions (found in F1 part 4)

- **map::emplace in MSVC 14.40 looks the key up first**: `emplace(key, value)` with an argument of the key type finds an
  existing key without building a node, where the game's (older) map built the node, copying the value, and dropped it.
  When the value's copy has side effects (cInputDeviceMap's stored events clone a body), use
  `emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(value))`, which builds first.
- **Event bodies** (nGZEvent::cIEventHeader, src/gz/cInputDeviceMap.h `cIEventBody`): slot 4 is the bytes a copy takes,
  slot 6 builds a copy in given memory, slot 0 destroys without freeing. A test fakes them as a C++ class that logs
  every size query, clone (numbered per side) and destruction.
- **A behaviour as a target**: code that only calls OnQuery (slot 4) can be tested with a hand-made vtable whose slot 4
  logs and answers values with only a high byte set (0x100, 0xff00); the original tests the answer as a byte.
- **Switching a caller to our callee whose test stood in for the original**: stand in for ours too (shape::StandIn, or a
  second CodePatch over `shape::CodeOf(&Adapter)`), or our callee runs for real on our side only.

## Notes for F3-F6

- F3 / F4 read and write variants (property sets, ini values): cRZVariant is now ours, so call our class directly; the
  four text parsers above are the original-calls to take first (they are short and shared).
- F5's legacy-control property tables hold variants: FormatPropertyValue can use cRZVariant::ToString directly.

## Conventions (found in F3)

- **cRZString held by value in our containers**: a one-member union around cRZString with a constructor that calls
  Construct and a destructor that calls Destruct (dynlib::Text, residext::Extension, the GZSndSys precedent); the game's
  copy is Construct(other, 0, npos). A type the original only builds in place (the map nodes) gets no copy constructor
  (`= delete`): a never-called copy is a mutant that can't be killed.
- **Embedded objects built inline by another class's constructor** (cGZRESIDtoExtensionMap): hook every slot body at its
  RVA with qualified adapters, and test three routes: the original slots on the game's vtable, ours through our
  vtable, and the adapters on an object with a copy of the game's vtable whose hooked slots trap (the slots its own
  body calls back through `this`, here AddExtension and Clear from CopyFrom, hold the adapters). Check the trap count
  after the destructor too: an adapter that destroys through the virtual destructor hits only there.
- **cRZFileName in tests**: the original's GetName reaches GetCurrentDirectoryW (an import trap); stand in for the
  constructor and GetName on both sides (shape::StandIn) with a fake that fills the name.
- **Ini file walks** (cTSIniFile section walk): stand in on both sides with a fake that calls the given callback with
  scripted lines, so the line callbacks are reached on each side's own code.
- **The game's CRT in the harness**: _strnicmp (0x84479b) runs as the original's (C locale) without help.

## Notes for F4

- DirectoryScan is ours (src/rz/DirectoryScan.h): these still call its members through OriginalFunction and can switch
  to the class: src/app/AppServices.size.cpp, src/gz/cGZDBSegmentDirectoryFiles.{h,cpp}, src/gz/cParserRun.cpp,
  src/ts/cTSAppFW.size.cpp, src/ts/cTSNeighborhoodManagerLoad.cpp, src/ts/cTSObjectFolder.cpp,
  src/ts/DirectoryListing.cpp (their tests stand in on both sides or run the original; `status.py --check` notes list
  each site). Its game-side users that are not ours yet: 0x1c4a73, 0x1c5fa0, 0x1c628f, 0x1c5736, 0x1c50d6, 0x1c5031.
- DynamicLibraryList is ours (src/ts/DynamicLibraryList.h); cTSAppFW::AddDynamicLibrariesHere still drives it through
  OriginalFunction (its local struct). Switching changes the slot's result: the original returns the list destructor's
  al (operator delete's leftover eax), ours 0; check what tests/app_dlls.cpp compares before switching.
- 0x1ad622 (ownerless, 30 callers; named cRZString::SplitAt): `SplitAt(char delimiter, cRZString *head)`, ret 8: head
  = the text before the first delimiter, this = the rest after it; with none, head is emptied, the two swap and the
  result is false. DynamicLibraryList has a local copy; the cRZString owner should take it and hook it.
- Gimex (lane LX): the factory, reader and writer are ours and reached through the framework's class objects (the
  factory's iid 0x02ffbb60, reader class 0x2a316c02, writer 0xca316c55), so no caller needs switching. Code that
  wants an image read or written should ask the factory (cIGZGimexFactory, src/gz/cGZGimexFactory.h), not the
  codecs. The factory keeps ijl15.dll loaded for the process (0x10e6930, never freed; the codec's own load is a
  second handle in 0x10e6948). Its statics 0x10e6924 / 0x10e6928 cache the resource manager and graphics system
  with no reference held: a shutdown that releases those services leaves them dangling (as the original).
- cGZDBSegmentDirectoryFiles's constructors and destructor build and destroy its cGZRESIDtoExtensionMap inline (the
  game's vtable): when that class goes to the standalone shape, make the member a cGZRESIDtoExtensionMap (our vtable)
  and the slot hooks can go (Dropping adapters).

## Conventions (found in F4)

- **Back end and service fakes**: one logging fake type with a shared vtable covers streams, filters, formatters, the
  framework and windows (tests/gzlog_output.cpp); where slot numbers clash (an output's slot 8 is GetFilter, a
  filter's is Clone) use one vtable per kind and one world of fakes per side built from the same seed
  (tests/gzlog_service_config.cpp). Log objects by id, never by address.
- **Constructors that allocate other classes** (cRZOFStream in the file back end): stand in for the constructor on
  both sides (shape::StandIn; cast the member pointer to pick the overload) with a fake built in place.
- **The image's std::string::reserve** gives a heap buffer back when the request fits the small buffer; ours never
  shrinks. Where an original reserves on a string it may have grown before, write `shrink_to_fit` for requests
  up to 15 and `reserve` above (QuoteText 0xc9d07a).
- **The service's locks** are called through their vtables (slot 1 lock, slot 2 unlock), so tests can put logging
  fakes there; keep that form in our service code.
- **Pointer globals the image reads** ("NoGroup" / "AnyGroup", 0x1085d98 / 0x1085d9c): point them at the test's own
  texts with gzlog_test::Slots rather than relying on the mapped image's data.
- tests/gzlog_support.h must be included before tests/code_patch.h (it defines NOMINMAX before windows.h).
- **Writers on real files** (lane MA, SetString): the stale-state branches (a section offset at the end of the file, a
  new file while the index is loaded, a new path while the old file is open) only run when the test changes the files
  under the object between calls: close both sides' files, compare, then delete or cut them at an indexed offset, or
  move the path. Count each such case and set a floor, or the mutants in those branches survive.
- **Helpers returning in al** (PathAttributesMatch, LoadXml): declare them bool and compare al only in the test; the
  original's upper bytes are leftovers. Cite the callers' `test al, al` in the test (tools/classinfo.py --disasm).
- **Classes the game builds inline and calls only from ours** (the property files): each side builds its own object
  with its own constructor (the original's by RVA, ours by name) and fakes the framework, the option store and the
  file streams the same on both sides; the XML serializer's Read / Write are stood in on both sides (the image's
  0x555d50 / 0x556a30 and our vtable's bodies via VirtualMemberCode), so the files are scripts of properties.
- **Stack-built serializables** (lane MB, ObjectSaveTypeTable): the original side is a block the size of the class
  with the game's vtable and the same members; ours is built by our constructor; run the slot through each side's
  vtable. Reset vectors the fakes fill with `swap` from a fresh vector, never `clear()`: a kept capacity changes
  whether the next push grows, and growth copies FolderRef (AddRef / Release in the log) on both sides.
- **Fakes that answer true must fill their out argument** (a catalog's GetName): the original reads the out value
  after a true answer without a null test, so a fake answering true with nothing crashes the original side only.
- **mutate.py needs status entries first**: a new file whose functions no status entry names is refused (the test
  selection can't map a test to it); write the entries (names `Class::Member (...)`) before the mutant batch.

## Conventions (found in F5)

- **Leak checks in the full suite**: threads earlier tests leave running allocate on the CRT heap, so heap walks are
  noise (ScriptVocab_Run failed only in the full suite, even counting only variant blocks). Use
  `difftest::AllocationCounter` (docs/lane/test.md, Pitfalls: this thread's allocations minus frees), and
  `FreeRecorder` for one block's fate (tests/gz_input_edit_filter.cpp).
- **The game's std::map / std::set in place**: MSVC 14.40's layout is the image's (head node at +0, size at +4; nodes
  left, parent, right, colour, nil, then the value at +0x10), so our code reads the game's tables as
  `std::map<uint32_t, ...>` at their cells and tests build them in place with placement new and destroy them after
  each side (tests/gz_legacy_control.cpp `Tables`). Building nodes from our code for the game to free later needs
  the allocators to agree: keep to reading until that is checked.
- **Overloaded creator names**: a class whose cIGZ face has a `Create` slot and a static `Create` creator needs the
  creator picked by type in the hook table (`static_cast<cIGZScriptableControl *(__cdecl *)()>(&X::Create)`).
- **Recorders for original callees on both sides**: functions the lane leaves original but a test reaches (the 22
  registrations, FormatPropertyValue, the script writer) are stood in on both sides by one recorder per RVA
  (`shape::CodePatch(rva, shape::CodeOf(&Recorder<I>))`), so the comparison covers the order and the arguments.
- **A mutant that removes a call to a no-op path is equivalent**: Create's `HandWinProc(window, false)` returns at
  once (0x863aec tests the flag first), as the original's call does.
- **The game's function-local statics** (lane MD: EvaluateWindowCondition's settings face, cell 0x10fb348, guard
  0x10fb34c): ours runs our CRT's `_Init_thread_header` / `_footer` on the game's guard against the game's thread
  epoch (word 1 of the image's TLS block, index at 0x10df814), as src/rz/COMDirectors.cpp does, so a static either
  side built is seen by the other. Tests give it built (guard INT_MIN, the cell set) and unbuilt (guard 0, the thread
  epoch set to -1 for the case and restored after), so the init path is compared too (tests/gz_load_ui_script.cpp).
- **Hex vtable literals belong in tests, not src**: a "the game's, for tests" `kVTable` constant in a src header makes
  callers.py treat our build as holding the original vtable, which opens every slot's callers check. Keep such
  constants in the test file (lane MD moved cGZLegacyTextList's; cGZLegacyControl.h's three are still there).
- **Window faces by interface id, not by cIGZWin slot**: LoadUIScript's slots 95 / 96 are on the text edit face
  0x231a1c57 (attach a vertical / other scrollbar), its slot 5 the face's window; the button face 0x8810's slot 3 is
  its window; the win proc host 0x5386d516's slot 6 takes (4, 0). Fakes: a drawfx Fake of any table long enough, its
  slots `Settle`d to the caller's argument counts.
- **Two fake systems in one test** (svcfx services, drawfx windows): log the service calls whose order against window
  calls matters (converters, settings, messages) as drawfx notes from the svcfx handlers, so one log keeps the order.

## Notes for F5 / F6

- RZWildcard::MatchCase (src/rz/RZWildcard.h) is ours: the cheat and command parser callers (0xafeb90
  nGZCommandParser::cMatchFunction::vf0, 0xb3b750 cListPropsCommand::vf4, 0xcacc10 cHelpCheat::vf4, 0x3f83d0) should
  call it directly when they are written; 0x9cce18 is a second matcher on top of Match (not done).
- The log service's configuration writer (slot 24) writes each formatter's, filter's and output's own text through
  their slots 5 / 19: those are ours, so a UI that shows the log configuration can call the service.
- PathAttributesMatch (src/rz/PathAttributes.h, flags in pathattr::) is ours: new code that tests a path's attributes
  calls it rather than an OriginalFunction at 0x1c1931. ts::cTSAppPropertyFiles (src/ts/cTSAppPropertyFiles.h) owns
  the property files; a native options UI or a TS2-Extender-style property API would read and write the framework's
  property-set service and call Save, not touch the files.
- cRZIniFile is whole: an ini-backed feature (mod .ini options) can use our class directly, writer included.
- The folder's objects are cTSObjectSelector (vtable 0xdcd1a4, factory 0x4d9870), not cTSObject; its +0x18 list holds
  cTSMissingObject entries (class 0x5b7ebdb0; Init's third argument is the object type, the fourth a 16-bit save
  index). The selector slots the save table uses are recorded as tentative method facts (cITSObjectSelector slots 4,
  7, 28, 38, 40, 42, 54, 57-59, 70; cITSObjectDefinition 3, 8, 9; cITSMissingObject 3): the lot campaign should
  confirm them.
- The product info helpers (src/app/ProductInfo.h, namespace product) are the registry view of installed products;
  a launcher or an options UI that lists installed packs calls them.

## F6 handoff (lane MC, 2026-10-24)

The legacy control (src/gz/cGZLegacyControl.h) is ours; its 22 registrations are ours since F6 part 1 (lane ME; see Done), called in the order the
first constructor runs them (`RunRegistrations` in src/gz/cGZLegacyControl.size.cpp). What follows describes them
(the tables they fill); what is left for F6 is the handlers. Each registration fills three tables for one window class:
- **The shared vocabulary** (cIGZScriptVocab at 0x10e6890; the object is ours, cGZScriptVocab): `AddId(name, id)`
  (slot 3, a cIGZString name, the id stored as a uint32 variant) for the class's script name, its type name and each
  property name. The names are image strings: read them with `GameAddress<const char>(rva)`, never copy them.
- **The types table** (`LegacyTypeMap`, `std::map<uint32_t, uint32_t>` at 0x10e689c): class id -> type id.
- **The handlers table** (`LegacyHandlerMap`, `std::map<uint32_t, LegacyHandler>` at 0x10e6894): type id -> the
  class's handler, `bool __cdecl (ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)`. Create
  calls it with true after making the window (apply the class's properties from the attributes) and with false
  after placing it; the result is not read.
Both maps are filled through 0x85ca6e (a std::map insert of a {key, value} pair, ecx the map: an existing key keeps
its value). The game's maps have MSVC 14.40's layout, so ours can insert in place; check first that a node our
code allocates can be freed by the game's code (the maps' exit-time destructors are original) or keep inserting
through 0x85ca6e as an original call.

**Worked example, 0x85da3d (364 bytes):** AddId(name 0xe48cc0, class id 0x82fe68c4); AddId(name 0xe48ccc, type id
0xc12cea13); handlers insert {0xc12cea13, 0x864376}; types insert {0x82fe68c4, 0xc12cea13}; AddId(name 0xdbb3a0,
0xb00), AddId(0xe48cd8, 0xb01), AddId(0xe48ce8, 0xb02): three property ids the handler 0x864376 reads from the
attribute list. Each registration builds its names as temporary std::string-backed cRZStrings (0x141136 /
0x141455): ours can pass a stack cRZString.

**Ours for F6 to call:** cGZScriptVocab (through the cell), FindUint32Attribute 0x86401e (the first uint32 attribute
of an id), cRZVariant, cRZString, the map typedefs, and (lane MD, all hooked, declared in src/gz/cGZLegacyControl.h)
the attribute readers FindTextAttribute 0x863fae (an attribute's text into a cRZString), FindPairAttribute 0x863e87
(two numbers "(a,b)"), FindColorAttribute 0x863e06 (three bytes "(r,g,b)"), HasAttribute 0x864077, LoadLocalizedText
0x86dceb (a string-table text), SetResourceKeyProperty 0x87b35c (a "{group,instance}" key as a window property) and
ApplyCommonProperties 0x86549d (what was called FormatPropertyValue: the properties every window has; the handlers
call it first). **Still original:** nothing of F5's; the window classes' slots (cIGZWin and the faces) are
ui-windowing's. The handlers
and their helpers sit in 0x864376.. and the unanalysed region 0x866410..0x86dbb5 (about 30 KB, not in
db/functions: run the analysis there first); cGZDummyWinProc (vtable 0xe49270, 8 bytes: the vtable and a count;
AddRef 0x85da25, Release 0x86de9e, slot 4 0x863b3e true for 0xa2bf8ad0 / 0xa2bf8ad1, deleting destructor 0x85d9e6,
QueryInterface and slot 3 folded 0x67546f / 0x1474b0) is built inline at 0x86da0c in that region, so it goes with F6.

**Test pattern** (tests/gz_legacy_control.cpp): build both maps in place at their cells per side (`Tables`), make
the vocabulary cell a recording fake (or ours), run a registration on each side (the original by RVA, ours by name)
and compare the vocabulary's calls (names by content, ids) and the two maps' contents (keys, values; a handler by
its RVA while it is the image's, `OriginalAddress`, by identity once ours). For a handler: a winfx window
(tests/winfx.h; SetProperty slot 127 logs the variant by content), attribute lists of recording variants (type 6
values and others), apply true and false. The registrations are table code and good byte-match candidates: try
`tools/matchcheck.py --rva` on each before writing a test.

**Byte-match trial** (`tools/matchcheck.py --rva ... --brief`, v8): cInputEditFilter's three bodies (QueryInterface
0xcabbc0, Filter 0xcaa120, the deleting destructor 0xcaa030, 160 B) MATCH once the source moved out of a .size.cpp
file (matchcheck said the original is speed-built: read its profile line first, the row's size guess can be wrong).
FindUint32Attribute 0x86401e (0.57, 93 vs 89 B: codegen) and NotificationHelp's slots (0.36: the original holds its
faces in EH-guarded auto-refs) do not. Small table code like the registrations is the likeliest to match; check the
profile, write plain C++, and fall back to a difftest.

## Left

F5's code is done (lanes MC and MD, see Done): no F5 function is left original. What is left:
- **F6 (done by lanes ME to NH, see Done):** all 22 registrations and every handler are ours, and the handlers make no
  original calls any more. What is left around it (lane NH's survey of 0x85a000..0x87c000, the rest of the range is other
  owners' or library code):
  - The window-to-script writer 0x87a9a9 that `cGZLegacyControl::GetScript` (slot 7, 0x86e758) calls is ours since
    lane FI (verified, with its header and 7 class writers); the class writers it still calls as originals are rows
    FJ..FL (the FJ..FL recipe above).
  - The grid matrices' other half: the counted holders (vtables 0xe49b9c / 0xe49bac / 0xe49bbc) are ours since lane FC,
    with their destructors 0x85d7c8 / 0x85d7e9 / 0x85d80a trapped. Lane FI hooked and verified the matrices' deleting
    destructors 0x85d901 (cells) and 0x85d928 (descriptions), and the cell's constructor 0x85d5f0, copy 0x85d59d and
    assignment 0x85d8b2 (`legacygrid::GridMatrix::Destroy`, `GridCell::Construct` / `Copy` / `Assign`, test
    LegacyGridMatrix_Destroy and LegacyGridMatrix_Cell). Left: the font constructor 0x940a6e and the constructor's
    second caller 0x940ae3 (ui-windowing).
  - The small-buffer helper 0x87b2ac (176 B, the key buffer of AppendFileBrowserKey) has no other caller once the hook is
    in; it stays in the image unreached.
  - The std::map / std::list instantiations around 0x85c51a..0x85d2e9 and 0x85cd2a..0x85cf68 are library code (the
    toolchain's): never reimplemented.
  - cGZDummyWinProc (vtable 0xe49270): done in F6 part 5.
- cInputEditFilter's +4 deleting-destructor thunk 0xca9fd9 stays the image's (it jumps to our hooked 0xcaa030); the
  helpers' builders (0xcaaed0, cNotificationHelp's constructor 0x6225c9 and the 25 classes) are other campaigns'.
- Original calls left in lane MD's code: 0x9c5a89 (the cached message server getter, cell 0x10f519c) and 0xb38f10
  (the cached cheat console getter, cell 0x10faf68): two small getters whose owners (the message server's users, the
  cheat console) are other campaigns'; the cIGZWin slots and the window faces' slots (win proc host 6, button 3,
  text edit 5 / 95 / 96, scrollbar 5, parent 21) are ui-windowing's.
- LoadUIScript's test compiles no script: the control set the service's CompileNode returns is a fake (its three slots
  are all LoadUIScript uses). A test that runs synthetic script text through our terp, control set and legacy
  control into LoadUIScript would cover the joins; the F5 exit check below covers them in game.
- **F5 exit check** (stage 2 above; not run yet, the coordinator's): menu, neighbourhood and lot loads with every UI
  script through our terp, control set and legacy control (the creators' traps: `Hooks.cGZScriptTerp_Create=1`,
  `Hooks.cGZScriptVocab_Create=1`, `Hooks.cGZScriptableControlSet_Create=1`, `Hooks.cGZLegacyControl_Create=1`) and
  through our LoadUIScript (hooked; about 150 UI classes call it): the window tree (host wintree dump) against a
  reference run's; an input edit window closed by Escape (the filter); ui-compare at the three stages. Watch the
  windows' conditions (visible / enabled flags from the settings) and the scrollbars of text edits.


F4's rest after lanes LY, LZ and MA (F4b, F4c and the property files of F4d are done, see Done):
- The log service is ours slot by slot but still built in place over the game's vtables (nGZLog's standalone shape
  is not done); AppDebugger keeps its own cRZStdAdapter-shaped struct.
- F4e and ObjectSaveTypeTable are done (lane MB, see Done). Original calls left in that code: 0xc0b314 (the
  neighbourhood's directory, cTSNeighborhoodManager's), 0x9cce18 (the case-insensitive wildcard matcher on top of
  RZWildcard::Match), 0x1c99d2, 0xb39360 and the folder's 0xc0b669 (the GUIDTranslation constructor): each goes with
  its owner. GUIDTranslation itself (vtable 0xdcc87c: QueryInterface 0xc0b688, Release 0xc0b7bd, Write 0xc0b82e,
  Read 0xc0b6ed, GetGZCLSID 0xc0b663) is not ours apart from Resolve; it belongs with the lot loader (F5 / F6 or the
  lot campaign).
- Untested path: ProcessPagefileUsage's Win9x / no-psapi branch (the harness's psapi fake always answers).
- Which of the "never running" helpers run (local/coverage run1, run3, run-1023): FindNewerProduct 0xcb4cc0 and
  IsUserAdministrator 0xc2896d run at Init, UnlockNeighborhoodFiles 0xc2a3d6 at about 16 s (the neighbourhood load);
  GetProductPaths 0xcb4690, BodyShopProductName 0xb417c0, AskYesNo 0xc2dbf1, ReportFatal 0xc28de0, AddCrashData
  0xc28e32, ProcessPagefileUsage 0xc27b49 and SpawnAndWait 0x1b1a3f never ran (crash, Body Shop, question and
  out-of-memory paths). ObjectSaveTypeTable::Read and GUIDTranslation::Resolve run on every lot load; Write /
  WriteEntry only on a save (no save in those runs).
- **F4 exit check** (stage 2 above): nGZLog's standalone shape is what is left of the row. For the in-game check add,
  now that the property files are ours: the user's userProps.xml after a menu-and-exit run identical to a reference
  run's (Save writes it at Shutdown), and an ini write (any option the game stores through cRZIniFile slot 10) leaving
  the same file. For F4e: a lot load and a lot save (the save type table; savediff of the lot's package against a
  reference run), and the neighbourhood load (UnlockNeighborhoodFiles, the product info at Init).

F1's rest after part 4 (lane LW):
- **The other cAbstractBehaviorTypeT instances**: about 372 functions, 48 KB, not one class. Each instantiation's
  slot bodies (slots 3, 5, 6, 8, 9, 12-15 and Release) sit with the class that derives from it: scene-graph nodes,
  graphics and camera classes, listeners. Many are shared_vf_ folds of the reflector's base bodies, so check the
  folding before hooking. Take each instance with its owning class's campaign (it is the base of that class's
  standalone shape), not as an F1 lane.
- 0x1a8dab (6 bytes, `mov eax, 0xbc168268; ret`, the input map's class id getter): its only caller, the event
  director's constructor 0x1a8346, is ours and doesn't call it (our director's data table holds the id). It has no
  entry: a "callers" entry with a trap belongs to COMDirectors.
- The thread classes' constructors stay with their current owners (ServicesB.cpp, nGZLog.cpp, cRZCallbackThread's in
  its group).

F3's rest after lane LX (all done by lanes MQ..MZ, 2026-10-25; kept as the codecs' map. Nothing of Gimex is left but
0x885849, 4 B, inlined): the Gimex codecs (EA code, about 60 KB of spans, 0x87da81..0x88d69a). The classes, the
table and the dispatchers are ours (Done, F3b); our table (src/gz/Gimex.size.cpp) holds the codecs' entry points as
OriginalAddress, so a codec lane replaces a row's entries and the table points at ours. Split by format (spans from
the table's entry points; each row is about / is / open / info / read / close / wopen / write / wclose):
- **FSH** (row 1, EA's own format): the glue is done (F3c part 3). Its back end is left: two blocks, all original
  calls today, reached only from the FSH row (and 0x896960 from cRZFastCompression3's code at 0x9cc611 / 0x9cf4f3).
  Lane MP surveyed them (names recorded tentative in Ghidra, `fsh_*` / `refpack_*`):
  - block 1, 0x88374d..0x88d69a: 149 functions, 40.2 KB. Leaves at the bottom: four resamplers 0x88374d..0x883eb9,
    the word helpers 0x88407b..0x884193 (endian by platform), about 40 colour converters 0x8841b0..0x884ade (one word
    to B, G, R, A and back), the format converters 0x884b24..0x88521a (big stack frames, _chkstk), small helpers
    0x885429..0x885849. The decode side 0x88584d..0x8888e7: directory reader 0x887a06, builder 0x885f9b (mipmap
    entries named "%s_mm%02d" with the image's sprintf), entry parser 0x8871b5, decoder 0x886e83, pixel unpacker
    0x887507, info helpers 0x886652..0x8881f8. The encode side 0x888939..0x88d69a: encoder 0x88905f (record type
    by platform from the info's source bits, kind 0x42c and flags 0x428), compressor 0x88aa6e, palette builders
    0x88b405..0x88bd45, block compressor 0x88bf12, GM compressor 0x88c070, the directory writer 0x88c719 and its
    helpers 0x889917..0x88a9ad, 0x88cc20..0x88d5e8.
  - block 2, 0x894e0e..0x89e722: 65 functions, 39.0 KB, EA's compression library: the RefPack decoder
    0x894e0e..0x8953ff and the type tests 0x895381..0x895802, block decoders 0x89583b..0x8963bd, the GM (DCT)
    decoder 0x89662e / 0x8968dd / 0x89694e, the RefPack compressor 0x896960..0x896db9, the block encoders
    0x896e1e..0x89d7b1 (the biggest functions: 0x89aa95 6.4 KB, 0x899d55 3.4 KB, 0x89cacf 3 KB; floor 0x838c10 and
    sqrt 0x82fc60), the GM encoder 0x89d938..0x89e47a (cos / sqrt tables built once into globals). Library callees:
    memcpy, memset, _chkstk, strncpy, floor, sqrt, cos (toolchain functions in ours).
  Lanes, bottom-up, about 10 KB each:
  1. block 1's leaves: resamplers, word helpers, colour converters, format converters, small helpers (about 9.5 KB;
     each testable alone over synthetic buffers, many likely byte matches). DONE (F3c part 4, lane MQ): 61 functions,
     8.4 KB, 10 byte matches. Left of it: 0x885849 (4 bytes, inline it in 0x88b405's lane); the permutation tables are
     read from the image (derive them for standalone).
  2. block 2's decoders: RefPack decoder, type tests, block decoders, GM decoder, plus the RefPack compressor
     (about 8 KB; check cRZFastCompression3's own copy first: one may serve both). DONE (F3c part 5, lane MR): 29
     functions, 8 KB; the decoder body and helpers are shared with cRZFastCompression3, the compressor is a variant.
     Left of it: the tables are read from the image (derive them for standalone).
  3. block 2's block encoders, first half 0x896e1e..0x8995da (about 10 KB). DONE (F3c part 6, lane MS): 10
     functions, the nondeterminism found (uninitialised stack in 0x896e1e and 0x897b56, see the Done entry). Left of it:
     the solver 0x899d55 and the colour fit's callees are original calls until lane 4; the 17-try cap is covered by ColoursSearchCap (stand-ins).
  4. block 2's block encoders, second half 0x8995da..0x89d7b1 (about 12 KB, 0x89aa95 alone is half). DONE (F3c part 7,
     lane MT: 9 functions, 10,694 bytes; F3c part 8, lane MU: the quantiser 0x89aa95, 6,424 bytes, its tables are rebuilt
     per call and read by nothing else, so ours are locals). Block 2's block encoders have no original calls left.
  5. the GM encoder 0x89d938..0x89e722 (3.6 KB) with block 1's encode leaves 0x888939..0x888f97 and
     0x88cc20..0x88d5e8 (about 4 KB). DONE (F3c part 9, lane MV: 37 functions, 8.2 KB; 0x888e00 stays reimplemented).
  6. block 1's decode side 0x88584d..0x8888e7 (about 12 KB: the reader, builder, parser, decoder, unpacker). DONE (F3c part 9b,
     lane MX: 43 functions, 12,245 bytes, see Done).
  7. block 1's encode side: encoder, compressor, palette builders, directory writer (about 10 KB).
     7a DONE (F3c part 10, lane MY: 23 functions, 9,109 bytes, `verified`, db/status/GimexFshWrite.jsonl, src/gz/GimexFshWrite*.size.cpp):
     the directory writer's helpers 0x889917..0x88a9ad (sizes, padding, chaining, mip merging, cube-map expansion, tiling), the
     palette / attachment builders 0x88b405..0x88bd45 and the pixel packers and compressors 0x88bd83..0x88c0e9.
     BlockCompress is tested on quiet images only (lane MS: the colour encoders read uninitialised stack otherwise).
     7b DONE (F3c part 11, lane MZ: EncodeShape 0x88905f, CompressShape 0x88aa6e, WriteDirectory 0x88c719, src/gz/GimexFshWriteShape.size.cpp
     and GimexFshWriteDirectory.size.cpp, tests/gz_gimex_fsh_write_shape.cpp; 30 mutants, 29 killed, 1 equivalent). GimexFsh.size.cpp's
     WriteFrame and CloseWriter now call ours (no original left; CloseWriter uses FreeWrittenDirectory), Entry / Directory are one struct
     (GimexFshDecode.h) and Gimex_FshRoundTrip writes kinds 0..5. Kept out of the tests, because the original misbehaves: its pixel packer
     0x88bd83 clears an area sized for another record type than the buffer EncodeShape allocated for some inputs (1-bit sources on platform 4,
     indexed frames under the block kinds, sources of no bits; mirrored in ours, ORIGINAL-BUG), and the original's GM / PS2-block decoders
     leave stack garbage in the pixels they write, so the round trip does not compare pixels of kinds 4 and 5. WriteDirectory's filler block
     is align + 1 bytes, so a codec gap above that is kept out (the original reads past its block).
  Found by lane MP (cause found by lane MS, F3c part 6: uninitialised stack in 0x896e1e and 0x897b56; the later lanes'
  tests clear the stack before each original call): with kinds 2..5 (info+0x42c, the compressed records) the original encoder / decoder gave
  different files and pixels on the two sides of a round trip with the same arguments, and different rounds on
  different runs: some compressor reads memory the caller doesn't set (uninitialised stack or heap, or addresses).
  Find what before testing those paths (the round trip only uses kinds 0 / 1). The float code (3, 4, 5) needs
  bit-exact results: compare outputs over many seeds and watch the FP evaluation
  order (x87 vs SSE in the original: check the disassembly before writing). Tests: Gimex_FshRoundTrip
  (tests/gz_gimex_fsh.cpp) is the end-to-end check for each step; each lane adds its own direct tests.
- **JPEG through libjpeg** (row 2): done in F3c part 2 (libjpeg 6a below it stays original calls).
- **JPEG through IJL** (row 3): done in F3c and F3c part 2. The shared wopen 0x87f3e4 and wclose 0x87fe5d are ours.
- **PNG** (row 4): done in F3c part 2 (libpng 1.0.5 / zlib 1.1.3 below it stay original calls).
- **BMP** (row 5) and **TGA** (row 6): done in F3c.
Left of rows 2..6 after F3c part 2: nothing (rows 2..6 are complete); the libraries below them are original calls.
After F3c part 3 every cell of the table is ours; the FSH back end (above) is the rest. Tests run a codec's entries on both tables with synthetic buffers (the
tests/gz_gimex_fakes.h fakes are the pattern for the layer above).

`tools/py -I tools/coverage.py queue local/coverage/run-1023.txt --group <class> --functions` per lane (the
`--subsystem ... --whole` view over-counts: Scope, above).

### Triage of the ran-in-game guessed helpers (FI)

The set: not library, not verified, executed in the coverage reference set, and guessed framework-gz from callers
(`called-only-from` / `called-mostly-from`) or guessed shared-utility. That gives 687 functions, 104.4 KB (106,922 bytes; the
"692 / 108 KB" estimate, recounted on 2026-11-04).

How each function was placed:
- **STL instance**: a fixpoint over the call graph. Every callee is library code, an STL or CRT marker (std::, operator
  new/delete, _Xlen*, memcpy/memmove/memset, ProbeAlloc deallocate 0x197100) or another STL-shaped function, and at
  least one callee is a marker. Earlier lanes' library verdicts (FB, FF, FG) and a hand-read of the framework-rooted
  ones are added on top.
- **Everything else**: the root-majority subsystem, meaning the first callers up the graph whose subsystem is assigned.
- Scripts are build/fi_*.py (scratch, uncommitted). Names recorded: lane FI, tentative.

| destination | functions | KB | examples |
|---|---:|---:|---|
| library: STL instances (confirm with tools/stlprobe, then db/library-matches.jsonl) | 175 | 27.4 | 0x2b3ef0, 0x4ce5e0, 0x4acd10, 0x15b2b0, 0x1d75b0; the framework-rooted ones: cGZFramework's map 0x141681 / 0x1414b6 / 0x141d50 / 0x141f34, nGZEvent 0x1a8694 / 0x1aa2cc..0x1aa66b, AnyStringSet 0x236726 / 0x236b4a / 0x2389fb / 0x238a64, string find 0x1adc1a / 0x1adc4e, tree erase 0xcb0810 / 0xcdc480 |
| library: CRT wrappers (db/crt-equivalents.jsonl) | 5 | 0.1 | 0x82fb8c (localtime64_s), 0x83bab7 (stat), 0x1ba00b (clock), 0x1b0ff4 (atof), 0x859f2e (VerQueryValueA thunk) |
| ui-game | 98 | 16.0 | 0x7e78a0, 0x9cd890, 0x7f0720, 0xae1f50, 0xae3cd0 |
| animation | 62 | 13.0 | 0x2588f0, 0xae3fb0, 0x25f740, 0x265a30, 0x255b30 |
| unrooted: calls-into chains (the 107 KB artefact in Scope; callers' campaigns) | 39 | 10.8 | 0x22d308 / 0x22d995 (root 0x22cf55), 0xc4262a (root 0xc42d84), 0x749c2f, 0xc2df67, 0x730b01 |
| world-lot | 63 | 7.8 | 0x7e4680, 0x7e84b0, 0x7e8d10, 0x7df930, 0x7e2630 |
| scenegraph | 66 | 6.9 | 0x3ab570, 0x3bc4d0, 0xaea1f0, 0xb3cdb0 / 0xb3cdf0 (lighting parser) |
| sim-core | 10 | 6.4 | 0xaf90e0, 0xafb260, 0xaf81e0 |
| ui-windowing | 6 | 5.7 | 0xb824b0 ParseFontStyleDefinition (5.4 KB), 0x1eaba0 |
| objects, sim-person, cas, persist-dbpf, other-ts, neighbourhood, audio-video, command-parser, lua-scripting, network-web, rendering-gfx, third-party | 91 | 9.3 | 0xcb6f00, 0xaebb00, 0xaeb5b0, 0x1c2732, 0x998ae8..0x998be4 (name lookup, network-web) |
| nothing: thunks, stubs under 7 B, folded shared_vf_* | 54 | 0.4 | 0x4043a0, 0x437e30, 0x148840 |
| nothing: 6-byte class-id getters (`mov eax, id; ret`; callers are ours, like 0x1a8dab above) | 12 | 0.1 | 0x1c184d, 0xc3740c, 0x1c057c, 0xb28897, 0xc673f2 |
| nothing: unwind block inside verified 0xc2896d | 1 | 0.1 | 0xc28b39 |
| **foundations** | 5 | 0.3 | 0x99e700 RefCountHolder_Destroy, 0x141533 cGZFramework::KeyRefPair_Construct, 0xc260bc cTSAppFW::GetGZCOMClassObject_9b3798f7, 0xc9bff9 nGZLog::FindLevelOrDefault, 0xca4ac0 cTSAppCommonImpl::FindListEntryMatching |

What follows from it:
- The ran-in-game helpers are almost none of foundations' own work.
  - About a quarter is unmatched STL.
  - About two thirds is callers' code that the `called-only-from` guess filed under framework-gz because the nearest
    named caller was a cRZString/cGZ* constructor.
  - The callers' campaigns should take their rows when they pick their next lanes (`coverage.py queue --group`
    on the root class).
- Proposed lane rows:
  - **FN (Opus, about 1.5 h): library confirmation of the 175 STL-shaped functions and 5 CRT wrappers, 27.5 KB.**
    - Group them by instantiation (map / tree / hash / list / vector / string).
    - Build the element types with tools/stlprobe.
    - Record the matches in db/library-matches.jsonl and the CRT wrappers in db/crt-equivalents.jsonl.
    - Any that don't match go back to their root's campaign.
    - It is tooling and structure work, hence Opus.
  - **FO (Sonnet, under 1 h, or folded into the next lane that touches cTSAppFW / nGZLog / cTSAppCommonImpl): the 5
    foundations helpers, 0.3 KB.** Each is a small difftest; none needs a new fake.
- The 12 class-id getters need no lane.
  - Their callers are verified, so the originals are in effect through those callers once nothing else reaches them.
  - A later `coverage.py` run shows whether anything still does.

#### FN result (lane WB, 2026-11-04)

- **Confirmed library.**
  - Of the 160 triage functions still unresolved (25.4 KB; the other 20 were already library), 32 match (6.0 KB):
    - vector: 19 (5.0 KB)
    - tree: 6 (0.6 KB)
    - hash: 5 (0.4 KB)
    - other: 2
  - Three CRT wrappers went into db/crt-equivalents.jsonl:
    - 0x82fb8c `_localtime64_s` and 0x83bab7 `_stat32i64`: hotpatch tail calls into their common_* bodies.
    - 0x1ba00b: a jump thunk to `clock`.
  - db/library-matches.jsonl gains 304 stlprobe rows in total, across the whole image. Only stlprobe rows were added;
    a full rewrite also changed about 1,770 rows from other inputs (d3dx9, openssl, ucrt, expat kinds), which is
    drift in the local libraries, left for a separate pass.
- **Settled by the coordinator.** 18 of the new rows sit on status entries, 15 of them `verified`:
  - 0x14a4c0, 0x14a800, 0x154bb0, 0x1aafcb, 0x1ba0fb, 0x1c01b9, 0x1fb410, 0x3c6bf0, 0x41bb70, 0x5cb103, 0x9cd620,
    0xa9c570, 0xafdda0, 0xb5afd0, 0xc6ec8a
  - mapped: 0x18b860, 0x731e9a, 0x85ca6e
  - Under the library rule their entries and hooks go.
- **Not shown to be library: 128 functions, 19.4 KB.** Each keeps its subsystems.py filing:
  - 80 (13.1 KB) guessed shared-utility;
  - 44 (5.8 KB) guessed framework-gz;
  - 4 (0.5 KB) with no guess.
  - No probe type reproduces them (no near miss at 0.5 or above), including the FAM 10/11 string-keyed and
    string-valued maps:
    - unordered_map<uint32, string>-shaped FNV try_emplace: 0x15b2b0, 0x1d75b0, 0x1d8050, 0x4ce5e0.
    - string-keyed hash: 0x1aa3a2, 0x1aa49c, 0x2227e2, 0x5f2154, 0x72e795, 0x744cd6, 0xc669a4.
    - map<uint32, string>: 0x146730, 0x1d2180, 0xca4e60.
    - map<int, int>: 0x141681, 0x4224b0.
    - list<pair<const uint, uint>>: 0x21876e, 0x222973.
    - u16string: 0x1c0ab6.
    - vectors of 1-, 2-, 4-, 8-, 12- and 20-byte elements: 0x1a96a0, 0x1be8b6, 0x314290, 0x299660, 0x299710,
      0x9cd360, 0xc0a727, 0xc0a624.
    - node and element destructors for 28- to 120-byte values: 0x72f2ae, 0x72f45e, 0xc6c9b4, 0x72e753.
  - Near misses (0.86 to 0.99), where the value type has game constructors or destructors: 0x1414b6, 0x64a77b,
    0x141f34 (node 0x1c, not 0x7c), 0x1aa66b, 0x1aa277.
  - Game code by inspection:
    - 0x2144b9: a destructor that stores cRZString's vftable.
    - 0x1ad812 / 0x1ad827: wrappers around append and assign.
    - 0x1b0ff4: `strtod(s, &end)` in a frame-pointer wrapper, not the UCRT shape. Its callers are 0x4fc8a0, 0x9cb4e1,
      0xafb660 and 0xafbd40.
  - These go to their root callers' campaigns, like the rest of FI's "callers' code". A lane that takes one first
    writes a probe for its exact value type. If that matches, the function is library and the lane does no work on it.
  - 0x859f2e (`jmp [IAT]` VerQueryValueA) is an import thunk: metrics already count it as done, and subsystems.py
    still gives it a row.
- **Tooling.**
  - stlprobe probe_one.cpp has new families:
    - FAM 8: std::string members.
    - FAM 9: vectors of string-headed elements.
    - FAM 10/11: map and unordered_map with string and scalar keys and values.
  - `/EHsc /O2 /Ob1` and `/O1` are added to OneFlagSets. A one-member unit under /Ob1 is the only one of 16 flag
    variants that reproduces 0x2b36f0.
  - stlprobe.ps1 has a new `-Missing` switch.
  - libmatch `--near` gained `--rvas FILE` and `--full`. It also finds the second shape of a name already placed
    elsewhere.
  - Rows list at most 120 inputs (`lib_count` keeps the total).
  - With these changes, "other" falls from 5,379,934 B to 5,338,003 B (46.1% to 46.0%), and library bytes rise from
    1,664,712 to 1,707,342.


### Coordinator decisions after FD (2026-11-04)

- FG (cRZCursorResource and factory, cRZStringResourceFactory, cGZClipboardViewer; ~2.3 KB, none executed in run-1023):
  deferred; leaves, taken by a writer-chain lane with room or when a standalone program needs them.
- The asynchronous name-lookup registry (std::list at 0x10f4ff0, lock, counter 0x10f4fe4; users 0x999103, 0x9994e7,
  0x999556, 0x999678) goes to network-web with cRZHTTPClient / cRZNetSocket, as cRZNameLookup does.
- 0x1bed67 is std::string copy assignment (library): AnyStringSet::Assign 0x237679 can be closed against it.
- ~cRZCOMDllDirector is the largest foundations entry in the map that only caller inference placed (337 functions,
  ~25 KB, lane FT): functions reached from the director destructor, not shown to be its own. The cleanup lane or a writer-chain lane
  checks which of it is really the director before any lane takes it.

### From the backlog (triage 2026-11-03)

- **Gimex / FSH codec:** F3b (LV): cGZGimexFactory/Reader/Writer plus EA's own Gimex C code around the libjpeg 6a / libpng
  1.0.5 / zlib 1.1.3 block (0x880168..0x8b7bb6, third-party: call through OriginalFunction, never reimplement); size
  larger than the F3 row's 16 KB; establish the codec question first (Open question 5). Gimex codec code, EA's (LX):
  FSH glue 0x87da81..0x87ed09 + back end 0x88374d..0x88d69a (~45 KB), JPEG-libjpeg rows 2.7 KB, IJL 2.5, PNG 4.5, BMP 5.6,
  TGA 3.5 KB. FSH back end (check: Left's FSH step list is current): steps 1-4 done (MQ, MR, MS, MT, MU), step 5 GM
  encoder 0x89d938..0x89e722 + block 1 encode leaves, then 6 (decode side), 7 (encode side); compressed kinds 2..5
  nondeterministic in the original: find why first; MP feedback notes/feedback/MP.md. FSH 7b: done by MZ except (b)
  BlockCompress 0x88bf12 alpha-0 mode untested (quiet images only); ORIGINAL-BUGs kept: 0x88bd83 clears a 16-bit area for
  indexed frames under block kinds; WriteDirectory's filler gap reads past its block when codec alignment > base
  alignment; step 5 gaps (MV): ReorderPlanes 0x888e00 reimplemented, GM printf warning paths not compared (counter is),
  ORIGINAL-BUG heap overflow in 0x88cf96 formats 0x16/0x1e (stride==width: last tile 0x21 past end, 0x20 slack),
  0x88d39e converts 4 pixels keeps 2. FSH permutation tables 0x105c2c0..0x105d240 read via GameAddress (MQ): standalone
  must derive them (PS2 PSMT4/PSMT8 block orders and inverses, 256-entry in-block permutations); GM decoder clip tables
  (MR): ours clamps out-of-range indices and counts clipOverruns where the original reads past the table (check whether
  a real GM stream can overrun; test index 0x3ff exactly); GM VLC/DC/address/zigzag/quantiser tables still via
  GameAddress. Standalone font engine: FreeType behind a T2K-shaped shim (decision 2026-10-27) is in rendering.md R6.
- **Left list fixes:** foundations.md "Left" lists F6 part 2 as open: stale (F6 handlers all done by part 6); rewrite Left:
  F5 exit check, F6 leftovers (Grid matrix: map of maps, ctor 0x85d3bc, SetCell 0x8640a4, SetEntry 0x86422c; holder
  classes with vtables 0xe49b9c/0xe49bac/0xe49bbc and their destructors read the layout: one class pass, MM's notes in
  notes/feedback/MM.md; FileBrowser 0x87b143; GetScript 0x87a9a9 and matrix holder/cell copy helpers 0x85d901, 0x85d928,
  0x85d7c8/e9/0a, 0x85d59d, 0x85d8b2, 0x85d5f0 (NH); in game check Grid UI scripts and FileBrowser lists), nGZLog standalone
  shape (service is ours slot by slot over the game's vtables; AppDebugger keeps its own cRZStdAdapter-shaped struct, LZ).
- **F1/F2/F4 leftovers:** cRZIniFile slot 10 SetString 0x9c6bbc (2.6 KB) still the game's (ours and tests reach it through
  the vtable; small lane or with F4d); 0x1ad83c TrimBoth is ours (src/rz/cRZIniFileRead.size.cpp) without a status entry;
  IR's cTSSG header comments and cTSSGSystemResources.cpp may quote game text: check against the no-game-content rule;
  cRZExceptionNotification (0x1c8c84 ctor + 5 slots) in the standalone shape, then GZFramework.size.cpp creates it (HK);
  F1 rest (LO, LT): cGZMessageQueue (27 fns)/2 (24), nGZEvent::cConnectionManager (13) + iterator (7), cGZEventReflector
  (22), cInputDeviceMap (factory 0x1a8d95 missing from functions.jsonl; registered by 0x1a8346; ctor 0x1a8af6), other
  cAbstractBehaviorTypeT instances, cRZCursor (11)/Manager (16)/SystemCursorW95 (10), cGZClipboardManagerClipboard (19),
  cConnectionChangedEvent (vtable 0xda239c); cRZMessage2Standard's 9 (own group); switch-to-ours call sites: 0x1b9f95 in
  StaticInitHT.size.cpp:53 and GZCOM.size.cpp:48 (check: .size.cpp comments only), timer ctor/dtor 0x9ccf32/0x9ccfa9 in
  cGZSndSys.size.cpp:88, cTSEffectsManager.cpp:66/71, cGZWinBtn.size.cpp:158/175, cGZWinScrollbar2.size.cpp:105/117
  (check each), cGZDBSegmentDirectoryFiles embeds a timer, cGZWinMgrW32.size.cpp:200 (0x9d0158; WinMgr_Run code-patches
  it); 7 unresolved slot-call receivers in LO's code; F2 rest (LQ): cGZBufferFontRenderer non-slot (1.4 KB),
  cRZLanguageUtility (23), cRZLanguageManager (30), cRZStringResource (13), GZServiceString 0x177110, cRZNameLookup
  0x998f2c, iterator templates, ownerless code near AnyStringSet/property-set serializers, cGZBuffer's converter
  0x8c6b10 (~60 per-format routines, table 0xe4f0d8) and 0x9c5a05; cRZStd AskBox: caption.Destruct() then the local's
  implicit destructor runs too (std::string destroyed twice; harmless with MSVC, UB; MH): fix with a test.

## Risks and open questions

1. **impfx and drawfx.** Build impfx on the stdcall table kind lane LM is adding for the device (one fake framework),
   which makes F1 wait for LM's merge; or standalone now and fold later. Recommended: wait (F2 can run meanwhile).
2. **Class attribution.** The cRZString group shows the class rule's blind spot (a member at offset 0); owner
   inference also pulls callers' helpers into a class (cRZIniFile's group is 8.7 KB, of which 2.0 KB is cRZIniFile's
   own and most of the rest world-lot's ini readers). Each lane's structure pass decides membership; the tooling fix is in the feedback.
3. **cGZLegacyControl's home.** It knows every cGZWin* kind's properties, so it could be ui-windowing's; it calls
   almost nothing, so it is placed here (bottom-up). Decide before F5. (Lane MC: kept here. Its frame calls the
   windows only through cIGZWin slots, which stay original calls into the window classes; F6 the same.)
4. **Threads in game.** Inline thread routines in tests don't show races; the smoke batch is the only check of the
   real interleaving (callback thread, sound thread, directory watcher). Acceptable for infrastructure?
5. **Gimex codecs** (answered by lane LV, 2026-10-23): Gimex is EA's image C library, statically linked (about
   0x87d6e2..0x880168, EA code, not third-party); PNG and the deflate it needs come from the static libpng 1.0.5 /
   zlib 1.1.3 block, so the only run-time library is Intel's JPEG Library for JPEG. 0x87f7a8 (filed under
   cGZGimexReader) does LoadLibraryA of "ijl15.dll", then "ijl11.dll", then "ijl10.dll" (module in 0x10e6948) and
   GetProcAddress of ijlInit, ijlRead, ijlWrite and ijlFree (0x10e694c / 0x10e6950 / 0x10e6954 / 0x10e6958); without
   ijlInit, ijlRead or ijlFree it frees the module (0x87f790) and JPEG is unavailable. 0x87f9e0 decodes through it
   (ijlInit, ijlRead, ijlFree on a 0x4e68-byte JPEG_CORE_PROPERTIES), 0x87fc52 is the write path. The factory
   (0x87c189) also loads ijl15.dll once (0x10e6930, flag 0x10e692c) and never frees it. A standalone build can link
   ijl15 directly or keep the run-time load (db/third-party.jsonl has the drop entry); tests fake it with impfx
   Libraries().
   Settled by lane LX (2026-10-23), with the boundary: EA's Gimex is 0x87d6e2..0x88d69a (the dispatchers and table,
   ours since LX, then the codecs from 0x87da81); the library block starts at 0x88d69a (libjpeg's jerror, the first
   library match) and runs to 0x8b7bb6. db/third-party.jsonl's Gimex row says 0x880168..0x8b7bb6, which takes in
   about 54 KB of EA codec code (PNG glue onward): its range should start at 0x88d69a (coordinator's file).
6. **Reference coverage**: our hooked sites are not measured in a replacement run; the per-stage headline needs a
   -Reference run.
