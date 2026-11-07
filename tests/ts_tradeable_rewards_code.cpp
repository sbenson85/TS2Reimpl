// Differential tests for the tradeable reward code coder (lane LG): the alphabet 0xd09870, the alphabet index 0xd09e40,
// the checksum 0xd09ac0, the unscrambler 0xd0a090, the decoder 0xd09af0 and the encoder 0xd09cd0 with its scrambler
// 0xd09e80 (src/ts/cTSTradeableRewardsService.h, namespace tradeable_rewards), each called on the same inputs as the image's
// and compared: results (the low byte or word the callers read), every output word written (buffers filled with 0x5e5e
// first, compared past the end of what is written), and the alphabet's global.
//
// TradeableRewardCode_RoundTrip settles lane KZ's finding (no code the image's encoder makes passes its own decoder): the
// scrambler 0xd09e80 reads words 10..12 (the checksum's three low bytes) from its caller's frame (esp + 0x112 / 0x114 /
// 0x116, past its own 0x7c bytes and the argument), which in the encoder is the alphabet's characters 43..45. The test
// shows it on the image itself: every code the image's encoder makes unscrambles (with the image's 0xd0a090) to "hij"
// (0x68 0x69 0x6a) in words 10..12 whatever its text, so it decodes only when the checksum's low bytes are those: "GFGGM"
// (found offline: CRC 0xe068e9ea) does, "ABCD" doesn't. A real original bug, not a harness artefact: the frame is the
// encoder's own in the game too (its only caller), and it holds the alphabet there.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "game/game.h"
#include "ts/cTSTradeableRewardsService.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace trc_test {

namespace code = tradeable_rewards;

constexpr uint32_t kAlphabetCell = 0x1107b10; // the original's alphabet (129 characters)
constexpr uint16_t kFill = 0x5e5e;

using AlphabetFn = void(__cdecl *)(wchar_t *);
using IndexFn = uint32_t(__cdecl *)(const wchar_t *, uint32_t);
using ChecksumFn = uint32_t(__cdecl *)(const void *, uint32_t);
using UnscrambleFn = uint32_t(__cdecl *)(uint16_t *);
using DecodeFn = uint32_t(__cdecl *)(const wchar_t *, uint16_t *, wchar_t *);
using EncodeFn = uint32_t(__cdecl *)(wchar_t *, int32_t, const wchar_t *);

