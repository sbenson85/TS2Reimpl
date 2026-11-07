#pragma once
// Fakes for the stream-facing functions of the SimAntics frame and stack (Read, Write, GetTreeName): a fixture of
// recording objects (input and output streams, the stream's version context, owner reader and writer, a text sink and a
// tree owner), measured against the fixture's own address like the world of simantics_treesim.h.
#include "simantics_treesim.h"

namespace tsw {

struct IoFixture {
    void *vtIn[24];
    void *vtOut[24];
    void *vtCtx[100];
    void *vtOwnerW[8];
    void *vtOwnerR[8];
    void *vtSink[8];
    void *vtOwner[48];
    struct StreamCell : Cell {
        uint32_t counter;
        uint32_t error;
    } in, out;
    struct CtxCell : Cell {
        uint32_t version;
    } ctx;
    Cell ownerWriter, ownerReader, sink;
    OwnerCell owner;
    uint32_t ownerSlot;
    uint32_t canary;
};

inline IoFixture *gIo = nullptr;

TSW_FN1(InGetByte, p)
{
    Rec(self, 4, {});
    *reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(p)) = static_cast<uint8_t>(Mix(gSeed, static_cast<IoFixture::StreamCell *>(self)->counter++, 4));
    return 1;
}
TSW_FN1(InGetWord, p)
{
    Rec(self, 6, {});
    *reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(p)) = static_cast<uint16_t>(Mix(gSeed, static_cast<IoFixture::StreamCell *>(self)->counter++, 6));
    return 1;
}
TSW_FN1(InGetWordB, p)
{
    Rec(self, 7, {});
    *reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(p)) = static_cast<uint16_t>(Mix(gSeed, static_cast<IoFixture::StreamCell *>(self)->counter++, 7));
    return 1;
}
TSW_FN1(InGetDword, p)
{
    Rec(self, 8, {});
    *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(p)) = Mix(gSeed, static_cast<IoFixture::StreamCell *>(self)->counter++, 8) % 4; // small counts
    return 1;
}
TSW_FN0(StreamError)
{
    Rec(self, 18, {});
    return static_cast<IoFixture::StreamCell *>(self)->error;
}
TSW_FN0(InContext)
{
    Rec(self, 20, {});
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gIo->ctx));
}
TSW_FN0(CtxVersion)
{
    Rec(self, 78, {});
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&static_cast<IoFixture::CtxCell *>(self)->version));
}
TSW_FN1(OutPutByte, v) { Rec(self, 4, {v}); return 1; }
TSW_FN1(OutPutWord, v) { Rec(self, 6, {v}); return 1; }
TSW_FN1(OutPutDword, v) { Rec(self, 8, {v}); return 1; }
TSW_FN2(OwnerWrite, stream, slot)
{
    Rec(self, 3, {stream, *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(slot))});
    return 0;
}
TSW_FN2(OwnerRead, stream, slot)
{
    Rec(self, 4, {stream});
    *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(slot)) = Mix(gSeed, 4, 99) % 3 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gIo->owner)) : 0u;
    return 0;
}
TSW_FN1(SinkSetText, p)
{
    RecText(self, 4, reinterpret_cast<const char *>(static_cast<uintptr_t>(p)));
    return 0;
}
TSW_FN2(IoOwnerTreeName, id, name)
{
    Rec(self, 34, {id, name});
    return 0;
}

inline void BuildIo(IoFixture &f, uint32_t seed)
{
    std::memset(&f, 0, sizeof(f));
    gIo = &f;
    gSeed = seed;
    auto P = [](auto fn) { return reinterpret_cast<void *>(fn); };
    f.vtIn[4] = P(&InGetByte);
    f.vtIn[6] = P(&InGetWord);
    f.vtIn[7] = P(&InGetWordB);
    f.vtIn[8] = P(&InGetDword);
    f.vtIn[18] = P(&StreamError);
    f.vtIn[20] = P(&InContext);
    f.vtCtx[78] = P(&CtxVersion);
    f.vtOut[4] = P(&OutPutByte);
    f.vtOut[6] = P(&OutPutWord);
    f.vtOut[8] = P(&OutPutDword);
    f.vtOut[18] = P(&StreamError);
    f.vtOwnerW[3] = P(&OwnerWrite);
    f.vtOwnerR[4] = P(&OwnerRead);
    f.vtSink[4] = P(&SinkSetText);
    f.vtOwner[34] = P(&IoOwnerTreeName);
    f.in.vt = f.vtIn;
    f.out.vt = f.vtOut;
    f.ctx.vt = f.vtCtx;
    f.ownerWriter.vt = f.vtOwnerW;
    f.ownerReader.vt = f.vtOwnerR;
    f.sink.vt = f.vtSink;
    f.owner.vt = f.vtOwner;
    std::mt19937 r(seed * 977u + 5);
    const uint32_t versions[] = {0x10, 0x19, 0x1a, 0x30, 0xa4, 0xa5, 0x100, 0x7fffffff};
    f.ctx.version = versions[r() % 8];
    f.in.error = r() % 5 == 0;
    f.out.error = r() % 5 == 0;
    f.canary = 0xC0DEC0DEu;
    gBase = &f;
    gBaseSize = sizeof(f);
}

} // namespace tsw
