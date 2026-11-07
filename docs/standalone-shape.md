# The standalone shape

What a class looks like when it is ready for the standalone build (`docs/roadmap.md`, stage 3), how to get a
finished, hook-shaped class there, and what to check. Defined by the pilot on cTSFamilyRelationshipArray (milestone
D, lane DK, 2026-10-17): `src/ts/cTSFamilyRelationshipArray.{h,cpp}`, `tests/ts_family_relationship_array.cpp`,
`src/hook/tables/cTSFamilyRelationshipArray.cpp`. Pixel formats (2026-10-11) reached the same goal with hand-built
vtable tables; this page replaces that technique for ordinary classes (why: "Alternatives" below).

## What the shape is

| | hook shape (before) | standalone shape |
|---|---|---|
| class | non-polymorphic struct, faces as member structs holding `const void *mVTable` | real C++ class deriving from its interfaces (multiple inheritance, one base per RTTI face, original order) |
| vtables | the game's, stored by our constructor with `GameAddress(kVTable)` | ours, emitted by the compiler (slot order = interface declaration order), adjustor thunks and RTTI included |
| constructor | `ConstructInPlace()` writing game vtables | an ordinary constructor; the hook on the old constructor RVA placement-news our class |
| how the game reaches our code | an RVA hook (jmp) on each slot function of the game's vtable | directly through our vtable: every object the game creates carries it |
| RVA hooks | the replacement itself (`MemberAddress(&X::f)`) | adapters (`XAdapters::f`): old entry points for direct calls and for objects still carrying game vtables |
| slots we did not own (folded `shared_vf_*`, adjustor thunks) | the game's code | ours (the class must implement every slot) |
| difftest, our side | object from the original constructor, our member called directly | object from our constructor, called through its own vtable slot |

## Steps (a finished, verified class)

1. **Faces.** `tools/classinfo.py <Class>`: every vtable (subobject offset, slot count, each slot's function, folded or
   not). Each face becomes an interface base. Disassemble the folded slots (`--disasm`): you implement them now.
2. **Interfaces.** One abstract class per face, deriving from `cIGZUnknown` (`src/gz/cIGZUnknown.h`), slots declared
   in slot order with the original's signatures (`ret N` per slot; returns: `al` vs `eax`; a struct returned by value
   uses a hidden pointer and returns it in eax, e.g. `cGZPersistResourceKey GetKey()`). Global names = the RTTI names.
   Shared interfaces live in `src/gz/cIGZ*.h` and `src/ts/cITS*.h`: use the one there; an interface only your class
   implements may stay in its header until a second implementer moves it there. Don't derive from
   the generated `ifaces::` classes: they are caller views and are not reliable enough to implement (e.g.
   `ifaces::cIGZPersistResource` has 11 slots, `vf1` for AddRef and a `uint8_t` QueryInterface).
3. **The class.** `class X : public IFaceA, public IFaceB, ...` in RTTI order (the base order *is* the subobject order:
   `+0, +4, +8, +0xc`). Data members follow; keep `static_assert` on `sizeof` and member offsets (base offsets are not
   constant expressions: name them, as `famrel::kSerialFace`, and let the tests check them). Every slot is an
   `override`. One function overrides the same slot in several faces (QueryInterface/AddRef/Release, or
   `GetRefCount` here: primary slot 12 and persist slot 11); the compiler then emits the adjustor thunks the original
   has (`sub ecx, N; jmp`). Not `final` (see "Must not").
