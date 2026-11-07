# Command parser: the script and cheat command language (campaign plan)

A leaf campaign of docs/roadmap.md ("Campaign order", step 5): others call it little (7 KB of its not-done code is
called from outside, by `coverage.py deps run-1023 --by group`), so it runs in parallel with the main campaigns. Working
notes for its lanes: scope, the plan, conventions and test patterns, what is done and left. The lane that changes the
subsystem updates this file on its branch (facts, not decompiler output). Verification: `verified` by difftest or byte
match (parsing and configuration code, not simulation). Opened by lane NO (2026-10-26).

## What it does in the game

nGZCommandParser is a small scripting language: a line is a command name and arguments, with `if/elseif/else/endif`,
`define/enddef` macros, `alias`, variables (`set`, `seti`, `setf`, ...), namespaces, `include` and an expression
evaluator (arithmetic, comparisons and logic over up to four floats, the functions `sin`, `rnd`, `strcmp`, ...; the
grammar is in src/gz/ExpressionEvaluator.h). Each user of the
language is a parser object (derived from nGZCommandParser::cBaseParser, 0x1c0 bytes, virtual bases) that registers its
own command objects and runs files or text through it:

- the **material / shader scripts** (nGZSceneGraph::cMaterialParser: the `materialDefinition`, `shader`, `pass`, render
  state and texture stage commands) and the **effects scripts** (particle, decal, model, light, sequence, sound, brush
  effects), both parsed at start (round 0) and again when packages add content;
- the **build-mode catalogue scripts** (cTSProductParser and one parser per product kind: walls, fences, roofs, stairs,
  fireplaces, awnings, decks/pools/foundations, terrain paints and brushes, user tools), run at start;
- the **graphics option rules** (an anonymous-namespace parser 0x5a21ffbf: `optionBlock`, `option setting`, vendor /
  card / driver-build tests that pick the video options for the detected card), run at start;
- **camera definitions** (cDefCameraCommand and its children), **light rigs** and light attributes, **weather and
  season** predicates used by those scripts (`hasRain`, `seasonEnum`, ...), **property** commands (`boolProp`, ...);
- the **cheat console**: cheats are commands too (cGZLogParser's 11 log cheats, the anonymous 0x882e630d set: `help`,
  `gameVersion`, `perf*`, `metrics*`, `run`, ...), registered at start; the console window (cTSWinCheatConsole, ui-game)
  feeds lines to a parser.

## Scope (run-1023)

