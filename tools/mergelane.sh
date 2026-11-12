#!/bin/bash
# The coordinator's merge of a lane branch as one background run (docs/orchestrator.md, "Merging a lane"): the steps
# that need no judgement, in order, stopping at the first that fails. The judgement stays outside: read the diff and
# the guard warnings first (`pre`), write the merge message and the coordinator mutants, then `run`.
#
# usage: tools/mergelane.sh pre <branch> [--tooling]
#          guard, testweaken and the diff stat, to read before merging.
#        tools/mergelane.sh run <branch> <out file> <hook-grep|-> [mutants.json filter] [--tooling]
#          guard and testweaken again (errors or weakening candidates stop it), merge with local/merge-msg.txt (it must
#          name the branch on a Lane-branch: line), annotations check, build (host, hook tables), the post-merge
#          checks, commit db/subsystems.jsonl if it changed, the mutants (when given: tools/mutate.py --filter), then
#          tools/validate.sh <out>.validate <hook-grep> (deploy, smoke, the full difftest, services, lot save) unless
#          the hook-grep is "-" (test-only or tooling merges: the full difftest alone then). On RESULT PASS the lane's
#          worktree and branch are removed. The summary is the lines starting "== "; the run ends with MERGELANE DONE.
# Read the result with:  grep -E "^(== |STOP|MERGELANE)" <out file>
cd "$(dirname "$0")/.."
PY=".venv/Scripts/python -I"
MODE=$1; B=$2
TOOLING="${GUARD_EXTRA:-}";   # GUARD_EXTRA="--paths db/x.jsonl": paths the task gave the lane
TOOLING_FLAG=""; for a in "$@"; do [ "$a" = "--tooling" ] && TOOLING="--tooling"; done

# Stale original_calls (a caller of ours still declaring a function the branch verifies) fail the post-merge checks:
# look in the branch's worktree before merging (a hint: entries main has fixed since the branch forked show too; `run` checks the merged tree).
stale() {
    local wt; wt=$(git worktree list --porcelain | awk -v b="refs/heads/$B" '/^worktree /{w=$2} $1=="branch" && $2==b {print w}')
    [ -n "$wt" ] || { echo "0 stale original_calls declaration(s) (no worktree for $B: not checked)"; return 0; }
    (cd "$wt" && "$OLDPWD/.venv/Scripts/python" -I tools/origcalls.py --stale 2>&1 | tail -12 | cut -c1-240)
}

pre() {
    stale
    $PY tools/guard.py "$B" $TOOLING 2>&1 | grep -v -E "GameAddress use (data/vtables only)|: calls original 0x" | cut -c1-300
    $PY tools/testweaken.py "$B" 2>&1 | tail -3
    git diff --stat "main...$B" | tail -25
    # A status group new in the branch needs a rule in db/subsystem-groups.jsonl, or the post-merge check-groups fails.
    for f in $(git diff --name-only --diff-filter=A "main...$B" -- db/status); do
        g=$(basename "$f" .jsonl)
        { cat db/subsystem-groups.jsonl; git show "$B:db/subsystem-groups.jsonl" 2>/dev/null; } | grep -q "\"group\": \"$g\"" \
            || echo "NEW GROUP $g has no rule in db/subsystem-groups.jsonl: add one on main before the run"
    done
}

if [ "$MODE" = "pre" ]; then pre; exit 0; fi
[ "$MODE" = "run" ] || { echo "usage: mergelane.sh pre|run <branch> ..."; exit 2; }
OUT=$3; PAT=$4; MUT=""; FILTER=""
if [ -n "$5" ] && [ "$5" != "--tooling" ]; then MUT=$5; FILTER=$6; fi
# The hook-grep must name a hook this branch's tables install (hook names are the tables' first strings, e.g.
# nGZSceneGraph_FindNodeByName, not the group's name): a pattern matching nothing fails the validation at its end.
if [ -n "$PAT" ] && [ "$PAT" != "-" ] && [ "$PAT" != "nothing" ]; then
    git grep -q -E "\{\"[^\"]*($PAT)" "$B" -- src/hook/tables || { echo "hook-grep '$PAT' matches no hook name in $B's src/hook/tables"; exit 2; }
fi
[ -n "$OUT" ] && [ -n "$PAT" ] || { echo "usage: mergelane.sh run <branch> <out> <hook-grep|-> [mutants.json filter] [--tooling]"; exit 2; }

