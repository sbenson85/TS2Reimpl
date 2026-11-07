"""Subsystem ownership map: assigns every non-library function to a subsystem -> db/subsystems.jsonl.

    .venv/Scripts/python -I tools/subsystems.py [--summary] [--no-write] [--leftovers N] [--classes-unmatched]
                                                [--check-groups]

--check-groups (the coordinator's merge check): lists the status groups with members still in "other" that neither
db/subsystem-groups.jsonl nor the group majority places, with their call-graph guesses, and notes list entries that
are stale, redundant or disagree with the majority; exit 1 if it lists anything.  Fix by adding or editing a list
entry ({"group", "subsystem", "reason"}; subsystem "mixed" for a group of several subsystems' code).

One line per function: {"rva", "subsystem", "basis"[, "guess"]}.  `basis` says how the function was assigned
(strongest first).  Evidence of ownership keeps its subsystem: third-party, vtable, ctor, static-init, string-ref, status, and
data-ref / thunk when what they follow is itself evidence.  A call-graph guess (called-only-from, called-mostly-from,
calls-into, shared-callers, and data-ref / thunk that follow a guess) is filed under subsystem "other" with the
guessed subsystem in "guess"; a function nothing connects is "other" with basis "unassigned" and no guess
(2026-11-03: guesses filed as owners misled campaign lists).  Except third-party: it is an exclusion category, not a
campaign (the EA-code denominator leaves it out), so a guess of third-party stays subsystem "third-party", with its
call-graph basis and "guess": "third-party" marking it as guessed (coordinator, 2026-11-04: the OpenSSL, HLSL compiler
and Gimex clusters sit in contiguous third-party code; moving them into EA code would change the headline on
reclassification alone).  Library functions (tsdb.library_rvas()) have no row.
    third-party:<name>     inside an address range of a third-party component (db/third-party.jsonl)
    vtable:<class>         a slot of a named class's vftable (class -> subsystem by name rules below)
    ctor:<class>           a function that stores a named class's vftable pointer into its own `this` (constructor,
                           destructor) or into an object it allocates and returns (an inlined factory), at the
                           vftable's subobject offset; the most derived such class wins.  Stores into locals,
                           members, globals or heap objects the function keeps don't count (VftableStores): before
                           2026-10-19 they did, and every function with an inlined cRZString local (3,900 of them,
                           cRZString has 3 bases so it beat most classes) was filed under cRZString/framework-gz.
    static-init            a static initialiser, by the classes it constructs / calls
    status:<class>         a db/status entry (mapped or above) whose name's class has a class-backed subsystem (name
                           rules, or 2/3 of the class's evidence-placed functions); an existing evidence basis wins
    status-rule:<name>     a db/status entry whose class (or, for a name without one, group file name) has no
                           class-backed subsystem but matches the name rules (classes without RTTI); not STL
    status-group:<class>   the same for a name without a known class, by its group file when that names a class
    band:<name>            an address range of db/subsystem-bands.jsonl: one module of unnamed code, read and judged by
                           hand as a whole (no RTTI, no strings: nothing below can tell), with its evidence in "reason"
    group-list:<group>     a member of a status group in db/subsystem-groups.jsonl (kept by the coordinator) that no
                           evidence above placed
    group-majority:<group> a member of a status group that no evidence placed, when >= 2/3 of the bytes of the
                           group's evidence-placed members are in one campaign subsystem (not other-ts,
                           shared-utility or third-party)
    stl-single-user        a non-library STL / std instantiation whose callers are all evidence-placed in one
                           subsystem (iterated; library-matched code has no row)
    string-ref             references a string whose text names a subsystem (keyword list below)
    called-only-from       every non-library caller (incl. tail-call jumps) is in one subsystem
    called-mostly-from     >= 75% of caller weight is in one subsystem
    calls-into             calls >= 2 assigned functions and >= 70% of them are in one subsystem
    data-ref               shares a rarely-referenced global (<= 12 referrers) with one subsystem
    thunk                  jump stub, follows its target
    shared-callers         called from >= 3 subsystems (a shared utility, kept apart)
    unassigned             nothing connects it
Facts only: no code bytes or disassembly are stored.  Strings and data references are computed from the
unpacked exe on each run and are not stored.
"""
import bisect
import collections
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

BASE = 0xA0000000

