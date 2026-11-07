#include "ts/cTSSimDNA.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/DNAInputAppearance.h"
#include "ifaces/DNAOutputAppearance.h"
#include "ifaces/DNASelectionSingleton.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/DNAMemoryAllocator.h"
#include "ifaces/DNAOwnedString.h"
#include "game/original.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cGZAllocatorService.h"
#include "app/AppDebugger.h"
#include "std/StdInstances.h"
#include "misc/HexParse.h"
#include <cstring>
#include <cstdlib>
#include <new>
#include <vector>

extern "C" __declspec(thread) int _Init_thread_epoch;

namespace nTSGenetics {
namespace {
constexpr uint32_t kFirstGene = 1, kGeneEnd = 16, kExcludedGene = 4;
constexpr uint32_t kWeightedCount = 10, kFirstStringCount = 11, kSecondStringCount = 15;
constexpr uint32_t kWeightedNameBase = 0x400, kWeightedValueBase = 0x800, kWeightBase = 0xc00;
constexpr uint32_t kFirstStringBase = 0x1000, kSecondStringBase = 0x1400;
uint32_t ReadCount(cIGZString *text);

struct RetainedPair {
    DNAStringPair strings;
    // Construct this guard only after GetGene returns: a throwing getter does not
    // transfer ownership to the caller in the original's EH state table.
    explicit RetainedPair(DNAStringPair value) : strings(value) {}
    ~RetainedPair() noexcept {
        if (strings.second) strings.second->Release();
        if (strings.first) strings.first->Release();
    }
};
DNAStringPair FetchPair(cITSSimDNA *dna, uint32_t gene)
{
    DNAStringPair result;
    dna->GetGene(&result, gene);
    return result;
}
struct TemporaryString {
    cRZString text;
    TemporaryString() {
        std::memset(text.mString.data(), 0, 16);
        text.mVTable = GameAddress(cRZString::kVTable);
        text.mRefCount = 0;
    }
    ~TemporaryString() { text.mVTable = GameAddress(cRZString::kVTable); }
};
// The resource formatter writes a 24-byte string whose heap storage is owned by
// the allocator service. Keep the STL layout, but release through that service.
struct ResourceText {
    std::string text;
    ResourceText() { std::memset(text.data(), 0, 16); }
    ~ResourceText() noexcept {
        if (text.capacity() > 15) appdebugger::FreeStringBuffer(&text, text.data(), uint32_t(text.capacity()));
        new (&text) std::string();
    }
};
struct PropertyKey : ResourceText {
    explicit PropertyKey(const char *value) {
        reinterpret_cast<ServiceStringObject *>(&text)->ConstructFromText(value); // 0x1773e0 (src/std/StdInstances.h)
    }
};
struct AnyString {
    const void *vtable;
    cIGZString *value;
    AnyString() : vtable(GameAddress(0xda3d98)), value(nullptr) {}
    explicit AnyString(const char *text) {
        reinterpret_cast<::AnyString *>(this)->Construct(text); // 0x213fbb (src/gz/cGZAny.size.cpp)
    }
    ~AnyString() noexcept { if (value) value->Release(); }
};
static_assert(sizeof(AnyString) == 8);
void FormatResource(const DNAResourceKey &key, ResourceText &out)
{
    OriginalFunction<void(__thiscall *)(const DNAResourceKey *, std::string *), 0x4fdd30>()(&key, &out.text);
}
void ReplaceString(cIGZString *&target, cRZString *replacement)
{
    auto *text = reinterpret_cast<cIGZString *>(replacement);
    if (target == text) return;
    if (text) text->AddRef();
    auto *old = target;
    target = text;
    if (old) old->Release();
}
cRZString *AllocateText(const char *text)
{
    auto *copy = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    if (!copy) return nullptr;
    std::memset(&copy->mString, 0, sizeof(copy->mString));
    try { return copy->Construct(text); }
    catch (...) { ::operator delete(copy, sizeof(*copy)); throw; }
}
cRZString *AllocateCopy(const cRZString &text)
{
    auto *copy = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    if (!copy) return nullptr;
    std::memset(&copy->mString, 0, sizeof(copy->mString));
    try { return copy->Construct(text, 0, UINT32_MAX); }
    catch (...) { ::operator delete(copy, sizeof(*copy)); throw; }
}
void ReplaceBoth(RetainedPair &pair, const char *text)
{
    ReplaceString(pair.strings.first, AllocateText(text));
    ReplaceString(pair.strings.second, AllocateText(text));
}
void ReplaceBoth(RetainedPair &pair, const cRZString &text)
{
    ReplaceString(pair.strings.first, AllocateCopy(text));
    ReplaceString(pair.strings.second, AllocateCopy(text));
}
struct ScopedStringFloat {
    alignas(DNAStringFloat) uint8_t storage[sizeof(DNAStringFloat)];
    DNAStringFloat *Get() { return reinterpret_cast<DNAStringFloat *>(storage); }
    ScopedStringFloat() {
        std::memset(storage, 0, sizeof(storage));
        Get()->text.Construct();
        Get()->value = 0;
    }
    explicit ScopedStringFloat(const DNAStringFloat &source) {
        std::memset(storage, 0, sizeof(storage));
        Get()->text.Construct(source.text, 0, UINT32_MAX);
        Get()->value = source.value;
    }
    ~ScopedStringFloat() noexcept {
        OriginalFunction<void(__thiscall *)(DNAStringFloat *), 0x1d99b0>()(Get());
    }
};
struct ScopedReference {
    cIGZString *text = nullptr;
    ~ScopedReference() noexcept { if (text) text->Release(); }
};
struct ScopedWeightedVector {
    DNAWeightedVector values{};
    ~ScopedWeightedVector() noexcept {
        OriginalFunction<void(__thiscall *)(DNAWeightedVector *), 0x4f5cc0>()(&values);
    }
};
struct ScopedStringVector {
    DNAStringVector values{};
    ~ScopedStringVector() noexcept {
        if (!values.begin) return;
        for (auto *text = values.begin; text != values.end; ++text)
            As<ifaces::DNAOwnedString>(text)->DeletingDestructor(0);
        FreeThroughAllocatorService(values.begin, uint32_t(values.capacity - values.begin));
        values = {};
    }
};
void DestroyWeighted(DNAWeightedGene *value)
{
    OriginalFunction<void(__thiscall *)(DNAWeightedGene *), 0x257860>()(value);
}
void CopyWeighted(DNAWeightedGene *out, const DNAWeightedGene *value)
{
    OriginalFunction<DNAWeightedGene *(__thiscall *)(DNAWeightedGene *, const DNAWeightedGene *), 0x4f5a10>()(out, value);
}
void CopyWeightedInline(DNAWeightedGene *out, const DNAWeightedGene &value)
{
    std::memset(&out->name.mString, 0, 24);
    out->name.Construct(value.name, 0, UINT32_MAX);
    try {
        std::memset(&out->value.mString, 0, 24);
        out->value.Construct(value.value, 0, UINT32_MAX);
    } catch (...) { out->name.Destruct(); throw; }
    out->weight = value.weight;
}
struct ScopedWeightedGene {
    alignas(DNAWeightedGene) uint8_t storage[68];
    DNAWeightedGene *Get() { return reinterpret_cast<DNAWeightedGene *>(storage); }
    ScopedWeightedGene() {
        std::memset(storage, 0, sizeof(storage));
        Get()->name.Construct(); Get()->value.Construct(); Get()->weight = 255;
    }
    ~ScopedWeightedGene() noexcept { DestroyWeighted(Get()); }
};
void CopyWeightedRange(DNAWeightedGene *first, DNAWeightedGene *last, DNAWeightedGene *out)
{
    auto *end = out;
    try { for (; first != last; ++first, ++end) CopyWeighted(end, first); }
    catch (...) { for (auto *value = out; value != end; ++value) DestroyWeighted(value); throw; }
}
void *AllocateVectorBlock(uint32_t bytes)
{
    auto *guard = &ts2_rva_10c95d8;
    auto **service = &ts2_rva_10c95d4;
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try { *service = GetAllocatorService(); }
            catch (...) { _Init_thread_abort(guard); throw; }
            _Init_thread_footer(guard);
        }
    }
    return As<ifaces::DNAMemoryAllocator>(*service)->Allocate(bytes);
}
struct PairFailureGuard {
    DNAStringPair *out;
    bool returned = false;
    ~PairFailureGuard() noexcept {
        if (returned) return;
        if (out->second) out->second->Release();
        if (out->first) out->first->Release();
    }
};
cRZString *AllocateString(const cIGZString *value)
{
    if (!value) return AllocateText("");
    auto *out = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    if (!out) return nullptr;
    std::memset(&out->mString, 0, 24);
    try { return out->Construct(*value); }
    catch (...) { ::operator delete(out, sizeof(*out)); throw; }
}
uint8_t ReadStrings(cITSSimDNA *dna, DNAStringVector *out, uint32_t countGene, uint32_t base)
{
    for (auto *value = out->begin; value != out->end; ++value) As<ifaces::DNAOwnedString>(value)->DeletingDestructor(0);
    out->end = out->begin;
    if (!dna->HasGene(countGene)) return 0;
    RetainedPair count(FetchPair(dna, countGene));
    const int32_t size = int32_t(ReadCount(count.strings.first));
    for (int32_t index = 0; index < size; ++index) {
        RetainedPair pair(FetchPair(dna, base + uint32_t(index)));
        if (!pair.strings.first) continue;
        TemporaryString text;
        text.text.Copy(*pair.strings.first);
        if (out->end == out->capacity)
            OriginalFunction<cRZString *(__thiscall *)(DNAStringVector *, cRZString *, const cRZString *), 0x38c670>()(out, out->end, &text.text);
        else {
            std::memset(&out->end->mString, 0, 24);
            out->end->Construct(text.text, 0, UINT32_MAX);
            ++out->end;
        }
    }
    return 1;
}
void WriteStrings(cITSSimDNA *dna, const DNAStringVector *values, uint8_t both, uint32_t countGene, uint32_t base)
{
    RetainedPair count(FetchPair(dna, countGene));
    {
        TemporaryString number;
        number.text.Sprintf(GameAddress<char>(0xd9b648), values->Size());
        count.strings.first->Copy(*number.text.AsInterface());
        if (both) count.strings.second->Copy(*number.text.AsInterface());
        dna->SetGene(countGene, &count.strings);
        for (uint32_t index = 0; index < values->Size(); ++index) {
            const cRZString *value = &values->begin[index];
            RetainedPair pair(FetchPair(dna, base + index));
            pair.strings.first->Copy(*value->AsInterface());
            if (both) pair.strings.second->Copy(*value->AsInterface());
            dna->SetGene(base + index, &pair.strings);
        }
    }
}
uint32_t ReadCount(cIGZString *text)
{
    if (!text || !text->Strlen()) return 0;
    // RVA 0x830972 is atoi (byte-identical to _wtoi's wrapper, but it parses chars); the receiver is cIGZString (char).
    return static_cast<uint32_t>(std::atoi(text->ToChar()));
}
}