4. **Bodies.** Mostly unchanged, minus the scaffolding:
   - `Owner()` / `this - 4` arithmetic goes: a function of a secondary face is a member of X; MSVC compiles it with
     `this` = the face, as the original was compiled.
   - Calls the original makes through its own vtable become plain calls on `this` (`GetRelationship(sim)`,
     `Read(stream)`): virtual in a non-final class, so the same dispatch the original makes.
   - Face pointers: `static_cast<IFace *>(this)` (carries the original's null test, `neg/sbb/and`) instead of
     `-(this != 0) & face` by hand.
   - Destruction: `delete this` (destructor inline + sized `operator delete`, as the original's Release).
   - Calls to *other* objects still go through the generated `ifaces::` views (`As<ifaces::T>(p)`).
5. **Constructor and creation.** The constructor is plain C++; the compiler installs the vtables (including the base
   interfaces' dead stores). Then route every creation path to it (next section). When the original has no constructor
   function (inlined into its factory, as cTSPersistResKeyList 0x1f4dc0 and the FAMT factory's creator 0x4f2740), there is
   no Construct adapter: the hook on the factory (`return new X;`) is the class's only switch.
   **Deleting destructor slot.** Where the original's vtable has a deleting destructor (`ret 4`, `flags & 1` frees), declare
   `virtual ~X();` in the class (or interface) at that slot's position: a virtual a class introduces is appended to its
   primary vtable after the inherited slots, in declaration order, and the destructor counts as one. The compiler's scalar
   deleting destructor then fills the slot (destructor, then sized `operator delete` on bit 0, returns this), and
   `delete this` in Release compiles to the original's `push 1; call [vtable + slot*4]`. Only the primary interface gets the
   slot (the other interfaces have no virtual destructor).
6. **Adapters.** (Non-virtual members of a class with several bases need no adapter since lane FZ: hook them with
   `MemberAddress<&C::f>()`, which accepts a zero this-adjustment and refuses the rest at compile time.) A struct of static `__fastcall` functions (`this` in ecx = the face of the slot, the dummy edx, the
   stack arguments: interchangeable with `__thiscall`), defined in the class's .cpp next to the bodies:
   `[[msvc::forceinline_calls]] return static_cast<X *>(face)->X::f(args);`. Qualified call: no virtual dispatch.
   The forced inlining makes each adapter a copy of the function, which keeps small byte matches (below). The
   constructor's adapter is `return ::new (memory) X;`. Hook entries point at the adapters with `reinterpret_cast<void *>`.
   A deleting destructor has no name to call without dispatch: its adapter does the two steps by hand
   (`self->X::~X(); if (flags & 1) ::operator delete(self, sizeof(X)); return self;`). It stores *our* vtables into a
   game-built object before freeing it; only visible with flags 0 on an object used after destruction.
   A static creator (`Create`, cdecl) needs no adapter: the hook points at it directly.
7. **Tests** (next sections), mutants, status notes, smoke test.

## Creation paths (find all of them)

- `tools/xrefs.py <ctor RVA> <Create RVA> <each vtable RVA>`. Code that stores a vtable address is a constructor or
  destructor (possibly inlined into a caller); every caller of the constructor is a creation site.
- For each site: if it is ours, it already runs our constructor. If it is the game's and calls the constructor RVA,
  the constructor hook (adapter) covers it. If it is the game's and stores the vtable itself (an inlined
  constructor), that function must be replaced first (or its class taken to the shape): until then it builds objects
  with the game's vtables, which the slot adapters still serve but which bypass our vtable.
- Factories registered by address (`cTSSimCOMDirector`'s constructor registers `Create` 0x4f26e0) reach our code
  through the hook on that address; no change in the registrar needed.
- Static instances: their initialisers are creation sites too (pixel formats: the image's .data held constant-initialised
  objects with the game's vtable; see `src/gz/PixelFormatStore.h`).
- A vtable reference xrefs prints as `in .text (data)` is code outside every known function (an un-named creator only
  stored by address): disassemble it (`classinfo.py --disasm <address>` works on any address), find who stores its
  address (`xrefs.py`), and hook it with a hand-computed CRC (`tools/hookcrc.py <table>` prints the image's CRC when the
  entry's is wrong, so write 0 and copy it).
- cTSFamilyRelationshipArray: two sites, both through the constructor 0x4f2070: `Create` (ours) and the persist resource
  factory `cTSPersistResourceFactoryFamilyRelationshipArray::CreateInstanceByType` 0x4f25d0 (since lane DW ours too,
  `new cTSFamilyRelationshipArray`). The vtables are referenced only by the constructor and Release. So with the two
  hooks on, no object carries the game's vtables.
- cTSPersistResourceFactoryFamilyRelationshipArray (DW): one site, the creator 0x4f2740 (`new 8`, vtable, count; not a
  known function, only its address is stored by the cTSSimCOMDirector constructor). The vtable is referenced only there and
  by the deleting destructor 0x4f2170.
- cTSPersistResKeyList (DW): one site, the factory 0x1f4dc0 (constructor inline; its address stored by the
  cTSPersistCOMDirector getter). Vtables referenced only there and by the deleting destructor 0x1f4d20.
- cTSRelationshipTable (DZ): the constructor 0x55ab10, called by cTSNeighbor::Create 0x4b73c0 (ours: now `new`) and by the
  game's constructor 0x52c4b0 (an unnamed class's constructor, not ours: reaches ours through the constructor hook).
  Vtables referenced only by the constructor and the deleting destructor 0x55abf0.
- cTSRelationshipEntry (DZ): no constructor function; built inline by the table's SetEntry 0x55ac60 and Read 0x55b420 (both
  ours). Vtables referenced only there.

## RTTI and vtable identity

- The compiler emits our RTTI (complete-object locator before each vtable, type descriptors named as the original's
  when the classes are global). Whether anything reads it: the image has no `__RTDynamicCast` / `__RTtypeid` (their
  exception texts "Bad dynamic_cast!" and "Attempted a typeid of NULL pointer!" are absent from the unpacked exe), and
  `tools/xrefs.py <type descriptor>` shows only RTTI data referencing the class's and its interfaces' descriptors (no
  code). Exception matching uses ThrowInfo, not the vtable locator. So nothing in the game reads our locators; check the
  descriptors per class anyway (`db/classes.jsonl` has `td`), since a class that is thrown or caught by type would be
  different.
- Vtable comparisons: xrefs on each vtable RVA; any reference outside the class's own constructor/destructor (a
  `cmp [obj], vtable`) must be understood before the shape is taken. None for this class.

## Tests

- **Original side**: object built by the original constructor (game vtables), function called at its RVA, as before.
  No constructor function (inlined into a factory): the original factory builds it (`ctx.Original<X *(__cdecl *)()>`),
  on the heap; free it with the members' destructors and `::operator delete(p, sizeof(X))`, not either side's destructor,
  except in the test of the destructor.
- **Our side**: object built by our constructor through the constructor hook's adapter (or our factory, the factory
  hook's entry), every call through the object's own vtable slot (`CallSlot<R>(face, slot, args...)` in the test; `R` may
  be `void` for a slot whose result is garbage), never `obj->f()` directly.
- **Vtable words** are the only bytes that differ by design: normalise exactly those (the test's `PushVTableWords`: the
  game's vtable on the original side, ours on our side, and ours is not the game's), compare every other byte.
- **Every slot through our vtable**: the slots no other test calls (folded functions, the secondary faces' QI/AddRef/Release
  thunks, slots that return constants) get one table-driven test (`FamilyRelationshipArray_Slots`), each slot called on
  each side's own object, comparing result, out-parameters, fields and vtable words.
- **Adapters**: one test calling each adapter on an object built by the *original* constructor (what runs when the
  constructor hook is off or the game calls an old entry directly), with the game's vtables copied and the forwarded
  slots replaced by a trap, so an adapter that dispatched virtually (in game: back into its own hook, an endless loop)
  fails the test (`FamilyRelationshipArray_Adapters`).
- Fakes that create the class (a framework fake answering `GetClassObject`) build it the side's way (`gSide`).
- Fakes that patch the object's vtable (a recording AddRef) copy the object's *own* vtable first, then patch, and put the
  object's own pointer back before the vtable words are captured.
- A slot that calls other slots of `this` (the factories' folded CreateInstance 0x431e60 calls slots 4 and 5) can hide a
  wrong argument behind an object that answers every id alike: record those calls with a vtable-copy fake that logs its
  arguments and forwards to the side's own slot (`RecordingCreateByType` in the FAMT factory test; a mutant asking for the
  wrong interface survived until then).
- A deleting destructor called through its slot needs a valid first vtable word: seed only the other faces' words with
  junk to show the destructor stores them back.
- Out-parameters: compare "untouched" as its own value (a sentinel), never as an offset from a heap address.
- Keep fakes consistent with what the original can survive: a QueryInterface fake answering true without writing its
  out-parameter crashes the original (it dereferences the result), so "true" always writes.
- Mutants worth keeping in the set: a slot-order swap in an interface, a face-order swap in the base list, a wrong
  face cast in QueryInterface, an adapter missing its `this` adjustment, an adapter dispatching virtually.

## Byte matching

- Hook-shaped matches survive where the adapter's inlined copy compiles to the same bytes: SetDirty, Clear,
  GetGZCLSID still MATCH (matchcheck v6 compares the hook's replacement, i.e. the adapter).
- Gained: **QueryInterface** (99 B) now MATCHes: `static_cast<IFace *>(this)` and a plain `AddRef()` are what the
  original's source wrote; the hand-written `-(this != 0) & face` and `As<>(this)->AddRef()` were not. Natural C++ in
  the shape tends to move codegen toward the original.
- Lost here: **Release** (0.871): byte-identical except the destructor's stores of *our* vtable addresses, which
  matchcheck refuses as named data. **RemoveRelationship** (0.701): the adapter's inlined copy leaves two `std::map`
  internals out of line (one more inlining level than the vtable's copy); the vtable's own copy is the code that
  matched before, but matchcheck only compares hook replacements. Both stay verified by their difftests.
- DW: cTSPersistResKeyList's QueryInterface (87 B) newly MATCHes the same way (0.923 in the hook shape, three hand-written
  shapes tried); GetGZCLSID stays a MATCH. The factory's deleting destructor (0.917) differs only in its vtable store.
- **matchcheck v7** (lane EB; docs/archive/proposals/byte-match-verification.md, "Vtables in the standalone shape"; approved by
  the user 2026-10-18 with v8, which adds namespaced classes; what counts: docs/verification.md): a store of our
  vtable resolves to the original's vftable of the same RTTI class and face (same
  slot count, no slot known to be another function), and a hook whose adapter only forwards
  (`[[msvc::forceinline_calls]] return self->X::f(args);`, exactly as in step 6) ties the RVA to the function our vtable
  holds in that slot: when the adapter doesn't match, that function is compared ("vtable-tie" on the MATCH line).
  Regained: Release and RemoveRelationship (DK); new: the FAMT factory's deleting destructor 0x4f2170 (DW). Keep
  adapters in the one-statement form, or the tie is not made (a hand-written adapter, like the deleting destructor's,
  must match by itself).
- Raw bytes: `bool result = false; if (q) result = f();` passes f's byte through as the original's `mov bl, al`;
  `return q && f();` normalises it to 0 / 1 (a behaviour change when a callee answers a byte like 0x80). Test answers like
  0x80 where the original moves a byte through.
- (Before v7) never matchable: constructors (vtable stores), and anything that stores or compares a vtable. Since v7
  a reference to our vtable resolves to the original's under the conditions above.
- (History: the proposal v7 grew from; v7 implements the tie through adapters, not a `kVTableTies` table.) Proposed
  matchcheck extension ("vtable tie"): a class in the shape lists (our vtable symbol, original vtable RVA)
  pairs (e.g. a `kVTableTies` table next to `kHooks`; MSVC names them `??_7X@@6B<Face>@@@`). matchcheck reads our
  vtable's relocations in the object file and checks slot i of ours against slot i of the original's as it checks a
  hook (bytes, callee proof, thunks); a reference to our vtable then resolves by rule 2 to the original vtable RVA
  when all its slots pair up. That compares the code the game actually runs (the vtable's functions, not the adapters),
  restores Release, makes constructors matchable, and covers adjustor thunks and folded slots. Until then, prefer
  difftests for functions the adapter inlining changes.

## Status entries

What each kind of function in the shape may be recorded as (slot functions behind adapters, the compiler's adjustor
thunks, folded `shared_vf_*` bodies and thunks, dropped adapters): docs/verification.md. Here:
- Slot functions keep their entries; the notes say the hook is an adapter and the difftest goes through our vtable.
- The pilot's own adjustor thunks (0x4f2f63, 0x4f2f6b, 0x4f2f73, 0x4f37c6, 0x4f37ce, 0x4f37d6, 0x4f2dfc) are the first
  entries of the "compiler-made thunks only in our vtables" row.
- A slot only a secondary interface declares (cTSPersistResKeyList's GetRefCount, persist face slot 11) has no thunk:
  the compiler builds the body with `this` = that face, as the original (`mov eax, [ecx + 8]`).
- Creators that are not known function starts here: 0x4f2740, 0x1f4dc0.

## In game (coordinator)

- One switch per class: with the constructor and Create hooks on, every object carries our vtables and every slot is
  ours, whatever the slot hooks say. Turning a slot hook off no longer turns that slot off. To take the class out
  (bisect), turn off **both** the constructor hook and the factory/Create hook; the slot adapters then serve the
  game-built objects as the hook shape did.
- Slots that were the game's until now run ours in game: AddRef, IsDirty, slots 10/11/12/15, the persist face's key
  accessors and every adjustor thunk.
- Heap: objects from the game's factory are allocated by the game's `operator new` and freed by our sized delete, as
  in the hook shape (both CRTs use the process heap).

## Dropping adapters (lane GJ, 2026-10-18; every class from now on)

Once the constructor / factory hooks give every object our vtables, a slot adapter that only the game's vtable reaches
does nothing: it is replaced by "in effect through callers" (docs/verification.md), no hook, the original trapped.

1. **Decide** (with a current `ts2host` build: callers.py scans it for every game vtable our code holds, whatever the
   source form; docs/lane/hook-and-record.md):
   `tools/py -I tools/callers.py --vtable-only --group <status stem> [--class <name>]`. A line `vtable
   slots only (...): its hook can go, "trap": "Hooks.<hook>=1"` is droppable; `<hook>` is the hook its verdict rests
   on (the nearest hooked code that keeps the game's vtable off objects: usually the constructor's, else the factory's;
   destructors, Release and thunks reached only through the vtable don't count, unless their own references are live
   without them: another class's live vtable, our code calling the original; rule: docs/lane/hook-and-record.md,
   "Traps"). When it rests on several hooks the
   line suggests a conjunction, `"trap": "Hooks.A=1&Hooks.B=1"` (lane GT), armed only while every hook holds (e.g. a
   class's constructor and destructor hooks, the COM directors' constructor and Get, a base twin's two hooks). Keep the
   adapter when the entry is a byte match (`"verify": "match"`: matchcheck ties the RVA to our member through the
   adapter; without it the entry is skipped), and for every `keep` line (direct calls, constructors, factories,
   non-virtual members, slots reached from original code). Dropping a hooked slot can change the other entries' deps
   (a deleting destructor's hook stops counting once only the vtable reaches it): rerun `--vtable-only` after removing
   rows and set each trap to what it says now (`status.py --check` reports any that differ).
2. **Remove**: the hook row (`src/hook/tables/<group>.cpp`), the adapter's declaration and definition when no row uses
   it any more, and its calls in the adapters test (the route that calls the adapter on a game-built object; trim the
   trap slots that only it needed). Keep every test that calls our member through our vtable, the Create / constructor
   path and the adapters still hooked; each dropped entry's test must still call its member and name its RVA.
3. **Status entry**: the `"in_effect": "callers"` fields (docs/verification.md, "Status entry fields") with the trap the
   line suggested, and a note saying the adapter is gone and why. Then `tools/py -I tools/callers.py --write-traps`
   (src/hook/trap_table.cpp; a conjunction's further terms go in a shared `kAlso_` array), `tools/status.py --check`
   and `tools/callers.py --check-traps`.
   Where the member itself is what our vtable holds (no adapter: PixelFormat, SimAnticsParams), only the row goes.
   The adapters test's route for a dropped adapter runs the original on both sides, calls our vtable slot, or is skipped;
   a test that checks the object left behind must then expect the original's state on both sides.
4. **In game**: the host reports every hook as the switch `Hooks.<name>` (1 installed, 0 disabled or not matching), so
   the traps arm with the class's constructor hook. Turning that hook off (bisecting) disarms them and the game's own
   slot code serves the game-built objects. A trap that fires logs `trap: original 0x... reached from ...` and stops
   the process with exit code 0x54524150: a caller the check missed (report it, restore the adapter). The armed log
   names every switch of a conjunction (`trap <name> armed at rva 0x... (Hooks.A & Hooks.B)`).
5. **Not only hooks**: the same form carries other switches. The XC static initialisers our build owns
   (db/status/StaticInit.jsonl) have `"trap": "Startup.OwnStart=1,2&Startup.OwnStatics=1"`, the switches under which
   the host's XC runner runs ours in their slots; callers.py covers an XC slot (0xd9a724..0xd9b3a8) together with the
   entry's row in an owned list (src/app/StaticInit*.cpp) and requires the trap to imply those switches. The objects
   those initialisers build stay the image's in game; how our code names them so an image-free build can own them is
   the next section.

## Owned statics (lane QB, milestone N)

A static of the image that our code uses (a global object, a cached pointer and its guard, a function pointer a loader
fills) must be one object in the hooked game, shared by our code and the image's, and must be our own definition in an
image-free program (milestone N: `gmdc_decode` against `ts2core_noimage`). The pattern is a per-build provider of one
named symbol, not a runtime switch:

- **Declare** it once, in the header of the code that owns it: `TS2_OWNED_STATIC(GeneralAllocator, 10cb098);`
  (src/game/owned.h; `extern "C" GeneralAllocator ts2_rva_10cb098;`, the RVA in lowercase hex without 0x). Code uses
  `&ts2_rva_10cb098` / `ts2_rva_10caea0` where it used `GameAddress<T>(0x10cb098)`.
- **Hooked builds** (ts2host, the difftest harness, anything linking ts2core with its origsyms.obj): tools/origsyms.py
  scans src/ for the declarations and defines `_ts2_rva_<hex> EQU <base + rva>` in the generated origsyms.asm. The
  symbol is the image's object; the code is the same relocated immediate GameAddress gave (matchcheck reads
  `ts2_rva_<hex>` as the RVA, checkgamebase.py checks the value). No behaviour changes in game, and no switch: the
  image's initialiser (or our XC entry in its slot) still builds the object.
- **Image-free programs**: ts2core_noimage drops origsyms.obj, so the program defines each owned static it links,
  zero-initialised like the image's .bss, in standalone/owned_statics.cpp (`TS2_OWNED_STATIC(T, hex){};` or with a
  constant initialiser), and runs what the game's XC initialiser does from its own start-up (an `init_seg(lib)` object
  there: the general allocator's Construct(nullptr, 0, 1, 0, nullptr, nullptr), Shutdown at exit). tools/noimage.py
  counts an owned symbol nobody defines as the image address it stands for, so the list stays honest.
- **Thread-safe statics** (`static T x = ...;` in a function, the image's TLS epoch at 0x10df814): use our compiler's
  own. A guard check is `if (*guard > _Init_thread_epoch)` with `extern "C" __declspec(thread) int _Init_thread_epoch;`
  (our CRT's), then `_Init_thread_header` / `_Init_thread_footer` as before. Our CRT's epoch and the image's count
  separately; a guard the image completed carries the image's epoch value, and the worst a mismatch does is take the
  slow path (header sees the guard done and returns). The guard itself stays the image's (an owned static, or
  `GameAddress` until owned). MSVC 14.40 hits an internal compiler error (C1001) when a constant-address guard is
  combined with `_Init_thread_epoch` and the header/footer in one function; `GameAddressOpaque<int>(rva)` for the guard
  avoids it.
- **Vtables of hook-shaped classes** (cRZString, cRZCriticalSection: members hooked at the game's slots, objects built
  inline by both sides): the vtable is an owned static, `TS2_OWNED_STATIC(const OwnedVTable, d9b470);` (`OwnedVTable`
  is an incomplete type in `game/owned.h`; only the address is used), and `X::VTable()` returns its address. Our
  constructors, destructors and inline builders store `X::VTable()` (the same one-instruction `offset` store as
  `GameAddress(kVTable)`). In hooked builds and the difftest harness the symbol is the game's table, whose slots are
  hooked to our members, so objects ours and the image's build are the same objects with the same word. An image-free
  program aliases the symbol to our own table: a namespaced face `rzface::X final : public cIGZX` whose overrides
  forward to the members (`[[msvc::forceinline_calls]]`, any extra slot such as the deleting destructor declared last),
  through `#pragma comment(linker, "/alternatename:_ts2_rva_d9b470=??_7X@rzface@@6B@")` in
  standalone/owned_statics.cpp, which also references `X::OwnVTable()` (reads a local face's vptr, folds to
  `offset ??_7X@rzface@@6B@`) so the face's vtable is emitted. Tests call our table's slots through `OwnVTable()`.
  Why not our table in game (lane QB tried it): vtable words are part of the objects the whole difftest suite compares
  (577 failures across cRZString users), and every image-side `GameAddress(kVTable)` store would have to change at once.
  A folded `shared_vf_*` slot body (4 B `IsValid`, 0x886f4) gets its own body in the face and is never hooked; it is
  not in effect in game (the game's table still points at the original), so its entry stays `mapped`, tested through
  `OwnVTable()`. matchcheck ties the namespaced class to the game's by its bare RTTI name.

## More cases (lane DZ: cTSRelationshipTable and cTSRelationshipEntry)

- **Deleting destructor in the middle of a vtable** (the table's slot 14 of 18): declare the virtual destructor in the
  *interface* at that position (`virtual ~cITSRelationshipTable() = default;` between slots 13 and 15) and override it in the
  class (`~cTSRelationshipTable() override;`). A destructor the class itself declares would be appended after slot 17. The
  compiled destructor stores only the class's vtables, as the original's (the interface's empty destructor adds no store), and
  `delete this` in Release compiles to the original's `push 1; call [eax+0x38]` (Release kept its MATCH).
- **No destructor slot at all** (the entry: Release destroys inline, no slot for it): keep the destructor non-virtual (private,
  so only Release can destroy); `delete this` then calls it inline with the sized delete, as the original (Release kept its MATCH).
- **A class built only by another class's code** (the entry: inline in the table's SetEntry 0x55ac60 and Read 0x55b420, no
  constructor function, no factory): when the builders are ours, `new X(...)` in them is the whole switch, and there is nothing
  to hook. The tests' original side is built by the original builder (`MakeOriginal` in tests/ts_relationship_entry.cpp: the
  original table constructor on a scratch buffer, the original SetEntry, the original table destructor with flags 0, then the
  test's field values); our side by our constructor.
- **Creation sites in another group's code**: cTSNeighbor::Create (ours) built the table with `operator new` +
  `Construct()`. It must change to `new cTSRelationshipTable` (outside the lane's files: say so in the report), and that
  group's test compares the new object's bytes: its vtable words are normalised there too (`Neighbor_Create`).
- **Non-virtual members** that were hooked (LoadEntry, SetEntry) get adapters like the slots: `MemberAddress` does not compile
  for a class with several bases.
- **Calls on `this` through a secondary face**: `return Write(stream);` (the class's own override, dispatched through the +4
  vtable) adjusts `this` without a null test, as the original (`add ecx, 4`); `static_cast<cIGZSerializable *>(this)->Write()`
  adds the `neg/sbb/and` null test. WriteForPerson / ReadForPerson gained their MATCH from this.
- **A byte field handed out by a folded getter** (`mov al, [ecx + 0x18]`, IsModified): declare the field `bool`. A `uint8_t`
  field returned as `bool` is normalised (`setne`), which a raw-byte test (0x80) catches. Tests reach the raw byte by offset.
- **Trap vtables need the slot's arity**: where the *original* legitimately calls a trapped slot (WriteForPerson calls the +4
  face's Write), a trap declared `__fastcall Trap(void *, void *)` returns with `ret` instead of `ret 4` and corrupts the
  stack (an access violation in our code, far from the cause). One trap per stack-argument count (`Trap0`..`Trap3`).
- **Fakes that patch vtables, per side**: copy the side's own vtables into the fake arrays first (`InstallCopies`), patch, and
  compare vtable words as codes (the side's own / the patched copy / anything else raw): the patched copy is then the same
  array on both sides and the slot under test is still the side's own.
- **Lost match**: cTSRelationshipEntry::GetValue (30 B) no longer matches through its adapter, inlined or not: the adapter loads
  the vector's end before its begin; the same body as a `__thiscall` member (the hook shape) matched. Verified by difftest.

## Checklist

- [ ] classinfo: every face, slot count and folded slot listed; folded bodies disassembled.
- [ ] xrefs on the constructor, factory and every vtable RVA: every creation site routed or listed as left.
- [ ] RTTI: type descriptors of the class and its bases have no code references; no vtable comparisons.
- [ ] interfaces: slot order and `ret N` match; overloads declared in reverse order (MSVC).
- [ ] class: bases in RTTI order; `static_assert` size and member offsets; no `GameAddress` vtables; not `final`.
- [ ] adapters: qualified calls, face casts, forced inlining; hook entries point at them.
- [ ] tests: our side built by our constructor, every call through its vtable; vtable words normalised and nothing
      else; a slots test for the rest; an adapters test with trap vtables.
- [ ] mutants: slot order, face order, face cast, adapter adjustment, adapter dispatch; plus the usual per function.
- [ ] matchcheck on every previously matched RVA; status entries updated (matches lost say why).
- [ ] report: creation sites left in game code, matches lost, in-game risks.

## Must not

- Hook a virtual member by member pointer: for a multiple-inheritance class `MemberAddress` does not compile (8-byte
  member pointers); for a single-inheritance one it compiles to a *vcall thunk*, which dispatches through the object's
  vtable: on an object with the game's vtables that is the game slot, i.e. the hook itself, an endless loop.
- Forward an adapter with a virtual call (same reason). Always `obj->X::f()`.
- Mark the class or its slots `final`: calls on `this` that the original makes through the vtable would be
  devirtualized (different code, and vtable-patching fakes stop seeing them).
- Store vtable addresses by hand (`GameAddress(kVTable)`, ours by symbol) or keep the face structs: the compiler does it.
- Derive from the generated `ifaces::` views without fixing their facts first.
- Hook folded `shared_vf_*` functions because the class now implements them.
- Normalise more than the vtable words in a comparison, or compare an object built by the original constructor on our
  side (that tests the hook shape, not the class).
- Take the shape for a class whose objects the game builds inline (vtable stored in a function that isn't ours) without
  saying so: those objects never see our vtable.

## Alternatives considered

- **Hand-built vtable tables** (pixel formats: `constinit` arrays of `__fastcall` thunks forwarding to non-virtual
  members, null RTTI word). Keeps `MemberAddress` hooks and all byte matches, but every class carries a table per face
  that must be kept in slot order by hand, the class is still not usable as its interface type, and the standalone
  build keeps an extra thunk per slot. Fine for a closed family (pixel formats); not the shape to write hundreds of
  classes in.
- **Dropping the slot hooks** (they are unreachable once every object carries our vtables): leaves the
  constructor-hook-off configuration without our code and the tools (matchcheck, status) without a replacement to
  check. Kept as adapters at the pilot; since 2026-10-18 (user, "drop adapters nothing references") adapters nothing
  references are dropped and their originals trapped ("Dropping adapters" above).

## Open items

Checked against the code on 2026-10-18 (lane FH). Done since the first version: the shared interface headers (src/gz/cIGZ*.h,
src/ts/cITSAppFW.h, src/ts/cITSDataContainer.h, which two classes implement), matchcheck's vtable tie (v7) and its
namespace-stripped class names (v8), whether adjustor thunks only in our vtables count (yes: user, 2026-10-18;
docs/verification.md), and the classes listed under "Done" below.

- **Interfaces with one implementer** stay in their class headers: `cITSFamilyRelationshipArray`
  (src/ts/cTSFamilyRelationshipArray.h) and `cITSPersistResKeyList` (src/ts/cTSPersistResKeyList.h); move them to src/ts/
  with a second implementer. Longer term `tools/ifacegen.py` could emit implementable interfaces (inheriting `cIGZUnknown`,
  full slot counts) from the facts, so callers and implementers share one definition.
- **cIGZPersistResourceFactory past slot 6**: the facts give slots 7-9 (deleting destructor, two 3-argument CreateInstance forms)
  from cTSResourceFactoryForIGZSerializable; the FAMT factory has slot 7 the destructor and slot 8 GetRefCount. The shared header
  declares slots 0-6 and each implementer (the FAMT factory, cGZPropertySetFactory) adds its own.
- **Two auto-ref helpers**: `cRZAutoRefCount` still lives in src/ts/cTSFamilyRelationshipArray.h (the FAMT factory includes that
  header for it), while cTSSaveGameInfo uses `rz/RZAutoRef.h`. Settle on one, in src/rz/.
- **A shared adapter helper** (a macro or template in src/game/): none yet, though every shape class now writes the same
  one-statement adapters by hand.
- **Creators that are not known function starts** (hooked with a hand-computed CRC; status.py notes "not a function start"):
  0x1f4dc0 and 0x4f2740 are in db/missed-functions.jsonl as manual entries (`tools/missedfuncs.py --add`); 0x1f6740
  (cTSResourceFactoryForIGZSerializable::Create), 0x1eba40 (cTSResourceFactoryStringSet::Create) and cTSFamilyHistory's
  creator 0x49e1e0 are not yet.
- **Classes left half-way**: cTSFamilyHistory (FA: slot 8 0x49d9c0 is a CSV dump needing `_wfopen` / `fprintf` / `_close`
  faked); cTSAppFW (A2, hook-shaped: it stores the game vtables itself, so gz::cGZApp's members are still reached through
  adapters); the game's
  inline-built string property sets (0x1bec45 + 0xd9f718 at the site) stay on the slot adapters until their owners convert.
- **Done** in the shape: cTSFamilyRelationshipArray (DK), its factory and cTSPersistResKeyList (DW), cTSRelationshipTable /
  Entry (DZ), cTSNeighborhoodInfo (EA), cTSLotInfo (ED), nTSGenetics::cTSSimDNA (EE), the property-set factory and string
  serializer delegates (EF), cTSNeighborhood (EG, EM), cTSNeighbor (EJ), cGZPersistResource and the string property-set resource
  (EL), cGZCOM and cGZCOMLibrary (EN), the COM directors (EO), the property sets, uint32 dispatcher and uint32 resource (EQ),
  gz::cGZFramework (ER), the uint32 serializer delegates (EU), gz::cGZApp (EV), cTSFamily (FA). Next: the persistence classes
  the save round-trip exercises that are still hook-shaped (`tools/status.py --classes`).

## A cRZUnknown base, five faces, hook-shaped members (cTSNeighborhoodInfo, lane EA)

The third retrofit, and the first with a cRZUnknown base (src/ts/cTSNeighborhoodInfo.*, tests/ts_neighborhood_info.cpp). What
was new against DK / DW:

- **cRZUnknown as a real base.** The layout allows it (vtable + count, 8 bytes, at +0x10 after four interfaces): the class is
  `class X : public cITSNeighborhoodInfo, public cIGZSerializable, public cIGZPersistResource, public cIGZSelfDescribing,
  public rz::cRZUnknown`. The global name `cRZUnknown` is held by the hook-shaped struct in rz/cRZRefCount.h (16 files and the
  cRZRefCount hooks use it), so the real class is `rz::cRZUnknown` in `src/rz/RZUnknownBase.h` (RTTI name differs; nothing reads
  it) until cRZRefCount's group is taken to the shape and the two merge. Its six slots are the game's shared bodies, out of line
  (0x1b945c QI, 0x148fb0 AddRef, 0x1b94e9 Release, the virtual destructor, 0x1b9538 RemoveRef, 0x147a30 the count); not hooked, no
  status entries (they belong to cRZRefCount / RZFrameworkHelpers).
- **Overrides forwarding to the base.** The original's AddRef/Release (folded 0x233070 / 0x233700: `add ecx, 0x10; jmp`
  cRZUnknown's) are `uint32_t AddRef() override { return rz::cRZUnknown::AddRef(); }`; with the base body out of line that is the
  original's code. One override fills all five faces' slot 1 (the compiler's adjustor thunks are the folded 0x233078 etc.).
  QueryInterface's default case is `return rz::cRZUnknown::QueryInterface(riid, out);` (the original's `lea ecx, [edx + 0x10];
  jmp 0x1b945c`), and the describe face is counted with `rz::cRZUnknown::AddRef()` (direct, as the original; the other faces
  through the vtable).
- **Same-named slots in two bases.** An override fills every base's slot of that name and signature. The persist face's
  GetRefCount (slot 11, 0x47c6e0 `add ecx, 8; jmp 0x147a30`) must not override cRZUnknown's slot 5 (the game's +0x10 vtable has
  0x147a30 itself there), so the base's slot 5 has another name (`RefCount`) and GetRefCount forwards to it. Check each secondary
  vtable for slots the class did *not* override before naming base slots.
- **A destructor slot only in a secondary base.** cRZUnknown's slot 3 is the only destructor slot (the primary interface has
  none): `~X() override;` in the class. The compiler's deleting destructor then sits in the +0x10 vtable (the original's thunk
  0x4a66ac + body 0x4a66c0); `delete this` in cRZUnknown's Release reaches it through the +0x10 vtable, as the original.
- **Members that are hook-shaped classes.** cRZString is still non-polymorphic with `Construct()` / `Destruct()` (they store the
  game's string vtable). As a plain member its implicit constructor and destructor would build and destroy its std::string a
  second time; each text is an anonymous union member (`union { cRZString mName; };`), built in the constructor's body and
  destroyed in the destructor's, as the original's inlined code does. Replace with plain members when cRZString takes the shape.
- **Creation sites**: Create 0x4a6720 (constructor inline) and 0x767bc7 inside an unnamed function at 0x76794d (game
  `operator new(0xd4)`, then the constructor 0x4a6350): the constructor hook's adapter covers the second. xrefs on the vtables:
  only the constructor, Create and the destructor 0x4a6500.
- **Adapters of secondary-face slots and byte matches.** `static_cast<Self *>(face)->Self::f()` (and even
  `static_cast<Self &>(*face)`) left a null test in the face-to-object-to-face round trip of the inlined call (`test ecx, ecx;
  cmove`), which cost the matches of GetKey / SetKey / SetInstance (17-19 B). `__assume(face != nullptr);` before the reference
  cast removes it: all three MATCH again. (A slot's `this` is never null.)
- **One table-driven adapters test.** A `__fastcall (face, edx, args)` adapter is ABI-identical to the `__thiscall` slot, so the
  test calls the original RVA and the adapter through the same thiscall pointer type: one table of {rva, adapter, face, trap slot,
  argument shape} covers all 31 adapters (NeighborhoodInfo_Adapters). Trap only the slot each adapter forwards to, not all of
  them: an adapter's body may legitimately call other slots of `this` (IsCategory13WithFlag4 calls slot 14, whose adapter is
  tested with its own trap).
- **Vtable-word normalisation with a base vtable.** After destruction the +0x10 word holds cRZUnknown's vtable (the game's
  0xda2fd0 / ours): the tests map each vtable word to one of {the world's copy of face f, the side's own vtable of face f, the
  side's cRZUnknown vtable}; anything else stays raw (and then differs between the sides). Our cRZUnknown vtable comes from the
  word our destructor leaves at +0x10.
- **Fakes that patch slots of the object's vtable** (recording AddRef, slot 4, slot 14, the persist key getters): the world copies
  the side's own vtables for all five faces and patches the copies; our side's test calls go through our pristine vtable
  (`gOurs[face][slot]`), so a patched slot is still tested as ours.
- **Byte matches**: kept all 9 (SetNumber, IsNumber, SetFlags, HasFlags, GetSlotValue, GetKey, SetKey, SetInstance,
  GetGZCLSID; the three persist-face ones only after the `__assume`), gained MatchesName (57 B). QueryInterface 0x4a6d00 scores
  1.000 but fails on its call to our `rz::cRZUnknown::AddRef` against 0x148fb0 (a folded body, not hookable): a "vtable tie" or a
  shared-body tie in matchcheck would settle it.

## One face, a factory only, self-calls patched by the tests (nTSGenetics::cTSSimDNA, lane EE)

The simplest retrofit so far (src/ts/cTSSimDNA.*, tests/ts_sim_dna.cpp): one interface (`nTSGenetics::cITSSimDNA`, 18 slots), 12
bytes, no constructor function. What was new against the earlier classes:

- **Interface from the ifaces facts, with real types.** `ifaces::SimDNA` has the slot order and arities right but `void *` for
  every vector, pair and DNA argument; the implementable interface declares them typed (`const DNAWeightedVector *`,
  `cITSSimDNA *source`), so the bodies lose their casts. The namespace is part of the RTTI name: declare the interface in it.
- **Calls on `this` through the vtable are the body's own fakes in the tests.** CopyFrom, the vector getters and setters and the
  appearance functions call HasGene / GetGene / SetGene / the vector slots on `this`; the old tests replaced those slots with a
  fake vtable (`selfVT`, `dnaVT`) on both sides. In the shape the fake array is a copy of the side's own vtable with the fake
  slots patched over it (rebuilt for each side; in a world fixture keep the patches in a separate array, `dnaPatch`, and overlay
  them on the copy when the side is set), the object carries the copy, and the slot under test is called through the side's
  pristine vtable (`CallOwn`), since the patched copy may fake the very slot under test (PropertyAccessors fakes GetGene).
- **Helpers that took `void *dna`** (FetchPair, ReadStrings, WriteStrings) now take `cITSSimDNA *`: the same virtual call, no view.
- **Adapters test: give the forwarded slot's body something to read.** With a null store the vector getters return after
  HasGene, so a mutant passing the wrong `second` flag survived; patching slots 5 / 6 of the trap copy with fakes that answer
  "present" and a pair of empty strings makes the channel visible (the count's Strlen call names the string) without growing
  the output vector (which would reach the allocator service).
- **A trap at the slot's arity and a fake for the slot the body calls legitimately**: Release's adapter calls slot 17 (`delete
  this`) on a game-vtable object, so the trap copy gets the recording delete fake there, not the game's destructor (which would
  free the test buffer).
- **Byte matches**: Release kept its MATCH (the adapter itself, v6 and v7). QueryInterface (41 B) gained one: `if (!out) return
  false; switch (id) { case 1: case kInterfaceId: ... }` compiles to the original's `sub eax, 1; je; sub eax, 0xc7aad38` chain,
  where `id != 1 && id != kInterfaceId` gave `cmp` / `cmp`. The deleting destructor's adapter (0.931) stores our vtable after
  loading the store, the original before (scheduling); being hand-written it gets no vtable tie. No v7-only matches.
- **Creation**: Create 0x4f9020 only (its address stored by the cTSSimCOMDirector constructor 0x415ae0); the vtable is referenced
  only by Create and the deleting destructor 0x4f5e40. The Create hook is the class's switch.
- **cTSNeighborhood (not converted, for the next lane)**: Create 0x473b80 (address stored by the cTSSimCOMDirector constructor)
  calls Construct 0x471d90, the only creation path; the four vtables (0xdc9be0 77 slots, 0xdc9d18 12, 0xdc9d4c 6, 0xdc9d68 4) are
  referenced only by Construct and Destruct 0x472490; type descriptor 0x10a5738 only by RTTI data. 26 of its 99 vtable functions
  are still game code, three of them large bodies with no reimplementation yet (slot 17 0x479cd0 268 B, slot 66 0x479de0 1515 B,
  the data-container face's slot 5 0x477d70 2402 B), and its tests span eight files (ts_neighborhood*.cpp): a lane of its own.

## cRZUnknown as the primary base, a creator hook, two routes per test (cTSLotInfo, lane ED)

src/ts/cTSLotInfo.*, tests/ts_lot_info.cpp, tests/ts_lot_info_services.cpp, tests/ts_lot_info_shape.h. What was new:

- **cRZUnknown first.** RTTI puts cRZUnknown at +0 (vtable + count at +4) and the four interfaces after it (cITSLotInfo +8,
  serializable +0xc, persist +0x10, describe +0x14): `class cTSLotInfo : public rz::cRZUnknown, public cITSLotInfo, ...`. The
  object pointer the game hands around is the +8 face, so every `cITSLotInfo *` adapter adjusts by -8. The +0 face's AddRef /
  Release are folded bodies (0x146a50 / 0x1f9aa0, jumps to cRZUnknown's); the overrides forward to the base as EA's do, and the
  base's slots 4 / 5 stay its own (the game's +0 vtable has 0x1b9538 / 0x147a30 there).
- **Creator hook.** The default constructor 0x4c8a40 is called only by the creator 0x4c9180 (`operator new(0xac)`, the
  constructor, the +8 face with a null test), registered by address: hooked as `static cITSLotInfo *Create() { return new
  (std::nothrow) cTSLotInfo; }` (the conversion's null test is the original's). The copy constructor's only caller is Clone (ours).
  Bisect switch: cTSLotInfo_Construct, cTSLotInfo_CopyConstruct and cTSLotInfo_Create together.
- **A slot nobody had** (56, 0x4cb630, `SetMappedValue`): the conversion fills every slot, so the last unreplaced one was written
  (map store + a standard message posted to the cached server, the server getter 0x9c5a89 still original).
- **Written-out adapters for v7 ties.** matchcheck v7 reads each adapter's definition from the source text: an adapter produced
  by a macro, or with `__assume(face != nullptr);` before the call, is not the one-statement form and gets no tie. With 50
  adapters a generator script wrote them out once. Numbers for this class (53 entries with a hook): **v7 18 MATCH (17 through the
  vtable tie)**, v6 1 (GetGZCLSID): `static_cast<Self *>(face)->Self::f()` keeps the face-to-object null test (`test ecx, ecx;
  cmove`), which the original's slot code doesn't have. EA's `__assume` + reference-cast form matches by the adapter itself
  under both v6 and v7 (18 / 18 here too), but isn't the one-statement form. Pick one per class: the one-statement form if v7 is
  agreed, otherwise the `__assume` form.
- **One test body, two routes.** tests/ts_lot_info_shape.h `lotshape::Call<Fn>(ctx, orig, object, face, rva, adapter, args...)`
  runs the original RVA on one side and, on ours, either our vtable's slot (looked up by the RVA's position in the game's vtable
  of that face) or the adapter on an object whose vtable copy has an arity-matched trap wherever the game's vtable holds that
  RVA. Each Pair runs both routes (seeds of the adapter route carry the top bit) and fails on any trap hit, so every existing
  test became a Slots and an Adapters test without a second table. Folded slots (no RVA of their own) are a separate table
  (LotInfo_Slots) through our vtable only.
- **Register garbage in a forwarded argument.** CreateNewLotInfo passes the rotation byte to the new lot's slot 21, and the
  original pushes the whole `eax` (upper bytes left from an earlier call). Our side now zero-extends (`uint32_t rotation =
  GetRotation();`), and the test masks the logged argument to its low byte, citing the push at 0x4cad7d. Mask in the test at the
  point the fixture logs the raw argument, not after Norm (a garbage word in the stack range was normalised to a pointer).

## Property-set factory and serializer delegates (lane EF)

The property-set family (lanes DP, DU, DV, DY) is a web of classes that contain or derive from each other, so the order is by
what creates what and what derives from what: cGZPropertySetFactory (top: creates resources), the binary and XML delegates
(leaves: created by the dispatcher), then the dispatcher, the sets and the resources. Done here: the factory and both
delegates (src/gz/cGZPropertySetResource.h, src/gz/cGZPropertySetSerializer.h, src/gz/cIGZPropertySetSerializer.h).

- **A template-instantiation class keeps its RTTI name** as an explicit specialization: `template <> class
  cGZPropertySetSerializerBinaryT<cKeyStringTraitsT<ibasic_string<char>, 0x4bb0d269, 0x6bb22f38>> : ...` with a `using` alias for
  readable code. matchcheck v7 then resolves a store of our vtable to the original's (0xdd4418). Read the template's integer
  arguments from the mangled name with MSVC's digits (A-P are 0-15: `$0GLLCCPDI@` is 0x6bb22f38, not ...fd8). Members of an
  explicit specialization are defined without `template <>`; the constructor and destructor are named by the template's name
  (`Alias::~cGZPropertySetSerializerBinaryT()`), not the alias.
- **Interfaces with default bodies.** cIGZExpatContentHandler's slots 6-11 are the interface's own `return false` bodies (shared
  by every handler: 0x147480, 0x1474b0, 0x18a7a0, 0x147110): declared virtual with inline bodies in the interface, so every
  implementer's vtable gets them; never hooked, no entries.
- **A secondary base with the destructor and a primary without one** (the factory: cIGZPersistResourceFactory + rz::cRZUnknown,
  as cTSNeighborhoodInfo): the original's deleting destructor 0xc0edaa is reached through the folded thunk 0xc0ed0e (shared with
  other classes of the same layout, e.g. cTSBinInfoFactory): no hook, no entry.
- **A bool argument passed on through a slot** (the factory's Write hands the serializer `binary`): the inlined adapter copy
  left stale upper bytes in the pushed dword (0x6e0000xx) where the original pushes a clean 0 / 1; only the adapters test saw it.
  Pass a `uint32_t` 0 / 1 through `VirtualCall` where the original pushes a dword.
- **A game class derived from ours, built inline**: cTSWantTreeLoader::vf3 0x553210 builds a 0x64-byte class derived from the
  XML delegate (vtables 0xdd4370 / 0xdd438c), storing the base's handler vtable itself. Those objects never carry our vtables;
  the slot adapters serve them (their bodies run on the derived object as on the base). Reported, not converted.
- **Trap vtables on a game-built object with two faces**: copy both of the game's vtables, trap only the forwarded slots of each
  face (handler slots 3-5 with arities 4 / 1 / 2), and leave slots the body calls (QI's AddRef) as the game's.
- **A class built only by our code** (the binary delegate: no constructor function, the dispatcher builds it inline): the
  original side of the tests builds the game's shape by hand (vtable word, fields), as the original's inline code does.
- **Byte matches**: the factory and the XML delegate had none. Lost: the binary delegate's deleting destructor 0x552160 (33 B,
  a v6 MATCH; v6 refuses our vtable store; v7 resolves the vtable by RTTI name but then fails the forwarding tie because the
  adapters' `Self` is the alias name and our vtable's symbol is the template's: a matchcheck fix, alias-aware).
- Left for the next lane: the dispatcher (only ever a base of the resources: no standalone object exists), the two sets
  (many creation sites: appinit, cTSSimDNA, SimAntics, the CAS factories), the resources (they derive from cGZPersistResource
  (ctor 0x9b5fd0 / dtor 0x9b6030 / Release 0x9b6260 still the game's), the set and the dispatcher: all three must be classes
  first), and the uint32 serializers (their slots are still the game's).

## cTSNeighborhood, first half: every slot body ours (lane EG; the conversion is still to do)

Lane EG implemented and verified the four slot bodies that were still game code, hooked as plain RVA hooks in the hook shape
(src/ts/cTSNeighborhoodDebug.cpp, src/ts/cTSNeighborhoodImport.cpp, tests/ts_neighborhood_debug.cpp, ts_neighborhood_import.cpp):
slot 17 RunLotResetCommands 0x479cd0, slot 66 DebugDrawFamilyArches 0x479de0, the +8 face's slot 5 ImportFromContainer 0x477d70,
and slot 31 GetSyncTableCount 0x475330 (a 5-byte `jmp 0x4b8fd0`: hookspec refuses it as a thunk, so it has a member and a difftest
but no hook or status entry; in game the jmp already lands in our hooked 0x4b8fd0). What the conversion lane needs:

- **Slot bodies left as game code: none.** What remains in the class's 99 functions is folded getters and setters (`shared_vf_*`:
  slots 1, 11, 13, 15, 53, 64, 72; persist face 5-9; container face 3; listed by `classinfo.py cTSNeighborhood --todo` with their
  one-line bodies) and the adjustor thunks of QI / AddRef / Release on the three secondary faces: in the shape these become
  overrides and compiler thunks.
- **Hooks**: 80 entries in src/hook/tables/cTSNeighborhood.cpp, all `MemberAddress` on the hook-shaped struct or face structs; each
  becomes an adapter (`MemberAddress` does not compile for a class with several bases). Non-slot members hooked: Construct 0x471d90,
  Destruct 0x472490, EnsureStateHelper 0x473a70, Create 0x473b80, the sync-table free functions (0x4b8ff0 / 0x4b8fd0 / 0x4b8f80) and
  FlagLotTokens 0x476e80 (cdecl, no adapter needed).
- **Calls on `this` through the vtable** are written `VirtualCall<..., N>(this)` throughout the bodies (and `VirtualCall<..., 6>(
  PersistFace())` for the group): in the shape they become plain calls on `this` (GetLot, UnregisterRenderIds, GetLotInventory, ...).
- **Tests** build the object with `Construct()` on a raw buffer (ts_neighborhood.cpp, _lots, _lots_b, _lots_c, the _lots_d family
  through ts_neighborhood_lots_d.h, _services, _debug, _import), and several patch slots of
  a copy of the game's vtable (37, 55, 57, 67, 76; the DK/EE "copy the side's own vtable, patch, call through the pristine one"
  rule applies).
- **Original callees left**: 0x472e10 (HighlightLot, thiscall (lot, name), ret 8: erases and re-registers the lot's render id in
  +0xa8 through the SG render id registry; 664 B, not a slot) from DebugDrawFamilyArches; the class's other original calls are listed
  in its status file.

## An interface chain at +0, a fake-vtable world, both routes in one Pair (cTSNeighbor, lane EJ)

src/ts/cTSNeighbor.*, tests/ts_neighbor.cpp, tests/ts_neighbor_services.cpp. What was new:

- **A "base vtable" that is only the interface's.** The hook-shaped Create stored 0xda66e4 at +4 before the class vtable, noted as
  a base constructor's. xrefs show 0xda66e4 stored by ~30 constructors of unrelated classes (cTSFamily, cTSLotInfo, the FAMT
  array, ...): it is cIGZPersistResource's own vtable, the interface constructor's store. No extra base class; check a "base
  vtable" this way before inventing one.
- **Interfaces chained in one face.** RTTI lists cITSNeighbor, cIGZSerializable and cIGZUnknown all at +0: cITSNeighbor derives
  from cIGZSerializable (slots 3-5), so `class cTSNeighbor : public cITSNeighbor, public cIGZPersistResource`, with
  `virtual ~cITSNeighbor() = default;` last in cITSNeighbor for the deleting destructor slot 53. The primary face is the object,
  so its adapters need no cast (`self->Self::f()`); they keep (or gain) their v6 matches by themselves.
- **A world whose `this` vtable is a table of fakes.** The old tests gave the neighbour a vtable of recording fakes (slots 1, 2,
  8, 10, 12, 15, 16, 53) and nulls elsewhere. In the shape each side's object carries a *copy* of its own vtables (ours on the
  vtable route, the game's on the original side and on the adapter route) with the fakes patched over it; the slot under test is
  called through our pristine vtable (the copy may fake that very slot, e.g. GetPersonData slot 10). One `Pair` runs both routes
  (adapter-route seeds carry the top bit), so every existing test became a Slots and an Adapters test (ED's pattern, without a
  shared header: the fixture is the test file's own).
- **A fake `this` that is not our class** (NeighborServices_BuildDisplayName: `this` is a services-fixture object): our side calls
  the function our vtable holds in slot 40 (taken from a neighbour built once on a scratch buffer) or the adapter, on the fake;
  the fake records a call on its own slot 40, so an adapter that dispatched virtually shows in the log.
- **Hidden-result and reference adapters can't be tied.** GetKey (`*out = ...; return out;`) and SetKey (`SetKey(*key)`) are not the
  one-statement form; in that form they lost their matches (null test). With EA's `__assume(face != nullptr)` + reference cast they
  MATCH by themselves under v6 and v7. The six other persist adapters keep the one-statement form: v7 MATCH through the tie, v6 not.
- **Numbers** (49 hooked entries): v7 29 MATCH (28 kept, IsPersonDataFlagged 0x4b84c0 gained), v6 23 (the six persist one-statement
  adapters lose the v6 match to the face null test).
- **Creation**: the creator 0x4b73c0 only (constructor inline, address stored by the cTSSimCOMDirector constructor 0x415ae0); the
  vtables are referenced only there and in the deleting destructor 0x4b72e0. Bisect switch: the Create hook (`FUN_a04b73c0`).

## A shared base with game-built subclasses, a partly hook-shaped base (cGZPersistResource and the string property-set resource, lane EL)

Continuing EF: src/gz/cGZPersistResource.*, src/gz/cGZPropertySetResource.h / cGZPropertySetResourceString.cpp, the dispatcher in
src/gz/cGZPropertySetSerializer.*, the set interface in src/gz/cIGZPropertySet.h; tests tests/gz_persist_resource.cpp,
tests/gz_property_set_resource.cpp, tests/gz_property_set_serializer.cpp.

- **A base class whose subclasses are mostly the game's.** cGZPersistResource (0x1c bytes, one face) has ~70 RTTI subclasses
  (every cSGResource, cRZStringResource, cGZBufferResource, ...); only the property-set resources are ours. The game's subclasses
  call the base's constructor 0x9b5fd0 / destructor 0x9b6030 directly and jump to its Release 0x9b6260: those three hooks plus the
  deleting destructor 0x9b6050 are adapters (placement new / qualified destructor / forwarding), so every game subclass now runs our
  base part (its vtable word is ours until the subclass's constructor overwrites it, and again after its destructor). Nothing else
  in a subclass changes; no subclass needed converting. The base's vtable 0xe603ac is referenced only by its own three functions.
- **A hook-shaped class taking over a global name.** The global `cGZPersistResource` was TopCallersA's 8-byte struct holding
  Release only: removed (struct, hook entry, status line, test) and the real class took the name, so its RTTI name and vtable
  symbol are the original's. matchcheck v7 then resolves our vtable store to 0xe603ac: the destructor and the hand-written deleting
  destructor adapter MATCH under v7 (v6: 0.500 / 0.917); Release kept its v6 MATCH.
- **A hook-shaped caller of a converted base.** The uint32 resource stays hook-shaped; its Construct / Destruct call the base's
  adapters through a `__thiscall` cast of the adapter's address (`reinterpret_cast<void(__thiscall *)(void *)>(&Adapters::Destruct)
  (self)`: a `__fastcall (self, void *)` adapter is the same ABI, and calling it as fastcall would load the dummy edx, one more
  instruction): Destruct 0xc2bcb3 and the deleting destructor 0xc2bcf0 kept their MATCH (the call resolves to 0x9b6030's hook).
  Its QI's default is a qualified call `reinterpret_cast<cGZPersistResource *>(this)->cGZPersistResource::QueryInterface()`.
- **A base that stays hook-shaped elsewhere: a real twin under a namespace.** The resource's +0x1c face is the string set, whose
  global name is the hook-shaped struct (lane DP; standalone sets have many creation sites outside the group). `gz::cGZPropertySetString`
  is a real class with the struct's layout (static_asserts on every offset against the struct) whose 15 slots run the struct's
  bodies on `this` (`Hooked()->SetProperty(...)`, a reinterpret_cast); calls those bodies make on `this` through the `ifaces` views
  reach our vtable. Its destructor frees the key text first (a union member, destroyed by hand), then clears the entries, as the
  original's 0x1777e0 / 0x1bed22. When the set takes the shape, the twin becomes the class and the struct goes.
- **Template-argument naming.** The resource is `cGZPropertySetResourceT<cKeyStringTraits, cGZPropertySetString>` (explicit
  specialization; the second argument is the global hook-shaped struct, only its name matters for the mangled name): its RTTI name
  is the original's. Inside it, `cGZPropertySetString` names the *base's* injected class name (the gz twin): spell the struct
  `::cGZPropertySetString`.
- **Same-named slots in three bases**: QueryInterface / AddRef / Release / the destructor override all three faces at once (the
  compiler's thunks are the original's 0x5553ed / 0x5553f5, 0x5522bd / 0x55609d, 0x5520c3 / 0x5520cb; the set face's AddRef /
  Release thunks 0x5522b5 / 0x556095 are folded). No other slot names collide.
- **Tests: vtable words with base vtables after destruction.** Each of the three words maps to 1 (the side's live vtable of that
  face), 2 (what the side's destructor leaves: the original's 0xe603ac / 0xda3dc0 (the set's RTTI base) / 0xdd4270, ours read from
  an object our destructor ran on), or stays raw (`ResWords`). The resource tests destroy through each face's destructor slot in turn
  (the two thunks).
- **The dispatcher has no object of its own** (only a base of the resource): the original side of its tests builds the game's
  shape by hand (vtable 0xdd4270, fields); its non-virtual ReadWith is called directly on ours, its adapters on the game's shape with
  arity-matched traps in slots 3-5.
- **Byte matches**: none lost; gained (v7 only) 0x9b6030 and 0x9b6050. The dispatcher and the string resource had none (EH frames,
  inlined adapters); the template-class adapters cannot get a v7 tie (matchcheck's `member_of` reads `?f@Class@` and does not parse
  a template class's `?f@?$Name@...` symbol, besides EF's alias issue).
- **Left**: the uint32 resource (0x44 bytes) needs the uint32 set and the uint32 dispatcher in the shape (its slots 0-2, Read 0x2425ad,
  Write 0x242899 and its two delegates are still the game's); the sets themselves (standalone creation sites: appinit, cTSSimDNA,
  SimAntics, the CAS factories).


## cTSNeighborhood, second half: four faces, 80 adapters, nine test files on two routes (lane EM)

The conversion EG's section prepared (src/ts/cTSNeighborhood.h, the cTSNeighborhood*.cpp bodies, tests/ts_neighborhood_shape.h).

- **Bases, in RTTI order**: `cITSNeighborhood` (cIGZUnknown 0-2, cIGZSerializable 3-5, slots 6-75, and the virtual destructor as
  slot 76: `Release`'s `delete this` is the original's slot-76 call with flags 1), `cIGZPersistResource` (+4, 12 slots),
  `cITSDataContainer` (+8, 6 slots; hoisted from cTSFamilyRelationshipArray.h into src/ts/cITSDataContainer.h, which now includes
  it) and `cIGZMessageTarget2` (+0xc, 4 slots). The folded `shared_vf_*` getters and setters are inline overrides; the QI and
  Release thunks of the three secondary faces (0x47721e / 0x477226 / 0x47722e, 0x4786fa / 0x478702 / 0x47870a) are the compiler's.
  xrefs: the four vtables are referenced only by Construct 0x471d90 and Destruct 0x472490; Construct is called only by Create
  0x473b80, which cTSSimCOMDirector's constructor registers. So `Create` (`new (std::nothrow) cTSNeighborhood`) is the one
  creation path, and the bisect switch is Create and Construct together.
- **Members with non-trivial types** (the name string, the lot and text vectors, the three member maps, the service ids) sit in
  anonymous unions, so the constructor and destructor bodies build and destroy them in the original's order with the releases
  interleaved; the destructor is the old Destruct body without the vtable stores.
- **Adapters**: one statement each, placed in the file of the body they inline (a status entry's `src` must hold the adapter, or
  matchcheck reports NOSYM). Secondary-face slots take the face pointer (`static_cast<Self *>(face)->Self::f(...)`); getting this
  wrong (a `reinterpret_cast`) is a mutant the tests kill. A struct return (persist GetKey) takes the hidden pointer as an explicit
  stack argument after the `void *` edx slot, with `__assume(face != nullptr)` so the face-to-object cast has no null test.
- **A non-slot member that slot bodies call** (EnsureStateHelper 0x473a70) stays a `static __fastcall` member taking `self`, and the
  hook points at it: an adapter would be a second function, and the callers' byte matches (Initialize) tie their call to the hook.
- **Tests**: tests/ts_neighborhood_shape.h gives every file the two routes. Vtable route: our object from our constructor, the
  call through the pristine slot of our vtable. Adapter route: the object from the original constructor, the adapter called with
  trap copies of the object's vtables (any trap hit fails, so an adapter that dispatches virtually instead of inlining is caught).
  Fakes copy each side's own vtable before patching (`CopyOwn`), and only vtable words are normalised. Neighborhood_Slots covers
  the slots no body test calls (folded getters, setters, thunks) through each side's own vtable.
- **Byte matches**: 27 of 80 functions under matchcheck v7 (25 under v6: the persist face's SetKey and SetInstance match only
  through v7's vtable tie).

## A service on the rz:: bases, built before the hooks (cGZCOM and cGZCOMLibrary, lane EN)

cGZCOM (startup F1) is the first framework service in the shape on EI's `rz::cRZSystemService`. What was new:

- **Faces.** `class cGZCOM : public cIGZCOM, public rz::cRZThreadSafeObject, public rz::cRZSystemService` gives the game's
  layout: cIGZCOM at +0, the system service's cIGZSystemService at +4 and cRZUnknownMT at +8, the lock pointer at +0x20.
  QueryInterface handles cIGZCOM's id and forwards the rest to `rz::cRZSystemService::QueryInterface`; AddRef and Release
  forward to the base (Release to `RemoveRef`, as the original's tail jump). The +4 face's SetServiceRunning (slot 11) is the
  compiler's thunk into our override.
- **Built before the hooks** (wrong: the hooks go in before the entry point; see the correction at the top of
  docs/subsystems/startup.md). The game constructs the framework, and cGZCOM inside it, before our hooks are in, so the live object
  never gets our vtables: in game every call goes through the game's vtables into the hooked RVAs, i.e. the adapters. The
  constructor and the framework's construct/destruct base (fw_lifecycle) are ours and placement-new our class, so the tests and
  any later build of the object get our vtables. The adapter is the only route in game; GZCOM_Adapters (trap vtables matched to
  each slot's arity) is what checks it. Nothing in the hook shape could tell these apart.
- **Objects we create inside a game-built object.** cGZCOMLibrary values live in cGZCOM's `std::set` (ordered by `operator<`,
  which asks the other library's path through its vtable). AddLibrary inserts our class, so once the hooks are in, the set holds
  libraries with our vtable inside an object with the game's; both work because every slot of both vtables is a body we own or
  a compiler thunk. The path is a hook-shaped `cRZString` in an anonymous union, built and destroyed by hand (its class is not
  in the shape yet).
- **Tests across a clock.** CreateGuid / CreateGuids read `_time64` and the local seconds since the GUID epoch and wait in
  quarter seconds. The tests fake the GetSystemTimeAsFileTime and SleepEx import slots (shared by both sides) and patch both
  0x145774 and our `appbase::SecondsSinceGuidEpoch` with a jump to one fake, so the two sides see the same clock.
- **Void slots in an adapter test.** A trap vtable comparison must not compare eax for void slots (FreeUnusedLibraries,
  RealShutdown, SetServiceRunning, UnloadLibraries, the callback enumeration): the result mode is per case.
- **matchcheck and adapters.** `--brief` compares the hooked RVA with the adapter: for a one-statement adapter that the compiler
  turns into a 5-byte `jmp` to the member (the constructors, FreeUnusedLibraries, UnloadLibraries, RealShutdown) it reports
  "size 5 vs original N" rather than checking the member. Where the member inlines into the adapter (CreateGuid, CreateGuids,
  Release, GetPath) it matches.

## One class per data-driven family, static storage, D2 forwards (the COM directors, lane EO)

src/rz/RZCOMDllDirectorShape.h, RZCOMDllDirector.cpp / .size.cpp, COMDirectors.cpp; tests/com_directors.cpp, tests/static_getters.cpp.
What was new:

- **A family of 41 classes from one table.** The directors differ only in RTTI name, id, constructor steps and a few
  overrides, so they are one template, `rz::cCOMDirectorT<N>` over the index in `comdir::kDirectorSpecs` (the steps stay
  data: `RunSteps(N)` in the constructor). Overrides are explicit specialisations of members the template declares
  (OnStart for 22, the hooks face for TSUI 3/5, Sound 5/6, HitMan 4/6); the template's own forwards exist only so they
  can be specialised. `template class cCOMDirectorT<N>;` for every N emits all vtables in one object.
- **Statics in the game's storage.** Each getter (and GetCOMDirector, the MadCow initialiser) does placement new of our
  class into the game's static under the game's guard and registers our own exit handler (a qualified destructor call).
  Every creation site of the 41 + root is one of these hooked functions (xrefs of the base constructor 0x1adffa and of
  every director vtable), so all 42 carry our vtables. The audio director (three faces) stays hook-shaped.
- **An override still in game code (handoff D2).** Our slot forwards with `OriginalFunction<Fn, rva>()(this, ...)` and the
  getter's status entry lists the RVA in `original_calls` (13 of them). The original runs on our object: the layouts are
  the same and it dispatches through our vtables.
- **A test object with no steps.** `cCOMDirectorT<N>(NoSteps{})` gives a director of class N without running its
  registrations (and without building its sub-directors): the slot tests and the vtable-word learning use it.
- **Vtable codes.** Take/Snap map each class's game vtable and ours (learnt once from objects built on a scratch buffer)
  to one code per class and face; "destroyed" in the exit-handler tests is "its +0 code became the base's".
- **DirectorSlots.** For every class, each slot the game's vtable shares with the base is called on both sides (or must be
  the base's function in ours), and each slot the game overrides must be an override in ours (28 expected).
- **Shared bodies.** 0x18e5f0 / 0x1ab22a (deleting destructors) and 0x1fc520 (id of PathPlan and TSCommon) are folded
  across classes: not hooked, tested through each class's own vtable slot only. TSPersist's slot 6 (0x1d3b30) is a
  5-byte jump to the base's GetClassObject: ours inherits it.
- **Exceptions.** Our derived constructors always unwind the base if a step throws (C++); the originals without
  `kCtorUnwinds` leave it. Only reachable on out-of-memory.
- **Numbers** (184 hooked entries in the group): 43 MATCH under v7 and v6 alike (46 before: the five deleting destructors
  of classes with an out-of-line destructor body are lost because ours inlines the empty derived destructor, and v7 cannot
  tie classes in namespace rz anyway; gained StoreFrameWork 0x1448f7 and the audio id 0x20f022). Bisect switch: the getters,
  GetCOMDirector, the constructors and InitMadCow together.


## Property sets, the uint32 dispatcher and the uint32 resource (lane EQ)

Continues EF and EL: every class of the property-set family that the game builds through a hooked constructor now carries our
vtables.

- **One table, two sets**: `gz::cGZPropertyTable` (non-polymorphic: refcount, sorted entry vector, cursor; 0x14 bytes) holds the
  bodies both sets share (find, set, remove, clear, first / next, value, count). `cGZPropertySetUint32` (an explicit specialization
  `cGZPropertySetT<cKeyUint32Traits>`, so the RTTI-style name matches the original's) and `cGZPropertySetString` (the hook-shaped
  struct and EF's `gz::` twin are gone: one class) are interface vptr + table. `cKeyUint32TraitsT` takes `long` template
  arguments because the mangled IID is negative. The string set's key text vector sits in an anonymous union so its destructor
  can run the original's order (key text, then entries); the RTTI base destructor 0x1bed22 is `DestroyEntries`.
- **Creation sites** (decided per caller): the uint32 set constructor 0x1bec5f and the string set constructor 0x22b157 (moved from
  AppInit's table) are placement-new adapters, so their 14 / 5 game callers and our uint32 resource build our class. Sites that
  inline the base constructor 0x1bec45 and store 0xd9f718 themselves (Prim6D's original, cTSConfigurationManager, 0x1774a0, the
  0xcb1xxx / 0xccdf90 group) stay on game vtables served by the adapters. (cGZPropertyManagerString, which derives from the set,
  is our class since lane PU: src/gz/cGZPropertyManager.h.) Our SimAntics `NewPropertySet` still builds a game-vtable set because its test patches 0xd9f718.
- **The uint32 dispatcher** is a real class (`cGZPropertySetSerializerT<cKeyUint32Traits>`) with Read / ReadWith / Write ours; its
  two delegates (binary 0x236be6, XML 0x241ea6) are still the game's classes, built through `OriginalFunction` constructors.
- **The uint32 resource** derives from cGZPersistResource, the set and the dispatcher (0x44 bytes, faces at +0x1c / +0x34); our
  factory's CreateInstance2 builds it with `new`, its only creation site. Its six adjustor thunks are compiler-made and named in
  PropSetRes_Uint32ConstructDestroy / PropSetRes_QueryInterface.
- **Tests**: as EM's two routes. Sequence tests alternate the route by seed parity; adapter-route traps are matched to each slot's
  arity (Trap1 / Trap2), and tests that end with Release restore the game vtable first (Release's delete goes through slot 13).

## A base class whose live object is a subclass, members the game's subclass overrides (gz::cGZFramework, lane ER)

The framework base (startup F2). The live object is a cCTGFramework built by the game before the hooks; its own slots 41..48 are
item F3. What was new:

- **Slots the subclass owns.** The base's slots 41..46 and 48 are never reached in game (cCTGFramework overrides them); until the
  subclass is in the shape, ours call the original bodies through `OriginalFunction` (listed in `original_calls` of the
  constructor's entry, the file's anchor), so the vtable is complete and the deleting destructor (slot 40) is ours.
- **Two routes in every test, chosen by seed half.** tests/framework.cpp (the hook-shape suite, ported with no lost case) runs each
  seed loop twice: the first half calls the adapters on an object from the original cCTGFramework constructor (the live object's
  case) with trap vtables on both faces (every slot a logging trap of the slot's arity, so a self-call that goes through the
  vtable shows up), the second half calls the pristine slots of our own vtable on an object from our constructor. One `Ops`
  table per route; only the vtable words are left out of the comparisons.
- **A process exit in a difftest.** AbortiveQuit ends in `exit`. The original's call (0x82f6bf) is redirected with a jump and ours
  goes through a static function pointer (`sExit`, default `&exit`); both reach a recorder that logs the code and `longjmp`s back.
  The function is not hooked: from the plugin `exit` would be our CRT's, and the game's atexit list would not run.
- **Static original callees in a template.** `origsyms.py` emits link symbols only for literal `OriginalFunction<Fn, 0xRVA>` uses,
  so a helper templated on the RVA does not link (LNK2019 `OriginalEntry<...>`). Pass a lambda that holds the literal use instead.
- **Secondary-face adapters.** As in lanes ED / EA, the one-statement form keeps the face-to-object null test and lost GetMainHWND
  / SetMainHWND's byte matches; those two use `__assume(self != nullptr)` + reference cast and match under v6 and v7.
- **matchcheck v7 ties and namespaces.** The v7 vtable tie looks our vtable up by its RTTI name: ours is `.?AVcGZFramework@gz@@`,
  the game's `.?AVcGZFramework@@`, so no adapter of a `gz::` (or `rz::`) class is ever tied, and every adapter that compiles to a
  5-byte `jmp` reports "size 5 vs original N". v6 and v7 give the same 13 / 65 here. Comparing the name without the namespace
  (or recording each class's namespace in classes.jsonl) would let the tie work.

## One template per delegate for both key types (the uint32 serializer delegates, lane EU)

Finishes the property-set family: the uint32 dispatcher's binary and XML delegates are our classes, and they share every body
with the string delegates.

- **Traits, not copies**: `cGZPropertySetSerializerTraits<cKeyStringTraits / cKeyUint32Traits>` holds what differs (set and key
  types, magic, original vtables, the XML name cells); `cGZPropertySetSerializerBinaryT<Traits>` and
  `cGZPropertySetSerializerXmlT<Traits>` are the classes, their bodies and adapters in `src/gz/cGZPropertySetSerializerDelegates.h`.
  The key differs only through overloads (`ReadKey` / `WriteKey` / `KeyFromText` / `KeyText` on `GZServiceString *` or
  `uint32_t *`). Explicit instantiations keep each build profile: string in `cGZPropertySetSerializer.cpp` /
  `cGZPropertySetSerializerXml.cpp` (/O2), uint32 in `cGZPropertySetSerializerUint32Delegates.size.cpp` (/O1). RTTI names are the
  template's, as the original's.
- **Read db/ names against the code**: 0x1adad0 is labelled "append" but calls assign 0x144402; the uint32 XML delegate assigns
  key attributes and the root name, as the string one does. Check a helper's callees before giving traits a behaviour flag.
- **Creation sites**: the dispatcher's `new` (Read / ReadWith / Write; the `OriginalFunction` constructors are gone) and the
  constructor hooks 0x236be6 / 0x241ea6 (adapters; the game's own stack-built binary delegates in 0x238159 / 0x2386c5 reach them).
  A hook was refused for WriteText 0x238987 (inside Write's chunk, not a function start): covered through Write.
- **Inline-built string sets** (0x1bec45 + 0xd9f718 at the site): only our SimAntics `NewPropertySet` was ours, and it now builds
  our class; the Prim6D test patches slot 3 of both the game vtable and ours (read from an instance). The game's sites
  (cTSConfigurationManager vf5 / vf15, 0x1774a0, 0xcb1790 / 0xcb18a0 / 0xcb1cf0 / 0xcb1e60 / 0xcb28b0, 0xccdf90) are unowned and
  stay on the slot adapters. cGZPropertyManagerString, derived from the set, is our class (lane PU, src/gz/cGZPropertyManager.h).
- **Status `src` for template bodies**: `tools/origcalls.py` pairs each `OriginalFunction` call with an entry whose `src` is the
  file that contains it, so entries whose body calls an original name the header.

## A system service with the app face, created only by an unconverted subclass (gz::cGZApp, lane EV)

- **Class**: `gz::cGZApp : rz::cRZSystemService, cIGZApp` (0x40 bytes, faces at +0 / +4 / +0x1c), moved out of
  `cTSAppFWShape.h` into `src/gz/cGZApp.{h,size.cpp}`. AddRef / Release are written as overrides calling the base (the game
  folds them into 0x1cc320 / 0xb359c9, so the thunks 0xc5a02e / 0xc5a036 jump into the folded bodies); the name stays a
  hook-shaped cRZString in an anonymous union.
- **Creation**: the subclass cTSAppFW (A2, hook-shaped) is the only creator and stores the game vtables itself, so the hooks on
  the cGZApp members are served by adapters until A2 converts the subclass; the deleting destructor 0xc59fc7 got its own entry.
- **Tests**: Slots goes through all 34 slots of the three faces on both sides (a fake g_Framework; the bare-ret slots 11 / 12
  have no result to compare), Adapters uses game-built objects with the slot under test trapped. Each thunk RVA is named in the
  test that calls it (kThunks in tests/gz_app.cpp).
- **Interface facts in the shared header can be wrong**: cITSAppCommon slots 13-16 return bool (al), which only showed when the
  bodies were ported (a void declaration let a caller's tail call through slot 14 pass unnoticed).

## A shared fake-vtable world on two routes, self-recursive slots (cTSFamily, lane FA)

src/ts/cTSFamily.h and the four cTSFamily*.cpp bodies; tests/ts_family.h (routes, `Call`, `CallSlot`), tests/ts_family_world.cpp
(build, copies, traps), the eleven ts_family*.cpp test files. What was new:

- **Faces**: as cTSNeighbor: `cITSFamily` (a cIGZSerializable, slots 6..79, the virtual destructor declared at slot 77 between 76
  and 78) and `cIGZPersistResource` (+4). 26 slots were folded `shared_vf_*` bodies (AddRef, the persist-face getter, the text and
  lot / field accessors 37..58, the persist face's key accessors 3..11): inline overrides now, never hooked. The persist face's QI /
  Release thunks (0x49f5e2 / 0x49fe9a) were hooked in the hook shape (as `FamilyPersistFace` members); they are compiler thunks now,
  unhooked, with status entries kept as EJ did.
- **Creation**: the constructor 0x49b340 is called only by the creator 0x49e180 (`new 0xc8`, constructor, null test; its address
  stored by the cTSSimCOMDirector constructor); the vtables are referenced only by the constructor and the destructor 0x49b690.
  cTSFamilyManager creates families through the framework, i.e. through 0x49e180: no change outside the lane. Bisect switch: the
  Create hook (FUN_a049e180) and cTSFamily_Construct together.
- **One fixture change converts every test.** All eleven test files go through the world's `Build` and either `Pair` / `Pair2` or
  their own `for (side)` loop. Making `Build` build the side's and route's object (game constructor or ours), copying the side's own
  vtables before patching the world's fakes, normalising the two vtable words in `Capture`, and running `Pair` once per route turned
  each existing case into a vtable-route and an adapter-route case. The calls themselves were rewritten mechanically from
  `original ? ctx.Original<Fn>(rva)(F(), a) : F()->M(a)` to `Call<Fn>(ctx, original, rva, FAM_ADAPTER(M), a)` (a regex; check the
  result for a dropped or extra parenthesis where the old member call had a cast in its arguments).
- **Bodies that call their own slot.** InsertKeyIntoGroup1 (slot 10) re-enters slot 10 through the vtable for the keys of a lot
  part, and InsertObjectIntoGroup2 (slot 18) re-enters slot 18. A trap that only counts makes the adapter route fail although the
  adapter is right. The traps here count and then forward to what the copy held in that slot (the side's own function or the
  world's fake), and they are installed on every side, original included: a legitimate re-entry counts alike on both sides, an
  adapter that dispatches virtually counts once more (and the count is part of the compared blob). Mutants of both kinds (an
  unqualified forward in InsertKeyIntoGroup1's and LoadSecondGroup's adapters) are killed.
- **A non-slot member with a hook** (QueryMemberContainer 0x49dfd0, ImportLotObjectsIntoGroup1/2, the destructor): `Call` finds no
  slot for the RVA and calls the adapter on both routes (on our object on the vtable route).
- **A hand-written deleting-destructor adapter that keeps its match**: the original 0x49ba50 calls the destructor 0x49b690 out of
  line. Inlining our destructor into the adapter lost the match (0.105); calling the Destruct adapter through a `__thiscall` cast of
  its address (EL's trick: a call that resolves to 0x49b690's hook, without loading the dummy edx) matches under v6 and v7.
- **Byte matches** (66 entries with a hook): v7 24 MATCH, v6 21. Kept: the flag-bit accessors, member id accessors, SetFamilyId,
  SetName / SetDescription, ClearIdSet, IsFamilyLot, SetFlag79, GetGZCLSID, the deleting destructor. Gained: GetUniqueId, SetFunds,
  FlushChanges (v6 and v7), GetFunds (v7 only). Lost under v6 only: GetMemberId, FindMemberId (v7 ties them).
- **Left: cTSFamilyHistory** (not converted): its slot 8 0x49d9c0 (1500 B) is still game code, a CSV dump of the entries to a file
  (`_wfopen` 0x83aa0f, `fprintf` 0x46f6a0 per field, `_close`): a difftest needs the three CRT functions faked. Its creator is inline
  at 0x49e1e0 (vtables stored at 0x49e1fb / 0x49e202, not a known function start: the hook needs a hand-computed CRC).

## A factory-built manager with a fake-slot world, three routes per Pair (cTSFamilyManager, lane FF)

src/ts/cTSFamilyManager.{h,cpp}, tests/ts_family_manager.cpp.

- **Faces**: `cITSFamilyManager` (declared in the class header: slots 3..25, the virtual destructor last as slot 26, the deleting
  destructor 0x4b4550), `cITSDataContainer` (+4, the shared header) and `cIGZMessageTarget2` (+8). Folded slots made overrides:
  AddRef (0x15e520, a byte-identical CRT match), the family count (slot 16, 0x3a5a20) and the container's CollectReferenced (slot
  3, 0x18a7a0 `xor al, al`). The secondary faces' QI / Release thunks (0x4b5f99 / 0x4b5fa1, 0x4b6897 / 0x4b689f) are the
  compiler's; they get status entries (FA / DK rule), their AddRef thunks are folded (no entry).
- **Creation**: no constructor function; Create 0x4b5440 (constructor inline, address registered by the cTSSimCOMDirector
  constructor 0x415ae0) is the only site, and the vtables are referenced only there and by 0x4b4550. `new (std::nothrow)
  cTSFamilyManager` in Create is the whole switch (bisect: FUN_a04b5440). Families are made through the framework (cTSFamily's
  Create, already ours).
- **Members with destructors in anonymous unions** (the id set and the log, as EM): the original destroys log, set, removed
  families, families; the vectors are trivially destructible structs, so plain members would run their `Destroy()` in the body
  before the set and log.
- **Calls on `this`** (`Call(this, 11, id)` etc.) became plain virtual calls (`GetFamily(id)`); in DoMessage (the +8 face's
  body) `RemoveEmptyFamilies()` adjusts `this` by -8 and dispatches through slot 15, as the original. Mutants that devirtualise a
  self-call (`cTSFamilyManager::FindFamilyByLot(lot)`) are killed: the fixture fakes those slots in each side's copy.
- **Tests: a world whose primary vtable was a sparse fake table** (eight slots set, nulls elsewhere). Now each side's object carries
  copies of its own three vtables (the game's on the original side and the adapter route, ours on the vtable route) with the eight
  fakes patched over the primary copy; the slot under test is the original RVA, our pristine vtable's slot (`gOurs`) or the
  adapter with an arity-matched trap in the slot it forwards to (`Call<Fn>(ctx, original, rva, face, ADAPTER(f), args)`; the slot is
  found from the RVA's position in the game's vtable of that face). `Pair` runs the original once and compares it with both routes,
  so every existing case (14 tests) runs on both; vtable words are coded as copy / the route's class vtable / raw. FamilyManager_Slots
  covers the folded slots and the four thunks (checking each thunk RVA is the game's slot) through each side's own vtable. The
  trap / VtCode / SlotOf / Call helpers are again a private copy (no shared test header exists yet).
- **Byte matches**: none were recorded in the hook shape. Gained (v6, v7 and v8 alike, by the adapter itself): QueryInterface
  0x4b5f50 (73 B: one `*out = face; AddRef(); return true;` tail per case, as the original duplicates it; one merged tail gave
  0.792), Release 0x4b6880 (23 B: `if (mRefCount <= 1) { delete this; return 0; } return --mRefCount;` gives the original's block
  order) and UnregisterMessages 0x4b6e40 (25 B). No v7 vtable tie turned into a MATCH (the tied members differ in frame and register
  allocation, as they did as hook-shaped members).

## A slot that writes a file, CRT calls through a seam, a creator hook (cTSFamilyHistory, lane FI)

src/ts/cTSFamilyHistory.{h,cpp}, tests/ts_family_history.cpp.

- **Faces**: as cTSFamily: `cITSFamilyHistory` (a cIGZSerializable; slots 6-9, `virtual ~cITSFamilyHistory() = default;` as slot
  10) and `cIGZPersistResource` (+4, the key at +0x18). Folded slots made overrides: AddRef 0x192140, GetPersistFace 0x1c04e0
  and the persist face's key accessors / GetRefCount; the persist QI / Release thunks 0x49f642 / 0x49fec7 are the compiler's
  (status entries, no hook), the AddRef thunk 0x4805dd is folded.
- **A destructor that must store the vtables**: `~X() override = default;` (the only member a std::vector) compiled to no
  vtable stores at all; the original's 0x49ba80 stores both before destroying the vector (visible with flags 0). Declaring
  `~X() override;` and defining `X::~X() {}` out of line gives the stores. Check a defaulted destructor against the original's.
- **CRT calls in a slot (slot 8, 0x49d9c0, the text dump)**: our body calls our CRT's `fopen` / `fprintf` / `fclose` through
  static function pointers (`sOpen`, `sPrint`, `sClose`, defaults `&fopen` etc.: ER's sExit pattern); the test swaps them for
  recording fakes and redirects the original's calls (0x83aa0f, 0x46f6a0, 0x838ff4) to the same fakes with a 5-byte jump. The
  fake fprintf formats with vsnprintf into a per-route buffer; the comparison is the call log (path, mode, each format and
  length, the FILE pointer, the close) plus the buffer. No file is ever opened. Folded library bodies hide which function
  was called: 0x83aa0f is `_wfopen` and `fopen` alike, 0x838ff4 `_close` and `fclose` alike; the narrow "w" at 0xdc9808 and
  the FILE * argument decide (fopen / fclose).
- **The creator** 0x49e1e0 (not in db/functions.jsonl; int3 padding before it, its address registered by the
  cTSSimCOMDirector constructor) is hooked as `static cITSFamilyHistory *Create()` with the CRC from tools/hookcrc.py: the
  class's switch (bisect: cTSFamilyHistory_Create). FamilyHistory_Create compares the original's object with ours (vtable words
  coded, the rest raw) and frees both with the sized delete.
- **Tests: three routes per case, one helper** (`Routes(ctx, what, seed, run)`): the run lambda builds the route's object
  (game shape with copies of the game's vtables, or ours from our constructor with copies of ours) and calls `Call<Fn>(ctx, h,
  rva, face, ADAPTER(f), args)`; route 0 is compared with routes 1 and 2. The old fake-vtable patches (recording AddRef,
  deleting destructor) are patched over the route's own copy. FamilyHistory_Slots covers the folded slots and the two thunks.
- **Byte matches** (11 hooked entries with a matchcheck verdict): v8 and v7 alike 3 MATCH: GetGZCLSID (kept), QueryInterface
  0x49f5f0 (82 B, gained: a switch with one `*out = face; AddRef(); return true;` tail per case) and Release 0x49feb0 (23 B,
  gained: FF's `if (mRefCount <= 1) { delete this; return 0; }` form). The deleting destructor (0.867) calls the vector's
  destructor where the original calls its out-of-line copy 0x49b600 (unknown to matchcheck). No v7 tie gives a MATCH.

## Three faces on tests/shape.h, a file-system slot with a stood-in scan (cTSNeighborhoodManager, lane FN)

src/ts/cTSNeighborhoodManager.{h,cpp}, src/ts/cTSNeighborhoodManagerLoad.cpp, tests/ts_neighborhood_manager_shape.h,
tests/ts_neighborhood_manager.cpp, tests/ts_neighborhood_manager_load.cpp.

- **Faces**: `cITSNeighborhoodManager` (slots 3-19, `virtual ~cITSNeighborhoodManager() = default;` as slot 20, the deleting
  destructor 0x4a1fb0), `cIGZPersistResource` (+4; the key at +0x34, all its accessors folded bodies made inline overrides) and
  `cIGZSerializable` (+8). The QI / Release thunks of the two secondary faces (0x4a54d7 / 0x4a54df, 0x4a5557 / 0x4a555f) are the
  compiler's (status entries, no hook). Self-calls (First -> Next, Open -> SetKey and Load, Save -> the persist face's GetGroup)
  are plain virtual calls; mutants that devirtualise them are killed.
- **Creation**: Create 0x4a3f20 is the only site; `new (std::nothrow) cTSNeighborhoodManager` is the class's switch (bisect:
  FUN_a04a3f20). The destructor is out of line (`~X() override;`, FI's rule) and destroys the list member.
- **Tests on tests/shape.h** (lane FJ): the class's helper header (`hoodmgrshape`: face offsets and slot counts, an arity table,
  per-route vtable copies with fakes patched over them, `Call<Fn>(ctx, object, rva, face, ADAPTER(f), args)`) is about 90 lines;
  the rest comes from shape.h. Every case runs three routes and route 0 is compared with routes 1 and 2.
  NeighborhoodManager_Slots covers the folded slots.
- **A 4 KB slot that walks the file system (Load, 0x4a4160)**: written in full (with its helper HasPackage 0x4a6000). Its
  callees that reach the disk or another class are originals stood in on both sides with a 5-byte jump: the directory scan
  object (0x1c48da build, 0x1c63d4 find, 0x1c5047 entry, 0x1c509f entry path, 0x1c49eb destroy; ours calls them through
  `OriginalFunction`, so patching the image covers both sides), PathExists (the image's and ours), the tutorial 0x4a27a0. The
  stand-ins make up entries from the scan's directory and the hoods' names. **One generator feeds every fake and stand-in in
  call order**, so a different call on our side parts the two worlds at once. The test passed on the first run; the 18 mutants
  (17 killed, one equivalent) are what showed that it reaches each branch.
- **Byte matches** (24 entries, 22 with a verdict): v8 and v7 alike 4 MATCH, the same under v6: QueryInterface 0x4a5480 (87 B,
  gained: FF's one-tail-per-case switch) and Release 0x4a5540 (23 B, gained: FF's form), GetGZCLSID 0x4a4050 and First 0x4a3f10
  (kept). Read, GetById and Add get v7 vtable ties, which change only their scores. Load and HasPackage report NOSYM: their
  adapters live in the class's main file while the members are in the Load file.

## Two conversions on tests/shape.h (cTSNeighborhoodGroupData, cTSObjectTypeAttributeManager, lane FP)

src/ts/cTSNeighborhoodGroupData.*, src/ts/cTSObjectTypeAttributeManager.*, tests/ts_neighborhood_group_data.cpp,
tests/ts_object_type_attribute_manager.cpp. Both were finished hook-shaped classes whose factory (constructor inline) is the
only creation site, registered by the cTSSimCOMDirector constructor; vtables referenced only by the factory and the deleting
destructor. The factory hook (`new (std::nothrow) X`) is each class's switch.

- **cRZUnknown as a secondary base, again** (the group: `cITSNeighborhoodGroupData` at +0, `rz::cRZUnknown` at +4 / count +8, as
  cTSNeighborhoodInfo's +0x10): AddRef / Release overrides forward to the base (the folded `add ecx, 4; jmp` 0x1dab20 / 0x1e3970),
  QueryInterface's default case is `rz::cRZUnknown::QueryInterface` (1.000 but no MATCH: the tail jump's target is our base,
  0x1b945c is unhooked), the destructor is the base's slot 3 (the +4 face's thunk 0x4728af). The folded getters of slots 7 / 8
  (+0x4c / +0x50) became inline overrides.
- **Destructor order with members between two released pointers**: the original destroys the ordered map (+0x54), releases +0x50
  and +0x4c, then the tables (+0x2c, +0xc). With the map in an anonymous union (destroyed first in the body) and the two raw
  pointers released after it, the implicit member destruction does the tables in the original's order. A mutant swapping the
  two releases is killed (the fakes log releases).
- **Names that hide**: a helper `AddRef(void *)` in the .cpp's anonymous namespace would be hidden inside members by the class's
  own `AddRef()` once the class has one; renamed `AddRefObject` / `ReleaseObject`.
- **A non-slot member called directly by slots** (LoadNeighborhoods 0x476a40, `__declspec(noinline)`): kept a member, its hook an
  adapter (compiles to a `jmp`). `MemberAddress` no longer compiles (8-byte member pointer), so the Add / Refresh tests read the
  code address as the member pointer's first word to patch their stand-in over it.
- **Fixtures**: both tests keep their worlds and gained `gRoute` (0 original, 1 our object from our constructor / Create with
  copies of our vtables, 2 the game's shape with copies of the game's), a `Call<Fn>(ctx, original, rva, face, ADAPTER(f), ...)`
  on shape.h's `SlotOf` / `TrapFor` / `Ours`, a `VtCode` (copy / class vtable of either side / cRZUnknown's after destruction /
  raw) and a three-route `Pair`. Every existing case runs on both routes; a Slots test calls the folded slots and thunks through
  each side's own vtable (`routes = 2`). The group's original side keeps building the game's shape by hand (no constructor
  function); the manager's comes from the original Create.
- **Self-calls need a logging forwarder, not just the side's own slot**: the group's BeginIteration (slot 19 calls 20) and the
  manager's AddAllFrom (slot 8) and Save (the persist face's GetKey) survived a devirtualising mutant while the copy held the
  route's own function there; a fake that logs and forwards to the route's own slot (`gRoute == 1 ? ours : game`) kills them.
- **Byte matches**: the group, matchcheck v8: 8 MATCH (kept Reset, BeginIteration, Next by the adapter; GetById / QueryByKey only
  through the vtable tie now; gained Remove, MarkDirtyByKey, GetByKey through the tie); v6 rules 3. The manager: v8 3 (GetGZCLSID,
  Clear, and the hand-written deleting-destructor adapter 0x4d8a30, whose vtable stores v8 resolves), v6 rules 2; QueryInterface
  0.625 (block order of the switch, either case order in the source).

## Two source files of slots, adapter parameter types for the v7 tie, a union member for destruction order (cTSNHoodTerrain, lane FO)

src/ts/cTSNHoodTerrain.{h,cpp,size.cpp}, src/ts/cTSNHoodTerrainQueries.cpp, tests/ts_nhood_terrain.cpp.

- **Every slot first.** 22 slots were still game code (DD's lanes 2 and 4 never ran): they were written in the hook shape and
  committed as a checkpoint (difftests, 51 mutants), then the class was converted. Converting a class with game-code slots
  would have given our vtable the game's slot functions anyway; the brief's "implement any slot that is still game code" cost
  more than the conversion here.
- **Faces**: `rz::cRZUnknown` at +0 (its slots 4 / 5 stay the base's; primary slot 6 `InvalidateRect` is a new virtual of the
  class, appended after the base's), `cITSNHoodTerrain` (a cIGZSerializable, 44 slots, declared in the class header) at +8,
  `cIGZPersistResource` +0xc, `cIGZMessageTarget2` +0x10. Folded slots made overrides: AddRef / Release (forward to the base,
  as ED), the water level setter / getter (slots 15 / 16, the face's +0x2c = the object's +0x34), AsPersistResource (slot 40),
  the persist key accessors (5-7, 9, 10) and GetRefCount (11, the thunk 0x4af9d0 in the original: a status entry, no hook).
- **Two .cpp files with slot bodies**: an adapter only inlines a body from its own translation unit, so the adapters sit at
  the end of each file, next to their bodies (generated once, written out, the one-statement form).
- **Adapter parameter type and the v7 tie.** Adapters first took `Adapters::Terrain *face` (a `using` in the adapter struct):
  matchcheck v7 made no vtable tie for any of them (only DoMessage, whose parameter was spelled `cIGZMessageTarget2 *`), and 8
  matches were lost. Spelling the interface (`cITSNHoodTerrain *face`) ties them all. Write the receiver's real type.
- **Non-virtual members of a multiply-inherited class** (SetSize, ResetHeights, ResetVertexFlags, RegisterCheatAndMessages)
  can't be hooked by `MemberAddress` (hooks.h asserts single inheritance): they get adapters too.
- **Destruction order**: the original destroys the message-id set first, then releases the two arrays, then the vectors in
  reverse order. A plain set member would be destroyed after the body's releases; in an anonymous union (as EM / FF) the body
  destroys it first, and the vectors follow as members. The destructor is out of line (FI).
- **Creation**: one site, Create 0x4af120 (constructor inline; registered by the cTSSimCOMDirector constructor 0x415ae0);
  vtables referenced only there and by 0x4ad290. `return new (std::nothrow) cTSNHoodTerrain;` is the switch (bisect:
  cTSNHoodTerrain_Create).
- **Tests**: the fixture builds every terrain from raw seeded bytes (as before) and installs the route's vtables: copies of
  the game's (original and adapter routes) or of ours (vtable route), with the fixture's two recording fakes (face slots 10
  and 21) patched over each copy. `TCall<Fn>(ctx, t, face, rva, ADAPTER(f), args)` calls the original, our pristine vtable's
  slot, or the adapter with `shape::TrapFor` in every copied slot that holds the RVA; Run adds the trap count to the compared
  blob. Each / Sweep compare the original with both routes, so every existing case (45 tests) runs on both. 42 call sites
  were rewritten by a script from the `ours ? t->mTerrain.f(..) : c.Original<Fn>(rva)(&t->mTerrain, ..)` pattern.
  NHoodTerrain_Slots covers the folded slots and the faces' AddRef / Release thunks through each side's own vtable and checks
  the base offsets.
- **Byte matches** (55 entries): hook shape v7 12 MATCH (502 B), v6 12. Shape: v7 14 MATCH (509 B: 11 through the vtable tie),
  v6 2 (GetGZCLSID, GetMaxTypeId). Gained: the three QI thunks 0x4b0602 / 0x4b0612 / 0x4b060a (tied to the compiler's thunks).
  Lost: GetKey 0x4af5d0 (17 B; its adapter is hand-written for the hidden return pointer, so no tie; the difftest covers it).
  Under v6 the one-statement adapters keep the face-to-object null test (`test ecx, ecx; cmove`), so the other 9 v6 matches go.

## Four faces, 45 entries on three test files, a recursive non-virtual member (cTSNeighborManager, lane FR)

src/ts/cTSNeighborManager.{h,cpp}, tests/ts_neighbor_manager_shape.h, tests/ts_neighbor_manager.cpp,
tests/ts_neighbor_manager_services.cpp, tests/ts_neighbor_manager_container.cpp, tests/ts_neighbor_manager_slots.cpp.

- **Faces**: `cITSNeighborManager` (a cIGZSerializable; slots 6-35, declared in the class header, with
  `virtual ~cITSNeighborManager() = default;` as slot 34 between GetPersistFace (33) and CountFlaggedNeighbors (35); the deleting
  destructor 0x4bb7d0), `cIGZPersistResource` (+4, the key at +0x44), `cITSDataContainer` (+8) and `cIGZMessageTarget2` (+0xc).
  Folded slots became overrides: AddRef 0x146a40, IsDirty 0x410230 (slot 11, it returns the raw byte, and other classes call it
  directly, so it gets no status entry), the nops 0x300a0 (slot 13, `ret 4`) and 0x140fe0 (slot 31), GetPersistFace 0x1c04e0,
  and the persist face's key accessors and GetRefCount. The three secondary faces' QI / Release thunks (0x4bd4de / 0x4bd4e6 /
  0x4bd4ee, 0x4be14a / 0x4be152 / 0x4be15a) are the compiler's (status entries, no hook). Their AddRef thunks are folded.
- **Creation**: Create 0x4bc570 (constructor inline, registered by the cTSSimCOMDirector constructor) is the only site. The four
  vtables are referenced only there and by 0x4bb7d0. `new (std::nothrow) cTSNeighborManager` is the switch (bisect: FUN_a04bc570).
  The destructor is out of line (FI's rule).
- **Self-calls** (the old `Manager()` / `Owner()` casts and slot calls) became plain virtual calls. That covers GetNeighbor
  from Import, RemoveNeighbor, CollectRelatedIds, DeleteNeighborResources, CreateNeighbor and the persist face's
  GetGroup / GetKey. Mutants that devirtualise three of them are killed.
- **Shared-interface return types**: `cITSDataContainer` slots return `bool`. In CollectReferenced the original ORs the raw
  low byte of AddSourceKey into its result, while ours ORs a normalised bool. Tests can't see the difference (every fake returns
  0/1), but a real context that returned another non-zero byte would differ in the result's low byte.
- **Tests**: a class helper header (`nbrmgrshape`, about 110 lines on tests/shape.h) holds the face offsets, slot counts,
  arity tables, per-route vtable copies with fakes patched over them, and `Call<Fn>(ctx, object, rva, face, NBRMGR_ADAPTER(f),
  args)`. `kNoFace` marks the non-virtual members, which run the adapter on routes 1 and 2. All 29 existing tests run three
  routes, and route 0 is compared with each. The objects on every route come from our constructor. Routes 0 and 2 then get the
  game's vtables, and vtable words are coded (copy / route's class vtable). NeighborManager_Slots covers the folded slots and the
  six thunks: route 0 calls the game's vtable slot (checked to hold the RVA) and route 1 calls our vtable's slot. 18 mutants
  (slot order, face order, QI face, adapter dispatch and adjustment, devirtualised self-calls, IsDirty normalisation, key
  accessor, Release bound, constructor and destructor, adapter delete flag, registration face) were all killed.
- **Byte matches** (45 entries with a verdict), v8: 8 MATCH. Release 0x4be130 (gained, FF's form), Write 0x4bf6c0,
  RegisterMessages 0x4bd270 and UnregisterMessages 0x4be650 are gained. GetGZCLSID, SetDirty, CountRelatedNeighborsOf and SetKey
  are kept. SetKey matches only under v8 (a vtable tie); v6 gives 0.571, so v6 has 7. QueryInterface 0x4bd480 is 0.676 (94 B;
  its five cases did not take FF's one-tail-per-case form). **Lost**: CountRelatedNeighbors 0x4bfd00 (153 B). It is recursive
  and `__declspec(noinline)`, so its adapter is a 5-byte tail jmp. Hooking the member directly would keep the match, but
  hooks.h's `MemberAddress` rejects the class's multiple-inheritance member pointers. A helper that takes the pointer's code
  word, or a static recursive helper, would win it back. UpdateKeyMap and RemoveNeighborEffects (never matched) are jmp adapters
  too.

- **Callees made ours afterwards (lane FU)**: the tutorial step AddTutorial 0x4a27a0 and DeleteFiles 0x4a2950 (4.7 KB, what
  Remove does to a neighbourhood's files; thiscall with `this` unused, kept a member as HasPackage) are non-virtual members in
  src/ts/cTSNeighborhoodManagerLoad.cpp next to Load (they share its directory scan wrapper and the inlined name part 0x1c8674;
  origcalls.py wants each original call in the file of the entry that declares it, so the wrapper stays in that one file),
  hooked through adapters. The list assign 0x41d6e0 is ServiceRefVector::AssignCounted
  (src/simantics/ServiceRefVectorAssign.cpp, SimAnticsGather's table). Load's and Remove's tests keep their isolation by
  standing the same stand-in in over our member too: a member pointer of a class with three bases is two words, so the tests'
  `MemberCode` takes its first word (the code address of a non-virtual member) rather than hooks.h's `MemberAddress`.

## Six faces, a hook-shaped base twin, two stages: forwarding overrides, then bodies in the class (cTSPersistSystem, lanes FX, GD)

src/ts/cTSPersistSystem.h (the class at the end of the file), src/ts/cTSPersistSystemShape.cpp, the shared
src/gz/cIGZPersistResourceManager.h and src/gz/cGZPersistResourceManagerShape.*, tests/ts_persist_system_fx.cpp. The class
has 165 slots on six faces (+0 the resource manager's 58, +4 the system service's 13, +8 cRZUnknownMT's 6, +0xa8
cITSPersistSystem's 75, +0xac cIGZFrameWorkHooks' 11, +0xb0 the scene graph's group-name translation provider's 2), ~150
verified bodies written hook-shaped over eleven test files, and a base (cGZPersistResourceManager, 0xa8 bytes) that is
another group's hook-shaped struct. That does not fit one lane, so it is converted in two stages.

- **Stage A (done)**: the real class, `cTSPersistSystem : gz::cGZPersistResourceManager, cITSPersistSystem,
  cIGZFrameWorkHooks, nRZSceneGraph::cGroupNameTranslationProvider` (the base: `cIGZPersistResourceManager, rz::cRZSystemService`),
  compiler vtables and thunks, our constructor and destructor at both levels; every slot an override that forwards to the
  existing hook-shaped member (`PrimaryBody()`, `ServiceBody()`, `ITSBody()`, `HooksBody()`: `reinterpret_cast` of `this` or
  the face, as the member expects). The old RVA hooks are unchanged and still serve direct calls; the constructor hook
  0x1d8b90 is the switch (without it the game builds its own object and nothing new runs).
- **Stage B (done, lane GD; below)**: the bodies are members of the class (Self() gone), the hooks one-statement adapters,
  the existing tests on two routes, and an Adapters test.
- **A twin for a base someone else owns** (EL's pattern): `gz::cGZPersistResourceManager` has the struct's layout
  (static_asserts per field against `::cGZPersistResourceManager`) and its slots run the struct's members on `this`
  (`Hooked()`). The calls those bodies make through `this`'s vtable reach ours. The base is only ever built inside
  0x1d8b90 (one caller of 0x9b3f30; the vtables 0xe60274 / 0xe60350 / 0xe60388 referenced only by its constructor and
  destructors), so hooking its constructor and destructors is safe. Its Shutdown slot (0x9b57e0, 337 B, never reached on
  cTSPersistSystem, which overrides it) calls the original.
- **Hook-shaped bodies return bytes; the interfaces say bool**: shared interfaces (cIGZSystemService, cIGZFrameWorkHooks,
  cIGZUnknown) declare `bool`, the bodies `uint8_t`; the overrides write `!= 0`. Game callers test `al` only, so this is
  only visible in a Slots test that compares whole `eax` (compare by kind: word, byte, bool, void).
- **Folded slots become inline overrides**: ITS 15 (`ret 8`), 19 / 65 (setters of +0xf0 / +0x1e0), 60 / 66 (getters of
  +0xb4 / +0x1e0); primary 29 is the base's vf27 body (0x1e36e0, the same RVA as base slot 27).
- **Library map destructors**: the id-range map's destructor 0x15d6a0 is a library match; ours is
  `std::unordered_map<uint32_t, uint32_t>`'s destructor run over the bytes (gz::DestroyPersistU32Map), not an original call.
- **Tests (tests/ts_persist_system_fx.cpp)**: Construct compares every dword of the two objects, vtable words coded and the
  heap structures by shape and allocation size. Destruct runs four routes (adapter, our +8 slot 3 with flags 0 / 1, the
  adapters on a game-built object with trap copies of its six vtables, arity-matched up to seven words) and compares the
  release log, freed blocks and the bytes left. Slots walks all 165 slots: a forwarding slot's body is patched with an
  arity-matched recorder (it must be reached once with the right face pointer and the arguments unchanged) and the game's
  vtable must hold that body's RVA in the same slot (so slot order and face order are checked against the game); the other
  slots run on both sides' objects. The +8 slot 3 is the Destruct test's; any slot in neither table fails.
- **Pitfall: a vector growing after a destructor can take a block just freed**: HeapValidate on the freed blocks gave random
  failures until the result vector was sized before the destructor ran (its growth reused a 0x18-byte map sentinel).

## A class with 28 slots still game code, forwarded; counted members for destruction order (cTSWantSimulator, lane GA)

src/ts/cTSWantSimulator.{h,cpp}, tests/ts_want_simulator_shape.h, tests/ts_want_simulator.cpp, tests/ts_want_simulator_slots.cpp.

- **Faces**: `cITSWantSimulator` (64 slots, declared in the class header; `virtual ~cITSWantSimulator() = default;` at slot 62
  between slots 61 and 63, the deleting destructor 0x4862e0), `cIGZMessageTarget2` (+4), `cITSDataContainer` (+8). Folded slots
  became overrides: AddRef 0x15e520, GetTick (slot 26, 0x22fa70), the container's CollectReferenced / Export (0x18a7a0,
  `xor al, al`). The four QI / Release thunks of the secondary faces (0x4927f9 / 0x49481a, 0x492801 / 0x494822) are the
  compiler's (status entries, no hook); the AddRef thunks are folded.
- **Slots still game code (28, about 29 KB) are forwarders** (handoff D2): `return OriginalFunction<Fn, rva>()(this, ...)`, every
  argument and the result a whole dword (an `int16_t` parameter would drop the high half the original may pass on), the +4 / +8
  faces' slots (DoMessage 0x487dd0, Import 0x492fd0) with `this` cast to the face. Their RVAs are the constructor entry's
  `original_calls`. WantSimulator_GameSlots checks each: a recording stub patched over the original's entry must be reached
  once from our vtable's slot, with the face as `this`, the arguments in order and the result returned. A slot-order mistake
  among forwarders would otherwise only show in game.
- **Counted raw pointers as members with a releasing destructor** (`WantRef`): the original releases +0xc4 / +0xc0 between the
  vectors and the maps, +0x94 / +0x90 after the 0x98 map, +0x24 / +0x20 / +0x1c before the id set, which is exactly the
  reverse declaration order once those fields are members with destructors. The destructor body is empty (out of line).
- **A narrow parameter the callers pass whole**: Counts (slot 17) passes its type dword to slot 7; with slot 7 declared
  `int16_t`, our call sign-extended it and the fake that logs slot 7's argument showed it. Slot 7 takes `uint32_t` now.
  Before narrowing a slot's parameter, check what its callers push.
- **Creation**: one site, 0x48d210, which calls the constructor 0x4855d0; the vtables are referenced only by the constructor
  and Destruct 0x485ba0. The constructor hook (placement new) is the switch (bisect: cTSWantSimulator_Construct).
- **Tests**: tests/ts_want_simulator_shape.h (about 90 lines on shape.h, FR's layout); every simulator, on every route, comes from
  our constructor and carries copies of its route's vtables with the fixtures' fakes patched over them. The 46 cases run three
  routes (the WantMemoryStore / WantSimContext ones two). Slot 30 got a logging forwarder (IsWantAllowed's self-call).
  WantSimulator_Destruct had an order dependency of its own: the log vector's reallocations were recorded as frees of
  whichever side grew it first (it failed when run alone); the log is reserved now.
- **Byte matches** (hook shape: 7 MATCH): v8 8 MATCH, v6 8. Kept: Release, PositiveAttributeA1, TogglePaused, IsPaused,
  FlushUpdates (adapters) and WantMemoryStore::SyncTick. Gained: GetWantDefinition 0x48dfc0 and DefinitionsSlot7 0x48dfd0
  (8 B tail jumps). Lost: the deleting destructor 0x4862e0 (0.917): its hand-written adapter calls our destructor, and the callee
  proof against 0x485ba0 (hooked by the Destruct adapter, a jmp) fails on the inlined WantRefList destructor. Value48, Value40,
  List30 and List38 score 1.000 through the vtable tie (a std::map call target only).

## Last slots and the shape in one lane, five faces, tests kept in one file (cTSNHoodOccupantManager, lane FY)

src/ts/cTSNHoodOccupantManager.{h,cpp,size.cpp}, src/hook/tables/cTSNHoodOccupantManager.cpp, tests/ts_nhood_occupants.cpp.

- **Order**: the eight slots still game code (removal, rect query, road snaps, road groups: 3.9 KB) were written, hooked and
  difftested in the hook shape first, committed as a checkpoint, then the class was converted. Writing them as face members
  and moving them into the class afterwards cost little: the bodies only lost their `Owner()` / `manager->` prefixes.
- **Faces**: `rz::cRZUnknown` (+0), `cITSNHoodOccupantManager` (+8, 26 slots, declared in the class header), cIGZSerializable
  (+0xc), cIGZPersistResource (+0x10, the key at +0x98) and cIGZMessageTarget2 (+0x14). The folded slots became overrides or
  stayed the base's: AddRef / Release forward to rz::cRZUnknown, RemoveRef / RefCount are inherited, AsPersistResource
  (0x4735d0) is an inline static_cast, and the persist face's key accessors and GetRefCount are inline on `mKey`. No status
  entries for them (shared bodies); NHoodOcc_Slots tests them through our vtables.
- **Creation**: Create 0x4a91f0 calls Construct 0x4a7ff0, its only caller, and the vtables are referenced only by Construct and
  the deleting destructor 0x4a84a0. The switch is two hooks: Create (`new (std::nothrow)`, hooked directly, cdecl) and the
  Construct adapter (placement new). Turn off both to bisect.
- **Destruction order**: the grid, the pending vertices, the id set and the road groups each sit in their own anonymous union.
  The out-of-line destructor destroys them in the original's order (groups, ids, vertices, cells) before the four lists.
- **Road groups** are `std::vector<cTSNHoodRoadGroup>` whose element is a tile vector over GZServiceAllocator. The group
  declares its copy operations, so it has no move constructor and reallocation copies (the original's 0x4a7630 uses its copy
  constructor 0x4a7ea0). A static_assert pins that. The group vector's destructor replaced the call to 0x4a8250, and the
  difftest compares capacities.
- **Self-calls** that the original makes through the vtable stay virtual: the four Create functions in Read, AsPersistResource
  and GetKey in Save, RemoveOccupant in FindLinkedOccupants, and FindOccupantsInRect (twice) in SnapRoadsInRect. The tests'
  fakes sit in those slots, and mutants that devirtualise two of them are killed.
- **Tests** stay in one file. A route block (about 90 lines) on tests/shape.h follows FR's helper: `OccCall<Fn>(ctx, m, rva,
  face, OCC_ADAPTER(f), args)`, `kNoFace` for the five non-virtual members, per-route vtable copies with the fixture's fakes in
  `gPatch`, and a trap-hit count added to every blob. Unlike FR, the objects still come from the fixture's seeded bytes
  (MakeManager), and Install puts the route's vtable copies on them. Sweep runs route 0 once and compares it with routes 1
  and 2. The call sites were rewritten mechanically (`ours ? m->F.f(a) : c.Original<T>(rva)(&m->F, a)` became one OccCall);
  only QI, the deleting destructor, Construct / Create, the Create* images, GetList and GetGZCLSID needed hand edits.
  shape::Ours builds our object once per run, and its generator would read the clock, so OCC_TEST fixes the clock seed and
  reads our vtables before any heap-poisoned comparison. NHoodOcc_Slots covers 22 inherited, folded and thunk slots, route 0
  against route 1.
- **Freeing is observable**: a deleting-destructor adapter that tests the wrong flag bit only leaks, and the first shape-mutant
  run let it survive. The destructor test now wraps KERNEL32!HeapFree in the original image's import table and in the test
  executable's, and counts frees of the object's block on every route. Both flag mutants are now killed. Other classes'
  destructor tests can reuse this (FreeWatch, tests/ts_nhood_occupants.cpp).
- **Mutants**: 31 on the eight slots (two killed only after the tests forced the cases: flagged roads in the second pass of
  SnapRoadsInRect, a 19-tile trace). 15 on the shape: adapter dispatch, QI default and face, destructor and constructor,
  AddRef, deleting flag (two), devirtualised Read / SnapRoadsInRect self-calls, SetKey, GetGroup, GetRefCount,
  AsPersistResource and a thunk adapter's adjustment. All 46 are killed.
- **Byte matches**, v8 (`--brief`): before, 4 MATCH (GetGZCLSID, GetOccupantLayerMask, UpdateOccupantPosition, Initialize; v6
  the same 4). After, 6 MATCH: GetGZCLSID and GetOccupantLayerMask are kept, and the four QI thunks 0x4aa0e7 / 0x4aa0ff /
  0x4aa0ef / 0x4aa0f7 are gained by the vtable tie (v6 0.286 each, so v6 has 2). **Lost**: Initialize (0.850, because the
  adapter's static_cast from the face tests for null, which the hook shape's Owner() did not) and UpdateOccupantPosition (1.000
  by the tie, but the original calls 0x4aa550 / 0x4a9c10 where ours calls our members, whose hooks are adapters). Both come
  back if `MemberAddress` accepts the class's member pointers, so the non-virtual members can be hooked directly.

Stage B (lane GD): src/ts/cTSPersistSystem.cpp, tests/ts_persist_system_shape.h (routes), tests/ts_persist_system_routes.cpp
(Adapters), the eleven converted test files.

- **Bodies in the class, moved by script**: the four face structs, their Self() and thunk templates are gone; each body is the
  override of its slot (or one of four non-virtual members: NotifyListeners, ReleaseSegments, SetUpSegment,
  OpenContentFolder), with its notes above it. The signatures are the interfaces': the key slots take the dword key
  (`Ptr<const uint32_t>(key)` inside), `bool` returns. Self-calls still go through the face's vtable as before (ifaces views,
  `VirtualCall<..., N>(ITSFace(), ...)`): virtual calls, not yet the plain `X(...)` form (that changes which face the
  compiler dispatches through for a member two bases declare, e.g. GetGroupIdForPathChars: ITS 10 and translation 0).
- **Raw bool**: OnIdle and Shutdown return the base's byte unchanged through the interface's `bool` (the original passes `al`
  on; a test comparing `eax` saw 2 become 1). `RawBool(uint8_t)` memcpys the byte into the bool; no normalisation.
- **Adapters inline their bodies**: `[[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->
  cTSPersistSystem::X(args);` compiles to a copy of the body (with the static_cast's null test for a secondary face), not a
  jump. No static check can tie an adapter to our vtable's function; the check is behavioural (below).
- **Non-virtual members hooked at the member**: `MemberAddress<&cTSPersistSystem::X>()` (this-adjustment 0, checked at compile
  time). Through an adapter, vf49 / vf52 lost their byte match: their call to NotifyListeners reached our adapter, not a
  function at 0x1de870. Hooking the member gave both back.
- **Secondary-face AddRef / Release / QI thunks**: the compiler's, in our vtables only; their 13 hooks are gone (as FA's).
  TSPersistSystem2_Thunks calls them through our vtables and checks the game's slots hold their RVAs.
- **Two routes (tests/ts_persist_system_shape.h)**: `psshape::Call(PS(Name), face, args...)`; route 1 calls our vtable's
  function in the slot where the game's vtable holds the RVA (found by FindSlot over the six game vtables); route 2 the
  adapter, with an arity-matched trap in that slot of the vtable the object carries: in place in a fixture's table whose slot
  holds code or nothing (some fakes know their object by its vtable word, so a copy would change what they log), otherwise in
  a copy the object carries for the call. Never written in place otherwise: a seeded world's random vtable word can point
  into live heap, and writing a trap there broke a later test (order-dependent). A face with no vtable or an unreadable one
  is guarded anyway (a dispatch faults). PS_TEST runs each body on both routes and labels failures by route.
- **TSPersistSystemGD_Adapters**: every hooked adapter must have run on route 2 guarded. Tests that ran before it count; for
  the rest it runs the tests registered at that RVA, route 2 only (alone it reruns 88). Not covered: the adapters of the two
  byte-match-only slots (SetKeyedObject 0x1e5200, vf168_56 0x1e3b10: no difftest exists for them).
- **Matches (v8)**: 12 / 102, FX's twelve; 7 of them only through the vtable tie (v6: 5 — GetServicePriority, vf56, vf49,
  vf52, ClearSpeculativeLoader; the seven tie-only ones are the adapter's face cast plus the body). GetResource 0x1e08cf
  is size-built (a `.size.cpp` file would bring it closer).

## Virtual bases and vtordisp (cSGResource and its ~40 derived classes, lane NY)

tests/sg_virtual_base_layout.cpp (SGResource_VirtualBaseLayout, a layout probe: no status entries rest on it).

- **Decision: plain C++ virtual inheritance.** `class cSGResource : public cGZPersistResource, public virtual cISGResource`
  with its constructor, destructor, overrides and members, and every derived class written the same way, gets the game's
  layout from MSVC 14.40 (default `/vd1`). No explicit layout (vbtable as data, faces as members, as cBaseParser had).
- **Evidence** (the probe is that declaration with stand-in bodies and slots 18 / 19 pure; compared with the image):
  - Layout: vbptr +0x1c, the members at +0x20 / +0x24 / +0x28 / +0x29, vtordisp +0x2c, the virtual base's vtable at +0x30,
    size 0x34; vbtable {-0x1c, 0x14}.
  - Constructor: the game's 0xa3c270 run on a buffer whose vbptr is preset (to a vbtable with our contents) leaves the same
    words as our constructor at the end of its body (vtable words compared as the face they are), with the virtual base at
    +0x30 and at +0x3c (a derived class with members).
  - vtordisp: cSGResource's constructor writes vbtable[1] - 0x14 (0xa3c2b5), 0 alone and 12 displaced; a derived constructor
    then writes its own, vbtable[1] - its own distance (cGeometryDataContainer 0xa6edb5: - 0x64), 0 in a complete object.
    Ours the same.
  - Thunks: every overridden slot of the virtual base's vtable is `sub ecx, [ecx-4]`, then `sub ecx, n` (absent when 0),
    then `jmp`; n = the virtual base's static offset less the offset of the subobject that introduced the overrider: slots
    0-2 (QueryInterface, AddRef, Release, from the primary at +0) the whole distance; cSGResource's own overrides 0 in
    cSGResource and vbase - 0x30 in a derived class; a derived class's overrides of functions the virtual base introduced
    (18, 19) 0. The game's 0xe695dc (cSGResource) and 0xe73314 (cGeometryDataContainer) and ours at both placements follow it.
  - `this`: a cSGResource override's body takes ecx = object + 0x30 (where cSGResource alone puts the virtual base) wherever
    the virtual base really is, and subtracts 0x30 without reading the vbtable (the game's GetOwner 0xa3c3c0 is `this - 0x30`).
    Our body called that way sees the object.
  - Mutants: the base not virtual, the adapter's distance, a member's width, the constructor's helper value, `#pragma
    vtordisp(0)` on cSGResource: killed. Equivalent: the bases declared in the other order (MSVC puts non-virtual bases first)
    and `vtordisp(0)` on a derived class (the vtordisp is cSGResource's and is inherited).
- **Adapters for the virtual base's slot bodies** (the game's 0xa3c3c0-0xa3cdd0 and the derived classes' bodies reached by
  the 11- and 14-byte vtordisp thunks): `__fastcall (void *face, void *)`, `reinterpret_cast<X *>(static_cast<uint8_t *>(face)
  - 0x30)` (the static distance, never through the vbptr), then `self->X::F(args)`. C++ has no static_cast from a virtual
  base to the class. The thunks themselves (`sub ecx, [ecx-4]; ...; jmp`, 8-14 bytes) stay the compiler's, in our vtables
  only.
- **Constructors take the most-derived flag** (`ret 4`). A complete object's constructor (flag 1) sets the vbptr and
  constructs the virtual base (cGeometryDataContainer 0xa6ed69-0xa6ed7c: vbptr 0xe73378 at +0x1c, the interface's vtable at
  +0x80); a base-part call (flag 0, from a derived class's constructor) does neither. cSGResource is abstract, so 0xa3c270
  never tests the flag (it is only ever a base part).
  - So **a constructor hook is not `new (p) X` when game subclasses call it with flag 0**: that would set X's vbptr and build
    the virtual base at X's own offset, inside the subclass's members. The 20 callers of 0xa3c270 are derived classes'
    constructors (0xa3dd80 cGeometryNode, 0xa4ddf0, 0xa4fd10, 0xa5bd40, 0xa6ed50, ...; flag 0), and cGeometryNode's 0xa3dd80
    is in turn the base constructor of ~17 world-lot nodes. C++ has no expression that runs a base-part constructor on its
    own (no placement form for it), and cSGResource is abstract besides. The factories (`return new X`, flag 1) are
    unaffected.
  - **Decision (lane OE, N1): hook the base constructor to our compiler's own constructor.** MSVC emits a class with
    virtual bases one constructor symbol (`??0X@...@QAE@XZ`), thiscall with the most-derived flag on the stack and `ret 4`:
    the game's ABI exactly, and with flag 0 it is the base-part routine (it leaves the vbptr alone, builds no virtual base,
    stores our vtables through the vbptr the derived constructor set and the vtordisp from the vbtable). C++ can't take a
    constructor's address, so the linker names it: in X's .cpp
    `#pragma comment(linker, "/alternatename:_OE_X_Constructor=??0X@ns@@QAE@XZ")`, in X's header `extern "C" const
    uint8_t OE_X_Constructor;`, and the hook row takes `const_cast<uint8_t *>(&OE_X_Constructor)` (src/sg/cSGResource.h,
    src/sg/cSGResourceClass.cpp). The row must stay a constant initializer: a function call there makes the whole
    `kHooks` array dynamically initialized, and matchcheck then sees none of the table's rows (every recorded match of the
    group drifts to SKIP). A wrong decoration fails the link (the C name stays unresolved), so the symbol can't silently
    drift. Proved by
    tests/sg_resource_class.cpp: SGResource_BasePart (flag 0 at four placements of the virtual base, +0x30 / +0x3c / +0x60
    / +0x80, every word against the game's 0xa3c270) and SGResource_GameDerived (the game's own cResourceNode constructor
    0xa4fd10 and destructor 0xa4fe50 run with 0xa3c270 / 0xa3c2f0 / 0xa3c4d0 patched to ours: the node's words, QI, Read
    and Write as the game's derived classes call them). Rejected: `new (p) X` (wrong, above); a hand-written base-part
    routine (duplicates what the compiler makes and needs the `??_7` addresses); waiting until every derived constructor is
    ours (20 callers, ~17 more behind cGeometryNode: the class would stay unhooked for months). The same technique fits
    cGeometryNode's 0xa3dd80 and any constructor the game calls with flag 0. In game, the derived classes then carry our
    vtables between their base-part call and their own vtable store, and the game's vtables of X (0xe695a4 / 0xe695dc) are
    stored by nothing that runs: their slots, the deleting destructor and the vtordisp thunks are in effect through
    callers (`callers.py` shows it once the constructor and destructor are hooked).
- **Destructors** need no flag: 0xa3c2f0 restores the three words (vtable, the virtual base's vtable, vtordisp) and
  releases the resource, reading the vbtable for the virtual base's place, so it works at any placement. The virtual base is
  an interface with nothing to destroy, so a complete-object and a base-part destruction do the same; a qualified
  `self->X::~X()` adapter fits both (tested, lane OE: SGResource_BasePart at four placements and inside the game's
  cResourceNode destructor in SGResource_GameDerived). The deleting destructor (0xa3c380, `delete 0x34`) is the complete
  object's; an abstract class's is never reached from a complete object, and ours is tested with flag 0 (an explicit virtual
  destructor call, `p->~X()`, is slot 12 with flag 0).
- **Slot results wider than the shared interface** (lane OE): the referent interface (sg/SGNodeInterfaces.h) declares
  Write `void`, but cSGResource's 0xa3ced0 returns the writer's answer, and game code (the derived classes' Write) calls
  it directly. The class keeps the slot `void` and a named member (`WriteResource`) that returns it; the hook's adapter
  calls that, so a game caller still gets the answer. Changing the shared slot's type is a shared fact (full suite).
