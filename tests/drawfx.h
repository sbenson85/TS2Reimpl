#pragma once
// drawfx: recording fakes for the 2D drawing interfaces (docs/ui-testing.md).
//
//   drawfx::Fake dc;                                   // one per object the code under test draws into
//   auto orig = drawfx::Side([&] { dc.Reset<fx_slots::cIGZDrawContext>(201); },
//                            [&] { ctx.Original<Fn>(0x92ef70)(&dc, &rect, argb, thickness); });
//   auto ours = drawfx::Side([&] { dc.Reset<fx_slots::cIGZDrawContext>(201); },
//                            [&] { GZWinDrawFrame(&dc, &rect, argb, thickness); });
//   drawfx::Same(ctx, orig.first, ours.first, "frame", seed, orig.second);   // names the first differing call
//
// Interfaces (tests/fx_slots.h, generated from the binary and db/methods by tools/slotsig.py --header):
//   fx_slots::cIGZDrawContext (49 slots), fx_slots::cIDraw2D (nGZGraphic4::cIDraw2D, 44), fx_slots::cIGZFontRenderer
//   (8), fx_slots::cIGZGraphicSystem (69), fx_slots::cIGZWin (173), fx_slots::cIGZWinMgr (48).
// Any other interface: add it to the --header command (docs/ui-testing.md) and regenerate.
#include "drawfx_core.h"
#include "fx_slots.h"
