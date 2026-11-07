#pragma once

#include "gz/cIGZUnknown.h"
#include "rz/cRZVariant.h"

#include <cstddef>
#include <cstdint>
#include <map>

// Interface implemented by the command parameter collection. Slot order follows the image's
// 16-entry vtable (0xeca57c); arguments are typed from the stack cleanup at each entry.
class cIGZCommandParameterSet : public cIGZUnknown {
public:
    virtual uint32_t GetParameterCount() const = 0; // 3
    virtual void SetParameterCount(uint32_t count) = 0; // 4
    virtual cRZVariant *GetParameter(uint32_t index) = 0; // 5
    virtual cRZVariant *FindParameter(uint32_t index) = 0; // 6
    virtual void SetParameter(uint32_t index, const cIGZVariant *value) = 0; // 7
    virtual void AppendParameter(const cIGZVariant *value) = 0; // 8
    virtual void RemoveParameter(uint32_t index) = 0; // 9
    virtual void SetVersion(int32_t version) = 0; // 10
    virtual bool FindText(int32_t index, const char *text, uint32_t flags) = 0; // 11
    virtual bool IsSpecialParameter(uint32_t index, uint32_t type) = 0; // 12
    virtual int32_t GetFirstParameterIndex() const = 0; // 13: lowest key, or -1
    virtual int32_t GetNextParameterIndex(uint32_t index) = 0; // 14
    virtual ~cIGZCommandParameterSet() = default; // 15, deleting destructor in the image
};

class cGZCommandParameterSet : public cIGZCommandParameterSet {
public:
    static constexpr uint32_t kVTable = 0xeca57c;

    cGZCommandParameterSet();
    ~cGZCommandParameterSet() override;

    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    uint32_t GetParameterCount() const override;
    void SetParameterCount(uint32_t count) override;
    cRZVariant *GetParameter(uint32_t index) override;
    cRZVariant *FindParameter(uint32_t index) override;
    void SetParameter(uint32_t index, const cIGZVariant *value) override;
    void AppendParameter(const cIGZVariant *value) override;
    void RemoveParameter(uint32_t index) override;
    void SetVersion(int32_t version) override;
    bool FindText(int32_t index, const char *text, uint32_t flags) override;
    bool IsSpecialParameter(uint32_t index, uint32_t type) override;
    int32_t GetFirstParameterIndex() const override;
    int32_t GetNextParameterIndex(uint32_t index) override;

public:
    uint32_t mRefCount; // +4: shared AddRef / Release state
    std::map<uint32_t, cRZVariant> mValues; // +8: tree head and element count
    cRZVariant mDefault; // +0x10: returned for an absent index by slot 5
};

static_assert(offsetof(cGZCommandParameterSet, mRefCount) == 4);
static_assert(offsetof(cGZCommandParameterSet, mValues) == 8);
static_assert(offsetof(cGZCommandParameterSet, mDefault) == 0x10);
static_assert(sizeof(cGZCommandParameterSet) == 0x24);

struct cGZCommandParameterSetAdapters {
    static bool __fastcall QueryInterface(cGZCommandParameterSet *, void *, uint32_t, void **);
    static uint32_t __fastcall GetParameterCount(cGZCommandParameterSet *, void *);
    static void __fastcall SetParameterCount(cGZCommandParameterSet *, void *, uint32_t);
    static cRZVariant *__fastcall GetParameter(cGZCommandParameterSet *, void *, uint32_t);
    static cRZVariant *__fastcall FindParameter(cGZCommandParameterSet *, void *, uint32_t);
    static void __fastcall SetParameter(cGZCommandParameterSet *, void *, uint32_t, const cIGZVariant *);
    static void __fastcall AppendParameter(cGZCommandParameterSet *, void *, const cIGZVariant *);
    static void __fastcall RemoveParameter(cGZCommandParameterSet *, void *, uint32_t);
    static void __fastcall SetVersion(cGZCommandParameterSet *, void *, int32_t);
    static bool __fastcall FindText(cGZCommandParameterSet *, void *, int32_t, const char *, uint32_t);
    static bool __fastcall IsSpecialParameter(cGZCommandParameterSet *, void *, uint32_t, uint32_t);
    static int32_t __fastcall GetFirstParameterIndex(cGZCommandParameterSet *, void *);
    static int32_t __fastcall GetNextParameterIndex(cGZCommandParameterSet *, void *, uint32_t);
    static cGZCommandParameterSet *__fastcall DeletingDestructor(cGZCommandParameterSet *, void *, uint32_t);
};