# ---- class-name rules: first match wins; matched against the qualified name, then the base classes ----
# (regex, subsystem).  The match string is "ns::cName" (demangled) or, for anonymous-namespace/template
# classes, the mangled text with '@' turned into ' '.
RULES = [
    (r"std |std::|basic_|ctype|codecvt|num_get|num_put|numpunct|^type_info|CCallbackImpl|_Facet|bad_alloc", "crt-glue"),
    (r"^[A-Za-z0-9]+Param$|Version0Param$", "simantics-vm"),
    (r"HTTPMessage|NetworkFileCache|TickerMessage|cRZHTTP|cRZFTP|cIGZNet", "network-web"),
    (r"^nTSCASCore", "cas"),
    (r"cHTML", "ui-windowing"),
    (r"^c(Base)?(Even|Odd)Triangle|cBasePolygon|cAppend(Valid)?WallLocator|cTSTriple", "world-lot"),
    (r"cSampleGrabber", "audio-video"),
    (r"^crashpad|crashpad", "third-party"),
    (r"^CMpegBase|^cRSC|Expat", "third-party"),
    (r"nGZCommandParser|cMetaParticle|cParticlePath|cLightAttributesCommand|CommandT |cBlockCommand|cFunctionT|cCommandT", "command-parser"),
    (r"cGZLua|nTSSimulatorLua|cTSSimulatorLua|cTSLua|Lua5|LuaScript", "lua-scripting"),
    (r"AttributeEdithShape|EdithShapeColor", "scenegraph"),
    (r"cTSBehavior|cTSTreeSim|cTSTreeStack|cTSTreeTable|cTSTreeConstant|cTSTreeProperties|cTSInteraction|cITSTree|cITSBehavior|XPrimParam|cTSPrimitive|cTSSimAntics|BHAV|cTSTTAB", "simantics-vm"),
    (r"Edith|cTSObject|cTSStack", "objects"),
    (r"cTSWant|cTSPerson|nTSSG::cPerson|cTSSim[A-Z]?(?!ulator|System|Editor)|cTSFamily|cTSRelationship|nTSGenetics|cTSTreeSim|cTSMemory|cTSMotive|cTSSkill|cTSCareer|cTSAspiration|cTSTurn|cTSLifetime|cTSPet|cTSSocial|cTSNPC|cTSBusiness|nTSBusiness", "sim-person"),
    (r"nTSCASCore|nTSUICAS|cTSUISimEditor|cTSCAS|cTSWinProc(SimEditor|MakePet|ChooseBreed|PetVanity|Vanity)", "cas"),
    # The whole node chain cBoundedNodeT<cIObjectV> > nTSSG::cObject > cAnimatable > cLocomotable (cPerson above is sim-person's),
    # and the game's own nGZSceneDBAnim subclasses (lane XN).
    (r"nGZSceneDBAnim|nTSSG::cAnimat|nTSSG::cLocomot|cAnimationRecorder|cTSWinAnimationRecorder|cAnimRes|cTSAnim"
     r"|nTSSG::cObject\b|VcIObjectV|nTSSG::c(AnimRequest|AnimEventCB|ChoreoRequest|CustomChoreoQueue|CustomAnimTimer|CustomEventHandler"
     r"|EventHandler|ChannelFunction|SharedChannelFunction)", "animation"),
    # The lot and build tools and dialogs are ui-game's, with their base classes (lane XN: 381 calls up to ui-game were theirs).
    (r"nTSWorld|cTSLot|nPathPlanner|cTSRoute|cTSTerrain|cTSNHoodTerrain|nTSSG::c.*(Roof|Wall|Floor|Fence|Pool|Terrain|Awning|Rug|Dormer|Stair|Foundation|Deck|Fireplace)|cWorld|cTSSimulatorWorld|cTSHouse|cTSLandscape", "world-lot"),
    (r"nTSSG::cNHood|cTSNeighbor|cTSNH|nTSMetaData|cTSNHood|cTSWinProcNeighbo|cTSUserToolNeighbo|cTSStoryTelling|cTSTown|cTSHood", "neighbourhood"),
    (r"cTSAudio|cGZSnd|cGZWave|cWave[A-Z]|cHit|cFreshPlayer|cGZVideo|cGZMadCow|cTSWinReia|Speex|cRZAudio|cGZMusic|cGZStream|cTSMusic", "audio-video"),
    (r"cGZDB|cGZPersist|cTSPersist|cGZBufferResource|cGZResource|cRZResource|cGZPackedFile|cGZDirectory|cGZFile|cRZFile|cRZStream|cRZDirectory|cRZSystemFile|cRZCompress|cGZCompress|cGZResMan", "persist-dbpf"),
    (r"cGZWWW|cRZNet|cTSWebService|cGZNet|cGZHTTP|cGZSocket|cTSTradeable|cTSAnalytics|cTSUpdate|cTSEA|cGZDownload|nGZNet", "network-web"),
    (r"cTSCustomContent|cTSContentRegistry|cTSStringSet|cTSMaterialCatalog|cTSCatalog|cTSBinCatalog|cTSSkinCatalog|cTSLotCatalog|cTSObjectFileRef|cTSInventory|cTSObject|cTSMetaData", "objects"),
    (r"nGZGraphic4|nRZGraphic4|nGZDrawSoftRender|cTSParticles|cTSEffects|cTSVisualEffect|cGZImage|cGZBitmap|cGZDraw|cGZFontSys|cGZFont|nGZNumerics|nGZCompGeom|cTSLightingMan|cGZSurface|cGZCanvas|cGZTexture|cGZGfx", "rendering-gfx"),
    (r"nGZSceneGraph|nRZSceneGraph|nTSSG|cTSWinSceneGraph|cTSWinSceneDB|cTSSceneGraph|cTSCamera|cPoolWater|cTSSG", "scenegraph"),
    (r"cGZWin|cWin[A-Z]|cRZWin|cGZFade|cRZWinKey|cGZDebugWin|cTSWinGen|cGZPie|cGZIME|cGZ.*Mgr.*Win", "ui-windowing"),
    (r"cTSWin|cTSUI|nTSUI|cTSMain|cTSUser|cTSGameState|cTSScriptedProgress|cTSDialog|cTSTutorial|cTSHelp|cTSToolbar|cTSPanel|cTSHUD|cTSAlbum|cTSScreen|cTSCursor|cTSWindow|cTSCallout|cTSNotification|cTSPhotoBooth|cTSPieMenu", "ui-game"),
    (r"cTSSimulator|cTSSimSystem|cTSCommonSystem|nTSSim|nTSCommon|cTSConfiguration|cTSEvent|cTSTime|cTSClock|cTSSystem|cTSGlobal|cTSCheat|cITSCheat|cTSSaveGame|cTSGame|cTSScript", "sim-core"),
    (r"cTSApp|cCTGFramework|cGZFrame|cGZApp|cIGZApp|cRZApp|cRZCmdLine|cGZCmdLine", "framework-gz"),
    (r"nGZLog|nGZEvent|cGZ|cRZ|cIGZ|nRZSTD|nGZ|nRZ|cHitIni|cRZIni|cRZLanguage|cRZVariant|cRZString|cCTG|cTSIniFile|cTSStatic|cIRZ", "framework-gz"),
    # The classes that would otherwise end in other-ts, by owning campaign: LL's triage (docs/subsystems.md), then lane XJ's
    # reading of callers and address neighbours.  Each owns its name's family, so these sit just above the catch-all.
    (r"cI?TSProduct|cTSPattern(Catalog|Base)|cTS(ModelMaterial|Fence|CANH|RoofPattern)Entry|cTSMaterial(Base|Filter)"
     r"|cTS(Routing|Target)?Slot|cTSFloatConstant|cTSStringPool|cTSMissingObject|cTSOldPortalReader"
     r"|cTSEnvironmentScoreManager|cTSResourceFactoryString", "objects"),
    (r"cTSRoutingObjectAggregate|cTSPathFilter|cTSBaseLotInfo|cTSLoadLotController", "world-lot"),
    (r"cTS(MetaParticles|BaseParticles|Decal|Model|Sequence|Water|Sound|Light|Brush|Scrubber)(Effect|Description|Component)"
     r"|cTSCompDescriptionBaseT|cI?TS(Visual)?Effect|cTSParticleEffect|cTSDecal$|cTSPlaneCollider|cTSCurveError|cTSTestMap"
     r"|cTSRampGenerator|c(Walls|Floors|WallsAndFloors)Collider|cPinTo\w+Constraint", "rendering-gfx"),
    (r"cTSOverlay|cTSLightingParserState|cTSCommonPoolWater|cTSSliceImposterCreator|cTSDelauneyMesh"
     r"|cTSRenderTargetTextureManager|cTSTextureRecipeImageData", "scenegraph"),
    (r"cTSDataTables|cTSConfig|cITSConf|cTSDataImporterExporter|cTSCommonCOMDirector|cTSConstantsClient", "sim-core"),
    (r"cTSAttractionManager|cTSAutonomyConstantsClient|cTSPiecewiseFunction|cTSLearnedBehavior|cTSBadge|cTSExpenseReport"
     r"|cTS(OCL)?ShoppingCart|cTSShoppingToken", "sim-person"),
    (r"SimDoNodeActionLog", "simantics-vm"),
    (r"cTSWeatherInfo|cTSNewNeighborhoodSpecification|cTS(Load|Save)NeighborhoodController"
     r"|cTSUninstalledNeighborhoodTemplates", "neighbourhood"),
    (r"^cEM[A-Z]|cEventModifierBase|cFresh|cTSCustomStationLoader|cTSSoundInfo", "audio-video"),
    (r"cTSToolTipManager|cTSCtrlCreator|cTSKissingPoints|cTSSaveLotController|cTSStartupController|cTSUCPState"
     r"|cTSWitchReagentThumbnail|cTSHoverProxyFilter|(Token|Fragment|FragsOfType|CollectionBin|Anchor|GameTip)\w*Filter"
     r"|cITSUIModeObject", "ui-game"),
    (r"cTSResourceFactory|cTSKeyXlate|cTSDirectoryCache|cSLToken|cTSArchive|cIStreamStringBehaviourAdaptor", "persist-dbpf"),
    (r"cTSWebCOMDirector", "network-web"),
    (r"^cTSString$", "framework-gz"),
    (r"cITS|^cTS|nTS|^cS[A-Z]|^cI[A-Z]", "other-ts"),
]
RULES = [(re.compile(p), s) for p, s in RULES]

# String keywords: a function referencing a string matching one of these (and otherwise unlabeled) joins.
STRING_RULES = [
    (r"E_ABORT|CO_E_INIT|D3DERR_|DDERR_|DIERR_", "third-party", "dx-error-strings"),
    (r"tex1D will|intrinsic function '%s'|internal error: l-value|texcoord", "third-party", "hlsl-compiler"),
    (r"BHAV|SimAntics|simantics|primitive|Edith|edith", "simantics-vm", "simantics-strings"),
    (r"DBPF|\.package|dbpf", "persist-dbpf", "dbpf-strings"),
    (r"lua_|Lua|\.lua|luaL_", "lua-scripting", "lua-strings"),
    (r"CASCore|CAS", "cas", "cas-strings"),
    (r"\.cmp|Gimex|GIMEX", "third-party", "gimex"),
]
STRING_RULES = [(re.compile(p), s, t) for p, s, t in STRING_RULES]

# Subsystem display order / description (the list in docs/subsystems.md).
SUBSYSTEMS = [
    "framework-gz", "persist-dbpf", "simantics-vm", "sim-person", "objects", "sim-core", "world-lot",
    "neighbourhood", "scenegraph", "rendering-gfx", "animation", "ui-windowing", "ui-game", "cas",
    "audio-video", "lua-scripting", "command-parser", "network-web", "third-party", "other-ts",
    "crt-glue", "shared-utility", "other",
]
NEUTRAL_SIDES = {"other-ts", "crt-glue", "shared-utility"}  # code of many modules: says nothing about the module around it
INFER_MAX_GAP = 0x40000
PROMOTED = {"both=guess"}  # the strata filed on inference: docs/subsystems.md, Filing the unassigned code, has the measurements
EVERYONE_CALLS = {"framework-gz", "crt-glue"}  # calling these says nothing about the caller's own subsystem (lane XJ: hand
# sample, 2 of 10 calls-into framework-gz guesses were framework code; the rest were the classes around them)
OTHER = "other"  # call-graph guesses (the guess in "guess") and unconnected functions
THIRD_PARTY = "third-party"  # an exclusion: guessed third-party stays here, "guess" marking it
status_conflicts = []  # (rva, existing subsystem, existing basis, status subsystem, status basis)


