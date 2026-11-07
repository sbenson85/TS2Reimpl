#!/bin/bash
# The coordinator's in-game validation of a merge (docs/orchestrator.md, "Running the game copy"): deploy, the smoke
# batch, the full difftest, the services check, then the seeded lot save, all on the checked-out commit. Run it in the
# background from the main checkout. It holds the checkout lock (tools/checkout_lock.py) for the whole run, so builds,
# deploys and mutation runs started meanwhile refuse instead of racing it.
# usage: tools/validate.sh <out file> <hook-grep> [retry_of]
#   <hook-grep>: an extended regex counted in the services run's host log (a hook name to see it ran; "nothing" for none).
#   retry_of: the run folder this run repeats, when it does.
# Each step records its exit code ("step <name> rc=N"); the run ends with RESULT PASS, RESULT QUALIFIED: <reasons> or
# RESULT FAIL: <reasons> (exit 0, 2, 1), then DONE (DONE only means it finished). tools/validate_result.py decides, from
# the facts this script records. QUALIFIED records evidence and never authorises a merge by itself, and while no lot
# fixture has a captured binding every run reads QUALIFIED at best: docs/orchestrator.md, "Validation results".
# Evidence stays in local/validation/<stamp>-<hash>/ (same page). Read the result with:
#   grep -E "^(summary|step|RESULT|evidence|age-|lotcheck|snapshot)|run,|services \"|^(traps|hooks) " <out>
#   grep -E "^\[(fail|error)\]" -A1 build/difftest.out.txt     (or difftest.out.txt in the run's folder)
cd "$(dirname "$0")/.."
OUT=$1; PAT=$2; RETRY=$3
H=$(git rev-parse --short=8 HEAD)
PY=.venv/Scripts/python
WINPID=$(cat /proc/$$/winpid 2>/dev/null || echo $$)
TOKEN=$($PY -I tools/checkout_lock.py acquire --what "validate.sh $OUT at $H" --pid "$WINPID") || {
    echo "RESULT FAIL: checkout busy (tools/checkout_lock.py show)" > "$OUT"; echo DONE >> "$OUT"; exit 1; }
export TS2_CHECKOUT_LOCK=$TOKEN
release_lock() { $PY -I tools/checkout_lock.py release --token "$TOKEN"; }
trap release_lock EXIT

RUN=local/validation/$(date -u +%Y%m%d-%H%M%S)-$H
mkdir -p "$RUN"
fact() { printf '%s=%s\n' "$1" "$2" >> "$RUN/facts.txt"; }
{
echo "HEAD $H"; date -u +%H:%M:%S; echo "evidence $RUN"
# Each run deploys the working tree, so HEAD alone isn't the identity.
git status --porcelain > "$RUN/tree-status.txt"
fact head "$(git rev-parse HEAD)"; fact hook_pat "$PAT"; fact retry_of "$RETRY"

DEPLOY=$(powershell -NoProfile -File tools/deploy.ps1 2>&1); RC=$?
echo "$DEPLOY" | tail -1; echo "step deploy rc=$RC"
fact deploy_rc $RC; fact host_sha256 "$(sha256sum local/game/EP9/TSBin/ts2host.exe | cut -d' ' -f1)"

SMOKE=$($PY -I tools/smoke.py --scenario lot-simtrace-108 --scenario lot-simtrace --scenario lot-townie-trace --scenario lot-resave --scenario own-start-menu --scenario cas --repeats 3 --no-build --reset-userdata --seed 0x5eed0001 --batch smoke-$H --timeout 1500 --keep-batches 3 2>&1); RC=$?
echo "$SMOKE" | grep -E "^(batch|summary)"; echo "step smoke rc=$RC"
echo "$SMOKE" > "$RUN/smoke.txt"
fact smoke_rc $RC; fact smoke_summary "$(echo "$SMOKE" | grep -E "^summary" | tail -1)"
date -u +%H:%M:%S

rm -f build/difftest.out.txt build/difftest.err.txt     # a failed build must not leave the last run's as this one's
DT=$(powershell -NoProfile -File tools/difftest.ps1 -Summary -Jobs 4 2>&1); RC=$?
echo "$DT" | tail -3; echo "step difftest rc=$RC"
echo "$DT" > "$RUN/difftest-summary.txt"; cp build/difftest.out.txt build/difftest.err.txt "$RUN/" 2>/dev/null
fact difftest_rc $RC; fact difftest_line "$(echo "$DT" | grep -E "[0-9]+ run, [0-9]+ failed" | tail -1)"

# The services run's own log: the newest one written after this marker (an older run's log must never stand in).
MARK=$(mktemp); sleep 1
powershell -NoProfile -File tools/scenario.ps1 @services-menu -ResetUserData -Quiet -NoDeploy > /dev/null 2>&1; RC=$?
echo "step services rc=$RC"; fact services_rc $RC
L=$(find local/saves -maxdepth 1 -name '*-services.ts2host.log' -newer "$MARK" -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | cut -d' ' -f2-)
rm -f "$MARK"
fact services_log "$L"
if [ -n "$L" ]; then
    echo "$L"
    SVC=$(grep 'services "main-menu"' "$L" | tail -1); echo "$SVC"; fact services_line "$SVC"
    TRAPS=$(grep -c "trap: original" "$L"); echo "traps $TRAPS"; fact traps "$TRAPS"
    HOOKS=$(grep -cE "$PAT" "$L"); echo "hooks $HOOKS"; fact hooks "$HOOKS"
fi

# The seeded lot save against the recorded reference saves (docs/save-diff.md, lotcheck; its outcome table gives each
# outcome's result). Added 2026-11-04: two seed-control gaps (our cGZCOM's GUID clock, the CAS clock seeds) went unseen
# for two weeks without it.
MARK=$(mktemp); sleep 1
powershell -NoProfile -File tools/scenario.ps1 @lot-save-frames -ResetUserData -Quiet -Seed 0x5eed0001 -NoDeploy > /dev/null 2>&1; RC=$?
echo "step lotsave rc=$RC"; fact lotsave_rc $RC
S=$(find local/saves -maxdepth 1 -type d -name '*-saved' -newer "$MARK" -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | cut -d' ' -f2-)
rm -f "$MARK"
fact snapshot "$S"
if [ -n "$S" ]; then
    LC=$($PY -I tools/lotcheck.py check "$S" 2>&1); LRC=$?
    echo "$LC" | grep -E "^age-|DIFF" | head -12; echo "lotcheck rc=$LRC ($S)"
    echo "$LC" > "$RUN/lotcheck.txt"; cp "$S.run.json" "$RUN/run.json" 2>/dev/null
    fact lotcheck_rc $LRC
fi

date -u +%H:%M:%S
R=$($PY -I tools/validate_result.py decide "$RUN/facts.txt" "$RUN/lotcheck.txt")
[ -n "$R" ] || R="RESULT FAIL: tools/validate_result.py gave no result"
echo "$R"; echo "$R" > "$RUN/result.txt"
# Only a run that isn't a clean PASS holds on to its snapshot, and only the newest few do.
[ "$R" = "RESULT PASS" ] || [ -z "$S" ] || $PY -I tools/validate_result.py keep "$RUN" "$S"
cp "$OUT" "$RUN/steps.txt"
echo DONE
} > "$OUT" 2>&1
case "$(cat "$RUN/result.txt" 2>/dev/null)" in "RESULT PASS") exit 0 ;; "RESULT QUALIFIED"*) exit 2 ;; *) exit 1 ;; esac
