cmake_minimum_required(VERSION 3.24)
project(TS2Reimpl C CXX ASM_MASM)

# RelWithDebInfo is Release's code plus debug info (tools/pdbfacts.ps1, line coverage), not CMake's default /Ob1: without
# Release's inlining an OriginalFunction<> passed as an argument is an absolute address of a rel32-only symbol, which
# checkgamebase rejects (ParticleCommands.cpp's helpers). /Z7 keeps the debug info in the objects, so compiles stay
# independent; /OPT:NOICF keeps folded bodies apart, which line data needs.
foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO "${CMAKE_${lang}_FLAGS_RELEASE} /Z7")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG:FULL /INCREMENTAL:NO /OPT:REF /OPT:NOICF")

# The game is 32-bit; tools/common.ps1 (Invoke-Build) configures it: Ninja Multi-Config on MSVC 14.40's x86 tools. The
# Visual Studio generator (-G "Visual Studio 17 2022" -A Win32 -T v143,version=14.40) gives the same objects.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure for Win32 (tools/common.ps1, Invoke-Build): the game is a 32-bit executable.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# /MP: compile each target's sources in parallel (the Visual Studio generator otherwise runs one cl.exe per project).
add_compile_options(/W4 /permissive- /utf-8 /MP)
if(CMAKE_GENERATOR MATCHES "^Ninja")
    # What MSBuild adds to every cl line by itself (Visual Studio generator) and bare cl.exe doesn't do; the rest of its
    # line (/Zc:wchar_t, /Zc:forScope, /fp:precise, /Gd...) is cl's default. Without them the objects differ.
    add_compile_definitions($<$<COMPILE_LANGUAGE:C,CXX>:_MBCS>)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/Zc:inline> $<$<COMPILE_LANGUAGE:C,CXX>:/Oy->)
    # Objects named as MSBuild names them (<stem>.obj): the linker map and the library's members carry these names.
    set(CMAKE_C_OUTPUT_EXTENSION_REPLACE 1)
    set(CMAKE_CXX_OUTPUT_EXTENSION_REPLACE 1)
    set(CMAKE_ASM_MASM_OUTPUT_EXTENSION_REPLACE 1)
    set(TS2_OBJECT_ROOT ${CMAKE_BINARY_DIR}/CMakeFiles)
    set(TS2_GAMEBASE_MEMBERS "/REMOVE:CMakeFiles\\ts2core.dir\\$<CONFIG>\\tools\\gamebase.obj"
                             "/REMOVE:CMakeFiles\\ts2core.dir\\$<CONFIG>\\generated\\origsyms.obj")
else()
    set(TS2_OBJECT_ROOT ${CMAKE_BINARY_DIR})
    set(TS2_GAMEBASE_MEMBERS "/REMOVE:ts2core.dir\\$<CONFIG>\\gamebase.obj" "/REMOVE:ts2core.dir\\$<CONFIG>\\origsyms.obj")
endif()