// The game's new / free over ours (the original alphabet's strings).
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Scope {
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    wchar_t saved[129];
    Scope() { std::memcpy(saved, GameAddress<wchar_t>(kAlphabetCell), sizeof(saved)); }
    ~Scope() { std::memcpy(GameAddress<wchar_t>(kAlphabetCell), saved, sizeof(saved)); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

std::wstring Alphabet()
{
    wchar_t a[256] = {};
    code::CodeAlphabet(a);
    return std::wstring(a, 128);
}

std::string Show(const wchar_t *w, size_t n)
{
    std::string s;
    char buf[8];
    for (size_t i = 0; i < n; ++i) {
        snprintf(buf, sizeof(buf), "%04x ", unsigned(uint16_t(w[i])));
        s += buf;
    }
    return s;
}

// A text for the encoder: 0..10 characters from the alphabet, its upper half, '_', ' ', characters it lacks.
std::wstring RandomText(std::mt19937 &rng, const std::wstring &alphabet)
{
    static const wchar_t kOther[] = {L'_', L' ', L'5', L'0', L'S', L'O', 0x5000, 0xffc0, 0x7f, 0x100, L'\t', 0x153};
    const uint32_t n = rng() % 11;
    std::wstring t;
    for (uint32_t i = 0; i < n; ++i) {
        switch (rng() % 4) {
        case 0: t += alphabet[rng() % 32]; break;
        case 1: t += alphabet[rng() % 128]; break;
        case 2: t += kOther[rng() % (sizeof(kOther) / sizeof(kOther[0]))]; break;
        default: t += alphabet[8 + rng() % 24]; break;
        }
    }
    return t;
}

const int32_t kIds[] = {0, 1, 2, 0x31, 0x32, 0x33, 0x7f, 0x80, 0x7fff, -1, -0x8000, 0x8000, 0xffff, 0x10000, 0x10032, 0x7fff0005,
                        int32_t(0x80000032), int32_t(0xffff0033)};

// A code that decodes: words (character indexes, the id, the checksum) scrambled the way the decoder undoes it.
std::wstring ValidCode(const std::wstring &alphabet, const uint16_t (&chars)[8], uint16_t id)
{
    uint16_t v[20] = {};
    for (int i = 0; i < 8; ++i) v[i] = chars[i];
    v[8] = id;
    const uint32_t crc = code::CodeChecksum(v, 9);
    v[9] = uint16_t(crc >> 24 & 0x7f);
    v[10] = uint16_t(crc >> 16 & 0x7f);
    v[11] = uint16_t(crc >> 8 & 0x7f);
    v[12] = uint16_t(crc & 0x7f);
    code::ScrambleCode(v, uint32_t(v[10]) | uint32_t(v[11]) << 8 | uint32_t(v[12]) << 16);
    std::wstring out;
    for (int i = 0; i < 19; ++i) out += alphabet[v[i]];
    return out;
}

} // namespace trc_test

using namespace trc_test;

DIFF_TEST(TradeableRewardCode_Alphabet, 0xd09870)
{
    Scope scope;
    wchar_t ours[256], first[256], again[256];
    for (auto *b : {ours, first, again})
        for (int i = 0; i < 256; ++i) b[i] = wchar_t(kFill);
    code::CodeAlphabet(ours);
    std::memset(GameAddress<wchar_t>(kAlphabetCell), 0, 129 * sizeof(wchar_t)); // not built yet: the build path
    ctx.Original<AlphabetFn>(0xd09870)(first);
    ctx.Original<AlphabetFn>(0xd09870)(again); // built: the copy
    if (std::memcmp(ours, first, sizeof(ours)) != 0)
        ctx.Fail("alphabet (built) differs:\n  orig %s\n  ours %s", Show(first, 130).c_str(), Show(ours, 130).c_str());
    if (std::memcmp(ours, again, sizeof(ours)) != 0) ctx.Fail("alphabet (copied) differs");
    if (std::memcmp(ours, GameAddress<wchar_t>(kAlphabetCell), 129 * sizeof(wchar_t)) != 0) ctx.Fail("the original's global differs");
    if (std::char_traits<wchar_t>::length(ours) != 128) ctx.Fail("alphabet length %u", unsigned(std::char_traits<wchar_t>::length(ours)));
}

DIFF_TEST(TradeableRewardCode_Index, 0xd09e40)
{
    Scope scope;
    const std::wstring alphabet = Alphabet();
    std::mt19937 rng(41);
    std::vector<wchar_t> tables[4];
    tables[0].assign(alphabet.begin(), alphabet.end());
    tables[0].push_back(0);
    for (int t = 1; t < 4; ++t) {
        // Small character sets: duplicates (the first wins), '_' and ' ' entries, zeros inside (no stop at a terminator),
        // the character at 127 only (the scan's last), and past 128 (never reached).
        static const wchar_t kSet[] = {L'A', L'_', L' ', 0, L'B', 0xffc0};
        for (int i = 0; i < 130; ++i) tables[t].push_back(kSet[rng() % (t == 3 ? 2 : 6)]);
        if (t == 2) {
            tables[t][127] = L'Q';
            tables[t][128] = L'R';
        }
    }
    uint32_t chars[0x220];
    uint32_t n = 0;
    for (uint32_t c = 0; c < 0x200; ++c) chars[n++] = c;
    for (uint32_t c : {0x5fu, 0x20u, 0x10020u, 0xffff005fu, 0xffc0u, 0x152u, 0x153u, 0xffffu, 0x8000u, 0x51u, 0x52u, 0x10041u}) chars[n++] = c;
    for (const auto &table : tables) {
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t o = ctx.Original<IndexFn>(0xd09e40)(table.data(), chars[i]) & 0xffff;
            const uint32_t u = code::AlphabetIndex(table.data(), wchar_t(chars[i]));
            if (o != u) {
                ctx.Fail("index of %x: orig %u ours %u", chars[i], o, u);
                return;
            }
        }
    }
}