def class_match_string(c):
    n = c["name"]
    if n.startswith(".?A"):
        n = n[4:].replace("@", " ")
    return n


def classify_class(c, by_name):
    """Subsystem of a class from its name, falling back to its base classes."""
    cand = [class_match_string(c)]
    for b in c.get("bases", []):
        cand.append(b.replace("@", " ") if b.startswith(".?A") else b)
    # a class is first judged by its own name, then by bases in order (most derived first)
    for s in cand:
        for rx, sub in RULES:
            if rx.search(s):
                return sub, s
    return None, None


def load_exe():
    import pefile
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    return pe


def reloc_refs(pe):
    """[(position rva, value)] for every relocated absolute dword (exact: the unpacked exe keeps its
    relocation table), so immediates/pointers to image addresses are found without disassembly."""
    import pefile
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
    img = pe.get_memory_mapped_image()
    out = []
    for block in pe.DIRECTORY_ENTRY_BASERELOC:
        for e in block.entries:
            if e.type == 3 and e.rva + 4 <= len(img):
                out.append((e.rva, int.from_bytes(img[e.rva:e.rva + 4], "little")))
    out.sort()
    return out


REG32 = {"eax": "eax", "ax": "eax", "al": "eax", "ah": "eax", "ecx": "ecx", "cx": "ecx", "cl": "ecx", "ch": "ecx",
         "edx": "edx", "dx": "edx", "dl": "edx", "dh": "edx", "ebx": "ebx", "bx": "ebx", "bl": "ebx", "bh": "ebx",
         "esi": "esi", "si": "esi", "edi": "edi", "di": "edi", "ebp": "ebp", "esp": "esp"}


def lib_names():
    """(_EH_prolog / _SEH_prolog helpers, operator new / new[]) as absolute addresses, from db/library-matches.jsonl."""
    prolog, new = set(), set()
    for m in tsdb.jsonl("library-matches.jsonl"):
        if any("_prolog" in n.lower() for n in m["names"]):
            prolog.add(BASE + int(m["rva"], 16))
        if any(n.startswith(("??2@", "??_U@")) for n in m["names"]):
            new.add(BASE + int(m["rva"], 16))
    return prolog, new


class VftableStores:
    """Where a function writes the vftable addresses it holds (the ctor:<class> rule).

    A vftable address in a function's body is a store of it (mov [m], imm32; or mov reg, imm32 then mov [m], reg). It is
    the function's own class only when the destination is the function's `this` (ecx at entry, followed through register
    copies) or an object a call returned to it that it returns in turn (an inlined factory: operator new, or in a
    function of at most SMALL_FACTORY bytes any call, for the pool allocators), at the vftable's subobject offset. Everything else is code that merely uses an object of that class: a local (esp/ebp frame), a member (another
    offset), a global, or a heap object built inline and kept. One linear pass per function, no branches followed: a
    register written anywhere before the store stops counting as `this` (calls clobber eax/ecx/edx, except the _EH_prolog
    helpers, which keep ecx); `xor r, r` keeps r as the new object (the null arm of `p ? new T : 0`).
    """

    SMALL_FACTORY = 512  # bytes: up to this size, any call's result may be the allocation (pool allocators)

    def __init__(self, pe):
        from capstone import CS_ARCH_X86, CS_MODE_32, Cs
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        self.img = pe.get_memory_mapped_image()
        self.prolog, self.new = lib_names()

    def classify(self, start, size, positions):
        """{position rva: ("this" | "new" | "", displacement)} for the vftable dwords at the given positions."""
        from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
        want = {BASE + p for p in positions}
        out = {}
        this, new = {"ecx"}, set()
        pending = {}  # register -> position of the vftable address it holds
        returns_new = False
        for i in self.md.disasm(bytes(self.img[start:start + size]), BASE + start):
            if i.mnemonic == "ret" and "eax" in new:
                returns_new = True
            hit = next((p for p in want if i.address <= p < i.address + i.size), None)
            ops = i.operands
            if i.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X86_OP_MEM:
                m = ops[0].mem
                base = REG32.get(i.reg_name(m.base), "") if m.base else ""
                src = hit
                if src is None and ops[1].type == X86_OP_REG:
                    src = pending.get(REG32.get(i.reg_name(ops[1].reg)))
                if src is not None and not m.index:
                    out[src - BASE] = ("this" if base in this else "new" if base in new else "", m.disp)
                continue
            if hit is not None and i.mnemonic == "mov" and ops and ops[0].type == X86_OP_REG:
                pending[REG32.get(i.reg_name(ops[0].reg), "")] = hit
                continue
            if i.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG:
                d, s = REG32.get(i.reg_name(ops[0].reg)), REG32.get(i.reg_name(ops[1].reg))
                for regs in (this, new):
                    if s in regs:
                        regs.add(d)
                    else:
                        regs.discard(d)
                pending.pop(d, None)
                continue
            if i.mnemonic == "call":
                target = ops[0].imm if ops and ops[0].type == X86_OP_IMM else None
                if target in self.prolog:
                    continue
                for r in ("eax", "ecx", "edx"):
                    this.discard(r)
                    new.discard(r)
                    pending.pop(r, None)
                if target in self.new or size <= self.SMALL_FACTORY:
                    new.add("eax")  # operator new, or a pool allocator's take in a short function
                continue
            zeroing = i.mnemonic == "xor" and len(ops) == 2 and ops[0].type == X86_OP_REG and ops[0].reg == ops[1].reg
            try:
                written = i.regs_access()[1]
            except Exception:
                written = []
            for w in written:
                r = REG32.get(i.reg_name(w))
                this.discard(r)
                if not zeroing:
                    new.discard(r)
                pending.pop(r, None)
        if not returns_new:
            out = {p: ("" if k == "new" else k, d) for p, (k, d) in out.items()}
        return out


def scan_dwords(relocs, funcs_sorted_starts, funcs_by_idx, targets_lo, targets_hi):
    """(function rva, value, pos) for relocated dwords in function bodies with a value in [lo, hi)."""
    res = []
    for pos, val in relocs:
        if not (targets_lo <= val < targets_hi):
            continue
        k = bisect.bisect_right(funcs_sorted_starts, pos) - 1
        if k >= 0:
            f = funcs_by_idx[k]
            if pos < int(f["rva"], 16) + f["size"]:
                res.append((f["rva"], val, pos))
    return res


STATUS_LEVELS = ("mapped", "reimplemented", "verified", "trace-verified", "replaced")  # all at `mapped` or above


def split_top(text, sep="::"):
    """`text` split at `sep` outside <> and ()."""
    parts, depth, cur, i = [], 0, "", 0
    while i < len(text):
        ch = text[i]
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth = max(0, depth - 1)
        if depth == 0 and text.startswith(sep, i):
            parts.append(cur)
            cur, i = "", i + len(sep)
            continue
        cur += ch
        i += 1
    parts.append(cur)
    return parts


def name_class(name):
    """The qualifier of a function name, `ns::cFoo` for `ns::cFoo::Bar thunk (shared_vf_1)`; None for a bare name.
    The name is cut at its first space outside <> (comments such as ` (deleting destructor)`, ` thunk`)."""
    depth, end = 0, len(name)
    for i, ch in enumerate(name):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth = max(0, depth - 1)
        elif ch == " " and depth == 0:
            end = i
            break
    parts = split_top(name[:end])
    if len(parts) < 2 or not all(parts):
        return None
    return "::".join(parts[:-1])


def outer_name(cls):
    """A class's own name without namespace or template arguments (tsdb.class_outer, which also reads mangled names)."""
    return tsdb.class_outer(cls)