// ---- construction and destruction ----

// 0x4f9020: operator new(12), the constructor inline (vtable, count 0, no store); null on a failed allocation.
cTSSimDNA *cTSSimDNA::Create()
{
    return new cTSSimDNA;
}

// The scalar deleting destructor (slot 17, 0x4f5e40) is the compiler's: this destructor, then operator delete(this, 12) on bit 0
// of the flags; returns this. The store is released but not cleared.
cTSSimDNA::~cTSSimDNA()
{
    if (mPropertyStore) static_cast<cIGZUnknown *>(mPropertyStore)->Release();
}

// ---- cIGZUnknown ----

// 0x4fb6a0: this for id 1 and the DNA interface, a reference added through slot 1.
bool cTSSimDNA::QueryInterface(uint32_t id, void **out)
{
    if (!out) return false;
    switch (id) {
    case 1:
    case kInterfaceId:
        *out = static_cast<cITSSimDNA *>(this);
        AddRef();
        return true;
    }
    return false;
}
// slot 1 (folded 0x148fb0)
uint32_t cTSSimDNA::AddRef()
{
    return ++mRefCount;
}
// 0x4fb6f0: at one or below, delete this (slot 17 with 1).
uint32_t cTSSimDNA::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// ---- the property store ----

// slot 7 (folded 0x4043a0): the same store again changes nothing; otherwise the new one is retained first, the old released last.
void cTSSimDNA::SetPropertyStore(void *store)
{
    void *old = mPropertyStore;
    if (store == old) return;
    if (store) static_cast<cIGZUnknown *>(store)->AddRef();
    mPropertyStore = store;
    if (old) static_cast<cIGZUnknown *>(old)->Release();
}
// slot 8 (folded 0x46390): not retained
void *cTSSimDNA::GetPropertyStore()
{
    return mPropertyStore;
}

