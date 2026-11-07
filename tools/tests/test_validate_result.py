#!/usr/bin/env python3
"""Tests for tools/validate_result.py (the rc combinations, the bounded snapshot keep) and for tools/validate.sh's
wiring, run end to end in a temporary repository where every game step, build and lock is a stub. No game.

Run: tools/py -I tools/tests/test_validate_result.py
"""
import itertools
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import snapstore             # noqa: E402
import validate_result as V  # noqa: E402

CLEAN = {
    "deploy_rc": "0", "smoke_rc": "0", "smoke_summary": "summary smoke-1: 18/18 passed; fault codes none",
    "difftest_rc": "0", "difftest_line": "6402 run, 0 failed", "services_rc": "0",
    "services_log": "local/saves/x-services.ts2host.log", "services_line": 'services "main-menu" 12', "traps": "0",
    "hook_pat": "nothing", "hooks": "0", "lotsave_rc": "0", "snapshot": "local/saves/x-saved", "lotcheck_rc": "0",
}
DIAGNOSTIC_REPORT = """\
skip  C:/fixtures/age-0x10/1: binding differs (seed)
  open  Neighborhoods/E001/E001_Neighborhood.package EC44BDDC changed (camera)
age-0x08: MATCH-OPEN (fixture C:/repo\\local\\fixtures\\lot-save\\age-0x08\\1; 1 open differences, within their relation)
diagnostic only: the fixture's binding was backfilled, not captured by its run
"""
UNSEEN_REPORT = """\
closest fixture C:/repo\\local\\fixtures\\lot-save\\age-0x08\\10 (1 differences)
  vary  Neighborhoods/E001/Lots/E001_Lot6.package 4F626A4D/FFFFFFFF/00000001/00000000 changed: new value, only in bytes
age-0x08: UNSEEN
"""


def facts(**changes):
    out = dict(CLEAN, **changes)
    return {k: v for k, v in out.items() if v is not None}


