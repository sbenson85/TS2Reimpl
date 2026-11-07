# db/

Facts extracted from the pinned target (`target/legacy-1.18.0.187.json`). No code bytes.
All addresses are RVAs as hex strings (the game runs with ASLR; image base 0xA0000000 on disk).
Regenerate rather than hand-edit:

| File | Produced by | Contents |
|---|---|---|
| `classes.jsonl` | `tools/rtti.py` | one MSVC RTTI class per line: demangled `name`, `mangled`, type descriptor `td`, `bases` (from the class hierarchy descriptor), `vftables` (`rva`, `offset` of the subobject, `entries`) |
| `functions.jsonl` | `tools/ghidra-export.ps1` (`ExportDb.java`) | one function per line: `rva`, `size`, Ghidra `name`/`namespace`, `source` of the name, calling convention, `thunk`, `callees` (RVAs, or `DLL!name` for imports) |
| `names.jsonl` | `tools/names.py` | best-known name per function with `source`: `community:<mod>`, `vtable+interface` (Class::Method via `interface-slots.jsonl`), `vtable` (Class::vf<slot>) |
| `interface-slots.jsonl` | `tools/interface_slots.py` | method names in vtable slot order for 36 cIGZ* interfaces (from Las Marionetas headers via MSVC's layout report; overloads numbered #n in slot order) |
| `community-hooks.jsonl` | `tools/community_hooks.py` | where TS2-Extender hooks/patches (from its published patterns): `matches`, containing `function`, `at_start` |
| `library-matches.jsonl` | `tools/libmatch.py` | statically linked CRT/vcruntime/STL functions identified byte-for-byte (relocations masked) against MSVC 14.40 / SDK 10.0.26100 libs and `tools/stlprobe` STL instantiations, plus the third-party builds (`tools/thirdparty.ps1`, `tools/thirdparty_build.py`) and the DirectX SDK's `d3dx9.lib`/`DxErr9.lib`; `names` are decorated. `kind`: `exact`, `prefix`, `unlisted`, `inside`, or `gap` (a whole library body in code no db function covers: a function to create, listed by `--boundaries`) |
| `library-exclusions.jsonl` | by hand | library matches reviewed and rejected: game code that has a library function's shape (`rva`, `reason`). `tools/libmatch.py` drops those rows when it regenerates `library-matches.jsonl` (and their `inside` rows); Concurrency Runtime lookalikes need no entry (`tsdb.lookalike`, and `libmatch.py` does not list them as matches on a status entry) |
| `crt-equivalents.jsonl` | by hand | CRT functions that don't byte-match our toolchain (EA's CRT version differs) but are identified by structure (a chain to a byte-matched function) or by a differential test against our CRT (`tests/crt_equivalents.cpp`); treated as library code by the tools |
| `static-initializers.jsonl` | `tools/initializers.py` | the 810 static initialisers the CRT runs before WinMain (from its `_initterm` tables): `size`, `callees`, `objects` (statics constructed there: addresses passed in ecx), `atexit` (their destructors). Most aren't in `functions.jsonl`; `tsdb.functions()` merges them in |
| `families.jsonl` | `tools/families.py` | groups of functions byte-identical apart from relocated operands (template copies); `relocs` = how many operands differ |
| `status/<group>.jsonl` | by hand | reimplementation status per function, one file per class or group (see CLAUDE.md; `tools/status.py` checks and totals them) |

Pipeline: `snapshot.ps1` â†’ `unpack.ps1` â†’ `ghidra-import.ps1` â†’ `rtti.py` â†’ `ghidra-apply-rtti.ps1` â†’ `ghidra-export.ps1`.

Third-party sources (official releases, each downloaded into its own `local/thirdparty/dl-*` folder
and built by `tools/thirdparty.ps1` (MSVC 14.40) or `tools/thirdparty_build.py` (cl 13.10 recipes)
into `local/thirdparty/obj/`; which version and flags match is in `third-party.jsonl`):
- Lua 5.0: https://www.lua.org/ftp/lua-5.0.tar.gz, plus the bug patches from https://www.lua.org/bugs.html
- zlib 1.1.3 / 1.1.4: https://zlib.net/fossils/
- libpng 1.0.5: https://sourceforge.net/projects/libpng/files/ (old releases)
- IJG libjpeg 6a / 6b: https://www.ijg.org/files/jpegsrc.v6a.tar.gz, `jpegsrc.v6b.tar.gz`
- Expat 1.2: ftp://ftp.jclark.com/pub/xml/expat1_2.zip (James Clark; mirrored on archive.org)
- Speex 1.0.x: https://downloads.xiph.org/releases/speex/
- OpenSSL 0.9.7d: https://www.openssl.org/source/old/0.9.x/openssl-0.9.7d.tar.gz
- cl 13.10.4035 (the compiler of Expat, OpenSSL and D3DX): the Windows Server 2003 SP1 DDK,
  https://download.microsoft.com/download/9/0/f/90f019ac-8243-48d3-91cf-81fc4093ecfd/1830_usa_ddk.iso
- D3DX9 / DxErr9: DirectX 9.0 SDK Update (Summer 2004), https://archive.org/download/dxsdk_sum2004/dxsdk_sum2004.exe
  (`tools/thirdparty_build.py --fetch-dxsdk` checks its SHA-1 and Microsoft's Authenticode signature, never runs it,
  and extracts only `Lib\d3dx9.lib` and `Lib\DxErr9.lib` with 7-Zip into `local/thirdparty/dxsdk-sum2004/`).