// ---- genes ----

uint8_t cTSSimDNA::CopyFrom(cITSSimDNA *source)
{
    if (!source) return 0;
    cITSSimDNA *from = source;
    auto copyGene = [&](uint32_t gene) {
        if (!from->HasGene(gene)) return;
        DNAStringPair result;
        from->GetGene(&result, gene);
        RetainedPair retained(result);
        SetGene(gene, &retained.strings);
    };
    for (uint32_t gene = kFirstGene; gene < kGeneEnd; ++gene)
        if (gene != kExcludedGene) copyGene(gene);

    auto copyFamily = [&](uint32_t countGene, uint32_t base, bool weighted) {
        if (!from->HasGene(countGene)) return;
        DNAStringPair result;
        from->GetGene(&result, countGene);
        RetainedPair retained(result);
        const uint32_t first = ReadCount(retained.strings.first);
        const uint32_t second = ReadCount(retained.strings.second);
        // ORIGINAL-BUG: negative parsed counts become unsigned and can cause
        // billions of iterations. Preserve the unsigned maximum and loop bound.
        const uint32_t count = first < second ? second : first;
        for (uint32_t index = 0; index < count; ++index) {
            if (weighted) {
                copyGene(kWeightedNameBase + index);
                copyGene(kWeightedValueBase + index);
                copyGene(kWeightBase + index);
            } else copyGene(base + index);
        }
    };
    copyFamily(kWeightedCount, kWeightedNameBase, true);
    copyFamily(kFirstStringCount, kFirstStringBase, false);
    copyFamily(kSecondStringCount, kSecondStringBase, false);
    return 1;
}