# Reimplemented game code, shared by the hook DLL and the tests. Sources are globbed so parallel
# pieces of work (each in its own files) never edit this list.
file(GLOB_RECURSE TS2CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER TS2CORE_SOURCES EXCLUDE REGEX "/src/hook/")
# tools/gamebase.asm defines the absolute symbol ts2_rva_0 (the game image base) that GameAddress compiles to;
# the generated origsyms.asm (below) defines one absolute symbol per OriginalFunction<Fn, 0xRVA>() callee.
set(TS2_GENERATED ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${TS2_GENERATED}) # ts2core_noimage's imported include path must exist at generate time (fresh build dirs)
set_source_files_properties(${TS2_GENERATED}/origsyms.asm PROPERTIES GENERATED TRUE)
add_library(ts2core STATIC ${TS2CORE_SOURCES} tools/gamebase.asm ${TS2_GENERATED}/origsyms.asm)
target_include_directories(ts2core PUBLIC src)
# No frame pointers, like the original (byte matching, tools/matchcheck.py). Our calls into original code
# must then pop exactly what the callee pops: tools/stackcheck.py checks that statically, TS2_ESPCHECK at
# run time.
# TS2_ESPCHECK (diagnostic build, in its own build folder): our code at /Od with the compiler's esp checks
# (/RTCs) after every call through a pointer and at every function's end; each failure is logged with its
# call stack (src/game/espcheck.h) and the program continues. The linker map (ts2host.map, difftest.map)
# names the functions. E.g. cmake -S . -B build-espcheck -A Win32 -T v143,version=14.40 -DTS2_ESPCHECK=ON
option(TS2_ESPCHECK "our code at /Od with run-time esp checks (diagnostic)" OFF)
if(TS2_ESPCHECK)
    target_compile_options(ts2core PRIVATE $<$<CONFIG:Release>:/Od /Ob0 /RTCs>)
    target_compile_definitions(ts2core PUBLIC TS2_ESPCHECK)
