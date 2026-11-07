#!/usr/bin/env python3
"""Tests for tools/testselect.py and tools/cppblocks.py (which tests a --filter selects; can they reach a mutant).

Run: .venv/Scripts/python -I tools/tests/test_testselect.py
Synthetic tests/ and src/ texts; the last class also checks lane KY's real case against the repo.
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import cppblocks as C  # noqa: E402
import testselect as T  # noqa: E402

REPO = Path(__file__).resolve().parent.parent.parent

SRC = """\
namespace nGZ {
void cShader::SetContext(int a)
{
    if (a) { a = 1; }
}
void cShader::EndBlock()
{
    auto f = [](int x) { return x + 1; };
    f(2);
}
static int Helper(int x) { return x * 2; }
struct cInline {
    int Get() const { return 4; }
};
}
"""

TESTS = {
    # the shader's tests: SetContext (rva named), EndBlock (in its own block only)
    "tests/gz_shader.cpp": """\
#include "gz_shader.h"
DIFF_TEST(Shader_SetContext, 0x1000)
{
    auto o = ctx.Original<void(__thiscall *)(void *, int)>(0x1000);
}
// EndBlock 0x1100: validates the shader
DIFF_TEST(Shader_EndBlock, kEndBlock)
{
    run(ctx);
}
""",
    "tests/gz_shader.h": "static constexpr uint32_t kEndBlock = 0x1100;\n",
    # a class tested through our vtable only: its RVAs appear nowhere
    "tests/rz_file.cpp": "DIFF_TEST(RZFile_Ops, 0x2000) { Ops(ctx); }\n",
    # wrapper macros
    "tests/lot.cpp": """\