void cTSSimDNA::CaptureAppearance(void *appearance, uint32_t mask)
{
    auto *source = As<ifaces::DNAInputAppearance>(appearance);
    if (source->GetType() == 1) {
        ResourceText text;
        auto *defaultKey = static_cast<DNAResourceKey *>(source->GetDefaultIdentifier());
        const uint32_t selection = source->GetSelectionIndex();
        DNAResourceKey selected;
        OriginalFunction<DNAResourceKey *(__cdecl *)(DNAResourceKey *, uint32_t, uint32_t, const DNAResourceKey *), 0xb5e590>()
            (&selected, 8, selection, defaultKey);
        const DNAResourceKey first = *static_cast<DNAResourceKey *>(source->GetPrimaryIdentifier());
        const DNAResourceKey third = *static_cast<DNAResourceKey *>(source->GetSecondaryIdentifier());
        RetainedPair gene6(FetchPair(this, 6));
        FormatResource(first, text);
        ReplaceBoth(gene6, text.text.c_str());
        SetGene(6, &gene6.strings);
        SetGene(2, &gene6.strings);
        RetainedPair gene1(FetchPair(this, 1));
        FormatResource(selected, text);
        ReplaceBoth(gene1, text.text.c_str());
        SetGene(1, &gene1.strings);
        RetainedPair gene3(FetchPair(this, 3));
        FormatResource(third, text);
        ReplaceBoth(gene3, text.text.c_str());
        SetGene(3, &gene3.strings);
    }
    // The second type query is deliberate: a receiver can change its answer.
    if (source->GetType() == 1) return;
    ResourceText text;
    auto captureKey = [&](uint32_t gene, const DNAResourceKey &key, RetainedPair &pair) {
        FormatResource(key, text);
        ReplaceBoth(pair, text.text.c_str());
        SetGene(gene, &pair.strings);
    };
    if (mask & 1) {
        RetainedPair pair(FetchPair(this, 3));
        const auto key = *static_cast<DNAResourceKey *>(source->GetSecondaryIdentifier());
        captureKey(3, key, pair);
    }
    if (mask & 2) {
        RetainedPair pair(FetchPair(this, 14));
        const auto key = *static_cast<DNAResourceKey *>(source->GetAlternateIdentifier());
        captureKey(14, key, pair);
    }
    if (mask & 4) {
        RetainedPair pair(FetchPair(this, 7));
        const auto key = *static_cast<DNAResourceKey *>(source->GetPairedIdentifier(1));
        captureKey(7, key, pair);
    }
    if (mask & 8) {
        RetainedPair pair(FetchPair(this, 8));
        const auto key = *static_cast<DNAResourceKey *>(source->GetPairedIdentifier(0));
        captureKey(8, key, pair);
    }
    if (mask & 0x10) {
        ScopedStringFloat value(*static_cast<DNAStringFloat *>(source->GetStringFloat()));
        RetainedPair gene12(FetchPair(this, 12));
        ReplaceBoth(gene12, value.Get()->text);
        SetGene(12, &gene12.strings);
        RetainedPair gene13(FetchPair(this, 13));
        {
            TemporaryString number;
            number.text.Sprintf(GameAddress<char>(0xdce3c8), double(value.Get()->value));
            ReplaceBoth(gene13, number.text);
            SetGene(13, &gene13.strings);
        }
    }
    if (mask & 0x20) {
        RetainedPair pair(FetchPair(this, 9));
        const uint32_t value = source->GetNumericValue();
        {
            TemporaryString number;
            number.text.Sprintf(GameAddress<char>(0xd9b648), value);
            ReplaceBoth(pair, number.text);
            SetGene(9, &pair.strings);
        }
    }
    if (mask & 0x40) SetWeightedGenes(static_cast<const DNAWeightedVector *>(source->GetWeightedGenes()), 1);
    if (mask & 0x80) SetStringGenes(static_cast<const DNAStringVector *>(source->GetStringGenes()), 1);
    if (mask & 0x100) SetAdditionalStringGenes(static_cast<const DNAStringVector *>(source->GetAdditionalStringGenes()), 1);
}

