"""Can Invoke-Build (tools/common.ps1) skip `cmake -S -B`? Its state lives in the checkout's own build/.

Reconfiguring costs about 6 s, and it only matters when its result would differ. The result depends on these, and
the state covers each of them:
  - which files the globs see: compared with the lists CMake itself recorded in build/CMakeFiles/VerifyGlobs.cmake, so
    this file names no glob of its own (the Visual Studio generator's ZERO_CHECK only catches a new file on the build
    after, which is why Invoke-Build has to decide);
  - CMakeLists.txt, by content;
  - what `file(STRINGS ...)` reads: a test or harness source that gains a line defining a macro the PCH headers read
    is built without the PCH, so configure also depends on the content of sources. Per file, the state keeps a digest
    of every line holding CMakeLists.txt's TS2_PCH_CONFIG_MACROS pattern, without its `^[ \\t]*` anchor (so looser than
    CMake's match: a line it can match is always in the digest), and of the whole file if it has a NUL byte (UTF-16,
    which CMake decodes and this doesn't). An edit to any other line of a source changes nothing, so a mutant or a
    changed test still skips configure;
  - what CMakeLists.txt names literally (harness/main.cpp, ...), by existence: configure fails on a missing source;
  - the python and D3DX `if(EXISTS)` checks, in this checkout and the main one;
  - everything CMake lists as a regeneration input (build/**/generate.stamp.depend, or a Ninja tree's RERUN_CMAKE
    statement: its modules, the detected compiler files, the FetchContent sub-builds), CMakeCache.txt, cmake.exe, the
    generated solution and projects (or Ninja files) and the fetched sources, by size and time.
Anything else configure could read, this file can't see, so `unsure()` refuses the CMakeLists.txt it doesn't
understand: a find_*(), an $ENV{}, a file() call other than these, a glob without CONFIGURE_DEPENDS. With such a
CMakeLists.txt every build configures, as it did before this file (`check --why` says why): extend the state, then
`unsure()`.

Any doubt means configure: a missing, unreadable or foreign state file, a failed read, an exception. A failed or
interrupted configure leaves no state, since `begin` deletes it first. A file edited while configure runs may or may
not be what CMake read, so `commit` writes no state when one is that new.

Usage (Invoke-Build's, nothing else needs it):
  python -I tools/configure_state.py check  --build B --main M --cmake C [--why]   exit 0: skip configure, 1: configure
  python -I tools/configure_state.py begin  --build B --main M --cmake C           before configuring
  python -I tools/configure_state.py commit --build B --main M --cmake C           after a successful configure
"""
import argparse
import hashlib
import json
import os
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = 1
STATE = "configure-state.json"
PENDING = "configure-state.pending"
# Two seconds of slack: a file's time and time.time_ns() are the same clock, but neither is exact.
RACE_MARGIN_NS = 2_000_000_000
FILE_COMMANDS = {"GLOB", "GLOB_RECURSE", "STRINGS", "MAKE_DIRECTORY"}
READERS = re.compile(r"\b(find_package|find_program|find_library|find_path|find_file|try_compile|try_run|configure_file|"
                     r"add_subdirectory|aux_source_directory|load_cache|cmake_host_system_information|site_name)\s*\(|"
                     r"\$ENV\{|\b(IS_DIRECTORY|IS_NEWER_THAN|IS_SYMLINK)\b")
EXISTS_CHECKED = ('"${TS2_PYTHON}"', '"${TS2_DXSDK_DIR}/d3dx9.lib"')
EXISTS_FILES = (".venv/Scripts/python.exe", "local/thirdparty/dxsdk-sum2004/d3dx9.lib")
MACROS = re.compile(r'set\(TS2_PCH_CONFIG_MACROS\s+"([^"]*)"\)')
LITERAL_PATH = re.compile(r"[\w./-]+\.(?:cpp|c|h|asm|py|jsonl|txt)\b")
GLOB_ENTRY = re.compile(r'file\((GLOB|GLOB_RECURSE) NEW_GLOB LIST_DIRECTORIES (?:true|false) "([^"]*)"\)\n'
                        r'set\(OLD_GLOB\n((?:  "[^"]*"\n)*)  \)\n')


def code_lines(text):
    return [line for line in text.splitlines() if not line.lstrip().startswith("#")]