`tools/py -I tools/coverage.py queue local/coverage/run-1023.txt --subsystem command-parser --whole --top 0`: **66,651 B
executed and not done in 527 functions, 283 groups**; the subsystem is 261 KB in 1,421 functions (docs/subsystems.md),
9.7 KB verified before this campaign. By family (executed not-done bytes; class bytes = the classes' size in db/):

| family | KB exec | fns | class KB | where | notes |
|---|---:|---:|---:|---|---|
| material / shader commands | 22.7 | 121 | 24.4 | 0xac6300-0xadc630, 0x1f9ac0, 0x209e20, 0x36da50, 0x4daa20 | src/gz/MaterialCommands.* already has the parser's blocks and 66 functions |
| product catalogue commands | 16.6 | 212 | 16.1 | 0xc130c5-0xc24b73 (+ a few at 0x9d6630-0xa103ad, 0x2b6caf) | ~100 tiny commands (30 of them 123-byte `*ModelName` setters); call cTSProduct* setters (other-ts, 7.1 KB) |
| option / config commands | 6.9 | 35 | 3.6 | 0xcccf80-0xcd55b0, 0xb0a600 | ?A0x5a21ffbf; 1.4 KB of owner-inferred helpers |
| parser core leftovers | 5.0 | 18 | 9.2 | 0xafc950-0xb06920, 0x14a4c0, 0x29a2e0 | cDefineCommand 0xb005e0, CommandParser's callees 0xafd520 / 0xb06920, cEndDefCommand's 0xafc950 / 0xafd3c0, the parser's functions (cVarExists, cMin/MaxVersion, cMatch, cCommandExists, cDefinitionExists) |
| camera commands | 4.0 | 25 | 3.5 | 0xb42ef0-0xb45040 | cDefCameraCommand + 11 children |
| effects commands | 3.7 | 20 | 12.2 | 0xbd0570-0xbf94c0 | src/gz/EffectCommands.*, ParticleCommands.* exist |
| light rig / attributes / user light | 2.4 | 21 | 2.5 | 0x2c1310-0x2df3b0, 0x3a5460 | src/gz/LightingCommands.* exists |
| cheats | 1.8 | 41 | 10.3 | 0xcac210-0xcaf550 | most ran only their 6-byte GetName (registration); bodies run when typed |
| weather / season functions | 1.6 | 16 | 1.7 | 0x247420-0x253ec0 | ?A0x612d56eb |
| expression evaluator | 1.5 (+4.7) | 9 | 1.3 (+4.9) | 0xb07bb0-0xb0a8b5 | see below: most of it is filed under third-party/unassigned |
| property commands | 0.4 | 6 | 1.4 | 0xb3b560-0xb3bbb0 | |

Filing errors to know about:
- **The expression evaluator is mostly not counted here.** db/subsystems.jsonl files its recursive-descent body
  (0xb09c50, 0xb0a200 and others) as `third-party` ("called-only-from" propagation through the CRT math functions), so
  `queue` shows only 1.5 KB of it; the whole evaluator is ~9 KB (lane NO, below).
- Other campaigns' groups hold command-parser code (docs/subsystems.md, "Triage"): cOverlayCheat's option-spec parser
  0xaf90e0 (4.4 KB, filed shared-utility), cTextureFilterHintCommand (2.4 KB, unassigned), cTSEffectCheat,
  cCatalogTextIndicesCommand, cOutputVertexFormatCommand, cLODCommand (2.6 KB). cTSWinCheatConsole (0xcaaed0, 5.2 KB) is
  ui-game's (a window proc) and stays there.

## What is already ours (before this campaign)

The parser framework is done (docs/subsystems/startup.md, lanes HL, HP, HU, IH, IM): cBaseParser (every slot, the
vtordisp thunks; hook-shaped), cArguments (standalone), cFileSource, cFileParser, cGZLogParser (constructor, destructor,
the log cheats' registration), EvalFunctionTable (the evaluator's sorted name table, 0xb094b0), the built-in commands
of gz/CommandParser.h (`set*`, `if`, `alias`, `define` partly, `include`, ...) and the command bases' non-virtual
members. Material, effects, particle and lighting commands are partly done (db/status/MaterialCommands.jsonl,
EffectCommands, ParticleCommands).

## Layers (callers above callees)

- **Users**: the parser subclasses (cMaterialParser, the effects parser, cTSProductParser and kin, the option parser,
  the camera and light-rig parsers, cGZLogParser, the cheat system) create commands and run scripts.
- **Commands**: each a cCommandT / cMetaCommandT / cBlockCommandT instance (gz/CommandParser.h: 16 / 12 / 0x44 bytes)
  with Execute (slot 4), GetName (slot 5, `mov eax, string; ret`, 6 bytes), GetHelp (slot 6); a block command adds the
  child commands of its block in its slot 3. Execute reads its arguments from cIArguments (ours) and writes into its
  parser's state or calls out (catalogue products, the option store, the log service, the scene graph's material
  objects).
- **Framework**: cBaseParser (ours), cArguments (ours), the evaluator (lane NO), the command bases.

Calls out of the campaign (cut in tests by recording fakes): cTSProduct* setters (other-ts, 7.1 KB, 127 callees), the
scene graph's material / shader / texture objects (4.3 KB), rendering's device caps (1.1 KB), framework 1.0 KB.

## Test boundary

- **Commands**: the tests/command_parser.h world (cptest): FakeVTable recording fakes for the argument list, the parser
  face and the parser state; `Differential()` runs the original and ours on the same memory and compares the call log,
  memory and result. Families with a big state have their own world on top (tests/gz_material_commands.h,
  gz_particle_commands.h). New families follow that: one `<family>_commands.h` world per parser state, fakes for every
  out-of-campaign object the commands call (a FakeVTable per kind), and the state as random bytes with real containers
  where the commands use them.
- **Arguments**: build a real cArguments (ours, standalone) from text rather than faking slot 11 / 15, where the
  family's tests allow; otherwise the cptest argument fake.