void cTSSimDNA::ApplyAppearance(void *appearance)
{
    if (!appearance) return;
    auto *target = As<ifaces::DNAOutputAppearance>(appearance);
    RetainedPair gene1(FetchPair(this, 1));
    RetainedPair gene6(FetchPair(this, 6));
    RetainedPair gene3(FetchPair(this, 3));
    auto parse = [](cIGZString *text) {
        DNAResourceKey key;
        const char *data = text->ToChar();
        hexparse::ParseGuidText(&key, data);
        return key;
    };
    auto key6 = parse(gene6.strings.first);
    auto key3 = parse(gene3.strings.first);
    auto key1 = parse(gene1.strings.first);
    ScopedReference selected;
    void *policy = As<ifaces::DNASelectionSingleton>(GetCachedSingleton())->GetPolicy();
    // The original snapshots policy slot 9 before SelectIdentifier can mutate it.
    auto shouldOverride = VirtualSlot<uint8_t(__thiscall *)(void *, uint32_t)>(policy, 9);
    const int32_t result = target->SelectIdentifier(this, &selected.text);
    if (shouldOverride(policy, uint32_t(result == 2))) key1 = parse(selected.text);
    target->SetPrimaryIdentifier(&key6);
    target->SetSelectedIdentifier(&key1);
    target->SetSecondaryIdentifier(&key3);
    if (target->GetType() == 1) return;
    RetainedPair gene14(FetchPair(this, 14));
    if (gene14.strings.first) {
        auto parsed = parse(gene14.strings.first);
        (void)parsed;
        // ORIGINAL-BUG: gene 14 is parsed, but the gene 3 key is passed instead.
        target->SetAlternateIdentifier(&key3);
    }
    RetainedPair gene7(FetchPair(this, 7));
    if (gene7.strings.first) { auto key = parse(gene7.strings.first); target->SetPairedIdentifier(1, &key); }
    RetainedPair gene8(FetchPair(this, 8));
    if (gene8.strings.first) { auto key = parse(gene8.strings.first); target->SetPairedIdentifier(0, &key); }
    RetainedPair gene12(FetchPair(this, 12));
    RetainedPair gene13(FetchPair(this, 13));
    if (gene12.strings.first && gene13.strings.first) {
        auto key = parse(gene12.strings.first);
        const char *number = gene13.strings.first->ToChar();
        const float value = float(OriginalFunction<double(__cdecl *)(const char *), 0x1b0ff4>()(number));
        ScopedStringFloat combined;
        OriginalFunction<void(__thiscall *)(DNAResourceKey *, cRZString *), 0x4fdcf0>()(&key, &combined.Get()->text);
        combined.Get()->value = value;
        target->SetStringFloat(combined.Get());
    }
    RetainedPair gene9(FetchPair(this, 9));
    if (gene9.strings.first) {
        const char *number = gene9.strings.first->ToChar();
        target->SetNumericValue(uint32_t(std::atoi(number)));
    }
    ScopedWeightedVector weighted;
    if (GetWeightedGenes(&weighted.values, 0)) target->SetWeightedGenes(&weighted.values);
    ScopedStringVector strings;
    if (GetStringGenes(&strings.values)) target->SetStringGenes(&strings.values);
    ScopedStringVector additional;
    if (GetAdditionalStringGenes(&additional.values)) target->SetAdditionalStringGenes(&additional.values);
}

