#pragma once
// Fixture for the lane S text primitives (Cinematic 0x7b, UIEffect 0x22, MakeActionString 0x32): the string loaders
// (original code, run on both sides) reach the string-table service through a cached global; the world points it
// at a fake whose slot 3 answers "found" or "not found" and hands out a provider object whose text comes from a
// sequence. The loader's find/replace pass uses two static cRZString objects and thread-safe-static guards; the
// fixture builds them so the guards read as initialised and the needle never matches.
#include "simantics_prims_s.h"
#include "rz/cRZString.h"

#include <intrin.h>
#include <windows.h>

namespace {

// ---- texts ------------------------------------------------------------------------------------------------------

const char *const kTexts[] = {"",     "0",    "1",           "2",         "12",
                              "0x1f", "0x0",  "007",         "abc",       "-1",
                              "4294967295", "99999999999", "@", "text with spaces",
                              "a long string that exceeds the small buffer of sixteen characters", "0x7fffffff", "5", "3", "9"};
constexpr int kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);

std::vector<const char *> gTexts;
size_t gTextPos = 0;
const char *gCurrentText = "";

// The provider's slot 7 (text length) is asked first and advances to the next text; slot 6 gives its characters.
uint32_t __fastcall Prov7(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8);
    gCurrentText = gTextPos < gTexts.size() ? gTexts[gTextPos++] : "";
    gTrace.push_back(static_cast<uint32_t>(std::strlen(gCurrentText)));
    return static_cast<uint32_t>(std::strlen(gCurrentText));
}

uint32_t __fastcall Prov6(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(6u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gCurrentText));
}

// The string-table service's slot 3 (key*, interface id, void **out, flag, zero): logs the key words the loader built
// and answers from the script; when found, *out is the provider object.
uint32_t __fastcall Svc3(void *self, void *, const uint32_t *key, uint32_t iid, void **out, int32_t flag, uint32_t zero)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 5u);
    gTrace.insert(gTrace.end(), {key[0], key[1], key[2], key[3], iid, static_cast<uint32_t>(flag), zero, out ? 1u : 0u});
    const uint32_t found = Scripted(self, 3);
    if ((found & 0xff) && out) *out = &gW->fk[11];
    return found;
}

// The fixed part of the string world: fk[10] the string service, fk[11] the provider. `texts` are the strings the
// loads will receive, in order.
void BuildStrings(World &w, std::mt19937 &r, int loads)
{
    gTexts.clear();
    gTextPos = 0;
    gCurrentText = "";
    for (int i = 0; i < loads; ++i) gTexts.push_back(kTexts[r() % kTextCount]);
    Fake &service = w.fk[10], &provider = w.fk[11];
    service.vt[3] = reinterpret_cast<void *>(&Svc3);
    RetValue(&service, 3, (r() % 4 == 0) ? (static_cast<uint32_t>(r()) & ~0xffu) : (static_cast<uint32_t>(r()) | 1u), w);
    provider.vt[7] = reinterpret_cast<void *>(&Prov7);
    provider.vt[6] = reinterpret_cast<void *>(&Prov6);
    Use(provider.vt, 2, 0);
}

// The locale chain (ctx slot 24 -> list, frame slot 20 -> owner, owner slot 38 -> language, list slot 14 -> locale)
// and the locale's two group objects (slots 70 and 18, each with slot 11 = key). fk[0] ctx, fk[1] frame, fk[2] list,
// fk[4] owner, fk[5] locale; the groups are w.sub[1] (slot 70) and w.sub[2] (slot 18).
void BuildLocale(World &w, std::mt19937 &r)
{
    Fake &ctx = w.fk[0], &frame = w.fk[1], &list = w.fk[2], &owner = w.fk[4], &locale = w.fk[5];
    Fake &group70 = w.sub[1], &group18 = w.sub[2];
    Use(ctx.vt, 24, 0);
    RetObject(&ctx, 24, &list, w);
    Use(frame.vt, 20, 0);
    RetObject(&frame, 20, &owner, w);
    Use(owner.vt, 38, 0);
    RetValue(&owner, 38, static_cast<uint32_t>(r()), w);
    Use(list.vt, 14, 1);
    RetObject(&list, 14, &locale, w);
    Use(locale.vt, 70, 0);
    Use(locale.vt, 18, 0);
    RetObject(&locale, 70, (r() % 3 == 0) ? nullptr : &group70, w);
    RetObject(&locale, 18, &group18, w);
    Use(w.vtSub[1], 11, 0);
    Use(w.vtSub[2], 11, 0);
    const uint32_t keys[] = {0, 1, 0x7fd46cd0u, 0x2026960bu, 0xffffffffu};
    RetValue(&group70, 11, (r() % 2) ? keys[r() % 5] : static_cast<uint32_t>(r()), w);
    RetValue(&group18, 11, (r() % 2) ? keys[r() % 5] : static_cast<uint32_t>(r()), w);
}
// Points the cached globals at this world's fakes (run before each run): the string service (0x10f51a8), the cached
// UI/cinematic service (0x10cc458 and 0x10cc2a0; null when gNullUi), the cached director (0x10f51ac) and the UI
// manager (0x10da968; present when gManagerOn). The flags are set by the world builder, per seed.
bool gNullUi = false;
bool gManagerOn = false;