class Decide(unittest.TestCase):
    def test_lot_rc_grid(self):
        """Every lotsave rc and lotcheck rc pair: the table in docs/save-diff.md."""
        expected = {0: V.PASS, 2: V.PASS, 3: V.QUALIFIED, 5: V.QUALIFIED}
        for lotsave, lotcheck in itertools.product((0, 1, 2), (0, 1, 2, 3, 4, 5, 6, -1)):
            code, line = V.decide(facts(lotsave_rc=str(lotsave), lotcheck_rc=str(lotcheck)))
            want = V.FAIL if lotsave else expected.get(lotcheck, V.FAIL)
            self.assertEqual(code, want, (lotsave, lotcheck, line))
            self.assertEqual(line.split(":")[0], {V.PASS: "RESULT PASS", V.QUALIFIED: "RESULT QUALIFIED",
                                                  V.FAIL: "RESULT FAIL"}[want])

    def test_lotsave_nonzero_fails_despite_an_accepted_match(self):
        code, line = V.decide(facts(lotsave_rc="1"))
        self.assertEqual(code, V.FAIL)
        self.assertIn("lotsave rc=1", line)

    def test_smoke_rc_fails_beside_a_clean_summary(self):
        code, line = V.decide(facts(smoke_rc="1"))
        self.assertEqual(code, V.FAIL)
        self.assertIn("smoke rc=1", line)

    def test_smoke_summary_fails_beside_rc_zero(self):
        for summary in ("summary smoke-1: 3/18 passed; fault codes c0000005", "summary smoke-1: 0/0 passed", None):
            self.assertEqual(V.decide(facts(smoke_summary=summary))[0], V.FAIL, summary)

    def test_every_step_rc_fails(self):
        for key in ("deploy_rc", "smoke_rc", "difftest_rc", "services_rc", "lotsave_rc"):
            for value in ("1", "-1", "", None, "x"):
                code, line = V.decide(facts(**{key: value}))
                self.assertEqual(code, V.FAIL, (key, value, line))

    def test_each_failure_is_named(self):
        cases = [(dict(difftest_line="6402 run, 3 failed"), "difftest rc=0: 6402 run, 3 failed"),
                 (dict(difftest_line=None), "no run line"),
                 (dict(services_log=""), "no log written by this run"),
                 (dict(services_line=""), "no services line"),
                 (dict(traps="2"), "traps 2"),
                 (dict(traps=None), "traps None"),
                 (dict(hook_pat="Particles_", hooks="0"), "hook pattern 'Particles_' not seen"),
                 (dict(snapshot=""), "lotsave: no snapshot written by this run"),
                 (dict(lotcheck_rc=None), "lotcheck rc=None")]
        for change, text in cases:
            code, line = V.decide(facts(**change))
            self.assertEqual(code, V.FAIL, change)
            self.assertIn(text, line)

    def test_hook_seen(self):
        self.assertEqual(V.decide(facts(hook_pat="Particles_", hooks="4"))[0], V.PASS)

    def test_no_facts_is_a_failure(self):
        code, line = V.decide({})
        self.assertEqual(code, V.FAIL)
        for step in ("deploy", "smoke", "difftest", "services", "lotsave"):
            self.assertIn(step, line)

    def test_failure_beats_qualified(self):
        code, line = V.decide(facts(lotcheck_rc="5", traps="1"), DIAGNOSTIC_REPORT)
        self.assertEqual(code, V.FAIL)
        self.assertNotIn("QUALIFIED", line)

    def test_diagnostic_names_outcome_fixture_and_reason(self):
        code, line = V.decide(facts(lotcheck_rc="5"), DIAGNOSTIC_REPORT)
        self.assertEqual(code, V.QUALIFIED)
        for text in ("DIAGNOSTIC", "rc=5", "age-0x08 MATCH-OPEN", "lot-save\\age-0x08\\1:",
                     "the fixture's binding was backfilled, not captured by its run"):
            self.assertIn(text, line)

    def test_unseen_names_outcome_fixture_and_reason(self):
        code, line = V.decide(facts(lotcheck_rc="3"), UNSEEN_REPORT)
        self.assertEqual(code, V.QUALIFIED)
        for text in ("UNSEEN", "rc=3", "lot-save\\age-0x08\\10:", "1 difference only where reference runs differ",
                     "E001_Lot6.package 4F626A4D/FFFFFFFF/00000001/00000000"):
            self.assertIn(text, line)

    def test_qualified_without_a_report_still_says_so(self):
        code, line = V.decide(facts(lotcheck_rc="5"))
        self.assertEqual(code, V.QUALIFIED)
        self.assertIn("no reason in the report", line)

    def test_a_lotcheck_abort_shows_its_last_line(self):
        code, line = V.decide(facts(lotcheck_rc="1"), "no host log beside the snapshot: x\n")
        self.assertEqual(code, V.FAIL)
        self.assertIn("lotcheck rc=1: no host log beside the snapshot", line)

    def test_read_facts(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "facts.txt"
            p.write_text("a=1\nhook_pat=x=y|z\nempty=\na=2\n", encoding="utf-8")
            self.assertEqual(V.read_facts(p), {"a": "2", "hook_pat": "x=y|z", "empty": ""})


class Keep(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.runs = self.root / "validation"
        self.store = self.root / "store"
        self.source = self.root / "saves" / "1-saved"
        (self.source / "sub").mkdir(parents=True)
        (self.source / "sub" / "a.package").write_bytes(b"lot")
        self.source.with_name("1-saved.run.json").write_text("{}", encoding="utf-8")
        self.source.with_name("1-saved.ts2host.log").write_text("log", encoding="utf-8")

    def run_dir(self, name):
        d = self.runs / name
        d.mkdir(parents=True)
        return d

    def test_keep_links_and_survives_the_source(self):
        run = self.run_dir("20261106-000000-aaaa")
        line = V.keep(run, self.source, store=self.store)
        self.assertIn("snapshot kept", line)
        kept = run / "snapshot" / "sub" / "a.package"
        self.assertGreater(os.stat(kept).st_nlink, 1)
        self.assertTrue((run / "snapshot.run.json").exists() and (run / "snapshot.ts2host.log").exists())
        shutil.rmtree(self.source)
        snapstore.Store(self.store).gc()
        self.assertEqual(kept.read_bytes(), b"lot")

    def test_bounded_and_keep_marked_are_apart(self):
        names = [f"2026110{n}-000000-aaaa" for n in range(1, 7)]
        for name in names:
            V.keep(self.run_dir(name), self.source, store=self.store, limit=2)
            if name == names[0]:
                (self.runs / name / "KEEP").write_text("milestone", encoding="utf-8")
            (self.runs / name / "facts.txt").write_text("a=1\n", encoding="utf-8")
        held = [n for n in names if (self.runs / n / "snapshot").is_dir()]
        self.assertEqual(held, [names[0], names[4], names[5]])
        self.assertTrue(all((self.runs / n / "facts.txt").exists() for n in names), "the evidence files stay")

    def test_low_disk_keeps_nothing(self):
        run = self.run_dir("20261106-000000-aaaa")
        line = V.keep(run, self.source, store=self.store, min_free_gb=10**9)
        self.assertIn("not kept", line)
        self.assertFalse((run / "snapshot").exists())

    def test_prune_of_nothing(self):
        self.run_dir("20261106-000000-aaaa")
        self.assertEqual(V.prune(self.runs), [])


STUB_PYTHON = r'''#!/bin/bash
# Stands in for the venv's python: the lock and the game-facing tools are canned, the tool under test is real.
case "$2" in
  tools/checkout_lock.py) echo token ;;
  tools/smoke.py) echo "batch smoke"; echo "summary smoke: $STUB_SMOKE_SUMMARY; fault codes none"; exit $STUB_SMOKE_RC ;;
  tools/lotcheck.py) printf "$STUB_LOTCHECK_REPORT"; exit $STUB_LOTCHECK_RC ;;
  *) exec "$REAL_PY" "$@" ;;
esac
'''
STUB_POWERSHELL = r'''#!/bin/bash
# Stands in for powershell.exe: deploy, difftest and the two scenarios.
stamp=$(date +%Y%m%d-%H%M%S)
case "$*" in
  *deploy.ps1*) mkdir -p local/game/EP9/TSBin; echo host > local/game/EP9/TSBin/ts2host.exe; echo deployed; exit 0 ;;
  *difftest.ps1*) mkdir -p build; echo "full output" > build/difftest.out.txt; echo stderr > build/difftest.err.txt
                  echo "6402 run, 0 failed"; exit 0 ;;
  *@services-menu*) printf 'services "main-menu" 12\n' > "local/saves/$stamp-services.ts2host.log"; exit 0 ;;
  *@lot-save-frames*) mkdir "local/saves/$stamp-saved"; echo lot > "local/saves/$stamp-saved/a.package"
                      echo '{"m": 1}' > "local/saves/$stamp-saved.run.json"; echo log > "local/saves/$stamp-saved.ts2host.log"
                      exit $STUB_LOTSAVE_RC ;;
esac
exit 99
'''


def git_bash():
    """Git's bash: the bare name may find WSL's in System32."""
    if os.name != "nt":
        return "bash"
    found = [up / "bin" / "bash.exe" for up in Path(shutil.which("git")).parents if (up / "bin" / "bash.exe").exists()]
    return str(found[0]) if found else "no-bash"


@unittest.skipUnless(os.name != "nt" or Path(git_bash()).exists(), "Git's bash not found")
class Script(unittest.TestCase):
    """tools/validate.sh itself, its stubs under a temporary repository: 4 runs of ~2 s each."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name) / "repo"
        for tool in ("validate.sh", "validate_result.py", "snapstore.py", "tsdb.py"):
            (self.repo / "tools").mkdir(parents=True, exist_ok=True)
            shutil.copy2(TOOLS / tool, self.repo / "tools" / tool)
        (self.repo / ".gitignore").write_text("local/\nbuild/\n.venv/\n", encoding="utf-8", newline="\n")
        (self.repo / "local" / "saves").mkdir(parents=True)
        venv = self.repo / ".venv" / "Scripts"
        venv.mkdir(parents=True)
        (venv / "python").write_text(STUB_PYTHON, encoding="utf-8", newline="\n")
        self.bin = Path(self.tmp.name) / "bin"
        self.bin.mkdir()
        (self.bin / "powershell").write_text(STUB_POWERSHELL, encoding="utf-8", newline="\n")
        env = dict(os.environ, GIT_CEILING_DIRECTORIES=self.tmp.name)
        for k in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE"):
            env.pop(k, None)
        for args in (["init", "-q"], ["add", "."], ["-c", "user.name=t", "-c", "user.email=t@example.com",
                                                    "commit", "-q", "-m", "x"]):
            subprocess.run(["git", *args], cwd=self.repo, check=True, env=env, stdout=subprocess.DEVNULL)
        self.env = env

    def validate(self, *args, smoke_rc=0, summary="18/18 passed", lotsave_rc=0, lotcheck_rc=0, report="age-0x08: MATCH\n"):
        env = dict(self.env, PATH=str(self.bin) + os.pathsep + self.env["PATH"], REAL_PY=sys.executable,
                   STUB_SMOKE_RC=str(smoke_rc), STUB_SMOKE_SUMMARY=summary, STUB_LOTSAVE_RC=str(lotsave_rc),
                   STUB_LOTCHECK_RC=str(lotcheck_rc), STUB_LOTCHECK_REPORT=report.replace("\\", "\\\\").replace("\n", "\\n"))
        done = subprocess.run([git_bash(), "tools/validate.sh", "out.txt", "nothing", *args], cwd=self.repo, env=env,
                              capture_output=True, text=True, timeout=120)
        out = (self.repo / "out.txt").read_text(encoding="utf-8")
        run = next(line.split()[1] for line in out.splitlines() if line.startswith("evidence "))
        return done.returncode, out, self.repo / run

    def test_clean_run_is_pass_with_its_evidence_and_no_snapshot(self):
        (self.repo / "dirty.txt").write_text("x", encoding="utf-8")
        code, out, run = self.validate("local/validation/earlier")
        self.assertEqual(code, 0, out)
        self.assertIn("\nRESULT PASS\n", out)
        self.assertTrue(out.endswith("DONE\n"))
        got = V.read_facts(run / "facts.txt")
        self.assertEqual(got["retry_of"], "local/validation/earlier")
        self.assertEqual(len(got["host_sha256"]), 64)
        self.assertEqual(len(got["head"]), 40)
        self.assertIn("?? dirty.txt", (run / "tree-status.txt").read_text(encoding="utf-8"))
        self.assertEqual((run / "difftest.out.txt").read_text(encoding="utf-8"), "full output\n")
        self.assertEqual((run / "lotcheck.txt").read_text(encoding="utf-8"), "age-0x08: MATCH\n")
        self.assertEqual(json.loads((run / "run.json").read_text(encoding="utf-8")), {"m": 1})
        self.assertEqual((run / "result.txt").read_text(encoding="utf-8"), "RESULT PASS\n")
        self.assertIn("RESULT PASS", (run / "steps.txt").read_text(encoding="utf-8"))
        self.assertFalse((run / "snapshot").exists())

    def test_diagnostic_is_qualified_and_keeps_its_snapshot(self):
        code, out, run = self.validate(lotcheck_rc=5, report=DIAGNOSTIC_REPORT)
        self.assertEqual(code, 2, out)
        self.assertIn("RESULT QUALIFIED: DIAGNOSTIC (lotcheck rc=5; age-0x08 MATCH-OPEN)", out)
        self.assertIn("snapshot kept", out)
        self.assertTrue((run / "snapshot" / "a.package").exists())

    def test_lotsave_rc_fails_the_run(self):
        code, out, run = self.validate(lotsave_rc=1)
        self.assertEqual(code, 1, out)
        self.assertIn("RESULT FAIL: lotsave rc=1", out)
        self.assertTrue((run / "snapshot").exists())

    def test_smoke_rc_fails_the_run(self):
        code, out, _ = self.validate(smoke_rc=1)
        self.assertEqual(code, 1, out)
        self.assertIn("RESULT FAIL: smoke rc=1", out)


if __name__ == "__main__":
    unittest.main()