{
stop() { echo "STOP: $1"; echo "MERGELANE DONE"; exit 1; }
step() { echo; echo "---- $1"; }
date -u +"== start %H:%M:%SZ, branch $B"

step guard
G=$($PY tools/guard.py "$B" $TOOLING 2>&1); echo "$G" | tail -3
echo "$G" | tail -1 | grep -q ", 0 errors" || stop "guard reports errors"
W=$($PY tools/testweaken.py "$B" 2>&1); echo "$W" | tail -2
echo "$W" | grep -q "^0 test-weakening candidates" || stop "testweaken has candidates"

step merge
[ -z "$(git status --porcelain --untracked-files=no)" ] || stop "the main checkout has uncommitted changes"
grep -q "^Lane-branch: $B\$" local/merge-msg.txt || stop "local/merge-msg.txt has no 'Lane-branch: $B' line"
# Merge without committing, so stale original_calls (which fail the post-merge checks) stop it before the commit; the
# merged tree is what counts (the branch alone may show entries main has fixed since it forked).
git merge --no-ff --no-commit -q "$B" || { git merge --abort; stop "git merge failed (resolve by hand)"; }
S=$($PY tools/origcalls.py --stale 2>&1); echo "$S" | tail -3 | cut -c1-240
echo "$S" | tail -1 | grep -q "^0 stale" || { git merge --abort; stop "stale original_calls after merging: switch those callers in the branch first"; }
git commit -q -F local/merge-msg.txt || stop "git commit of the merge failed"
echo "== merged $(git log --oneline -1 | cut -c1-110)"
$PY tools/annotations_check.py 2>&1 | tail -2

step build
powershell -NoProfile -Command ". .\tools\common.ps1; Invoke-Build ts2host; if (\$LASTEXITCODE) { exit 1 }; Invoke-Build ts2hooktables; exit \$LASTEXITCODE" > local/mergelane-build.out 2>&1 \
    || { tail -30 local/mergelane-build.out; stop "build failed (local/mergelane-build.out)"; }
echo "== build ok"

step checks
BAD=0
check() { # name, command...: prints the tail; a non-zero exit is counted
    local name=$1; shift
    local o; o=$("$@" 2>&1); local rc=$?
    echo "[$name rc=$rc] $(echo "$o" | tail -2 | cut -c1-220)"
    [ $rc -eq 0 ] || BAD=$((BAD + 1))
}
check write-traps $PY tools/callers.py --write-traps
check subsystems $PY tools/subsystems.py
check check-traps $PY tools/callers.py --check-traps
check status $PY tools/status.py --check
check origcalls $PY tools/origcalls.py --stale
check ownedcheck $PY tools/ownedcheck.py
check check-groups $PY tools/subsystems.py --check-groups
check drift $PY tools/matchcheck.py --drift
echo "== checks: $BAD failed"
if ! git diff --quiet -- src/hook/trap_table.cpp; then echo "== trap table changed: review and commit it, then rebuild"; BAD=$((BAD + 1)); fi
if ! git diff --quiet -- db/subsystems.jsonl; then
    git add db/subsystems.jsonl && git commit -q -m "db: subsystems after merging $B

Co-Authored-By: Claude <noreply@anthropic.com>" && echo "== committed db/subsystems.jsonl"
fi
[ $BAD -eq 0 ] || stop "post-merge checks failed"

if [ -n "$MUT" ]; then
    step mutants
    $PY tools/mutate.py "$MUT" --filter "$FILTER" --timeout 300 --log "${MUT%.json}.run" > "${MUT%.json}.out" 2>&1
    grep -E "^ ?[0-9]+\. " "${MUT%.json}.out" | grep -v "killed  " | cut -c1-200
    echo "== mutants: $(grep -E "mutants, " "${MUT%.json}.out" | tail -1)"
    grep -q "MUTATE DONE" "${MUT%.json}.out" || stop "the mutation run did not finish (${MUT%.json}.out)"
    [ -z "$(git status --porcelain --untracked-files=no)" ] || stop "the mutation run left the tree changed"
fi

if [ "$PAT" = "-" ]; then
    step difftest
    DT=$(powershell -NoProfile -File tools/difftest.ps1 -Summary 2>&1); echo "$DT" | tail -3
    echo "== suite: $(echo "$DT" | grep -E "[0-9]+ run, [0-9]+ failed" | tail -1)"
    echo "$DT" | grep -qE "[0-9]+ run, 0 failed" || stop "the suite failed"
    RESULT="RESULT PASS"
else
    step validate
    bash tools/validate.sh "$OUT.validate" "$PAT" > /dev/null 2>&1
    grep -E "^(evidence|summary|RESULT|lotcheck|age-)|run,|^(traps|hooks) " "$OUT.validate" | cut -c1-300 | sed 's/^/== /'
    RESULT=$(grep -E "^RESULT" "$OUT.validate" | tail -1)
fi

if echo "$RESULT" | grep -q "^RESULT PASS"; then
    step cleanup
    WT=$(git worktree list --porcelain | awk -v b="refs/heads/$B" '/^worktree /{w=$2} $1=="branch" && $2==b {print w}')
    [ -n "$WT" ] && git worktree remove -f -f "$WT" && echo "== removed worktree $WT"
    git branch -d "$B" 2>&1 | tail -1
else
    echo "== not PASS: the worktree and branch are kept; write the disposition, then remove them by hand"
fi
date -u +"== end %H:%M:%SZ"
echo "MERGELANE DONE"
} > "$OUT" 2>&1