def unsure(text):
    """Why this file can't vouch for this CMakeLists.txt (see the module docstring), or None."""
    code = "\n".join(code_lines(text))
    m = READERS.search(code)
    if m:
        return f"CMakeLists.txt uses {m.group(0)}"
    for m in re.finditer(r"\bfile\(\s*([A-Z_]+)([^)]*)\)", code):
        if m.group(1) not in FILE_COMMANDS:
            return f"CMakeLists.txt uses file({m.group(1)})"
        if m.group(1).startswith("GLOB") and "CONFIGURE_DEPENDS" not in m.group(2):
            return f"a glob without CONFIGURE_DEPENDS: {m.group(0)}"
    if len(re.findall(r"\bfile\(\s*STRINGS\b", code)) != 1 or not macro_pattern(text):
        return "file(STRINGS) isn't the one TS2_PCH_CONFIG_MACROS scan"
    for m in re.finditer(r"\bexecute_process\(([^)]*)\)", code):
        if "git rev-parse" not in m.group(1):
            return f"CMakeLists.txt runs {m.group(1)[:40]}"
    for m in re.finditer(r"\binclude\(\s*([^)\s]*)", code):
        if m.group(1) != "FetchContent":
            return f"CMakeLists.txt includes {m.group(1)}"
    for m in re.finditer(r"\bEXISTS\s+(\S+)", code):
        if m.group(1).rstrip(")") not in EXISTS_CHECKED:
            return f"CMakeLists.txt tests EXISTS {m.group(1)}"
    return None


def macro_pattern(text):
    """The compiled pattern of the macro scan, with CMake's anchor off, or None when it isn't the expected kind."""
    m = MACROS.search(text)
    if not m or not m.group(1).startswith("^[ \\t]*"):
        return None
    pattern = m.group(1)[len("^[ \\t]*"):]
    if not re.fullmatch(r"[\w#\[\]\\ +*()|-]+", pattern):
        return None
    return re.compile(pattern.encode())


def digest(path, pattern):
    """What `file(STRINGS)` could see of a file (the module docstring): "" when no line can match."""
    data = Path(path).read_bytes()
    if b"\0" in data:
        return "raw " + hashlib.sha256(data).hexdigest()
    lines = set()
    for m in pattern.finditer(data):
        start = data.rfind(b"\n", 0, m.start()) + 1
        end = data.find(b"\n", m.end())
        lines.add(data[start:end if end >= 0 else len(data)])
    return hashlib.sha256(b"\n".join(sorted(lines))).hexdigest() if lines else ""


def stamp(path):
    s = os.stat(path)
    return [s.st_size, s.st_mtime_ns]


def globbed(pattern):
    """The paths a recorded `dir/*.ext` pattern sees now; None for any other pattern."""
    m = re.fullmatch(r"(.*)/\*(\.[\w.]+)", pattern)
    if not m:
        return None
    return m.group(1), m.group(2).lower()


def recorded_globs(build):
    """[(kind, directory, suffix, [paths at configure])] from CMake's own file, or None when it can't be parsed whole."""
    text = (Path(build) / "CMakeFiles/VerifyGlobs.cmake").read_text(encoding="utf-8")
    out = []
    for kind, pattern, old in GLOB_ENTRY.findall(text):
        g = globbed(pattern)
        if g is None:
            return None
        out.append((kind, g[0], g[1], re.findall(r'"([^"]*)"', old)))
    return out if out and len(out) == text.count("file(GLOB") else None


def scan_dir(kind, directory, suffix):
    found = []
    if kind == "GLOB":
        with os.scandir(directory) as it:
            found = [f"{directory}/{e.name}" for e in it if e.name.lower().endswith(suffix)]
    else:
        for at, _, files in os.walk(directory):
            at = at.replace("\\", "/")
            found += [f"{at}/{n}" for n in files if n.lower().endswith(suffix)]
    return found


def globs_changed(globs):
    for kind, directory, suffix, old in globs:
        if sorted(scan_dir(kind, directory, suffix)) != sorted(old):
            return f"the files {directory}/*{suffix} changed"
    return None


def literal_paths(text):
    """Every path-like token of CMakeLists.txt, relative to the root; most exist, the rest just have to stay absent."""
    return sorted({m.group(0).lstrip("/") for line in code_lines(text) for m in LITERAL_PATH.finditer(line)})


def existing(root, main):
    return [os.path.exists(Path(top) / f) for top in (root, main) for f in EXISTS_FILES]


def generated_files(build):
    """What a configure leaves in build/ that a build needs and can't make: {path: [size, time]}."""
    build = Path(build)
    out = {}
    ninja = (build / "build.ninja").exists()
    made = [*build.glob("*.ninja"), *build.glob("CMakeFiles/*.ninja"), *build.glob("_deps/*-subbuild/*.ninja")] if ninja \
        else [*build.glob("*.sln"), *build.glob("*.vcxproj")]
    for f in [build / "CMakeCache.txt", *made]:
        out[str(f)] = stamp(f)
    if ninja:
        for line in ninja_inputs(build):
            out[line] = stamp(line)
    else:
        depends = [build / "CMakeFiles/generate.stamp.depend", *build.glob("_deps/*-subbuild/CMakeFiles/generate.stamp.depend")]
        for depend in depends:
            for line in depend.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    out[line] = stamp(line)
    # Touched when the build finds a glob changed: `globs_changed` is the check for that, and it can't lag a build.
    out.pop(str(build / "CMakeFiles/cmake.verify_globs").replace("\\", "/"), None)
    return out


