// nTSSG::cTSSGSystem: capturing the post-effects render target (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "rz/StaticGetters.h"
#include "sg/cTextureManager.h"

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// Game text: the render target's name.
constexpr uint32_t kPostEffectsTexture = 0xdae0e0;
// The interface the render target is asked for (its slot 0).
constexpr uint32_t kTextureIid = 0x0b96cc22;
// The framework classes the two surfaces are made from.
constexpr uint32_t kFullClass = 0x3b75b71b;
constexpr uint32_t kFullIid = 0x1b75b70b;
constexpr uint32_t kTileClass = 0x4dee64d7;
constexpr uint32_t kTileIid = 0x4dee64cf;
// Tiles are 128 pixels square; areas from 0x24000 pixels up are copied in one go.
constexpr uint32_t kTile = 0x80;
constexpr uint32_t kTiledLimit = 0x24000;

// The texture manager (0xae8750, ours: sg/cTextureManager.h) and image service (0x18d120, gz/GraphicServices.h).
void *TextureService() { return nGZSceneGraph::TextureManager(); }
void *ImageService() { return nGZGraphic4::GetImageManipCached(); }

// The texture manager's named handle (0xae8280 builds it from a name, 0xae8730 gives its object, 0xae82b0 ends it).
using TextureHandle = nGZSceneGraph::cTextureHandle;

// A reference released (slot 2) when the frame leaves.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held()
    {
        if (object) Call<2>(object);
    }
};

void Drop(void *&ref)
{
    if (void *old = ref) {
        ref = nullptr;
        Call<2>(old);
    }
}

bool Valid(void *surface) { return Call<6, uint8_t>(surface) != 0; }

struct Point {
    int32_t x, y;
};
struct Rect {
    int32_t left, top, right, bottom;
};

} // namespace

namespace nTSSG {

uint8_t cTSSGSystem::CapturePostEffects(const int32_t *rect, uint32_t a, uint32_t b)
{
    const char *name = GameAddress<const char>(kPostEffectsTexture);
    if (!Call<25, uint8_t>(TextureService(), name) || mCaptureSuspended) return 0;
    TextureHandle handle(name);
    Held texture;
    if (void *source = handle.Get()) Call<0>(source, kTextureIid, &texture.object);
    if (!texture.object || !Valid(texture.object)) return 0;

    const uint32_t width = Call<17>(Call<8, void *>(texture.object, 0u), 0u);
    const uint32_t height = Call<18>(Call<8, void *>(texture.object, 0u), 0u);
    const uint32_t format = Call<19>(Call<8, void *>(texture.object, 0u));
    const uint32_t kind = Call<5>(Call<8, void *>(texture.object, 0u));

    void *&full = mRef2C8.mObject;
    void *&tile = mRef2CC.mObject;
    // Surfaces that no longer fit the render target are shut down (slot 3) and dropped.
    if (full && Valid(full) &&
        (Call<20>(full) != width || Call<21>(full) != height || Call<22>(full) != format || Call<5>(full) != kind)) {
        Call<3>(full);
        Drop(full);
    }
    if (tile && Valid(tile) && (Call<22>(tile) != format || Call<5>(tile) != kind)) {
        Call<3>(tile);
        Drop(tile);
    }
    if (!full || !Valid(full)) {
        void *framework = GetDirectorFrameworkObject();
        Drop(full);
        Call<3>(framework, kFullClass, kFullIid, &full);
        Call<29>(full, kind, width, height, format, 1u);
    }
    if (!tile || !Valid(tile)) {
        void *framework = GetDirectorFrameworkObject();
        Drop(tile);
        Call<3>(framework, kTileClass, kTileIid, &tile);
        Call<29>(tile, kind, kTile, kTile, format);
    }
    if (!Valid(full) || !Valid(tile)) return 0;

    bool copied = false;
    if (uint32_t(rect[2] - rect[0]) * uint32_t(rect[3] - rect[1]) < kTiledLimit) {
        // Tile by tile: the render target's part into the tile's corner, then the tile into the full surface.
        copied = true;
        for (uint32_t row = 0; copied && row < uint32_t(rect[3] - rect[1]); row += kTile) {
            for (uint32_t column = 0; column < uint32_t(rect[2] - rect[0]); column += kTile) {
                uint32_t across = uint32_t(rect[2] - rect[0]) - column;
                if (across > kTile) across = kTile;
                uint32_t down = uint32_t(rect[3] - row) - uint32_t(rect[1]);
                if (down > kTile) down = kTile;
                const Rect part{int32_t(column), int32_t(row), int32_t(across + column), int32_t(down + row)};
                const Point corner{0, 0};
                if (!Call<15, uint8_t>(tile, &corner, Call<17, void *>(texture.object, 0u), &part)) {
                    copied = false;
                    break;
                }
                const Point at{int32_t(column), int32_t(row)};
                const Rect whole{0, 0, int32_t(across), int32_t(down)};
                if (!Call<15, uint8_t>(full, &at, tile, &whole)) {
                    copied = false;
                    break;
                }
            }
        }
    }
    if (!copied && !Call<16, uint8_t>(full, Call<17, void *>(texture.object, 0u))) return 0;

    void *out = nullptr; // never released (original)
    return Call<3, uint8_t>(ImageService(), full, rect, a, b, &out) ? 1 : 0;
}

} // namespace nTSSG
