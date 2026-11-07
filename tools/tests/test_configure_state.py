#!/usr/bin/env python3
"""Tests for tools/configure_state.py: every change configure could care about means "configure". A made-up checkout
and build directory; no CMake runs.

Run: tools/py -I -m pytest tools/tests/test_configure_state.py
"""
import argparse
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import configure_state as C  # noqa: E402

CMAKELISTS = r'''# a comment may say find_package() or file(WRITE)
file(GLOB_RECURSE CORE CONFIGURE_DEPENDS src/*.cpp)
file(GLOB TESTS CONFIGURE_DEPENDS tests/*.cpp)
file(MAKE_DIRECTORY ${TS2_GENERATED})
set(TS2_PYTHON "x")
if(NOT EXISTS "${TS2_PYTHON}")
endif()
execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir)
include(FetchContent)
set(TS2_PCH_CONFIG_MACROS
    "^[ \t]*#[ \t]*define[ \t]+(_CRT_[A-Z_]+|_USE_MATH_DEFINES)")
foreach(f ${TESTS} harness/main.cpp)
    file(STRINGS ${f} hit REGEX "${TS2_PCH_CONFIG_MACROS}" LIMIT_COUNT 1)
endforeach()
'''


class Unsure(unittest.TestCase):
    def test_the_expected_shape_is_understood(self):
        self.assertIsNone(C.unsure(CMAKELISTS))

    def test_what_it_cannot_follow_is_refused(self):
        for extra in ("find_package(Foo)\n", "find_program(X y)\n", "configure_file(a b)\n", "add_subdirectory(x)\n",
                      "set(A $ENV{PATH})\n", "file(READ x y)\n", "file(GLOB Z src/*.h)\n", "include(extra.cmake)\n",
                      "execute_process(COMMAND python x.py)\n", "if(EXISTS \"${OTHER}\")\nendif()\n",
                      "if(a IS_NEWER_THAN b)\nendif()\n", "file(STRINGS ${g} v)\n"):
            self.assertTrue(C.unsure(CMAKELISTS + extra), extra)

    def test_a_scan_it_cannot_read_is_refused(self):
        self.assertTrue(C.unsure(CMAKELISTS.replace('"^[ \\t]*#', '"#')))
        self.assertTrue(C.unsure(CMAKELISTS.replace("_CRT_[A-Z_]+", "_CRT_.*")))


class Digest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.file = Path(self.tmp.name) / "a.cpp"
        self.pattern = C.macro_pattern(CMAKELISTS)

    def tearDown(self):
        self.tmp.cleanup()

    def digest(self, data):
        self.file.write_bytes(data)
        return C.digest(self.file, self.pattern)

    def test_other_lines_do_not_count(self):
        self.assertEqual(self.digest(b"int a;\n"), "")
        with_macro = self.digest(b"#define _USE_MATH_DEFINES\nint a;\n")
        self.assertEqual(with_macro, self.digest(b"#define _USE_MATH_DEFINES\nint b;\nint c;\n"))

    def test_lines_the_scan_can_match_count(self):
        seen = {self.digest(text) for text in (b"", b"#define _USE_MATH_DEFINES\n", b"  #  define\t_USE_MATH_DEFINES 1\n",
                                               b"#define _CRT_SECURE_NO_WARNINGS\n")}
        self.assertEqual(len(seen), 4)

    def test_a_file_cmake_decodes_is_taken_whole(self):
        a = self.digest("int a;\n".encode("utf-16"))
        self.assertTrue(a.startswith("raw "))
        self.assertNotEqual(a, self.digest("int b;\n".encode("utf-16")))


