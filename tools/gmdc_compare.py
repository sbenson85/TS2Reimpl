"""Compares the hooked game's GMDC decode with the acceptance test's expected summaries (milestone N-f).

    tools/py -I tools/gmdc_compare.py HOST_LOG EXPECTED [--show 20]

HOST_LOG is the host's log (ts2host.log) from a run with TS2HOST_GMDC=1: one `gmdc-expected - <instance> <group>
<resource> <summary>` line per decoded GMDC (src/hook/gmdc_diag.cpp). EXPECTED is any text holding the acceptance test's
`gmdc-expected <package> <instance> <group> <resource> <summary>` lines (the output of the gmdc_decode_acceptance
difftest). Lines are found anywhere in a line (the host log has a timestamp in front) and paired by the key
(instance, group, resource); the package is ignored. A key the host decoded several times counts once when every
decode agrees, and as differing when they disagree.

Prints: matched (same summary), differing (same key, other summary; each shown), only-expected (the scenario never
decoded it: not an error by itself), only-host (not in the expected list: not an error). Exit 0 when there is at least
one match and no difference, 1 otherwise.
"""
import argparse
import re
import sys

LINE = re.compile(
    r"gmdc-expected\s+(\S+)\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+"
    r"(E\d+:[0-9a-f]{16} L\d+:[0-9a-f]{16} G\d+:[0-9a-f]{16} T\d+:[0-9a-f]{16} N\d+:[0-9a-f]{16} "
    r"S\d+:[0-9a-f]{16} B\d+:[0-9a-f]{16} all:[0-9a-f]{16})"
)


def parse(text):
    """{(instance, group, resource): set of summaries}, in first-seen order."""
    out = {}
    for line in text.splitlines():
        m = LINE.search(line)
        if m:
            out.setdefault(m.group(2, 3, 4), set()).add(m.group(5))
    return out


def compare(host, expected):
    matched, differing = [], []
    for key, sums in host.items():
        if key not in expected:
            continue
        if sums == expected[key]:
            matched.append(key)
        else:
            differing.append(key)
    only_host = [k for k in host if k not in expected]
    only_expected = [k for k in expected if k not in host]
    return matched, differing, only_host, only_expected


def fmt(key):
    return "instance %s group %s resource %s" % key


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("host_log")
    ap.add_argument("expected")
    ap.add_argument("--show", type=int, default=20, help="how many keys to list per category")
    args = ap.parse_args(argv)
    with open(args.host_log, encoding="utf-8", errors="replace") as f:
        host = parse(f.read())
    with open(args.expected, encoding="utf-8", errors="replace") as f:
        expected = parse(f.read())
    matched, differing, only_host, only_expected = compare(host, expected)
    print("host keys %d, expected keys %d" % (len(host), len(expected)))
    print("matched %d, differing %d, only-expected %d, only-host %d"
          % (len(matched), len(differing), len(only_expected), len(only_host)))
    for key in differing[: args.show]:
        print("DIFF " + fmt(key))
        for s in sorted(host[key]):
            print("  host     " + s)
        for s in sorted(expected[key]):
            print("  expected " + s)
    for title, keys in (("only-expected", only_expected), ("only-host", only_host)):
        for key in keys[: args.show]:
            print("%s %s" % (title, fmt(key)))
    return 0 if matched and not differing else 1


if __name__ == "__main__":
    sys.exit(main())