Notable facts:
- The game's STL template code matches our compiler's output at `/EHsc /O1` in most translation units
  and `/O2` (or `/O2 /Ob1`) in some; others were built with `/GS-` or without exception handling (no `/EHsc`: most of
  the iostream code), so `tools/stlprobe/stlprobe.ps1` builds every source under each of those flag sets (its header
  lists sources, flag sets and objects). It records the sources' hash in `local/stlprobe/sources.sha256`;
  `libmatch.py` warns when the objects are stale (rebuild them after changing `tools/stlprobe/`, or a regeneration
  drops the rows they found). A lane builds into its own directory: `stlprobe.ps1 -Out DIR`, then
  `libmatch.py --stlprobe DIR`. UCRT functions match only partly: EA's Windows SDK version differs from ours (10.0.26100).
- An explicit instantiation of `std::map/set` covers only the derived class; the code is in the `_Tree`
  base (`_Extract`, `_Erase_tree`, `_Find_lower_bound`, ...), so `probe.cpp` instantiates
  `std::_Tree<_Tmap_traits/_Tset_traits<...>>` directly. The game's tree/list/vector/deque code for its
  own structs depends only on the element's size, alignment and whether it has a destructor, so
  `probe_sizes.cpp` sweeps stand-in `Blob4<N>` types (N = 4..128 step 4, trivial and with an
  out-of-line destructor) under maps keyed by uint/int/short/uint64/pointer/float/string/wstring, sets,
  multimaps, vectors, lists and deques. `_Extract` (erase's body, e.g. 0x15feb0) matches at `/O2`. The same sweep
  over `ProbeAlloc<T>` (`-DALLOC=1`), a stateless allocator calling out of line like the game's allocator-service one,
  matches containers using it, and `std::map`'s own members are instantiated in objects of their own
  (`-DMAPCLASS=1`: beside `_Tree` they change how `/O2` inlines its destructor). `probe_ios.cpp` instantiates the
  iostreams (libcpmt has only their non-template parts under `/MT`).
- Not matched yet: string copy constructor with `_Construct` inlined (0x173790, 162 B), `vector<4-byte>`
  insert (0x18b860), `basic_string::assign(const char*)` (0x154bb0). None match at /O1, /O2, /Ob1,
  /Ob3, /Ox, /Os, /Od or the 14.44 toolset; probably built with whole-program optimisation (/GL),
  which libmatch cannot read from an object file.
- Not matched (lane MJ, every flag set): `_Try_emplace` for map/unordered_map (e.g. 0x96eed0, 0x85a0ef: the game copies
  `_Find_lower_bound`'s result with `movsd` and its temp-node constructor takes three arguments; C++14/17/20 alike),
  `_Hash::_Forced_rehash` with string keys (0x60d755: one store order differs; perhaps EA's case-insensitive string),
  `_Freenode` of nodes whose element destructor takes an argument (game element types), `_Copy_nodes` 0x3cb020
  (register allocation), deque `_Assign_range` 0x9cea90, and filebuf `overflow`/`uflow`/`_Endwrite` (0x1b7fa2,
  0x1b84e3, 0x1b6787). Much of the rest grouped under std names is STL over game element types whose constructors,
  destructors or comparisons are inlined (no stand-in reproduces them), or game templates (`cGZPropertyManagerT`,
  `cRZRefCountedT`, `c3DArray`).
- Scalar deleting destructors of probe classes (`??_G`) are generic: they match the game's own classes' deleting
  destructors by accident. libmatch keeps one only when its code calls land on the library functions (the destructor
  or what it inlines; operator delete alone doesn't tell). Short probe bodies nothing confirms are dropped too: under
  16 bytes of their own without a call to library code, and under 32 when they are one instantiation calling a plain
  member we have the body of while the game calls something else (`basic_ostringstream::str` matched scene-graph
  virtuals).
- `cRZString` derives from MSVC `std::basic_string<char>` (plus `cIGZString`), so its layout follows
  the VS 2022 STL, not STLport as in the Ultimate Collection. Layout: vptr at 0, `std::string` (24 bytes) at 4.
- The game's `std::string`/STL types match ours exactly because we build with the same MSVC 14.40 toolset.

`third-party.jsonl` (by hand, from `tools/thirdparty.ps1` builds and string/call-graph analysis): one line per third-party component statically linked into the exe: `name`, `version`, `license`, `kind` (`matched` = identified byte-for-byte via libmatch, `unmatched` = identified but built with a toolchain we do not have, `drop` = not needed in the standalone build), `rvas` (ranges), `bytes` (matched library bytes), `build` (compiler, flags and configuration that match), `evidence`, `notes` (approximate or unfinished parts are marked). EA's own builds use 14.40 (`/O1 /Oi /GS-` for the Gimex libpng/libjpeg 6a/zlib 1.1.3 and Speex 1.0.3; `/O2 /GS-` for Lua 5.0). Expat 1.2 and OpenSSL 0.9.7d were prebuilt with cl 13.10. The second libjpeg/libpng/zlib set (6b, 1.0.5, 1.1.4) is D3DX's private copy, inside the statically linked D3DX9 of the Summer 2004 DirectX SDK. `tools/subsystems.py` puts every db function inside these ranges in the `third-party` subsystem.

`missed-functions.jsonl` (tools/missedfuncs.py): code Ghidra didn't record. `kind: "missed"`: a direct
call target that isn't a function start (rva, size to the next start, callers, callees).
`kind: "resized"`: a record that falls through into an unrecorded gap (mostly `_EH_prolog` functions
Ghidra sized at their 10-byte prologue), with the corrected size, `ghidra_size` and the gap's
callees. `tools/tsdb.py functions()` merges both, so every tool sees them.