void SetTextGlobals(World &w)
{
    *GameAddress<void *>(0x10f51a8) = &w.fk[10];
    *GameAddress<void *>(0x10cc458) = &w.fk[12];
    *GameAddress<void *>(0x10cc2a0) = gNullUi ? nullptr : &w.fk[12];
    *GameAddress<void *>(0x10f51ac) = &w.fk[13];
    *GameAddress<void *>(0x10da968) = gManagerOn ? &w.fk[13] : nullptr;
    gReadPos = 0;
    gTextPos = 0;
    gCurrentText = "";
}

// Saves and restores the game globals the text primitives touch, and builds the find/replace statics.
class TextFixture {
public:
    explicit TextFixture(difftest::Context &ctx) : mCtx(ctx)
    {
        mSaved.push_back({0x10f51a8, *ctx.image.At<uint32_t>(0x10f51a8)});
        mSaved.push_back({0x10cc458, *ctx.image.At<uint32_t>(0x10cc458)});
        mSaved.push_back({0x10cc2a0, *ctx.image.At<uint32_t>(0x10cc2a0)});
        mSaved.push_back({0x10f51ac, *ctx.image.At<uint32_t>(0x10f51ac)});
        mSaved.push_back({0x10da968, *ctx.image.At<uint32_t>(0x10da968)});
        mSaved.push_back({0x10cc064, *ctx.image.At<uint32_t>(0x10cc064)});
        mSaved.push_back({0x10cc088, *ctx.image.At<uint32_t>(0x10cc088)});
        std::memcpy(mNeedle, ctx.image.At<uint8_t>(0x10cc044), sizeof(mNeedle));
        std::memcpy(mReplacement, ctx.image.At<uint8_t>(0x10cc068), sizeof(mReplacement));
        // The loader's find/replace pass: guards that read as initialised (an epoch no guard exceeds) and a needle
        // that never matches.
        const uint32_t tlsIndex = *ctx.image.At<uint32_t>(0x10df814);
        mTls = reinterpret_cast<int **>(__readfsdword(0x2c))[tlsIndex];
        mEpoch = mTls[1];
        mTls[1] = 0x7fffffff;
        *ctx.image.At<int>(0x10cc064) = 1;
        *ctx.image.At<int>(0x10cc088) = 1;
        reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(0x10cc044))->Construct("@@");
        reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(0x10cc068))->Construct("");
        gBeforeRun = SetTextGlobals;
    }
    ~TextFixture()
    {
        gBeforeRun = nullptr;
        for (auto &s : mSaved) *mCtx.image.At<uint32_t>(s.first) = s.second;
        mTls[1] = mEpoch;
        std::memcpy(mCtx.image.At<uint8_t>(0x10cc044), mNeedle, sizeof(mNeedle));
        std::memcpy(mCtx.image.At<uint8_t>(0x10cc068), mReplacement, sizeof(mReplacement));
    }

private:
    difftest::Context &mCtx;
    std::vector<std::pair<uint32_t, uint32_t>> mSaved;
    uint8_t mNeedle[0x20], mReplacement[0x20];
    int *mTls = nullptr;
    int mEpoch = 0;
};

} // namespace