- **Errors**: commands and the evaluator throw nGZCommandParser::cError (vtable 0xda5060, a std::string message);
  tests catch `cIGZExceptionBase` on both sides and compare the message text.
- **CRT math**: the evaluator's functions call the image's statically linked math (exp, sin, pow, ... at 0xc4xxxx /
  0x83xxxx), which test the image's SSE2 flag 0x110a38c (uninitialised in the harness, which picks the x87 path; the
  game sets it to 1). Tests set it to 1 for their run (tests/gz_expression_evaluator.cpp, `Sse2Math`); ours calls our
  CRT and the results then match bit for bit on the tested inputs.
- **Cheats**: the log cheats call nGZLog's service (ours) and its back ends: tests/gz_log_parser_fakes.h.

## Conventions

- Hook shape for commands is the framework's (gz/CommandParser.h, MaterialCommands.h): the game's code still creates
  most commands with the game's vtables, so every slot is an RVA hook. A family whose registration function is ours
  builds its commands with `new` + our class only once the bases are in the standalone shape (lane N3 below).
- The 6-byte GetName / GetHelp bodies (`mov eax, imm; ret`) are hookable (5 bytes is the minimum) but each is its own
  function: check `shared_vf_*` before hooking (most are unique; folded ones belong to the base).
- Name tables (render states, log levels) use EnumTable (gz/CommandParser.h): reuse it.

## Next lanes (lane NO's plan, 2026-10-26)

Leaves first. KB = executed not-done (class KB in brackets).

| lane | groups | KB | needs | test | risk |
|---|---|---:|---|---|---|
| N1 (lane NO) | the expression evaluator: Evaluate 0xb0a870, the recursive descent (0xb09940 ... 0xb0a660), its helpers, the 25 cEval* function classes (27 table entries) | 6.2 (9) | - | plain: expression text in, value or error out | low-medium: CRT math last-bit differences; float vector code |
| N2 (lane NV, done) | parser core leftovers: cDefineCommand (Execute 0xb005e0, 0xb038f0, 0xafd790, 0x14a4c0), CommandParser's callees 0xafd520 / 0xb06920 (the registration helpers), cEndDefCommand's 0xafc950 / 0xafd3c0 / 0x29a2e0, the parser's functions (cVarExists 0xafef10, cMin/MaxVersion, cMatch, cCommandExists, cDefinitionExists, cPropExists) | 5.0 (9.2) | N1 for the functions' tests | cptest | medium: the definition collector runs the meta-command protocol |
| N3 | the command bases in the standalone shape: cCommand, cCommandT, cMetaCommand(T), cBlockCommand(T), cExpressionFunction(T) as real classes with our vtables; the creation sites the registration functions already ours | 0 (structure) | N2 | cptest, slots test | medium: folded base slots shared with 680 classes (AddRef 0x145260): our own trivial bodies, never hooked |
| N4 | cheats: the 0x882e630d set (help, gameVersion, perf*, metrics*, run, assertResponse, argSpecTest, cPITextCvt), the log cheats' bodies (cLogFileCheat ... cLogWindowCheat), cCommandStub, cOverlayCheat's option-spec parser 0xaf90e0 | 1.8 (14.7) | - | cptest + nGZLog fakes | medium: perf / metrics cheats reach the profiler (check what they call first) |
| N5 | option / config commands (?A0x5a21ffbf) and the device-cap functions (cQueryBoolean / IntegerDeviceCap, cQueryAvailableVideoMemory) | 7.5 (4.5) | - | cptest + option store fake + device caps fake | medium: 1.4 KB of owner-inferred helpers to place first |
| N6 | camera commands, weather / season functions, property commands, light rig / attributes / user light | 8.4 (9.1) | - | cptest | low |
| N7a, N7b | material / shader commands: (a) render-state and texture-state commands 0xad1420-0xad8a00; (b) the big bodies: cColorScalarCommand, cRenderTargetCommand, cFFTextureMatrixCommand, cBindShaderProgramConstantsCommand, cTextureMatrixAnimationCommand, cTextureBlendCommand, cStageCommand, cMaterialAttributeCommand, cValidateRenderShaderContext, cPreCreate, cFFMaterialCoef | 11 + 12 | - | tests/gz_material_commands.h | low-medium: the shader program constants bind into rendering objects |
| N8a, N8b | product catalogue commands (by product: walls / fences / roofs / stairs; fireplaces / awnings / decks / terrain / user tools) | 8 + 8.6 | - | cptest + cTSProduct* fakes, or take the cTSProduct* setters along (mostly tiny) | low: repetitive; many byte-match candidates (a cheaper-model lane) |
| N9 | effects commands rest (cGroupEffectCommand, the cGroup*Command and *EffectCommand pairs per effect kind) | 3.7 (8) | - | tests/gz_particle_commands.h | low |

