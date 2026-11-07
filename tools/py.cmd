@echo off
rem The main checkout's venv Python, from the main checkout or any worktree (PowerShell or cmd):
rem   tools\py.cmd -I tools/status.py --check
for /f "delims=" %%G in ('git rev-parse --path-format^=absolute --git-common-dir') do set "TS2_GITDIR=%%G"
"%TS2_GITDIR%\..\.venv\Scripts\python.exe" %*
