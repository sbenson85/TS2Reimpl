#pragma once
#include <cstdint>

// Logs the game process's first faults (see faultlog.cpp). imageBase/imageSize: the game image, so
// addresses inside it are printed as RVAs.
void StartFaultLog(uintptr_t imageBase, uint32_t imageSize);