def ninja_inputs(build):
    """The regeneration inputs of a Ninja tree, absolute with forward slashes: the implicit inputs of the RERUN_CMAKE
    statement in CMakeFiles/common.ninja (`$:` and `$ ` are its escapes)."""
    text = (build / "CMakeFiles/common.ninja").read_text(encoding="utf-8")
    statement = next(line for line in text.splitlines() if line.startswith("build ") and ": RERUN_CMAKE " in line)
    inputs = statement.split(": RERUN_CMAKE ", 1)[1]
    paths = re.findall(r"(?:\$.|[^\s$])+", inputs.split(" | ", 1)[1] if " | " in inputs else "")
    return [str(build / re.sub(r"\$(.)", r"\1", p)).replace("\\", "/") for p in paths]


def fetched(build):
    """{fetched source tree: its top-level names}: configure refetches one that went missing."""
    return {str(d): sorted(os.listdir(d)) for d in sorted(Path(build).glob("_deps/*-src"))}


def snapshot(args):
    """Everything but the sources, as of now."""
    text = (ROOT / "CMakeLists.txt").read_bytes()
    return {"version": VERSION, "root": str(ROOT), "main": args.main, "build": args.build,
            "cmake": [args.cmake, *stamp(args.cmake)], "cmakelists": hashlib.sha256(text).hexdigest(),
            "literal": {p: os.path.exists(ROOT / p) for p in literal_paths(text.decode("utf-8"))},
            "exists": existing(ROOT, args.main), "generated": generated_files(args.build),
            "generated_dir": os.path.isdir(Path(args.build) / "generated"), "fetched": fetched(args.build)}


def check(args):
    """Why configure has to run, or None to skip it."""
    state = json.loads((Path(args.build) / STATE).read_text(encoding="utf-8"))
    now = snapshot(args)
    for key, value in now.items():
        if state.get(key) != value:
            return f"{key} changed"
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    pattern = macro_pattern(text)
    globs = recorded_globs(args.build)
    if pattern is None or globs is None:
        return "CMakeLists.txt or VerifyGlobs.cmake not understood"
    why = globs_changed(globs)
    if why:
        return why
    for path, (size, mtime, seen) in state["sources"].items():
        if stamp(path) != [size, mtime] and digest(path, pattern) != seen:
            return f"{path}: a line configure reads changed"
    return None


def source_files(text, globs):
    files = {p for _, _, _, old in globs for p in old}
    files.update((ROOT / p).as_posix() for p in literal_paths(text) if (ROOT / p).is_file())
    return sorted(f for f in files if os.path.isfile(f))


def commit(args):
    """After a successful configure. True when the state was written."""
    build = Path(args.build)
    begun = json.loads((build / PENDING).read_text(encoding="utf-8"))
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    globs = recorded_globs(build)
    pattern = macro_pattern(text)
    state = snapshot(args)
    if unsure(text) or globs is None or pattern is None or state["cmakelists"] != begun["cmakelists"] or globs_changed(globs):
        return False
    state["sources"] = {}
    for path in source_files(text, globs):
        size, mtime = stamp(path)
        if mtime >= begun["started"] - RACE_MARGIN_NS:
            return False  # possibly edited while configure ran (the module docstring)
        state["sources"][path] = [size, mtime, digest(path, pattern)]
    tmp = build / f"{STATE}.{os.getpid()}.tmp"
    tmp.write_text(json.dumps(state), encoding="utf-8")
    os.replace(tmp, build / STATE)
    return True


def begin(args):
    build = Path(args.build)
    (build / STATE).unlink(missing_ok=True)
    # The sha is read after the clock, so a CMakeLists.txt saved from here on either fails the comparison in commit().
    started = time.time_ns()
    sha = hashlib.sha256((ROOT / "CMakeLists.txt").read_bytes()).hexdigest()
    (build / PENDING).write_text(json.dumps({"started": started, "cmakelists": sha}), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=("check", "begin", "commit"))
    ap.add_argument("--build", required=True)
    ap.add_argument("--main", required=True)
    ap.add_argument("--cmake", required=True)
    ap.add_argument("--why", action="store_true", help="say on stderr why configure has to run")
    args = ap.parse_args()
    if args.action == "check":
        try:
            why = check(args)
        except Exception as ex:  # any doubt means configure
            why = f"{type(ex).__name__}: {ex}"
        if why and args.why:
            print(why, file=sys.stderr)
        sys.exit(1 if why else 0)
    # A state that can't be written only means the next build configures.
    try:
        if args.action == "begin":
            begin(args)
        else:
            committed = commit(args)
            if not committed and args.why:
                print("no state written", file=sys.stderr)
    except Exception as ex:
        (Path(args.build) / STATE).unlink(missing_ok=True)
        if args.why:
            print(f"{type(ex).__name__}: {ex}", file=sys.stderr)


if __name__ == "__main__":
    main()