#define FACE_TEST(Name, Rva, Method) \\
    DIFF_TEST(LotInfo_##Name, Rva) \\
    { for (int seed = 0; seed < 200; ++seed) Pair(ctx, #Name, seed); }
FACE_TEST(GetId, 0x3000, &X::GetId)
FACE_TEST(GetName, 0x3010, &X::GetName)
#define OCC_TEST(name, rva) static void name##Body(difftest::Context &ctx); DIFF_TEST(name, rva) { name##Body(ctx); } \\
    static void name##Body(difftest::Context &ctx)
OCC_TEST(NHoodOcc_Init, 0x3020)
{
    Init(0x3030);
}
""",
    # a shared table outside the DIFF_TEST blocks
    "tests/table.cpp": """\
static const Row kRows[] = {{0x4000, 1}, {0x4010, 2}};
DIFF_TEST(Table_All, 0x4100) { for (auto &r : kRows) Run(ctx, r); }
""",
    "tests/dyn.cpp": """\
struct Registered { Registered() { add(std::string("COMDirectors_Construct_") + t.name, t.ctor, body);
    new difftest::Registrar(names.back().c_str(), rva, std::move(body)); } };
""",
}

ENTRIES = [
    {"rva": "0x1000", "name": "nGZ::cShader::SetContext (slot 3)", "src": "src/gz/Shader.cpp", "test": "tests/gz_shader.cpp"},
    {"rva": "0x1100", "name": "nGZ::cShader::EndBlock (slot 9)", "src": "src/gz/Shader.cpp", "test": "tests/gz_shader.cpp"},
    {"rva": "0x2000", "name": "cRZFile::Ops", "src": "src/rz/cRZFile.h", "test": "tests/rz_file.cpp"},
    {"rva": "0x2100", "name": "cRZFile::Seek", "src": "src/rz/cRZFile.size.cpp", "test": "tests/rz_file.cpp"},
    {"rva": "0x2200", "name": "cRZFile::Grow", "src": "src/rz/cRZFile.cpp"},  # reimplemented, no test of its own
    {"rva": "0x4000", "name": "cTable::Row", "src": "src/gz/Table.cpp", "test": "tests/table.cpp"},
]


class Blocks(unittest.TestCase):
    def test_functions_and_classes(self):
        quals = [(b.qual, b.kind) for b in C.blocks(SRC)]
        self.assertEqual(quals, [("cShader::SetContext", "function"), ("cShader::EndBlock", "function"),
                                 ("Helper", "function"), ("cInline::Get", "function")])
        at = SRC.index("x + 1")
        self.assertEqual(C.enclosing(C.blocks(SRC), at).qual, "cShader::EndBlock")  # a lambda inside: the function
        self.assertIsNone(C.enclosing(C.blocks(SRC), SRC.index("namespace")))

    def test_template_struct_is_a_container(self):
        text = "template <class T> struct Box {\n    T Get() { return v; }\n    T v;\n};\n"
        self.assertEqual([b.qual for b in C.blocks(text)], ["Box::Get"])

    def test_wrapper_macros(self):
        blocks = C.test_blocks(TESTS["tests/lot.cpp"])
        self.assertEqual([(b.name, b.rva) for b in blocks],
                         [("LotInfo_GetId", "0x3000"), ("LotInfo_GetName", "0x3010"), ("NHoodOcc_Init", "0x3020")])
        occ = blocks[2]
        self.assertIn("Init(0x3030)", TESTS["tests/lot.cpp"][occ.start:occ.end])  # the block after the call is its body


class Select(unittest.TestCase):
    def setUp(self):
        self.tests = T.scan_tests(texts=TESTS)

    def names(self, filt, read=None):
        return [t["name"] or "dyn:" + t["file"] for t, _ in T.select(self.tests, filt, read)]

    def test_registry(self):
        by = {t["name"]: t for t in self.tests if t["name"]}
        self.assertEqual(by["Shader_EndBlock"]["rva"], "0x1100")  # a constant from the included header
        self.assertEqual(by["Shader_EndBlock"]["mentions"], {"0x1100"})  # its comment line and constant
        self.assertEqual(by["Shader_SetContext"]["mentions"], {"0x1000"})
        self.assertIn("0x3030", by["NHoodOcc_Init"]["mentions"])

    def test_harness_semantics(self):
        self.assertEqual(self.names("Shader_"), ["Shader_SetContext", "Shader_EndBlock"])
        self.assertEqual(self.names("shader_endblock"), [])  # names are case-sensitive
        self.assertEqual(self.names("GZ_SHADER.cpp"), ["Shader_SetContext", "Shader_EndBlock"])  # stems are not
        self.assertEqual(self.names("EndBlock,RZFile"), ["Shader_EndBlock", "RZFile_Ops"])
        self.assertEqual(self.names("@list", lambda p: "RZFile_Ops\n\nShader_EndBlock\n"),
                         ["Shader_EndBlock", "RZFile_Ops"])
        self.assertEqual(self.names("dyn"), ["dyn:tests/dyn.cpp"])
        self.assertEqual(self.names("COMDirectors_Construct_cFoo"), ["dyn:tests/dyn.cpp"])  # may match
        self.assertEqual(self.names("Nothing"), [])


class Reach(unittest.TestCase):
    def check(self, filt, targets):
        return T.check(filt, targets, root=REPO, tests=T.scan_tests(texts=TESTS), entries=ENTRIES,
                       includes=lambda rel: {"tests/gz_shader.h"} if rel == "tests/gz_shader.cpp" else set())

    def test_ky_case_same_file_other_function_is_refused(self):
        ok, lines, _ = self.check("Shader_SetContext", [("src/gz/Shader.cpp", "cShader::EndBlock")])
        self.assertFalse(ok)
        self.assertIn("tests that would: Shader_EndBlock", lines[-1])
        ok, _, info = self.check("Shader_EndBlock", [("src/gz/Shader.cpp", "cShader::EndBlock")])
        self.assertTrue(ok)
        ok, _, _ = self.check("gz_shader", [("src/gz/Shader.cpp", "cShader::EndBlock")])
        self.assertTrue(ok)

    def test_helper_without_entry_uses_the_file_rule(self):
        ok, lines, _ = self.check("Shader_SetContext", [("src/gz/Shader.cpp", "Helper")])
        self.assertTrue(ok, lines)
        self.assertTrue(any("[file (no status entry is named Helper)]" in l for l in lines), lines)
        ok, lines, _ = self.check("RZFile", [("src/gz/Shader.cpp", "Helper")])
        self.assertFalse(ok)

    def test_function_no_test_names_falls_back_to_its_test_file(self):
        # cRZFile::Seek (0x2100) is named nowhere: tests/rz_file.cpp, its entry's test, counts; same stem .h/.size.cpp
        ok, lines, _ = self.check("RZFile_Ops", [("src/rz/cRZFile.size.cpp", "cRZFile::Seek")])
        self.assertTrue(ok, lines)
        ok, _, _ = self.check("Shader", [("src/rz/cRZFile.size.cpp", "cRZFile::Seek")])
        self.assertFalse(ok)

    def test_function_without_test_named_nowhere_uses_the_file_rule(self):
        ok, lines, _ = self.check("RZFile_Ops", [("src/rz/cRZFile.cpp", "cRZFile::Grow")])
        self.assertTrue(ok, lines)
        self.assertIn("which no test names", lines[-2])

    def test_rva_in_a_shared_table(self):
        ok, lines, _ = self.check("Table_All", [("src/gz/Table.cpp", "cTable::Row")])
        self.assertTrue(ok, lines)
        self.assertIn("outside its DIFF_TEST blocks", lines[-1])

    def test_own_test_file_silent_about_the_rva(self):
        # an entry whose RVA another file names (lot.cpp's NHoodOcc_Init), while its own test file never does
        entries = ENTRIES + [{"rva": "0x3030", "name": "cRZFile::Flush", "src": "src/rz/cRZFile.cpp",
                              "test": "tests/rz_file.cpp"}]
        ok, lines, _ = T.check("RZFile_Ops", [("src/rz/cRZFile.cpp", "cRZFile::Flush")], root=REPO,
                               tests=T.scan_tests(texts=TESTS), entries=entries, includes=lambda rel: set())
        self.assertTrue(ok, lines)
        self.assertIn("names none of its RVAs", lines[-1])
        ok, lines, _ = T.check("LotInfo", [("src/rz/cRZFile.cpp", "cRZFile::Flush")], root=REPO,
                               tests=T.scan_tests(texts=TESTS), entries=entries, includes=lambda rel: set())
        self.assertFalse(ok, lines)  # lot.cpp's tests don't name it either (only NHoodOcc_Init does)
        self.assertIn("NHoodOcc_Init", lines[-1])

    def test_selects_nothing(self):
        ok, lines, _ = self.check("MaterialCommandz", [("src/gz/Shader.cpp", None)])
        self.assertFalse(ok)
        self.assertIn("selects no test", lines[0])

    def test_unknown_file(self):
        ok, lines, _ = self.check("Shader", [("src/gz/Other.cpp", None)])
        self.assertFalse(ok)
        self.assertIn("no test in tests/ reaches it", lines[-1])

    def test_targets_of_mutants(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "src").mkdir()
            (Path(d) / "src/S.cpp").write_text(SRC, encoding="utf-8")
            mutants = [{"file": "src/S.cpp", "old": "a = 1", "new": "a = 2"},
                       {"file": "src/S.cpp", "edits": [{"old": "x * 2", "new": "x * 3"}, {"old": "return 4", "new": "return 5"}]},
                       {"file": "src/S.cpp", "old": "namespace nGZ {", "new": "namespace nGZ2 {"}]
            self.assertEqual(T.targets_of(mutants, d), [("src/S.cpp", "cShader::SetContext"), ("src/S.cpp", "Helper"),
                                                        ("src/S.cpp", "cInline::Get"), ("src/S.cpp", None)])


class RealRepo(unittest.TestCase):
    """Lane KY's coordinator mutants: cShaderCommand::EndBlock under --filter MaterialCommands."""

    def test_ky(self):
        ok, lines, _ = T.check("MaterialCommands", [("src/gz/MaterialCommands.cpp", "cShaderCommand::EndBlock")])
        self.assertFalse(ok, lines)
        self.assertIn("ShaderCommand_EndBlock", lines[-1])
        ok, lines, _ = T.check("ShaderCommand_EndBlock", [("src/gz/MaterialCommands.cpp", "cShaderCommand::EndBlock")])
        self.assertTrue(ok, lines)


if __name__ == "__main__":
    unittest.main()
