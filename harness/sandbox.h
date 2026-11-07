#pragma once
// File-system sandbox for the original's file imports (harness/sandbox.cpp).
#include <string>

// Replacement for a file import (KERNEL32 CreateFileW, ReadFile, ...) that only works inside the
// sandbox directory and on handles opened there; nullptr if the import isn't sandboxed.
void *SandboxedImport(const std::string &dll, const std::string &fn);
bool InitSandbox();
void CleanupSandbox();

// Raises the import trap naming `what` (defined in image.cpp).
[[noreturn]] void TrapImport(const std::string &what);