def class_index(cls_sub, placed_names):
    """class key -> subsystem, from db/classes (`cls_sub`: class name -> subsystem by the name rules) and from
    evidence-placed named functions (`placed_names`: (function name, subsystem) pairs, a class takes its functions'
    subsystem when at least 2/3 of them agree).  Keys are the full class name and, when it is unambiguous, its outer
    name.  db/classes wins over function votes for the same key."""
    full, outer = {}, collections.defaultdict(set)
    votes = collections.defaultdict(collections.Counter)
    for name, sub in placed_names:
        c = name_class(name)
        if c:
            votes[c][sub] += 1
    for c, cnt in votes.items():
        sub, k = cnt.most_common(1)[0]
        if 3 * k >= 2 * sum(cnt.values()):
            full[c] = sub
    for c, sub in cls_sub.items():
        full[c] = sub
    for c, sub in full.items():
        outer[outer_name(c)].add(sub)
    return full, {o: next(iter(subs)) for o, subs in outer.items() if len(subs) == 1}


def lookup_class(cls, index):
    full, outer = index
    if cls in full:
        return full[cls], cls
    o = outer_name(cls)
    if o in outer:
        return outer[o], o
    return None, None


# STL / std instantiations: library code a lane met while working on its class, never filed by the lane's group
STL_QUALIFIER = re.compile(r"(^|::|<|\s)(std|stdext)::|^(std|stdext)$|(^|::)(vector|list|map|multimap|set|multiset|"
                           r"deque|pair|hash_map|hash_set|unordered_map|unordered_set|basic_string|basic_[a-z]+|"
                           r"_Tree\w*|_List\w*|_Vector\w*|_Hash\w*|_Deque\w*|_String\w*|allocator|auto_ptr|"
                           r"iterator|_Ptr_base|shared_ptr)(<|$)")


def is_stl(qualifier):
    """True for a qualifier naming an STL / std instantiation (`std::...`, `vector<...>`, `_Tree<...>`)."""
    return bool(STL_QUALIFIER.search(qualifier or ""))


def rule_subsystem(text):
    """The subsystem the class-name RULES give a qualifier or group name (`nGZSceneGraph::cFooImpl` -> scenegraph), or
    None.  other-ts (the catch-all for any cTS / nTS name) says nothing about a campaign, so it gives None."""
    for rx, sub in RULES:
        if rx.search(text):
            return None if sub == "other-ts" else sub
    return None


def status_seeds(status, names, index, shared_vf, forced=None):
    """(rva, subsystem, basis) for each db/status entry whose name's class has a subsystem: a class-backed one (db/classes
    by the name rules, or the class's evidence-placed functions), else the name rules applied to the qualifier itself
    (`status-rule:`, for classes without RTTI, such as `nGZSceneGraph::cViewerRefNodeBaseImpl`; lane WH, 2026-11-04);
    for a name without either, the same two by its group file's name.  Folded `shared_vf_*` bodies are skipped: they
    serve several classes; so are STL / std qualifiers for the fallbacks (library instantiations, not the group's
    class: coordinator, 2026-11-04).  `forced`: group -> subsystem for a GROUP_LIST entry with "force": the group's campaign
    owns its members whatever class they are written on (`cTSObject::GenericSimCall` is SimAntics opcode 0x01)."""
    forced = forced or {}
    out = []
    for rva, e in sorted(status.items()):
        if e.get("status") not in STATUS_LEVELS or rva in shared_vf:
            continue
        name = e.get("name") or names.get(rva, {}).get("name", "")
        if "shared_vf_" in name:
            continue
        if group_name(e) in forced:
            out.append((rva, forced[group_name(e)], ("group-force:" + group_name(e))[:48]))
            continue
        sub = None
        cls = name_class(name)
        if cls:
            sub, key = lookup_class(cls, index)
            basis = "status:" + str(key)
        stl = stl_named(name)  # nor a bare name that is one
        if sub is None and cls and not stl:
            sub, basis = rule_subsystem(cls), "status-rule:" + cls
        if sub is None and not stl:
            group = group_name(e)
            if group:
                sub, key = lookup_class(group, index)
                basis = "status-group:" + str(key)
                if sub is None:
                    sub, basis = rule_subsystem(group), "status-rule:" + group
        if sub:
            out.append((rva, sub, basis[:48]))
    return out


# Status groups (a db/status file named for a group rather than a class): its members whose class doesn't resolve take
# the group's subsystem, from the coordinator's list (GROUP_LIST) or from the 2/3 byte majority of its members placed
# by evidence (lane WH, 2026-11-04).  These are never a majority target: other-ts and shared-utility say nothing about
# a campaign, and third-party is an exclusion (moving EA code there would change the headline on reclassification).
GROUP_LIST = "subsystem-groups.jsonl"  # {"group", "subsystem", "reason"}; kept by the coordinator
BAND_LIST = "subsystem-bands.jsonl"  # {"lo", "hi", "subsystem", "name", "reason"}: reviewed address ranges of unnamed code
NO_GROUP_MAJORITY = {"other-ts", "shared-utility", THIRD_PARTY, OTHER}
MIXED = "mixed"  # a list entry for a group reviewed as several subsystems' code: its members keep per-function evidence


def group_name(entry):
    """The status group of a db/status entry: its file name without `.jsonl`."""
    return (entry.get("_file") or "").rsplit(".", 1)[0]


def stl_named(name):
    """True for a function name that is an STL / std instantiation (by its qualifier, or a bare name that says `std::`).
    A non-STL qualifier decides: `nGZCommandParser::EvalFunctionTable::EvalFunctionTable (... std::map ...)` is the
    class's, whatever its comment says."""
    cls = name_class(name)
    if cls:
        return is_stl(cls)
    return "std::" in (name or "")


def group_members(status, names, shared_vf):
    """group -> [rva] of the db/status entries (mapped or above) a group rule may place: not folded `shared_vf_*`
    bodies (several classes' code) and not STL / std instantiations (the single-user rule's, below)."""
    out = collections.defaultdict(list)
    for rva, e in sorted(status.items()):
        if e.get("status") not in STATUS_LEVELS or rva in shared_vf:
            continue
        name = e.get("name") or names.get(rva, {}).get("name", "")
        if "shared_vf_" in name or stl_named(name):
            continue
        g = group_name(e)
        if g:
            out[g].append(rva)
    return out


def group_seeds(members, assign, sizes, listed):
    """(seeds, report) for the status groups.  `members`: group -> [rva] (group_members); `assign`: rva -> (subsystem,
    basis) placed by evidence so far; `sizes`: rva -> bytes; `listed`: group -> subsystem (GROUP_LIST).
    A listed group's unplaced members take its subsystem (`group-list:<group>`); otherwise, when at least 2/3 of the
    placed members' bytes share one subsystem (not NO_GROUP_MAJORITY), the unplaced members take it
    (`group-majority:<group>`).  A group listed MIXED takes no list subsystem (the majority still applies) and counts
    as reviewed for the merge check.  seeds: [(rva, subsystem, basis)]; report: group -> {"placed": Counter(subsystem ->
    bytes), "unplaced": [rva], "majority": subsystem or None, "listed": subsystem or None, "rule": "list" | "majority"
    | None}."""
    seeds, report = [], {}
    for g, rvas in sorted(members.items()):
        placed = collections.Counter()
        unplaced = []
        for r in rvas:
            if r in assign:
                placed[assign[r][0]] += sizes.get(r, 0)
            elif r in sizes:
                unplaced.append(r)
        majority = None
        if placed:
            top, b = placed.most_common(1)[0]
            if top not in NO_GROUP_MAJORITY and 3 * b >= 2 * sum(placed.values()):
                majority = top
        if g in listed and listed[g] != MIXED:
            sub, rule = listed[g], "list"
        elif majority:
            sub, rule = majority, "majority"
        else:
            sub, rule = None, "mixed" if g in listed else None
        report[g] = {"placed": placed, "unplaced": unplaced, "majority": majority, "listed": listed.get(g), "rule": rule}
        if sub:
            basis = ("group-list:" if rule == "list" else "group-majority:") + g
            seeds += [(r, sub, basis[:48]) for r in unplaced]
    return seeds, report


def load_forced_groups():
    """group -> subsystem for the GROUP_LIST entries marked "force"."""
    return {e["group"]: e["subsystem"] for e in tsdb.jsonl(GROUP_LIST) if e.get("force")}


