#include "ui/GZWinHelpers.h"

#include "game/vtable.h"
#include "ifaces/cIGZDrawContext.h"

namespace {

void FillStrip(ifaces::cIGZDrawContext *dc, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    int32_t strip[4] = {left, top, right, bottom};
    dc->FillRect(strip);
}

} // namespace

void __cdecl GZWinDrawFrame(void *context, const int32_t *rect, uint32_t argb, int32_t thickness)
{
    auto *dc = As<ifaces::cIGZDrawContext>(context);
    dc->PushState();
    dc->SetColor(argb);
    const int32_t left = rect[0], top = rect[1], right = rect[2], bottom = rect[3];
    FillStrip(dc, left, top, right, top + thickness);
    FillStrip(dc, left, top + thickness, left + thickness, bottom - thickness);
    FillStrip(dc, right - thickness, top + thickness, right, bottom - thickness);
    FillStrip(dc, left, bottom - thickness, right, bottom);
    dc->PopState();
}

uint32_t __cdecl GZWinShadeColor(uint32_t red, uint32_t green, uint32_t blue, int32_t delta)
{
    auto shade = [delta](uint32_t channel) -> uint32_t {
        const int32_t value = int32_t(channel & 0xff) + delta;
        return value < 0 ? 0u : value > 0xff ? 0xffu : uint32_t(value);
    };
    return 0xff000000u | shade(red) << 16 | shade(green) << 8 | shade(blue);
}

void __cdecl GZWinDrawBevel(void *context, const int32_t *rect, uint32_t argb, uint32_t raised)
{
    auto *dc = As<ifaces::cIGZDrawContext>(context);
    const uint32_t red = argb >> 16 & 0xff, green = argb >> 8 & 0xff, blue = argb & 0xff;
    const int32_t delta = (raised & 0xff) ? 0x38 : -0x38;
    dc->SetColor(argb | 0xff000000u);
    dc->FillRect(const_cast<int32_t *>(rect));
    const int32_t left = rect[0], top = rect[1], right = rect[2], bottom = rect[3];
    dc->SetColor(GZWinShadeColor(red, green, blue, delta));
    FillStrip(dc, left, top, right, top + 1);
    FillStrip(dc, left, top, left + 1, bottom);
    dc->SetColor(GZWinShadeColor(red, green, blue, -delta));
    FillStrip(dc, left + 1, bottom - 1, right, bottom);
    FillStrip(dc, right - 1, top + 1, right, bottom);
}