DNAStringPair *cTSSimDNA::GetGene(DNAStringPair *out, uint32_t gene)
{
    *out = {};
    PairFailureGuard result{out};
    if (!mPropertyStore) {
        ReplaceString(out->first, AllocateText(""));
        ReplaceString(out->second, AllocateText(""));
    } else {
        AnyString value;
        TemporaryString number;
        for (unsigned channel = 0; channel < 2; ++channel) {
            number.text.Sprintf(GameAddress<char>(0xd9b648), gene | (channel ? 0x10000000u : 0));
            uint8_t found;
            {
                PropertyKey key(number.text.mString.c_str());
                found = As<ifaces::cIGZPropertySetString>(mPropertyStore)->GetPropertyValue(&key.text, &value);
            }
            ReplaceString(channel ? out->second : out->first, AllocateString(found ? value.value : nullptr));
        }
    }
    result.returned = true;
    return out;
}
uint8_t cTSSimDNA::SetGene(uint32_t gene, const DNAStringPair *values)
{
    if (!mPropertyStore) return 0;
    TemporaryString number;
    for (unsigned channel = 0; channel < 2; ++channel) {
        number.text.Sprintf(GameAddress<char>(0xd9b648), gene | (channel ? 0x10000000u : 0));
        auto *text = channel ? values->second : values->first;
        PropertyKey key(number.text.mString.c_str());
        void *store = mPropertyStore;
        const char *data = text ? text->ToChar() : GameAddress<char>(0xd9b9d0);
        AnyString value(data);
        As<ifaces::cIGZPropertySetString>(store)->SetProperty(&key.text, &value);
    }
    return 0;
}
uint8_t cTSSimDNA::HasGene(uint32_t gene)
{
    if (!mPropertyStore) return 0;
    AnyString unused;
    TemporaryString number;
    for (unsigned channel = 0; channel < 2; ++channel) {
        number.text.Sprintf(GameAddress<char>(0xd9b648), gene | (channel ? 0x10000000u : 0));
        uint8_t found;
        { PropertyKey key(number.text.mString.c_str()); found = As<ifaces::cIGZPropertySetString>(mPropertyStore)->HasProperty(&key.text); }
        if (found) return 1;
    }
    return 0;
}
uint8_t cTSSimDNA::GetWeightedGenes(DNAWeightedVector *out, uint8_t second)
{
    for (auto *value = out->begin; value != out->end; ++value) DestroyWeighted(value);
    out->end = out->begin;
    if (!HasGene(10)) return 0;
    RetainedPair count(FetchPair(this, 10));
    const int32_t size = int32_t(ReadCount(second ? count.strings.second : count.strings.first));
    for (int32_t index = 0; index < size; ++index) {
        RetainedPair name(FetchPair(this, 0x400u + uint32_t(index)));
        RetainedPair value(FetchPair(this, 0x800u + uint32_t(index)));
        RetainedPair weight(FetchPair(this, 0xc00u + uint32_t(index)));
        ScopedWeightedGene result;
        auto *nameText = second ? name.strings.second : name.strings.first;
        auto *valueText = second ? value.strings.second : value.strings.first;
        auto *weightText = second ? weight.strings.second : weight.strings.first;
        if (nameText) result.Get()->name.Copy(*nameText);
        if (valueText) result.Get()->value.Copy(*valueText);
        if (weightText) {
            const int32_t number = std::atoi(weightText->ToChar());
            result.Get()->weight = number < 0 ? 0 : number > 255 ? 255 : number;
        }
        if (out->end == out->capacity) out->InsertReallocate(out->end, result.Get());
        else {
            if (second) CopyWeightedInline(out->end, *result.Get());
            else CopyWeighted(out->end, result.Get());
            ++out->end;
        }
    }
    return 1;
}
uint8_t cTSSimDNA::GetStringGenes(DNAStringVector *out) { return ReadStrings(this, out, 11, 0x1000); }
uint8_t cTSSimDNA::GetAdditionalStringGenes(DNAStringVector *out) { return ReadStrings(this, out, 15, 0x1400); }
uint8_t cTSSimDNA::SetStringGenes(const DNAStringVector *values, uint8_t both)
{
    if (!mPropertyStore) return 0;
    WriteStrings(this, values, both, 11, 0x1000);
    return 1;
}
uint8_t cTSSimDNA::SetAdditionalStringGenes(const DNAStringVector *values, uint8_t both)
{
    if (!mPropertyStore) return 0;
    WriteStrings(this, values, both, 15, 0x1400);
    return 1;
}
uint8_t cTSSimDNA::SetWeightedGenes(const DNAWeightedVector *values, uint8_t both)
{
    if (!mPropertyStore) return 0;
    RetainedPair count(FetchPair(this, 10));
    {
        TemporaryString number;
        number.text.Sprintf(GameAddress<char>(0xd9b648), values->Size());
        count.strings.first->Copy(*number.text.AsInterface());
        if (both) count.strings.second->Copy(*number.text.AsInterface());
        SetGene(10, &count.strings);
        for (uint32_t index = 0; index < values->Size(); ++index) {
            const auto &gene = values->begin[index];
            RetainedPair name(FetchPair(this, 0x400 + index));
            RetainedPair value(FetchPair(this, 0x800 + index));
            RetainedPair weight(FetchPair(this, 0xc00 + index));
            name.strings.first->Copy(*gene.name.AsInterface());
            value.strings.first->Copy(*gene.value.AsInterface());
            weight.strings.first->Sprintf(GameAddress<char>(0xd9b648), gene.weight);
            if (both) {
                name.strings.second->Copy(*gene.name.AsInterface());
                value.strings.second->Copy(*gene.value.AsInterface());
                weight.strings.second->Sprintf(GameAddress<char>(0xd9b648), gene.weight);
            }
            SetGene(0x400 + index, &name.strings);
            SetGene(0x800 + index, &value.strings);
            SetGene(0xc00 + index, &weight.strings);
        }
    }
    return 1;
}