def load_held_groups():
    """The GROUP_LIST entries marked "held": third-party or EA is undecided, and what the call graph says of a member is no
    answer, so a member without evidence stays a guess of third-party and the headline does not move by reclassification."""
    return [e["group"] for e in tsdb.jsonl(GROUP_LIST) if e.get("held")]


def load_bands():
    """(lo, hi, subsystem, name) from db/subsystem-bands.jsonl, sorted: a reviewed address range of one campaign's code.
    A campaign subsystem, as for the group list; third-party ranges are db/third-party.jsonl's.  Ranges may not overlap."""
    p = tsdb.ROOT / "db" / BAND_LIST
    out = []
    if p.exists():
        for line in p.read_text(encoding="utf-8").splitlines():
            if line.strip():
                e = json.loads(line)
                lo, hi = int(e["lo"], 16), int(e["hi"], 16)
                if e["subsystem"] not in SUBSYSTEMS or e["subsystem"] in NO_GROUP_MAJORITY:
                    raise SystemExit(f"db/{BAND_LIST}: {e['lo']}: not a campaign subsystem: {e['subsystem']}")
                if lo >= hi or not e.get("name") or not e.get("reason"):
                    raise SystemExit(f"db/{BAND_LIST}: {e['lo']}: needs lo < hi, a name and a reason")
                out.append((lo, hi, e["subsystem"], e["name"]))
    out.sort()
    for x, y in zip(out, out[1:]):
        if y[0] < x[1]:
            raise SystemExit(f"db/{BAND_LIST}: {y[3]} overlaps {x[3]}")
    return out


def band_seeds(rvas, bands):
    """(rva, subsystem, basis) for the rvas inside a band.  `rvas` in any order; the caller's put() keeps earlier evidence."""
    starts = [b[0] for b in bands]
    out = []
    for r in rvas:
        a = int(r, 16)
        k = bisect.bisect_right(starts, a) - 1
        if k >= 0 and a < bands[k][1]:
            out.append((r, bands[k][2], "band:" + bands[k][3]))
    return out


def load_group_list():
    """group -> subsystem from db/subsystem-groups.jsonl (the coordinator's exceptions): a campaign subsystem or MIXED.
    Not third-party: that is db/third-party.jsonl's address ranges (an exclusion from the EA-code denominator)."""
    p = tsdb.ROOT / "db" / GROUP_LIST
    out = {}
    if p.exists():
        lines = p.read_text(encoding="utf-8").splitlines()
        for line in lines:
            if line.strip():
                e = json.loads(line)
                if e["subsystem"] != MIXED and (e["subsystem"] not in SUBSYSTEMS or e["subsystem"] in NO_GROUP_MAJORITY):
                    raise SystemExit(f"db/{GROUP_LIST}: {e['group']}: not a campaign subsystem: {e['subsystem']}")
                out[e["group"]] = e["subsystem"]
    return out


def stl_single_user_seeds(stl_rvas, assign, weak, callers):
    """(rva, subsystem) for the STL-shaped functions (`stl_rvas`: non-library, named as an STL / std instantiation)
    whose callers (direct calls and tail-call jumps, library callers already left out of `callers`) are all placed by
    evidence in one subsystem (not shared-utility, nor third-party: an exclusion): an instantiation one subsystem alone
    uses is that subsystem's code.  Iterated, so one instantiation called only from another follows it.  Placements go into `assign`."""
    out = []
    changed = True
    while changed:
        changed = False
        for rva in sorted(stl_rvas):
            if rva in assign:
                continue
            cs = [c for c in callers.get(rva, ()) if c != rva]
            if not cs or any(c not in assign or c in weak for c in cs):
                continue
            subs = {assign[c][0] for c in cs}
            if len(subs) == 1 and next(iter(subs)) not in ("shared-utility", THIRD_PARTY, OTHER):
                sub = next(iter(subs))
                assign[rva] = (sub, "stl-single-user")
                out.append((rva, sub))
                changed = True
    return out


def shared_vtable_seeds(classes, cls_sub, shared_vf, assign):
    """(rva, subsystem, class) for each folded `shared_vf_*` body whose vtables all belong to classes of one subsystem:
    folding across a family says nothing about ownership when the whole family has one owner.  The catch-all other-ts
    takes no such body."""
    subs, who = collections.defaultdict(set), {}
    for c in classes:
        sub = cls_sub.get(c["name"])
        for v in c["vftables"]:
            for r in v["entries"]:
                if r in shared_vf:
                    subs[r].add(sub)
                    who.setdefault(r, class_match_string(c))
    return [(r, next(iter(s)), who[r]) for r, s in sorted(subs.items())
            if len(s) == 1 and None not in s and "other-ts" not in s and r not in assign]


def contained(F):
    """rva -> rva of the function it starts inside: a body extended by tools/missed-functions (`resized`) covers pieces the
    analysis listed as functions of their own, whose bytes are the parent's."""
    out, parent, end = {}, None, 0
    for rva in sorted(F, key=lambda r: int(r, 16)):
        a, n = int(rva, 16), F[rva]["size"]
        if a < end and a + n <= end:
            out[rva] = parent
        elif a + n > end:
            parent, end = rva, a + n
    return out


def pointer_tables(relocs, in_data, skip=frozenset(), minimum=2):
    """Pointer tables in the data sections: [(first position, position after, [values])] for each run of at least `minimum`
    consecutive relocated dwords.  `relocs` is (position, value - BASE) sorted by position; `in_data(position)` says a
    position is in a data section; `skip`: positions that start a known vtable, which ends the run before them."""
    out, run = [], []

    def flush():
        if len(run) >= minimum:
            out.append((run[0][0], run[-1][0] + 4, [v for _, v in run]))
        run.clear()

    for pos, val in relocs:
        if run and (pos != run[-1][0] + 4 or pos in skip):
            flush()
        if in_data(pos):
            run.append((pos, val))
        else:
            flush()
    flush()
    return out


def table_users(tables, refs_to):
    """For each table index, the functions that reach it: those referring to one of its positions (`refs_to(position)`
    gives them), plus, through a table of tables, the users of every table holding a pointer into it."""
    lo = [t[0] for t in tables]

    def find(v):
        k = bisect.bisect_right(lo, v) - 1
        return k if k >= 0 and v < tables[k][1] else None

    users = [set() for _ in tables]
    parents = [set() for _ in tables]
    for k, (a, b, values) in enumerate(tables):
        for p in range(a, b, 4):
            users[k] |= refs_to(p)
        for v in values:
            c = find(v)
            if c is not None and c != k:
                parents[c].add(k)
    for _ in range(6):  # tables nest a few levels at most
        changed = False
        for k in range(len(tables)):
            for q in parents[k]:
                if not users[q] <= users[k]:
                    users[k] |= users[q]
                    changed = True
        if not changed:
            break
    return users


def flanks(order, assign, weak):
    """rva -> (subsystem of the nearest evidence function before it, the same after it, the gap between those two) for each
    function without evidence, in address order.  The link order keeps a module's code together, so what lies between two
    functions of one subsystem is that subsystem's, far more reliably than its callers say (the method in
    docs/subsystems.md).  Neutral subsystems are no evidence of a module; a missing side is None with a gap of None."""
    out, ev = {}, []
    for rva in order:
        a = assign.get(rva)
        if a and rva not in weak and a[0] not in NEUTRAL_SIDES:
            ev.append((int(rva, 16), a[0]))
    ks = [e[0] for e in ev]
    for rva in order:
        if rva in assign and rva not in weak:
            continue
        k = bisect.bisect_left(ks, int(rva, 16))
        lo = ev[k - 1] if k else None
        hi = ev[k] if k < len(ev) else None
        out[rva] = (lo and lo[1], hi and hi[1], hi[0] - lo[0] if lo and hi else None)
    return out