else()
    # /GS-: the original's objects mostly lack stack cookies (VC_FEATURE: 448 of 2058 with /GS; 11 of ~8000 EH
    # functions have one), and a cookie changes every frame that has one (matchcheck v5 can't match EH frames with it).
    target_compile_options(ts2core PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:/Oy;/GS->)
    # EA built some modules optimised for size (their functions are packed, not 16-byte aligned): our files for
    # those are named <name>.size.cpp and get the same flags (tools/flagprofile.py; matchcheck applies them too).
    file(GLOB_RECURSE TS2CORE_SIZE_SOURCES CONFIGURE_DEPENDS src/*.size.cpp)
    set_source_files_properties(${TS2CORE_SIZE_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release,RelWithDebInfo>:/O1;/Oi>")
endif()

# Named interfaces for the game's vtables (`#include "ifaces/<Name>.h"`), generated from the method
# facts in db/methods/*.jsonl by tools/ifacegen.py (not committed, so parallel lanes never conflict).
# The venv is the main checkout's, also when building in a worktree.
set(TS2_PYTHON "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
if(NOT EXISTS "${TS2_PYTHON}")
  execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE TS2_GIT_COMMON OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(TS2_MAIN_CHECKOUT "${TS2_GIT_COMMON}" DIRECTORY)
  set(TS2_PYTHON "${TS2_MAIN_CHECKOUT}/.venv/Scripts/python.exe")
endif()
file(GLOB METHOD_FACTS CONFIGURE_DEPENDS db/methods/*.jsonl)
add_custom_command(OUTPUT ${TS2_GENERATED}/ifaces/ifaces.stamp
                   COMMAND ${TS2_PYTHON} -I ${CMAKE_SOURCE_DIR}/tools/ifacegen.py --db ${CMAKE_SOURCE_DIR}/db
                           --out ${TS2_GENERATED}/ifaces
                   DEPENDS ${METHOD_FACTS} ${CMAKE_SOURCE_DIR}/tools/ifacegen.py ${CMAKE_SOURCE_DIR}/db/classes.jsonl
                   COMMENT "Generating vtable interfaces (tools/ifacegen.py)")
add_custom_target(ts2ifaces DEPENDS ${TS2_GENERATED}/ifaces/ifaces.stamp)
add_dependencies(ts2core ts2ifaces)
# One absolute symbol per original function our code calls (src/game/original.h): tools/origsyms.py scans the
# sources for OriginalFunction uses on every build (an always-run target; it rewrites the file only when the
# set of callees changes), so each call is a direct `call rel32` to game base + RVA. Not committed.
# The executables' preferred base: a rel32 reference to an absolute symbol lands at its value + the image base,
# so the symbols are valued (game base + RVA) - TS2_EXE_BASE (tools/origsyms.py).
set(TS2_EXE_BASE 0x400000)
add_custom_target(ts2origsyms
                  COMMAND ${TS2_PYTHON} -I ${CMAKE_SOURCE_DIR}/tools/origsyms.py --root ${CMAKE_SOURCE_DIR}
                          --out ${TS2_GENERATED}/origsyms.asm --exe-base ${TS2_EXE_BASE}
                  BYPRODUCTS ${TS2_GENERATED}/origsyms.asm
                  COMMENT "Generating original-function symbols (tools/origsyms.py)")
add_dependencies(ts2core ts2origsyms)
target_include_directories(ts2core PUBLIC ${TS2_GENERATED})

# The hook tables and installer, linked into the host. Hook tables (src/hook/tables/*.cpp) register
# themselves, so they're linked as objects (an object library), not a static library.
# The tables and the registry are a separate object library: the test runner links them too, to
# replay captured calls on the replacements (docs/record-replay.md).
file(GLOB HOOK_TABLE_SOURCES CONFIGURE_DEPENDS src/hook/tables/*.cpp)
add_library(ts2hooktables OBJECT ${HOOK_TABLE_SOURCES} src/hook/registry.cpp)
target_link_libraries(ts2hooktables PUBLIC ts2core)
if(TS2_ESPCHECK)
    target_compile_options(ts2hooktables PRIVATE $<$<CONFIG:Release>:/Od /Ob0 /RTCs>) # header-defined replacements
else()
    # Header-defined replacements (templates such as VtordispThunks, tsglobals::AssignSlot) are instantiated here, so
    # the tables build with ts2core's flags: they are shipped replacement code like the rest.
    target_compile_options(ts2hooktables PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:/Oy;/GS->)
endif()
file(GLOB HOOK_SOURCES CONFIGURE_DEPENDS src/hook/*.cpp)
list(FILTER HOOK_SOURCES EXCLUDE REGEX "/registry\.cpp$")
add_library(ts2hooks OBJECT ${HOOK_SOURCES})
target_link_libraries(ts2hooks PUBLIC ts2core)

# Zydis (MIT), the x86 decoder the record-and-replay tracer uses (docs/record-replay.md): the
# pinned amalgamated release, fetched at configure time (not vendored; it's 12 MB of tables).
include(FetchContent)
FetchContent_Declare(zydis
    URL https://github.com/zyantific/zydis/releases/download/v4.1.0/zydis-amalgamated.zip
    URL_HASH SHA256=6ce26e5bf812aa9384af4a514fe81afa53f3fd00d579ba9d80a710e2218f48ff
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(zydis)
add_library(zydis STATIC ${zydis_SOURCE_DIR}/Zydis.c)
target_include_directories(zydis PUBLIC ${zydis_SOURCE_DIR})
target_compile_definitions(zydis PUBLIC ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
target_compile_options(zydis PRIVATE /W0)

# FreeType (FreeType License, FTL: credit in src/font/FontFusion.h), the font engine behind the Font Fusion shim (src/font/; docs/subsystems/rendering.md):
# the pinned release, fetched at configure time (not vendored), built static with only the modules the game's fonts and the
# Windows fonts folder need (TrueType, Type 1, CFF; the smooth and mono renderers; the hinters). The module list and the
# options are ours (src/font/freetype/), so FreeType's own CMakeLists is not used (SOURCE_SUBDIR names no CMakeLists).
FetchContent_Declare(freetype
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz
    URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(freetype)
add_library(ts2freetype STATIC
    ${freetype_SOURCE_DIR}/src/base/ftsystem.c ${freetype_SOURCE_DIR}/src/base/ftinit.c
    ${freetype_SOURCE_DIR}/src/base/ftdebug.c ${freetype_SOURCE_DIR}/src/base/ftbase.c
    ${freetype_SOURCE_DIR}/src/base/ftbitmap.c ${freetype_SOURCE_DIR}/src/base/fttype1.c
    ${freetype_SOURCE_DIR}/src/sfnt/sfnt.c ${freetype_SOURCE_DIR}/src/truetype/truetype.c
    ${freetype_SOURCE_DIR}/src/type1/type1.c ${freetype_SOURCE_DIR}/src/cff/cff.c
    ${freetype_SOURCE_DIR}/src/psaux/psaux.c ${freetype_SOURCE_DIR}/src/pshinter/pshinter.c
    ${freetype_SOURCE_DIR}/src/psnames/psnames.c ${freetype_SOURCE_DIR}/src/smooth/smooth.c
    ${freetype_SOURCE_DIR}/src/raster/raster.c ${freetype_SOURCE_DIR}/src/autofit/autofit.c)
target_include_directories(ts2freetype PUBLIC src/font/freetype ${freetype_SOURCE_DIR}/include)
target_compile_definitions(ts2freetype PUBLIC "FT_CONFIG_MODULES_H=<ts2_ftmodule.h>" "FT_CONFIG_OPTIONS_H=<ts2_ftoption.h>")
target_compile_definitions(ts2freetype PRIVATE FT2_BUILD_LIBRARY _CRT_SECURE_NO_WARNINGS)
target_compile_options(ts2freetype PRIVATE /W0)
target_link_libraries(ts2core PUBLIC ts2freetype)

# Record and replay: the instruction tracer (record/), used by the harness and later the host.
add_library(ts2record STATIC record/tracer.cpp record/replay.cpp)
target_include_directories(ts2record PUBLIC record)
target_link_libraries(ts2record PUBLIC zydis)

# Host mechanisms the difftest runner unit-tests too (the frame clock, the device call log; docs/ui-testing.md).
file(GLOB TS2HOST_SHARED_SOURCES CONFIGURE_DEPENDS host/shared/*.cpp)

# Object libraries go on the link line after the target's own objects, as MSBuild orders them. Ninja would put them first
# as sources: link order picks which COMDAT copy survives (.size objects are /O1) and the static initialisers' order.
function(ts2_link_objects target)
    foreach(lib ${ARGN})
        if(CMAKE_GENERATOR MATCHES "^Ninja")
            target_link_libraries(${target} PRIVATE $<TARGET_OBJECTS:${lib}>)
        else()
            target_sources(${target} PRIVATE $<TARGET_OBJECTS:${lib}>)
        endif()
    endforeach()
endfunction()

# Differential test runner: maps the original exe and compares it against ts2core.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(difftest harness/image.cpp harness/main.cpp harness/sandbox.cpp ${TEST_SOURCES} ${TS2HOST_SHARED_SOURCES})
ts2_link_objects(difftest ts2hooktables)
target_include_directories(difftest PRIVATE harness src)
target_link_libraries(difftest PRIVATE ts2core ts2record ole32 uuid)
# The test runner's precompiled header (cold builds: lane SO, 2026-11-01): tests/difftest_pch.h, the STL and C runtime
# headers only (why: its comment). It is force-included first, so a source that #defines a configuration macro those
# headers read is built without it and no file's macro environment changes. Shipped code (ts2core, ts2hooktables) has
# no PCH: one holding <string> changes MSVC's inlining of std::string members in the files using them (lane SO's object
# comparison), and matchcheck recompiles those targets' files from their logged command lines, which it can't do with
# PCH flags (another cl host, and the quoted /Fp path).
set(TS2_PCH_CONFIG_MACROS
    "^[ \t]*#[ \t]*define[ \t]+(_CRT_[A-Z_]+|_USE_MATH_DEFINES|WIN32_LEAN_AND_MEAN|_HAS_[A-Z_]+|_SILENCE_[A-Z_]+|__STDC_WANT_[A-Z_]+)")
target_precompile_headers(difftest PRIVATE ${CMAKE_SOURCE_DIR}/tests/difftest_pch.h)
foreach(f ${TEST_SOURCES} ${TS2HOST_SHARED_SOURCES} harness/image.cpp harness/main.cpp harness/sandbox.cpp)
    file(STRINGS ${f} hit REGEX "${TS2_PCH_CONFIG_MACROS}" LIMIT_COUNT 1)
    if(hit)
        set_source_files_properties(${f} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
endforeach()
# The original code is mapped as private executable memory; SafeSEH/CFG don't apply to it.
# Exception handlers inside the mapped original live in private (non-image) memory; Windows only
# dispatches to such handlers with DEP off, so the runner opts out of NX (main.cpp also turns DEP
# off at runtime). This lets the original's try/catch frames work as they do in-game.
target_link_options(difftest PRIVATE /SAFESEH:NO /NXCOMPAT:NO /LARGEADDRESSAWARE /BASE:${TS2_EXE_BASE} /DYNAMICBASE:NO /FIXED) # as the game (and ts2host): addresses above 2 GB

# Host (docs/roadmap.md, stage 2): runs the game image in our own exe instead of the original.
# ts2tls.dll gives the hand-mapped image implicit TLS on every thread (host/tls_slot.c).
add_library(ts2tls SHARED host/tls_slot.c)
if(CMAKE_GENERATOR MATCHES "^Ninja")
    target_compile_definitions(ts2tls PRIVATE _WINDLL) # MSBuild's, for a DLL project
    target_link_options(ts2tls PRIVATE /SUBSYSTEM:CONSOLE) # also its default
endif()
file(GLOB TS2HOST_SOURCES CONFIGURE_DEPENDS host/*.cpp)
add_executable(ts2host WIN32 ${TS2HOST_SOURCES} ${TS2HOST_SHARED_SOURCES} harness/image.cpp harness/sandbox.cpp)
ts2_link_objects(ts2host ts2hooks ts2hooktables)
target_include_directories(ts2host PRIVATE harness src)
target_link_libraries(ts2host PRIVATE ts2tls ts2core ole32)
# Same stack as the original (1 MB) and large-address-aware like it; DEP off for the reason above.
target_link_options(ts2host PRIVATE /SAFESEH:NO /NXCOMPAT:NO /LARGEADDRESSAWARE /STACK:0x100000,0x1000 /BASE:${TS2_EXE_BASE} /DYNAMICBASE:NO /FIXED)
# The map names our functions in crash stacks and espcheck reports (tools/stackcheck.py --symbolize).
target_link_options(ts2host PRIVATE /MAP)
target_link_options(difftest PRIVATE /MAP)

# Calls to original code are `call rel32` to absolute addresses (src/game/original.h), so both executables are linked
# fixed (/FIXED /DYNAMICBASE:NO: no relocation, they load at their preferred base or not at all).
# The linked game-base symbols must resolve to kGameBase + rva (the linker map is the evidence), the executable must
# be fixed, and no object may take the address of an original function (only rel32 calls are right, see
# tools/checkgamebase.py); the build fails otherwise.
foreach(t ts2host difftest)
    add_custom_command(TARGET ${t} POST_BUILD
        COMMAND ${TS2_PYTHON} -I ${CMAKE_SOURCE_DIR}/tools/checkgamebase.py $<TARGET_FILE_DIR:${t}>/${t}.map $<TARGET_FILE:${t}>
                ${TS2_OBJECT_ROOT}/ts2core.dir/$<CONFIG> ${TS2_OBJECT_ROOT}/ts2hooktables.dir/$<CONFIG>
                $<$<STREQUAL:${t},ts2host>:${TS2_OBJECT_ROOT}/ts2hooks.dir/$<CONFIG>> ${TS2_OBJECT_ROOT}/${t}.dir/$<CONFIG>
        COMMENT "Checking the linked game base (tools/checkgamebase.py)")
endforeach()

# Standalone pixel format check (docs/roadmap.md, milestone A): the nGZGraphic4 pixel formats with no game image.
# Built from standalone/ plus only the pixel format sources: not ts2core, no harness, and not src/game/game.cpp,
# so GameAddress/GameBase are unresolved symbols and any code that still needed the image fails to link.
# The default reference file is the committed fixture generated from the original by tests/pixel_format_report.cpp.
add_executable(pixelformat_check standalone/pixelformat_check.cpp src/gz/PixelFormat.cpp src/gz/PixelFormatsManager.cpp
    src/gz/PixelFormatVTables.cpp src/gz/PixelFormatStore.cpp)
target_include_directories(pixelformat_check PRIVATE src standalone)
target_compile_definitions(pixelformat_check PRIVATE PIXELFORMAT_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pixelformat_report.txt")

# Image-free library (milestone N on): a copy of ts2core.lib without its two members gamebase.obj and origsyms.obj
# (lib /REMOVE; /WX makes a missing member an error), so nothing is compiled twice and ts2core itself is unchanged. A
# standalone exe links it like any static library: only the objects it reaches are pulled in, and every GameAddress
# (_ts2_rva_0) or OriginalFunction (OriginalEntry<RVA>) left on that path is an unresolved external, so the link fails
# until the image is no longer needed. (A filtered $<TARGET_OBJECTS:ts2core> keeps the .asm objects under the VS
# generator.) Not built by default.
set(TS2_NOIMAGE_LIB ${CMAKE_BINARY_DIR}/$<CONFIG>/ts2core_noimage.lib)
add_custom_command(OUTPUT ${TS2_NOIMAGE_LIB}
                   COMMAND ${CMAKE_AR} /nologo /WX /OUT:${TS2_NOIMAGE_LIB} ${TS2_GAMEBASE_MEMBERS} $<TARGET_FILE:ts2core>
                   DEPENDS ts2core $<TARGET_FILE:ts2core>
                   WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                   COMMENT "ts2core without the game-base providers (ts2core_noimage.lib)" VERBATIM)
add_custom_target(ts2core_noimage_lib DEPENDS ${TS2_NOIMAGE_LIB})
add_library(ts2core_noimage STATIC IMPORTED)
foreach(config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${config} CONFIG_UPPER)
    set_target_properties(ts2core_noimage PROPERTIES IMPORTED_LOCATION_${CONFIG_UPPER}
                          ${CMAKE_BINARY_DIR}/${config}/ts2core_noimage.lib)
endforeach()
set_target_properties(ts2core_noimage PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src;${TS2_GENERATED}"
                      INTERFACE_LINK_LIBRARIES ts2freetype)

# Milestone N (docs/roadmap.md): a GMDC resource decoded from a .package with no game image (standalone/gmdc_decode.cpp).
add_executable(gmdc_decode EXCLUDE_FROM_ALL standalone/gmdc_decode.cpp standalone/owned_statics.cpp
    standalone/noimage_trap.cpp standalone/crash_report.cpp)
target_include_directories(gmdc_decode PRIVATE src standalone ${TS2_GENERATED})
target_compile_options(gmdc_decode PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:/Oy;/GS->)
# The trap in a COMDAT of its own (/Gy, also in Debug), so /OPT:REF drops it when nothing live refers to it.
set_source_files_properties(standalone/noimage_trap.cpp PROPERTIES COMPILE_OPTIONS /Gy)
target_link_libraries(gmdc_decode PRIVATE ts2core_noimage)
add_dependencies(gmdc_decode ts2ifaces ts2core_noimage_lib)
# The normal link (N-e; docs/subsystems/scenegraph.md, "Milestone N", the CMake decision). MSVC resolves every symbol a
# pulled-in object refers to, also in sections /OPT:REF later drops as dead, so an image symbol only dead code names
# still fails the link. Before the link, tools/noimage.py --write-aliases lists every image symbol (GameAddress's
# _ts2_rva_0, OriginalEntry<RVA>, an owned static _ts2_rva_<hex>) the image-free library and the program's objects leave
# undefined, as /alternatename directives that send it to ts2_noimage_trap (standalone/noimage_trap.cpp), written as
# the .drectve of an object the link takes as an input (MSBuild's link response file cannot hold a nested @file; the
# object sits outside the directories the tool scans). After the link the gate (noimage.py --check) reads the map: the exe is kept only when the trap was
# dropped (no live code reaches an image symbol) and no image reference is left, else the build fails and the exe is
# deleted, so no exe that could reach the trap exists.
set(TS2_NOIMAGE_ALIASES ${CMAKE_BINARY_DIR}/$<CONFIG>/gmdc_decode_noimage_aliases.obj)
add_custom_command(TARGET gmdc_decode PRE_LINK
                   COMMAND ${TS2_PYTHON} -I ${CMAKE_SOURCE_DIR}/tools/noimage.py --write-aliases ${TS2_NOIMAGE_ALIASES}
                           ${TS2_NOIMAGE_LIB} ${TS2_OBJECT_ROOT}/gmdc_decode.dir/$<CONFIG>
                   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
# The map names what the program links; tools/noimage.py turns map + objects into the list of image references left.
# TS2_NOIMAGE_FORCE (diagnostic; turn it off again after measuring, never run the forced exe) links with /FORCE:UNRESOLVED so the map exists while the link
# would still fail; that exe must not be run.
option(TS2_NOIMAGE_FORCE "link gmdc_decode with /FORCE:UNRESOLVED for tools/noimage.py (diagnostic)" OFF)
target_link_options(gmdc_decode PRIVATE /MAP /OPT:REF /INCREMENTAL:NO ${TS2_NOIMAGE_ALIASES}
                    $<$<BOOL:${TS2_NOIMAGE_FORCE}>:/FORCE:UNRESOLVED>)
if(NOT TS2_NOIMAGE_FORCE)
    add_custom_command(TARGET gmdc_decode POST_BUILD
                       COMMAND ${TS2_PYTHON} -I ${CMAKE_SOURCE_DIR}/tools/noimage.py
                               $<TARGET_FILE_DIR:gmdc_decode>/gmdc_decode.map ${TS2_OBJECT_ROOT}/ts2core.dir/$<CONFIG>
                               ${TS2_OBJECT_ROOT}/gmdc_decode.dir/$<CONFIG> --check --no-save
                               --delete $<TARGET_FILE:gmdc_decode>
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
endif()

# Milestone O (docs/roadmap.md): one static object rendered from the packages to a BMP with no game image
# (standalone/render_object.cpp; docs/subsystems/scenegraph.md, "Milestone O"). The same link and gate as gmdc_decode.
# It does not link yet (live code still reaches image symbols, so the gate fails and deletes the exe); build it with
# `cmake --build build --config Release --target render_object`, and measure what it still needs with
# -DTS2_NOIMAGE_FORCE=ON and tools/noimage.py on build/Release/render_object.map (never run the forced exe).
add_executable(render_object EXCLUDE_FROM_ALL standalone/render_object.cpp standalone/owned_statics.cpp
    standalone/owned_graphics.cpp standalone/noimage_trap.cpp standalone/crash_report.cpp)
target_include_directories(render_object PRIVATE src standalone ${TS2_GENERATED})
target_compile_options(render_object PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:/Oy;/GS->)
# D3DX9 and DxErr9 are library code the game links statically: the image-free program links the same SDK's libraries
# (DirectX 9.0 SDK Update, Summer 2004; tools/thirdparty_build.py --fetch-dxsdk puts them in local/thirdparty/), and
# standalone/owned_graphics.cpp sends the OriginalFunction entries of those functions to them.
set(TS2_DXSDK_DIR "${CMAKE_SOURCE_DIR}/local/thirdparty/dxsdk-sum2004")
if(NOT EXISTS "${TS2_DXSDK_DIR}/d3dx9.lib" AND DEFINED TS2_MAIN_CHECKOUT)
    set(TS2_DXSDK_DIR "${TS2_MAIN_CHECKOUT}/local/thirdparty/dxsdk-sum2004")
endif()
target_link_libraries(render_object PRIVATE ts2core_noimage d3d9 "${TS2_DXSDK_DIR}/d3dx9.lib" "${TS2_DXSDK_DIR}/DxErr9.lib"
                      legacy_stdio_definitions)
add_dependencies(render_object ts2ifaces ts2core_noimage_lib)
set(TS2_RENDER_OBJECT_ALIASES ${CMAKE_BINARY_DIR}/$<CONFIG>/render_object_noimage_aliases.obj)
add_custom_command(TARGET render_object PRE_LINK
                   COMMAND ${TS2_PYTHON} -I ${CMAKE_SOURCE_DIR}/tools/noimage.py --write-aliases ${TS2_RENDER_OBJECT_ALIASES}
                           ${TS2_NOIMAGE_LIB} ${TS2_OBJECT_ROOT}/render_object.dir/$<CONFIG>
                   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
# The 2004 library's objects carry no SAFESEH table and name VC 7.1's single-threaded libraries as defaults.
target_link_options(render_object PRIVATE /SAFESEH:NO /NODEFAULTLIB:libcp.lib /NODEFAULTLIB:libc.lib)
target_link_options(render_object PRIVATE /MAP /OPT:REF /INCREMENTAL:NO ${TS2_RENDER_OBJECT_ALIASES}
                    $<$<BOOL:${TS2_NOIMAGE_FORCE}>:/FORCE:UNRESOLVED>)
if(NOT TS2_NOIMAGE_FORCE)
    add_custom_command(TARGET render_object POST_BUILD
                       COMMAND ${TS2_PYTHON} -I ${CMAKE_SOURCE_DIR}/tools/noimage.py
                               $<TARGET_FILE_DIR:render_object>/render_object.map ${TS2_OBJECT_ROOT}/ts2core.dir/$<CONFIG>
                               ${TS2_OBJECT_ROOT}/render_object.dir/$<CONFIG> --check --no-save
                               --delete $<TARGET_FILE:render_object>
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
endif()

# Shared compile cache for new Ninja trees (docs/lane/brief.md; Invoke-Build passes both variables). A fresh worktree
# takes the objects another checkout already compiled, with everything that is in an object's content in the key
# (compiler, flags, every header, the source). ccache's hash_dir=false and base_dir leave the checkout's path out of
# the key, so what is compiled must not depend on that path, and two things do:
# - cl hashes the source's full path into anonymous-namespace names (?A0x12345678), which reach the image through RTTI.
#   /d1trimfile takes the checkout off that path, here and in an uncached build alike; ignore_options keeps the root
#   itself out of the key.
# - difftest: __FILE__ and source_location hold real paths, one of them a fixture's (tests/pixel_format_report.cpp),
#   and each object names its PCH by the PCH's path, so nothing there could be shared anyway. Left out.
set(TS2_COMPILE_CACHE "" CACHE FILEPATH "ccache.exe, for new Ninja trees (empty: none)")
set(TS2_COMPILE_CACHE_DIR "" CACHE PATH "where it keeps objects")
if(TS2_COMPILE_CACHE)
    set(launcher ${TS2_COMPILE_CACHE} cache_dir=${TS2_COMPILE_CACHE_DIR} base_dir=${CMAKE_SOURCE_DIR} hash_dir=false
                 max_size=3G ignore_options=/d1trimfile:*)
    get_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t ${targets})
        get_target_property(type ${t} TYPE)
        if(type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY)$" AND NOT t STREQUAL difftest)
            set_target_properties(${t} PROPERTIES CXX_COMPILER_LAUNCHER "${launcher}" C_COMPILER_LAUNCHER "${launcher}")
            target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/d1trimfile:${CMAKE_SOURCE_DIR}/>)
        endif()
    endforeach()
endif()