Handed on by rendering (lane XO, 2026-11-06; rendering.md, "Where the 45.1 KB still live goes"), not yet in a row above: cTSEffectCheat and
cTSEffectsManagerCheat, 6.4 KB (slots 0xbc7590 / 0xbc7e30, constructors 0xbc46d0 / 0xbc4fb0 with their maker 0xbc5aa0, the SciTE launcher
0xbc9c00 with 0x1b14c5 / 0x1b184b / 0x1b18c4), best taken with cOverlayCheat and N4's 0xaf90e0.

## Risks and open questions

1. The standalone shape for commands depends on N3; until then a family's commands keep the game's vtables (hooks at
   every slot) and cannot be built by our registration code with our classes.
2. The cheats' bodies mostly did not run in the coverage runs: in-game checks need the cheat console (`-Quiet` runs can
   type cheats? ask the coordinator).
3. Product catalogue commands call cTSProduct* (other-ts / objects): decide in N8 whether those setters come along.

## Done

**N1, the expression evaluator (lane NO, 2026-10-26): 41 functions, 8,099 bytes, `verified` and hooked** (db/status/ExpressionEvaluator.jsonl;
not yet run in game). src/gz/ExpressionEvaluator.h has the grammar, the levels and the built-ins.

- **What is ours**:
  - ExpressionEvaluator: a view of EvalFunctionTable. Evaluate 0xb0a870, the eight levels 0xb09940 ... 0xb0a660 and
    ReadQuotedName 0xb0a580 (filed as cBaseParser's before; `this` is unused).
  - The helpers ExpectChar 0xb096d0, Compare3 0xb09380, IsNearZero 0xb09660, IsTrue 0xb09680 and ScaleRange 0xb09400.
  - The 25 cEval* slot-0 bodies (0xb07bb0 ... 0xb08860).
  - rztext::FindNoCase 0x1b1a52, strstr's case-blind search (src/rz/RZStringSearch.*, /O1).
- **Hook shape**: the game's static cEval objects keep the game's vtables (EvalFunctionTable's constructor, ours,
  inserts them); each slot body is hooked to a non-virtual member with the hidden result pointer made explicit.
  - ParsePrimary calls the entry through `cIEvalFunction` (one slot, `Evaluate(out, cursor, evaluator)`), so a built-in
    that throws part-way leaves what it wrote in the caller's buffer, as the original does. Returning by value would
    leave the buffer untouched, and the tests catch that.
- **rnd**: its generator is ours (`EvalRandomGenerator()`, a function-local static seeded from the clock like the
  original's at 0x10faa48). Nothing else reads the original's object.
- **Tests** (tests/gz_expression_evaluator.cpp, 17 tests):
  - Each level is compared on 6,000 generated expressions plus a fixed corpus; Evaluate on 30,000. Every built-in is
    compared directly on about 920 argument texts, 300 of them full-precision four-element vectors for the CRT math.
  - Each side runs on its own table. The original's is the game's objects. Ours is made of stand-ins whose one-slot
    vtable points at our member, so no original code runs inside ours.
  - The image's SSE2 math flag is set for the run, as the game sets it.
- **Mutants**: 61 runs, at least one mutant per function. Four distinct mutants survived (one of them in two runs), and
  all four are equivalent:
  - ExpectChar's `%c` argument as unsigned char: printf converts it to unsigned char anyway.
  - Compare3's `>` as `>=`: the elements are unequal at that point.
  - exp and sin computed in float: on x86 the UCRT's expf / sinf are inline `(float)exp(double)`.
  - A replacement mutant for each was killed, including one-ulp changes to exp and sin.
- **ORIGINAL-BUG**: `strstr` searches the text from its second character (so `strstr("abc", "a")` is 0), and an empty
  text throws std::length_error.
  - The tests leave that one case out (`ReachesEmptyStrstr`): both sides throw it, but the capture reads cError's
    layout, and length_error is a different object.
- **Callers still on the original** (other groups' files; switch them to ours when next touched, then drop the
  `original_calls` entries):
  - src/gz/cParserSlots.cpp: the eval slot calls 0xb0a870 through OriginalFunction.
  - src/ts/cTSCommonSystemObjects.cpp: calls 0xb096d0 and 0xb0a580 through OriginalFunction.
- **Not covered**:
  - FindNoCase is tested on ASCII only (the CRT's tolower of a negative char is outside its domain on both sides).

**N2, parser core leftovers (lane NV, 2026-10-26): 35 functions, 7,287 bytes, `verified` and hooked**
(db/status/ParserFunctions.jsonl, db/status/MacroDefinition.jsonl; not yet run in game).

- **What is ours**:
  - src/gz/ParserFunctions.*: the slot-0 Evaluate bodies of minVersion, maxVersion, varExists, commandExists,
    definitionExists, match and propExists, their 14 GetName / GetHelp getters (byte matches), and ReadName 0xb0a600.
  - src/gz/MacroDefinition.*: cMacroDefinition (layout, constructor, Destruct 0xafe6c0, slot 0 0xaff0d0), the held
    definition (DefinitionRef) and the parser's sorted definition vector (Insert 0xafd3c0, EmplaceReallocate 0xafc950,
    CopyDefinitions 0x29a2e0), the searches (LowerBound 0xafd790, EqualRange 0xafd520, the comparisons 0xafea90 /
    0xafeb10), the 8-byte line vector's growth 0x14a4c0, define's Execute 0xb005e0 and HandleLine 0xb038f0, and the
    parameter substitution ExpandParameters 0xb06920.
  - CommandParser.cpp's undefine, enddef and create now call ours (their `original_calls` dropped).
- **Hook shape**: the function objects and definitions keep the game's vtables; only their own slots are hooked. The
  functions' shared base slots (QueryInterface, AddRef, Release, SetContext, GetFunction, the deleting destructors)
  are folded across the expression functions and stay for N3.
- **Tests**: tests/gz_parser_functions.cpp (8 tests), tests/gz_macro_definition.cpp (7) and
  tests/command_parser_define.cpp (2). tests/command_parser_create.cpp now stands in for 0xb06920 on both sides.
- **Mutants**: 47 (tools/mutate.py), at least one per tested function, all killed. One survived first (define parsing
  the parameters case-blind): the test now compares the parameter list's parse flags, and it is killed.
- **Not covered**: the line vector's length error past max_size (unreachable for 8-byte elements); the destroyed
  definition's dead bytes (the member destructors' leftovers).

**What is left** is the Next lanes table above, N3 to N9.

## Pitfalls

- cDefineCommand (and cIfCommand) are cMetaCommandT: the state is at +8, not +0xc as for cCommandT. The command
  fixture of tests/command_parser_env.h puts it at +0xc only; a meta command's test sets +8 itself.
- The word "enddef"'s length is a dword the game's static initialisers set (0x10749a0); the harness runs none, so a
  test of HandleLine sets it (6) for its duration.
- ExpandParameters calls the parameter list's Count once more after a match (the "index == Count" check); keep the
  call sequence when touching it, the test logs every call.
- Calls through a member cArguments (a definition's parameters, a pending call's arguments) go through a pointer
  cast from the address, so the compiler cannot bind them to our class: objects the image built carry its vtable.

## Left (from the backlog, 2026-11-03)

- cCreateCommand / cDumpParserStateCommand / cEndDefCommand Execute (1435/1656/717 B; HG).
- tests/command_parser_env.h puts cDefineCommand/cIfCommand state at +0xc but the classes keep it at +8 (define tests set +8 themselves): let the fixture take the offset (NV).
- Ghidra names of members with status entries look doubled (e.g. DefinitionVector::nGZCommandParser__DefinitionVector__Insert): check the tool that pushes status names to Ghidra (NV).