def inferred(order, assign, weak, max_gap=INFER_MAX_GAP):
    """[(rva, subsystem, stratum)]: the guesses and unassigned functions the address order settles.
    "both=guess": both flanks and the call graph agree.  "both": both flanks agree and the graph has nothing to add.
    "both>guess": both flanks agree against the graph (the graph's guess lost in the hand sample).
    "side=guess": the graph picks one of two different flanks.
    Only within a gap of max_gap, and never into third-party: code filed there stays an exclusion on its own evidence."""
    out = []
    for rva, (lo, hi, gap) in flanks(order, assign, weak).items():
        guess = assign[rva][0] if rva in assign else None
        if guess == THIRD_PARTY or gap is None or gap >= max_gap:
            continue
        if lo == hi:
            if lo == THIRD_PARTY:
                continue
            out.append((rva, lo, "both=guess" if guess == lo else "both" if guess in (None, "shared-utility")
                        else "both>guess"))
        elif guess in (lo, hi):
            out.append((rva, guess, "side=guess"))
    return out


def settle_by_address(order, assign, weak):
    """Apply inferred(): the strata in PROMOTED become filed (basis "inferred:<prior basis>", kept apart from evidence by
    is_inferred); a function the graph left with nothing or with shared-utility, between two flanks that agree, takes the
    flanks as its guess only (basis "flanks": the holdouts put that stratum at 60-80%, too low to file)."""
    for rva, sub, stratum in inferred(order, assign, weak):
        if stratum in PROMOTED:
            assign[rva] = (sub, "inferred:" + assign[rva][1])
            weak.discard(rva)
        elif stratum == "both":
            assign[rva] = (sub, "flanks")
            weak.add(rva)


def is_inferred(row):
    return row["basis"].startswith("inferred:")


