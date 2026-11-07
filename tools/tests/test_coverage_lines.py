#!/usr/bin/env python3
"""Tests for tools/coverage_lines.py's report parsing and formatting. Run:

  tools/py -I tools/tests/test_coverage_lines.py
"""
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import coverage_lines as cl  # noqa: E402

failures = []


def check(cond, what):
    if not cond:
        failures.append(what)


COBERTURA = """<?xml version="1.0"?>
<coverage line-rate="0.5"><packages><package name="p"><classes>
<class name="a" filename="Users\\me\\wt\\src\\ts\\a.cpp"><methods/><lines>
<line number="10" hits="1"/><line number="11" hits="0"/></lines></class>
<class name="a2" filename="Users\\me\\wt\\src\\ts\\a.cpp"><methods/><lines>
<line number="10" hits="0"/><line number="11" hits="3"/><line number="12" hits="0"/></lines></class>
<class name="b" filename="Users\\me\\wt\\src\\other\\ts\\a.cpp"><methods/><lines>
<line number="99" hits="0"/></lines></class>
<class name="c" filename="Users\\me\\wt\\src\\ts\\aa.cpp"><methods/><lines>
<line number="98" hits="0"/></lines></class>
</classes></package></packages></coverage>"""

# A file's two listings merge, a hit winning; a path that only ends alike or shares a prefix is another file.
lines = cl.parse_lines(COBERTURA, "src/ts/a.cpp")
check(lines == {10: 1, 11: 3, 12: 0}, f"parse_lines: {lines}")
check(cl.parse_lines(COBERTURA, "src\\ts\\a.cpp") == lines, "parse_lines takes backslashes")
check(cl.parse_lines(COBERTURA, "src/ts/none.cpp") == {}, "parse_lines: no such file")

# Lines with no code between missed lines join them; a line that ran, or a run of code that ran, splits them.
check(cl.missed_ranges({1: 0, 2: 0, 5: 0, 6: 1, 7: 0}) == [(1, 5), (7, 7)], "missed_ranges joins across no-code lines")
check(cl.missed_ranges({1: 1, 2: 2}) == [], "missed_ranges: all ran")
check(cl.missed_ranges({}) == [], "missed_ranges: empty")
check(cl.missed_ranges({9: 0, 3: 0, 4: 1}) == [(3, 3), (9, 9)], "missed_ranges sorts")

SOURCE = """struct Ref {
    ~Ref()
    {
        if (p) p->Release();
    }
};
int f(int a)
{
    if (a) {
        return 1;
    }

    return 2;
}""".split("\n")

check(cl.is_destructor(SOURCE, 4, 4), "is_destructor: a body under its ~ line")
check(not cl.is_destructor(SOURCE, 10, 10), "is_destructor: an ordinary function")
check(not cl.is_destructor(SOURCE, 4, 30), "is_destructor: a long range isn't a destructor")
check(not cl.is_destructor(SOURCE, 13, 13), "is_destructor: ~ further back doesn't count")
check(cl.is_whole_function(SOURCE, 8) and cl.is_whole_function(SOURCE, 9), "is_whole_function: from the brace or after it")
check(not cl.is_whole_function(SOURCE, 10), "is_whole_function: a line inside a function that ran")
check(not cl.is_whole_function(SOURCE, 4), "is_whole_function: an indented brace isn't a function's")
check(cl.snippet(SOURCE, 3, 5) == "if (p) p->Release();", "snippet skips a bare brace")
check(cl.snippet(SOURCE, 11, 12) == "", "snippet of a range with no code")

covered = {4: 0, 9: 1, 10: 0, 13: 1}
text = cl.report("src/x.cpp", covered, SOURCE)
check(text[0] == "src/x.cpp: 4 lines with code, 2 missed", f"report totals: {text[0]}")
check(text[1].split() == ["10", "return", "1;"], f"report range: {text[1]}")
check("1 destructor range(s) hidden" in text[2], f"report hides a destructor: {text}")
check(len(text) == 3, f"report length: {text}")
shown = cl.report("src/x.cpp", covered, SOURCE, show_all=True)
check(len(shown) == 3 and "if (p)" in shown[1] and "return 1" in shown[2], f"report --all: {shown}")
check(cl.report("src/x.cpp", {1: 1}, SOURCE) == ["src/x.cpp: 1 lines with code, 0 missed"], "report: clean file")
check(cl.report("src/x.cpp", {7: 0, 8: 0, 9: 1}, SOURCE)[1].split() == ["7-8", "int", "f(int", "a)"], "report: a range's form")
check(cl.report("src/x.cpp", {8: 0, 9: 0, 13: 1}, SOURCE)[1].split()[:2] == ["8-9", "fn"], "report tags a whole function")
check(cl.report("src/x.cpp", {7: 0, 8: 0}, SOURCE)[1:] == ["  the filter never reaches this file"], "report: file not reached")

check(cl.summary_line("[pass] x\n2 run, 0 failed\n") == "2 run, 0 failed", "summary_line")
check(cl.summary_line("[pass] x\n") is None, "summary_line: a run that died")

if failures:
    print("\n".join(failures))
    sys.exit(1)
print("coverage_lines: all checks passed")