template<> DNAWeightedGene *DNAVector<DNAWeightedGene>::InsertReallocate(DNAWeightedGene *position, const DNAWeightedGene *value)
{
    constexpr uint32_t kMaximum = 0x3c3c3c3;
    const uint32_t size = Size();
    if (size == kMaximum) std::_Xlength_error("vector too long");
    const uint32_t oldCapacity = begin ? uint32_t(capacity - begin) : 0;
    uint32_t newCapacity = oldCapacity > kMaximum - oldCapacity / 2 ? kMaximum : oldCapacity + oldCapacity / 2;
    if (newCapacity < size + 1) newCapacity = size + 1;
    const uint32_t offset = begin ? uint32_t(position - begin) : 0;
    auto *storage = static_cast<DNAWeightedGene *>(AllocateVectorBlock(newCapacity * 68));
    auto *inserted = storage + offset;
    auto *cleanupFirst = inserted + 1;
    try {
        CopyWeighted(inserted, value);
        cleanupFirst = inserted;
        if (position == end) CopyWeightedRange(begin, end, storage);
        else {
            CopyWeightedRange(begin, position, storage);
            cleanupFirst = storage;
            CopyWeightedRange(position, end, inserted + 1);
        }
    } catch (...) {
        for (auto *element = cleanupFirst; element != inserted + 1; ++element) DestroyWeighted(element);
        FreeThroughAllocatorService(storage, newCapacity);
        throw;
    }
    if (begin) {
        for (auto *element = begin; element != end; ++element) DestroyWeighted(element);
        FreeThroughAllocatorService(begin, oldCapacity);
    }
    begin = storage; end = storage + size + 1; capacity = storage + newCapacity;
    return inserted;
}

// ---- the RVA hooks' entry points ----

using Adapters = cTSSimDNAAdapters;

// No name reaches the deleting destructor without dispatch: the two steps by hand (stores our vtable in a game-built object
// before freeing it; only visible with flags 0).
bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(id, out);
}
uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}
}