def build(hide=frozenset()):
    """The map's inputs and assignment.  `hide`: rvas that take no evidence seed (the accuracy check, tools/subsystems_eval.py:
    each is left to the inference rules, then compared with the row it has in the full run)."""
    funcs = tsdb.functions()
    F = {f["rva"]: f for f in funcs}
    lib = tsdb.library_rvas()
    status = tsdb.load_status()
    classes = tsdb.jsonl("classes.jsonl")
    names = {n["rva"]: n for n in tsdb.jsonl("names.jsonl")}
    tails = {t["rva"]: t["targets"] for t in tsdb.jsonl("tail-calls.jsonl")}
    inits = tsdb.jsonl("static-initializers.jsonl")
    tp = tsdb.jsonl("third-party.jsonl")

    assign = {}   # rva -> (subsystem, basis)
    weak = set()  # rvas whose subsystem is only a call-graph guess (written as "other" with "guess")
    taken = set()  # rvas whose address some function takes
    seeding = [True]  # the evidence phase: `hide` applies to it

    def put(rva, sub, basis, strong=True):
        if rva in lib or rva not in F or rva in assign or (seeding[0] and rva in hide):
            return
        assign[rva] = (sub, basis)
        if not strong:
            weak.add(rva)

    # --- seeds 1: third-party address ranges
    for t in tp:
        for lo, hi in t["rvas"]:
            lo, hi = int(lo, 16), int(hi, 16)
            short = re.sub(r"[^A-Za-z0-9]+", "-", t["name"].split("(")[0].strip()).strip("-").lower()
            for rva, f in F.items():
                if lo <= int(rva, 16) < hi:
                    put(rva, "third-party", "third-party:" + short)
    for sec in load_exe().sections:
        if sec.Name.strip(bytes([0])) == b".bind":
            lo, hi = sec.VirtualAddress, sec.VirtualAddress + sec.Misc_VirtualSize
            for rva in F:
                if lo <= int(rva, 16) < hi:
                    put(rva, "third-party", "third-party:steam-drm-bind")
    # Lua & others in third-party table are covered; GZ wrappers (cGZLua5 etc.) stay with their class rules.

    # --- seeds 2: vtables of named classes
    cls_sub = {}
    unmatched_classes = []
    for c in classes:
        if not c["vftables"]:
            continue
        sub, via = classify_class(c, None)
        if sub is None:
            unmatched_classes.append(c)
            continue
        cls_sub[c["name"]] = sub
    slot_votes = collections.defaultdict(collections.Counter)
    for c in classes:
        sub = cls_sub.get(c["name"])
        if not sub:
            continue
        cname = class_match_string(c)
        for v in c["vftables"]:
            for r in v["entries"]:
                if r in names and names[r]["name"].startswith("shared_vf_"):
                    continue
                slot_votes[r][(sub, cname)] += 1
    for r, votes in slot_votes.items():
        sub_tot = collections.Counter()
        for (s, n), k in votes.items():
            sub_tot[s] += k
        sub = sub_tot.most_common(1)[0][0]
        cname = next(n for (s, n), k in votes.most_common() if s == sub)
        put(r, sub, "vtable:" + cname[:40])
    shared_vf = {r for r, n in names.items() if n["name"].startswith("shared_vf_")}

    # --- seeds 3: functions that store a named class's vftable address (constructors/destructors)
    pe = load_exe()
    starts = sorted(int(r, 16) for r in F)
    fsorted = [F[hex(s)] for s in starts]
    vt_class = {}  # vftable address -> (class, offset of the subobject it belongs to)
    for c in classes:
        for v in c["vftables"]:
            vt_class[BASE + int(v["rva"], 16)] = (c, v.get("offset", 0))
    relocs = reloc_refs(pe)
    if vt_class:
        lo, hi = min(vt_class), max(vt_class) + 1
        hits = scan_dwords(relocs, starts, fsorted, lo, hi)
        byfunc = collections.defaultdict(list)
        for rva, val, pos in hits:
            if val in vt_class and vt_class[val][0]["name"] in cls_sub and rva not in assign and rva not in lib:
                byfunc[rva].append((val, pos))
        stores = VftableStores(pe)
        for rva, lst in byfunc.items():
            kinds = stores.classify(int(rva, 16), F[rva]["size"], [p for _, p in lst])
            best = None
            for val, pos in lst:
                c, off = vt_class[val]
                if kinds.get(pos) not in (("this", off), ("new", off)):
                    continue  # a local, a member, a global or a heap object built inline: not this class's code
                d = len(c.get("bases", []))
                if best is None or d > best[0]:
                    best = (d, c)
            if best:
                put(rva, cls_sub[best[1]["name"]], "ctor:" + class_match_string(best[1])[:40])

    # --- strings and data references (computed, not stored)
    # global data refs: dwords pointing into the image's data sections
    data_secs = [(s.VirtualAddress, s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData), s.Name.rstrip(b"\0").decode())
                 for s in pe.sections if not (s.Characteristics & 0x20000000)]  # not executable
    dlo = min(a for a, b, n in data_secs) + BASE
    dhi = max(b for a, b, n in data_secs) + BASE
    refs = scan_dwords(relocs, starts, fsorted, dlo, dhi)
    img = pe.get_memory_mapped_image()

    def cstr(rva):
        if rva >= len(img):
            return None
        e = img.find(b"\0", rva, rva + 200)
        if e < 0 or e - rva < 5:
            return None
        s = img[rva:e]
        if all(32 <= b < 127 for b in s):
            return s.decode()
        return None

    gl_refs = collections.defaultdict(set)        # global address -> referencing functions
    str_hits = collections.defaultdict(collections.Counter)
    for rva, val, pos in refs:
        gl_refs[val].add(rva)
        s = cstr(val - BASE)
        if s:
            for rx, sub, tag in STRING_RULES:
                if rx.search(s):
                    str_hits[rva][(sub, tag)] += 1
    for rva, cnt in str_hits.items():
        if len(cnt) == 1 and rva not in assign:
            sub, tag = next(iter(cnt))
            put(rva, sub, "string-ref:" + tag)

    # --- seeds 4: db/status names.  A lane that maps or reimplements a function has named its class; that class's
    # class-backed subsystem (by the name rules, or by its evidence-placed functions) is evidence.  An existing
    # evidence basis wins; disagreements are kept in status_conflicts for the summary.
    placed = [(names[r]["name"], a[0]) for r, a in assign.items() if r in names and a[0] != "third-party"]
    placed += [(e["name"], assign[r][0]) for r, e in status.items() if r in assign and e.get("name")
               and assign[r][0] != "third-party"]
    index = class_index(cls_sub, placed)
    status_conflicts.clear()
    for rva, sub, basis in status_seeds(status, names, index, shared_vf, load_forced_groups()):
        if rva in assign:
            if assign[rva][0] != sub:
                status_conflicts.append((rva, assign[rva][0], assign[rva][1], sub, basis))
            continue
        put(rva, sub, basis)

    for rva, sub, cname in shared_vtable_seeds(classes, cls_sub, shared_vf, assign):
        put(rva, sub, ("vtable-shared:" + cname)[:48])

    # --- graph: callers / callees, thunks
    callees = {}
    for rva, f in F.items():
        cs = set(c for c in f.get("callees", []) if c in F)
        cs |= set(c for c in tails.get(rva, []) if c in F)
        callees[rva] = cs
    callers = collections.defaultdict(set)
    for rva, cs in callees.items():
        if rva in lib:
            continue
        for c in cs:
            callers[c].add(rva)
    # taking a function's address in code (a callback, a thread entry, a window procedure) connects it like a call
    text = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Characteristics & 0x20000000]
    fn_lo, fn_hi = min(starts) + BASE, max(starts) + 1 + BASE
    for rva, val, pos in scan_dwords(relocs, starts, fsorted, fn_lo, fn_hi):
        tgt = hex(val - BASE)
        if tgt in F and rva != tgt and rva not in lib:
            callers[tgt].add(rva)
            taken.add(tgt)

    # --- seeds 5: status groups (GROUP_LIST, else the placed members' 2/3 byte majority), then STL / std
    # instantiations that one subsystem alone calls.  Both follow evidence only (nothing weak exists yet).
    members = group_members(status, names, shared_vf)
    gseeds, group_report = group_seeds(members, assign, {r: f["size"] for r, f in F.items()}, load_group_list())
    for rva, sub, basis in gseeds:
        put(rva, sub, basis)
    # reviewed address ranges, where the code has no RTTI and no strings to say whose it is: after the class and group
    # evidence (a vtable, a constructor, a status name or a listed group inside a band wins), before anything the graph says
    banded = set()  # a band is a helper layer: its callers are many, and calling it says nothing about them (as EVERYONE_CALLS)
    for rva, sub, basis in band_seeds(F, load_bands()):
        put(rva, sub, basis)
        if assign.get(rva) == (sub, basis):
            banded.add(rva)
    stl_rvas = set()
    for rva in F:
        if rva in lib or rva in assign or rva in shared_vf or rva in hide:
            continue
        name = (status.get(rva) or {}).get("name") or names.get(rva, {}).get("name", "")
        if name and "shared_vf_" not in name and (stl_named(name) or "std::" in name):  # or described as one
            stl_rvas.add(rva)
    stl_single_user_seeds(stl_rvas, assign, weak, callers)
    # static initialisers: they construct objects in ecx; assign by what they call (done in propagation below)
    init_rvas = {e["rva"] for e in inits}

    def sub_of(r):
        a = assign.get(r)
        return a[0] if a else None

    def propagate(modes):
        changed = True
        rounds = 0
        while changed and rounds < 60:
            changed = False
            rounds += 1
            for rva in F:
                if rva in assign or rva in lib:
                    continue
                f = F[rva]
                if "called" in modes:
                    cs = sorted(c for c in callers.get(rva, ()) if c != rva)  # sorted: a tie must not depend on hashing
                    subs = [sub_of(c) for c in cs]
                    known = [s for s in subs if s]
                    if known:
                        cnt = collections.Counter(known)
                        top, k = cnt.most_common(1)[0]
                        if len(cnt) == 1 and len(known) == len(cs):
                            put(rva, top, "called-only-from", False); changed = True; continue
                        if len(cnt) >= 3 and "shared" in modes:
                            put(rva, "shared-utility", "shared-callers", False); changed = True; continue
                        if k / len(known) >= 0.75 and len(known) >= 2 and len(known) >= 0.6 * len(cs) and "mostly" in modes:
                            put(rva, top, "called-mostly-from", False); changed = True; continue
                if "calls" in modes:
                    cs = sorted(c for c in callees[rva] if c not in lib and c not in banded
                                and sub_of(c) not in (None, "shared-utility", "other-ts"))
                    known = [sub_of(c) for c in cs]
                    if f["thunk"] and len(known) == 1:
                        put(rva, known[0], "thunk", cs[0] not in weak); changed = True; continue  # as strong as its target
                    known = [k for k in known if k not in EVERYONE_CALLS]
                    if len(known) >= 2:
                        cnt = collections.Counter(known)
                        top, k = cnt.most_common(1)[0]
                        if k / len(known) >= 0.7:
                            put(rva, top, "calls-into", False); changed = True; continue

    # static initialisers
    for e in inits:
        r = e["rva"]
        if r in assign:
            continue
        subs = [sub_of(c) for c in e["callees"] + e["atexit"] if sub_of(c)]
        subs = [s for s in subs if s not in ("shared-utility",)]
        if subs:
            top, k = collections.Counter(subs).most_common(1)[0]
            put(r, top, "static-init")
    seeding[0] = False
    # phase order: strict first, then looser, then data refs, then looser again
    propagate({"called"})
    propagate({"called", "calls"})
    vt_starts = {v - BASE for v in vt_class}
    tables = pointer_tables([(p, v - BASE) for p, v in relocs], lambda p: not any(a <= p < b for a, b in text), vt_starts)
    users = table_users(tables, lambda p: gl_refs.get(p + BASE, set()))

    def table_rule():
        """A table's functions belong to whoever uses the table: every user placed, in one subsystem."""
        n = 0
        for (lo, hi, values), voters in zip(tables, users):
            known = [x for x in voters if sub_of(x) and sub_of(x) not in ("shared-utility", "other-ts")]
            subs = {sub_of(x) for x in known}
            if len(subs) == 1:
                strong = len(known) == len(voters) and not any(x in weak for x in known)
                for v in values:
                    r = hex(v)
                    if r in F and r not in assign and r not in lib:
                        put(r, next(iter(subs)), "ptr-table:" + hex(lo), strong)
                        n += 1
        return n

    # data refs: a rarely-referenced global linking an unassigned function to one subsystem
    for rnd in range(3):
        newly = table_rule()
        for g, fs in gl_refs.items():
            if len(fs) < 2 or len(fs) > 12:
                continue
            voters = [x for x in fs if sub_of(x) and sub_of(x) not in ("shared-utility",)]
            subs = collections.Counter(sub_of(x) for x in voters)
            if len(subs) == 1:
                top = next(iter(subs))
                strong = not any(x in weak for x in voters)  # evidence only when every referrer is
                for x in fs:
                    if x not in assign and x not in lib:
                        put(x, top, "data-ref", strong); newly += 1
        propagate({"called", "calls", "mostly"})
        if not newly:
            break
    propagate({"called", "calls", "mostly", "shared"})
    # the undecided groups stay out of the EA-code denominator on a guess, until the coordinator decides them
    for g in load_held_groups():
        for rva in members.get(g, ()):
            if rva not in lib and (rva not in assign or rva in weak):
                assign[rva] = (THIRD_PARTY, "group-held:" + g)
                weak.add(rva)
    settle_by_address([r for r in sorted(F, key=lambda r: int(r, 16)) if r not in lib], assign, weak)
    # a piece inside a resized body is that body's, whatever the graph said of it
    for rva, parent in contained(F).items():
        if rva not in lib and parent in assign and (rva not in assign or rva in weak):
            assign[rva] = (assign[parent][0], "inside:" + parent)
            weak.discard(rva)
            if parent in weak:
                weak.add(rva)
    return F, lib, assign, weak, status, unmatched_classes, shared_vf, callers, callees, cls_sub, (members, group_report)


def map_rows(F, lib, assign, weak):
    """The db/subsystems.jsonl rows: class-backed entries keep their subsystem; a call-graph guess (and a function nothing
    connects) is "other", the guess kept in "guess"; a guess of third-party stays "third-party" (an exclusion, not a
    campaign), "guess" marking it."""
    rows = []
    for rva in sorted(F, key=lambda r: int(r, 16)):
        if rva in lib:
            continue
        sub, basis = assign.get(rva, (OTHER, "unassigned"))
        row = {"rva": rva, "subsystem": sub, "basis": basis}
        if rva in weak:
            row = {"rva": rva, "subsystem": OTHER if sub != THIRD_PARTY else sub, "basis": basis, "guess": sub}
        rows.append(row)
    return rows


