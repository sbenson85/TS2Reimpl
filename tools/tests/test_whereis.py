#!/usr/bin/env python3
"""Tests for tools/whereis.py's parsing and tsdb's class-name resolution (used by classinfo.py too). Run:

  tools/py -I tools/tests/test_whereis.py
"""
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import tsdb  # noqa: E402
import whereis  # noqa: E402

failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


T1 = ".?AV?$cBoundedNodeT@VcIViewerRefNode@nGZSceneGraph@@VcViewerRefNodeImpl@2@@nGZSceneGraph@@"
T2 = ".?AV?$cBoundedNodeT@VcILightRefNode@nGZSceneGraph@@VcLightRefNodeImpl@2@@nGZSceneGraph@@"
T3 = ".?AV?$cViewerRefNodeRecursiveT@VcIViewerRefNodeRecursive@nGZSceneGraph@@VcViewerRefNodeRecursiveImpl@2@@nGZSceneGraph@@"
NAMES = ["nGZSceneGraph::cViewer_LEGACY", "nGZSceneGraph::cViewer", "cViewer", "nA::cTwin", "nB::cTwin", T1, T2, T3]

# Class names
check(tsdb.class_outer("nGZSceneGraph::cViewer_LEGACY") == "cViewer_LEGACY", "outer of a qualified name")
check(tsdb.class_outer(T1) == "cBoundedNodeT", "outer of a mangled template")
check(tsdb.class_outer(".?AVcFoo@ns@@") == "cFoo", "outer of a mangled plain class")
check(tsdb.resolve_class("cViewer_LEGACY", NAMES) == ("nGZSceneGraph::cViewer_LEGACY", []), "unqualified, one match")
check(tsdb.resolve_class("cViewer", NAMES)[0] == "cViewer", "an exact name wins over a qualified one")
name, cands = tsdb.resolve_class("cTwin", NAMES)
check(name is None and sorted(cands) == ["nA::cTwin", "nB::cTwin"], "two namespaces: candidates")
check(tsdb.resolve_class("cViewerRefNodeRecursiveT", NAMES)[0] == T3, "one template instantiation: resolved")
name, cands = tsdb.resolve_class("cBoundedNodeT", NAMES)
check(name is None and sorted(cands) == sorted([T1, T2]), "two instantiations: candidates")
check(tsdb.resolve_class("LEGACY", NAMES) == (None, ["nGZSceneGraph::cViewer_LEGACY"]), "no match: substring list")
check(tsdb.pretty_class(T1) == "cBoundedNodeT<cIViewerRefNode,cViewerRefNodeImpl>", "pretty template")
check(tsdb.pretty_class("nA::cTwin") == "nA::cTwin", "pretty leaves plain names")

# Class extents and members
SRC = """// header
class cFwd;
class cFoo : public cIBar {
public:
    void Draw(int a);   // 0x1234 { not a brace
    const char *Name() const { return "}"; }
    /* } */ ~cFoo();
    struct Inner {
        void Draw2();
    };
    char c = '}';
};
void cFoo::Draw(int a) {}
""".splitlines()
check(whereis.class_extent(SRC, 2) == (2, 2), "forward declaration: no extent")
check(whereis.class_extent(SRC, 3) == (3, 12), "extent skips braces in comments, strings, chars; nested struct")
ext = whereis.class_extent(SRC, 3)
check(whereis.member_line(SRC, ext, "Draw") == 5, "member line")
check(whereis.member_line(SRC, ext, "~cFoo") == 7, "destructor line")
check(whereis.member_line(SRC, ext, "Missing") is None, "missing member")
check(whereis.member_line(SRC, (3, 12), "Name") == 6, "inline member")

# Names
check(whereis.split_name("nGZSceneGraph::cViewerRefNodeRecursiveImpl::Cull") == ("cViewerRefNodeRecursiveImpl", "Cull"),
      "split a qualified name")
check(whereis.split_name("ns::cViewerRefNodeRecursiveT<cIViewerRefNodeRecursive>::vf83 Cull (-> x)") ==
      ("cViewerRefNodeRecursiveT", "vf83"), "split a templated name with a note")
check(whereis.split_name("FUN_a0aa7b90") == ("", "FUN_a0aa7b90"), "split an unqualified name")

# Docs sections
DOC = "intro cFoo\n# Top\nnone\n## A\ncFoo and cFoo\n```\n# not a heading cFoo\n```\n### B\ncFooBar\n"
check(whereis.doc_sections(DOC, re.compile(r"\bcFoo\b")) == [(1, "(top)", 1), (4, "## A", 3)],
      "sections with mentions (fenced # is not a heading)")
check(whereis.doc_sections("## cFoo / cBar\n### part 1\ncFoo here\n", re.compile(r"\bcFoo\b")) ==
      [(1, "## cFoo / cBar", 0), (2, "### part 1", 1)],
      "a heading naming it is listed with no mentions of its own")

# Test-file tags
rv = re.compile(whereis.rva_regex(["0xa6aec0", "0x18ae40"]), re.I)
check(whereis.fx_tags("struct FakeDc final : public cIGZDrawContext {", "cIGZDrawContext", rv) == {"derives"}, "derives")
check(whereis.fx_tags("if (riid == cIGZDrawContext::kIid)", "cIGZDrawContext", rv) == {"kIid"}, "kIid")
check(whereis.fx_tags("shape::StandIn d{0x18AE40, &X, &Y};", "cX", rv) == {"StandIn", "rva"}, "StandIn over an RVA")
check(whereis.fx_tags("// fakes: cIDisplayList, cIShader (slot tables kDl, kSh)", "cIShader", rv) == {"fakes"}, "fakes comment")
check(whereis.fx_tags("CodePatch p(0x00a6aec0, &Y);", "cX", rv) == {"CodePatch", "rva"}, "CodePatch, leading zeros")
check(whereis.fx_tags("x = 0xa6aec01;", "cX", rv) == set(), "a longer literal is not the RVA")

print(f"\n{len(failures)} failed" if failures else "\nall passed")
sys.exit(1 if failures else 0)
