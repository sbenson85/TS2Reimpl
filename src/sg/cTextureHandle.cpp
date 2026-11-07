// The texture handles, the texture manager's cached getter and the resource-name helper (lane SA); see cTextureManager.h.
#include "sg/cTextureManager.h"

#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StringKeyParser.h"

#include <cstring>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kSceneGraphService = 0xe5074b6b; // cSceneGraphSystem (first argument of the query)
constexpr uint32_t kSceneGraphIid = 0x25074b4d;
constexpr uint32_t kNoGroup = 0x1c0532fa;           // the group that counts as none
constexpr uint32_t kHashedGroupBits = 0x7f000000;   // or-ed into a group name's CRC24

using ServiceQuery = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
using SystemTextureManager = cITextureManager *(__thiscall *)(void *);
using GroupOfName = uint32_t(__thiscall *)(void *, const char *);

// Releases the system's reference on every way out after the query (the original's one unwind action covers slot 6).
struct ReleaseOnExit {
    ScopedRef *ref;
    ~ReleaseOnExit() { ref->ReleaseObject(); }
};

// The text after the first '!', unless a "##" comes before it (or at it).
const char *AfterBang(const char *text)
{
    const char *const bang = std::strchr(text, '!');
    if (bang) {
        const char *const hashes = std::strstr(text, "##");
        if (hashes == nullptr || hashes > bang) return bang + 1;
    }
    return text;
}

} // namespace

cITextureManager *TextureManager()
{
    void **const cache = &ts2_rva_10f8f94;
    if (*cache == nullptr) {
        ScopedRef system{nullptr};
        if (void *const service = GetServiceObject())
            VirtualCall<ServiceQuery, 5>(service, kSceneGraphService, kSceneGraphIid, &system.mObject); // receiver: cIGZFrameWork (slot 5 is 0x1428e2, ret 0xc)
        ReleaseOnExit release{&system};
        *cache = VirtualCall<SystemTextureManager, 6>(system.mObject); // receiver: nGZSceneGraph::cISceneGraphSystem (slot 6 is 0xa4d300, ret 0)
    }
    return static_cast<cITextureManager *>(*cache);
}

cTextureHandle *cTextureHandle::CopyFrom(const cTextureHandle *other)
{
    mId = other->mId;
    TextureManager()->AddRefId(mId);
    return this;
}

cTextureHandle *cTextureHandle::InitFromId(uint32_t id)
{
    mId = id;
    TextureManager()->AddRefId(mId);
    return this;
}

cTextureHandle *cTextureHandle::InitFromName(const char *name)
{
    mId = TextureManager()->FindOrLoad(name);
    TextureManager()->AddRefId(mId);
    return this;
}

cTextureHandle *cTextureHandle::Assign(const cTextureHandle *other)
{
    TextureManager()->AddRefId(other->mId);
    TextureManager()->ReleaseId(mId);
    mId = other->mId;
    return this;
}

void cTextureHandle::Destroy() noexcept { Reset(); }

void cTextureHandle::Reset()
{
    if (mId != kNone) {
        TextureManager()->ReleaseId(mId);
        mId = kNone;
    }
}

cIGZUnknown *cTextureHandle::Get() const
{
    if (mId == kNone) return nullptr;
    return TextureManager()->GetTexture(mId);
}

bool __cdecl ResolveResourceName(cIGZString *out, const char *text, uint32_t group, const char *groupName)
{
    if (text && text[0] == '#' && text[1] == '#') {
        out->FromChar(text);
        return true;
    }
    if (groupName) {
        StringKeyParser::EnsureServiceObject();
        if (void *const keys = ts2_rva_10f8f2c)
            group = VirtualCall<GroupOfName, 0>(keys, groupName); // receiver: nRZSceneGraph::cGroupNameTranslationProvider
        else
            group = StringKeyParser::Crc24(groupName, 1) | kHashedGroupBits;
    }
    if (group == kNoGroup) group = 0;
    if (group != 0) {
        if (text == nullptr) {
            out->Sprintf("%c0x%08x%c", '#', group, '!');
            return true;
        }
        out->Sprintf("%c0x%08x%c%s", '#', group, '!', AfterBang(text));
        return true;
    }
    if (text == nullptr) {
        out->Erase(0, 0xffffffffu);
        return true;
    }
    out->FromChar(AfterBang(text));
    return true;
}

} // namespace nGZSceneGraph