def main():
    args = sys.argv[1:]
    if any(a in ("-h", "--help") for a in args):
        print(__doc__)
        return
    F, lib, assign, weak, status, unmatched_classes, shared_vf, callers, callees, cls_sub, groups = build()
    rows = map_rows(F, lib, assign, weak)
    if "--no-write" not in args:
        with open(tsdb.ROOT / "db/subsystems.jsonl", "w", encoding="utf-8", newline="\n") as fh:
            for r in rows:
                fh.write(json.dumps(r, separators=(",", ":")) + "\n")
    summary(F, lib, rows, status, args)
    un = [r["rva"] for r in rows if r["basis"] == "unassigned"]
    nocall = [r for r in un if not callers.get(r)]
    print(f"unassigned (other, no guess): {len(un)} functions, {sum(F[r]['size'] for r in un)} bytes; "
          f"{len(nocall)} ({sum(F[r]['size'] for r in nocall)} bytes) have no direct caller (reached only indirectly)")
    for i, a in enumerate(args):
        if a == "--why":
            why(args[i + 1], F, assign, callers, callees)
    if "--classes-unmatched" in args:
        for c in unmatched_classes:
            print("UNMATCHED", c["name"])
    if "--check-groups" in args:
        unresolved, notes = group_check(*groups, {r["rva"]: r for r in rows}, {r: f["size"] for r, f in F.items()},
                                       load_group_list(), load_forced_groups())
        print_group_check(unresolved, notes)
        if unresolved or notes:
            sys.exit(1)


def group_check(members, report, rows, sizes, listed=None, forced=frozenset()):
    """The merge check (`--check-groups`).  unresolved: [(group, [rva in other], placed Counter, guesses Counter)] for
    the status groups with members still filed under "other" that neither the list nor the majority rule places
    (guesses: the call-graph guesses of those members, by bytes, to help pick a list entry).  notes: [text] for list
    entries that name no group with members, or that the majority rule now covers (redundant, or disagreeing)."""
    listed = {g: rep["listed"] for g, rep in report.items() if rep["listed"]} if listed is None else listed
    unresolved, notes = [], []
    for g, rep in sorted(report.items()):
        if rep["rule"]:
            continue
        left = [r for r in members.get(g, ()) if rows.get(r, {}).get("subsystem") == OTHER]
        if left:
            guesses = collections.Counter()
            for r in left:
                guesses[rows[r].get("guess") or "-"] += sizes.get(r, 0)
            unresolved.append((g, left, rep["placed"], guesses))
    for g, sub in sorted(listed.items()):
        rep = report.get(g)
        if rep is None:
            notes.append(f"{GROUP_LIST}: {g} has no status members (stale entry)")
        elif g in forced:
            continue  # the list decides, majority or not
        elif rep["majority"] == sub:
            notes.append(f"{GROUP_LIST}: {g} -> {sub} is now the members' majority (entry redundant)")
        elif rep["majority"] and sub == MIXED:
            notes.append(f"{GROUP_LIST}: {g} is listed mixed but its placed members' majority {rep['majority']} applies"
                         " (entry redundant)")
        elif rep["majority"]:
            notes.append(f"{GROUP_LIST}: {g} -> {sub} but its placed members' majority is {rep['majority']}")
        elif not rep["unplaced"]:
            notes.append(f"{GROUP_LIST}: {g} has no unplaced members (entry redundant)")
    return unresolved, notes


def print_group_check(unresolved, notes):
    print(f"status groups with members in other and no group rule: {len(unresolved)}"
          f" ({sum(len(u[1]) for u in unresolved)} functions)")
    for g, left, placed, guesses in sorted(unresolved, key=lambda u: -sum(u[3].values())):
        pl = ", ".join(f"{s} {b}" for s, b in placed.most_common(3)) or "none"
        gs = ", ".join(f"{s} {b}" for s, b in guesses.most_common(3))
        print(f"  {g}: {len(left)} in other, {sum(guesses.values())} B (guesses: {gs}); placed members: {pl}")
    for n in notes:
        print("  " + n)


def summary(F, lib, rows, status, args):
    by = collections.defaultdict(list)
    guessed = collections.defaultdict(list)
    for r in rows:
        by[r["subsystem"]].append(r["rva"])
        if r.get("guess"):
            guessed[r["guess"]].append(r["rva"])
    tp_guessed = [r["rva"] for r in rows if r["subsystem"] == THIRD_PARTY and r.get("guess")]
    inferred_rows = {r["rva"] for r in rows if is_inferred(r)}
    total_b = sum(F[r["rva"]]["size"] for r in rows)
    libb = sum(F[r]["size"] for r in lib if r in F)
    print(f"non-library functions: {len(rows)}, {total_b} bytes ({libb} bytes in {len(lib)} library functions excluded)")
    print(f"{'subsystem':16s} {'funcs':>6s} {'bytes':>9s} {'%':>5s} {'verified B':>10s} {'evidence B':>10s}"
          f" {'inferred B':>10s} {'+guessed B':>10s}")
    order = [s for s in SUBSYSTEMS if s in by or s in guessed] + [s for s in by if s not in SUBSYSTEMS]
    for s in order:
        rv = by.get(s, [])
        b = sum(F[r]["size"] for r in rv)
        vb = sum(F[r]["size"] for r in rv if status.get(r, {}).get("status") == "verified")
        gb = sum(F[r]["size"] for r in guessed.get(s, []))
        ib = sum(F[r]["size"] for r in rv if r in inferred_rows)
        print(f"{s:16s} {len(rv):6d} {b:9d} {100*b/total_b:5.1f} {vb:10d} {b-ib:10d} {ib:10d} {gb:10d}")
    print("  (+guessed B: bytes filed under other whose guess is this subsystem; for third-party, the guessed bytes"
          " already counted in its row, which stay third-party)")
    print(f"third-party by call-graph guess (kept third-party): {len(tp_guessed)} functions,"
          f" {sum(F[r]['size'] for r in tp_guessed)} bytes, verified"
          f" {sum(F[r]['size'] for r in tp_guessed if status.get(r, {}).get('status') == 'verified')}")
    print(f"status names disagreeing with an existing evidence basis (existing kept): {len(status_conflicts)}")
    for c in status_conflicts[:int(next((args[i + 1] for i, a in enumerate(args) if a == "--conflicts"), 5))]:
        print(f"  {c[0]} {F[c[0]]['size']} B: {c[1]} ({c[2]}) vs status {c[3]} ({c[4]})")
    bas = collections.Counter()
    basb = collections.Counter()
    for r in rows:
        k = r["basis"].split(":")[0]
        bas[k] += 1
        basb[k] += F[r["rva"]]["size"]
    print("by basis (funcs, bytes):", {k: (bas[k], basb[k]) for k in bas})
    for s in order:
        big = sorted(by.get(s, []), key=lambda r: -F[r]["size"])[:5]
        if big:
            print(f"  {s}: " + ", ".join(f"{r}({F[r]['size']})" for r in big))
    big6 = ["0x522ac", "0xd5663", "0x598ac0", "0x5a12c0", "0x122d40", "0x11f780"]
    amap = {r["rva"]: r for r in rows}
    print("large functions:")
    for r in big6:
        a = amap.get(r)
        print(f"  {r} size={F[r]['size'] if r in F else '?'} -> {a['subsystem'] if a else 'n/a'}"
              f" ({a['basis'] if a else ''}{', guess ' + a['guess'] if a and a.get('guess') else ''})")
    for i, a in enumerate(args):
        if a == "--leftovers":
            n = int(args[i + 1])
            un = sorted([r for r in rows if r["basis"] == "unassigned"], key=lambda r: -F[r["rva"]]["size"])[:n]
            for r in un:
                print("  unassigned", r["rva"], F[r["rva"]]["size"], len(callers_of(r["rva"], F)))


def why(rva, F, assign, callers, callees):
    print(rva, F[rva]["size"], assign.get(rva))
    print(" callers:", [(c, assign.get(c, ("-",))[0]) for c in sorted(callers.get(rva, ()))][:30])
    print(" callees:", [(c, assign.get(c, ("-",))[0]) for c in sorted(callees[rva])][:40])


def callers_of(r, F):
    return [x for x, f in F.items() if r in f.get("callees", [])]


if __name__ == "__main__":
    main()