DIFF_TEST(TradeableRewardCode_Checksum, 0xd09ac0)
{
    std::mt19937 rng(7);
    uint8_t bytes[300];
    for (uint32_t round = 0; round < 400; ++round) {
        for (auto &b : bytes) b = uint8_t(rng());
        const uint32_t count = round < 300 ? round : rng() % 300;
        const uint32_t o = ctx.Original<ChecksumFn>(0xd09ac0)(bytes, count);
        const uint32_t u = code::CodeChecksum(bytes, count);
        if (o != u) {
            ctx.Fail("checksum of %u bytes: orig %08x ours %08x", count, o, u);
            return;
        }
    }
}

DIFF_TEST(TradeableRewardCode_Unscramble, 0xd0a090)
{
    std::mt19937 rng(11);
    for (uint32_t round = 0; round < 3000; ++round) {
        uint16_t in[21];
        for (auto &w : in) w = uint16_t(rng());
        if (round < 19 * 16) { // every single bit of every value
            for (int i = 0; i < 19; ++i) in[i] = 0;
            in[round / 16] = uint16_t(1u << (round % 16));
        } else if (round % 3 == 0) {
            for (int i = 0; i < 19; ++i) in[i] &= 0x1f;
        } else if (round % 3 == 1) {
            for (int i = 0; i < 19; ++i) in[i] &= 0x7f;
        }
        uint16_t o[21], u[21];
        std::memcpy(o, in, sizeof(in));
        std::memcpy(u, in, sizeof(in));
        const uint32_t ro = ctx.Original<UnscrambleFn>(0xd0a090)(o) & 0xff;
        const uint32_t ru = code::UnscrambleCode(u) ? 1 : 0;
        if (ro != ru || std::memcmp(o, u, sizeof(o)) != 0) {
            ctx.Fail("unscramble round %u: result %u / %u", round, ro, ru);
            for (int i = 0; i < 21; ++i) fprintf(stderr, "  %2d in %04x orig %04x ours %04x\n", i, in[i], o[i], u[i]);
            return;
        }
    }
}

// The encoder, and through it the scrambler 0xd09e80 (its only caller): every text kind and id against the image's.
DIFF_TEST(TradeableRewardCode_Encode, 0xd09cd0)
{
    Scope scope;
    const std::wstring alphabet = Alphabet();
    std::mt19937 rng(23);
    for (uint32_t round = 0; round < 6000; ++round) {
        const std::wstring text = round < 128 ? std::wstring(1, alphabet[round]) + L"ABC" : RandomText(rng, alphabet);
        const int32_t id = round % 3 ? kIds[rng() % (sizeof(kIds) / sizeof(kIds[0]))] : int32_t(rng() % 0x40);
        wchar_t o[24], u[24];
        for (int i = 0; i < 24; ++i) o[i] = u[i] = wchar_t(kFill);
        const uint32_t ro = ctx.Original<EncodeFn>(0xd09cd0)(o, id, text.c_str()) & 0xff;
        const uint32_t ru = code::EncodeCode(u, id, text.c_str()) ? 1 : 0;
        if (ro != ru || std::memcmp(o, u, sizeof(o)) != 0) {
            ctx.Fail("encode round %u (id %x, text %s): result %u / %u\n  orig %s\n  ours %s", round, unsigned(id),
                     Show(text.c_str(), text.size()).c_str(), ro, ru, Show(o, 24).c_str(), Show(u, 24).c_str());
            return;
        }
    }
}