class Check(unittest.TestCase):
    """A state written by commit() as after a configure, then each change."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = self.root = Path(self.tmp.name).resolve() / "root"
        build = self.build = root / "build"
        for d in ("src/sub", "tests", "harness", "build/CMakeFiles", "build/generated", "build/_deps/zydis-src"):
            (root / d).mkdir(parents=True)
        (root / "CMakeLists.txt").write_text(CMAKELISTS, encoding="utf-8")
        self.src = root / "src/sub/a.cpp"
        self.test = root / "tests/t.cpp"
        self.main_cpp = root / "harness/main.cpp"
        for f in (self.src, self.test, self.main_cpp):
            f.write_text("int a;\n", encoding="utf-8")
        (build / "_deps/zydis-src/x").write_text("", encoding="utf-8")
        self.cmake = root / "cmake.exe"
        self.cmake.write_text("", encoding="utf-8")
        self.input = root / "module.cmake"
        self.input.write_text("", encoding="utf-8")
        (build / "CMakeCache.txt").write_text("", encoding="utf-8")
        (build / "T.sln").write_text("", encoding="utf-8")
        (build / "d.vcxproj").write_text("", encoding="utf-8")
        (build / "CMakeFiles/generate.stamp.depend").write_text(f"# CMake\n{self.input.as_posix()}\n", encoding="utf-8")
        self.write_globs()
        self.args = argparse.Namespace(build=str(build), main=str(root), cmake=str(self.cmake))
        patcher = mock.patch.object(C, "ROOT", root)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.age_sources()
        C.begin(self.args)
        self.assertTrue(C.commit(self.args))

    def tearDown(self):
        self.tmp.cleanup()

    def write_globs(self):
        def entry(kind, pattern, files):
            old = "".join(f'  "{f.as_posix()}"\n' for f in files)
            return f'file({kind} NEW_GLOB LIST_DIRECTORIES false "{self.root.as_posix()}/{pattern}")\nset(OLD_GLOB\n{old}  )\n'
        (self.build / "CMakeFiles/VerifyGlobs.cmake").write_text(
            "# generated\n" + entry("GLOB", "tests/*.cpp", [self.test]) + entry("GLOB_RECURSE", "src/*.cpp", [self.src]),
            encoding="utf-8")

    def age_sources(self):
        long_ago = time.time() - 3600
        for f in (self.src, self.test, self.main_cpp):
            os.utime(f, (long_ago, long_ago))

    def why(self):
        return C.check(self.args)

    def test_unchanged_skips(self):
        self.assertIsNone(self.why())

    def test_edits_configure_does_not_read_skip(self):
        self.src.write_text("int b;\n", encoding="utf-8")
        self.test.write_text("int b;\n", encoding="utf-8")
        self.assertIsNone(self.why())

    def test_new_and_deleted_files(self):
        for new in (self.root / "src/new.cpp", self.root / "src/sub/new.cpp", self.root / "tests/new.cpp"):
            new.write_text("", encoding="utf-8")
            self.assertTrue(self.why(), new)
            new.unlink()
            self.assertIsNone(self.why(), new)
        for gone in (self.src, self.test):
            saved = gone.read_bytes()
            gone.unlink()
            self.assertTrue(self.why(), gone)
            gone.write_bytes(saved)

    def test_a_macro_line_in_a_scanned_file(self):
        for f in (self.test, self.main_cpp):
            saved = f.read_bytes()
            f.write_text("#define _USE_MATH_DEFINES\nint a;\n", encoding="utf-8")
            self.assertIn("configure reads", self.why())
            f.write_bytes(saved)

    def test_cmakelists(self):
        path = self.root / "CMakeLists.txt"
        saved = path.read_bytes()
        path.write_text(CMAKELISTS + "# more\n", encoding="utf-8")
        self.assertTrue(self.why())
        path.write_bytes(saved)
        self.assertIsNone(self.why())

    def test_what_configure_made_or_read(self):
        for f in (self.build / "CMakeCache.txt", self.build / "T.sln", self.build / "d.vcxproj", self.input, self.cmake):
            with self.subTest(f=f.name):
                s = f.stat()
                f.write_text("changed", encoding="utf-8")
                self.assertTrue(self.why())
                f.write_text("", encoding="utf-8")
                os.utime(f, ns=(s.st_atime_ns, s.st_mtime_ns))
                self.assertIsNone(self.why())

    def test_gone_means_configure_never_a_crash(self):
        for f in (self.build / "CMakeCache.txt", self.build / "CMakeFiles/VerifyGlobs.cmake",
                  self.build / "CMakeFiles/generate.stamp.depend", self.build / C.STATE):
            with self.subTest(f=f.name):
                moved = f.with_suffix(".hidden")
                f.rename(moved)
                result = run_main(self.args)
                moved.rename(f)
                self.assertEqual(result, 1)
                self.assertEqual(run_main(self.args), 0)

    def test_a_foreign_or_broken_state(self):
        state = self.build / C.STATE
        saved = state.read_bytes()
        for text in ("not json", "{}", '{"version": 0}'):
            state.write_text(text, encoding="utf-8")
            self.assertEqual(run_main(self.args), 1, text)
        data = json.loads(saved)
        data["root"] = "elsewhere"
        state.write_text(json.dumps(data), encoding="utf-8")
        self.assertEqual(run_main(self.args), 1)

    def test_fetched_sources(self):
        (self.build / "_deps/zydis-src/x").unlink()
        self.assertTrue(self.why())

    def test_begin_forgets_so_a_failed_configure_configures_again(self):
        C.begin(self.args)
        self.assertFalse((self.build / C.STATE).exists())
        self.assertEqual(run_main(self.args), 1)

    def test_no_state_for_a_file_edited_during_configure(self):
        C.begin(self.args)
        self.test.write_text("int b;\n", encoding="utf-8")
        self.assertFalse(C.commit(self.args))
        self.assertFalse((self.build / C.STATE).exists())

    def test_no_state_for_a_cmakelists_edited_during_configure(self):
        C.begin(self.args)
        (self.root / "CMakeLists.txt").write_text(CMAKELISTS + "# more\n", encoding="utf-8")
        self.assertFalse(C.commit(self.args))

    def test_no_state_for_a_cmakelists_it_cannot_vouch_for(self):
        (self.root / "CMakeLists.txt").write_text(CMAKELISTS + "find_package(Foo)\n", encoding="utf-8")
        C.begin(self.args)
        self.assertFalse(C.commit(self.args))


class NinjaTree(unittest.TestCase):
    """What a Ninja tree's configure made or read: its build files and the inputs of the RERUN_CMAKE statement."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.build = Path(self.tmp.name) / "build"
        (self.build / "CMakeFiles").mkdir(parents=True)
        (self.build / "sub dir").mkdir()
        self.module = Path(self.tmp.name) / "module.cmake"
        self.spaced = self.build / "sub dir/in.cmake"
        for f in (self.module, self.spaced, self.build / "CMakeCache.txt", self.build / "build.ninja",
                  self.build / "build-Release.ninja", self.build / "CMakeFiles/impl-Release.ninja"):
            f.write_text("", encoding="utf-8")
        module = self.module.as_posix().replace(":", "$:").replace(" ", "$ ")
        (self.build / "CMakeFiles/common.ninja").write_text(
            "rule RERUN_CMAKE\n  command = x\n\n"
            f"build build.ninja: RERUN_CMAKE CMakeFiles/VerifyGlobs.cmake_force | {module} sub$ dir/in.cmake\n  pool = console\n",
            encoding="utf-8")

    def test_inputs(self):
        self.assertEqual(C.ninja_inputs(self.build), [self.module.as_posix(), self.spaced.as_posix()])

    def test_generated_files(self):
        files = C.generated_files(self.build)
        for f in (self.module.as_posix(), self.spaced.as_posix(), str(self.build / "build-Release.ninja"),
                  str(self.build / "CMakeFiles/impl-Release.ninja"), str(self.build / "CMakeCache.txt")):
            self.assertIn(f, files)


def run_main(args):
    argv = ["configure_state.py", "check", "--build", args.build, "--main", args.main, "--cmake", args.cmake]
    with mock.patch.object(sys, "argv", argv), self_exit() as code:
        C.main()
    return code[0]


class self_exit:
    """Catches main()'s sys.exit; the code lands in the list `with` hands back."""

    def __enter__(self):
        self.code = []
        return self.code

    def __exit__(self, kind, value, tb):
        if kind is SystemExit:
            self.code.append(value.code)
            return True
        return False


if __name__ == "__main__":
    unittest.main()