DIFF_TEST(TradeableRewardCode_Decode, 0xd09af0)
{
    Scope scope;
    const std::wstring alphabet = Alphabet();
    std::mt19937 rng(31);
    for (uint32_t round = 0; round < 8000; ++round) {
        std::wstring codeText;
        uint16_t chars[8];
        const uint32_t kind = round % 8;
        if (kind <= 3) { // valid codes: random characters, trailing spaces, all spaces, ids around 0x33
            for (auto &c : chars) c = uint16_t(rng() % 128);
            if (kind == 1)
                for (uint32_t i = rng() % 9; i < 8; ++i) chars[i] = 103; // ' '
            if (kind == 2) {
                for (auto &c : chars) c = 103;
                if (rng() % 2) chars[rng() % 8] = uint16_t(rng() % 103);
            }
            const uint16_t id = kind == 3 ? uint16_t(0x30 + rng() % 6) : uint16_t(rng() % 0x80);
            codeText = ValidCode(alphabet, chars, id);
            if (rng() % 4 == 0) codeText[rng() % 19] = alphabet[rng() % 128]; // broken (mostly), or the bits it ignores
            if (rng() % 8 == 0) codeText[rng() % 19] = L'_';
        } else if (kind == 4) { // the image's encoder's codes
            const std::wstring text = RandomText(rng, alphabet);
            wchar_t made[24] = {};
            if (!(ctx.Original<EncodeFn>(0xd09cd0)(made, int32_t(rng() % 0x33), text.size() <= 8 ? text.c_str() : L"GFGGM") & 0xff))
                continue;
            codeText = made;
        } else if (kind == 5) { // random characters
            for (int i = 0; i < 19; ++i) codeText += alphabet[rng() % 128];
        } else if (kind == 6) { // lengths around 19
            for (auto &c : chars) c = uint16_t(rng() % 128);
            codeText = ValidCode(alphabet, chars, uint16_t(rng() % 0x33));
            if (rng() % 2) codeText.pop_back();
            else codeText += alphabet[rng() % 32];
        } else { // empty or short
            codeText = std::wstring(rng() % 3, L'A');
        }
        wchar_t o[12], u[12];
        uint16_t oid[2] = {kFill, kFill}, uid[2] = {kFill, kFill};
        for (int i = 0; i < 12; ++i) o[i] = u[i] = wchar_t(kFill);
        const uint32_t ro = ctx.Original<DecodeFn>(0xd09af0)(codeText.c_str(), oid, o) & 0xff;
        const uint32_t ru = code::DecodeCode(codeText.c_str(), reinterpret_cast<int16_t *>(uid), u) ? 1 : 0;
        if (ro != ru || std::memcmp(o, u, sizeof(o)) != 0 || std::memcmp(oid, uid, sizeof(oid)) != 0) {
            ctx.Fail("decode round %u (%s): result %u / %u, id %04x / %04x\n  orig %s\n  ours %s", round,
                     Show(codeText.c_str(), codeText.size()).c_str(), ro, ru, oid[0], uid[0], Show(o, 12).c_str(), Show(u, 12).c_str());
            return;
        }
    }
}

// KZ's finding, on the image and ours: the encoder's codes carry "hij" in words 10..12; they decode only when the checksum's
// low bytes are those.
DIFF_TEST(TradeableRewardCode_RoundTrip, 0xd09e80)
{
    Scope scope;
    const std::wstring alphabet = Alphabet();
    std::mt19937 rng(5);
    uint32_t decoded[2] = {};
    for (uint32_t round = 0; round < 3000; ++round) {
        std::wstring text = round == 0 ? L"GFGGM" : round == 1 ? L"ABCD" : round == 2 ? L"GFGGMXYZ" : RandomText(rng, alphabet);
        if (text.size() > 8) text.resize(8);
        const int32_t id = int32_t(rng() % 0x33);
        uint32_t ok[2];
        for (int side = 0; side < 2; ++side) {
            wchar_t made[24] = {};
            if (side == 0) ctx.Original<EncodeFn>(0xd09cd0)(made, id, text.c_str());
            else code::EncodeCode(made, id, text.c_str());
            uint16_t v[20] = {};
            for (int i = 0; i < 19; ++i) v[i] = uint16_t(ctx.Original<IndexFn>(0xd09e40)(alphabet.c_str(), made[i]));
            ctx.Original<UnscrambleFn>(0xd0a090)(v);
            if (v[10] != 0x68 || v[11] != 0x69 || v[12] != 0x6a)
                ctx.Fail("side %d round %u: words 10..12 %02x %02x %02x, not \"hij\"", side, round, v[10], v[11], v[12]);
            uint16_t back = 0;
            wchar_t chars[12] = {};
            ok[side] = ctx.Original<DecodeFn>(0xd09af0)(made, &back, chars) & 0xff;
            decoded[side] += ok[side];
            if (round == 0 && !(ok[side] && back == id && std::wstring(chars) == L"GFGGM"))
                ctx.Fail("side %d: \"GFGGM\" doesn't round-trip", side);
            if (round == 1 && ok[side]) ctx.Fail("side %d: \"ABCD\" round-trips", side);
        }
        if (ok[0] != ok[1]) ctx.Fail("round %u: decodes %u / %u", round, ok[0], ok[1]);
    }
    if (decoded[0] != decoded[1] || decoded[0] < 2) ctx.Fail("decoded %u / %u of 3000", decoded[0], decoded[1]);
}
