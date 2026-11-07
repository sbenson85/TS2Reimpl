#pragma once
// cIGZMessage2 (iid 0xa52294b4) and cIGZMessage2Standard (iid 0x65297976): the framework's typed messages, for classes in the
// standalone shape to derive from (lane IY, from cTSAudioEvent's +4 face, vtable 0xda827c, 37 slots; cRZMessage2Standard's
// vtable 0xda4064 has the same slots). The four data words, the string, the counted object and the extra object are
// properties or fields of the implementing class.
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZString;

class cIGZMessage2 : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xa52294b4;
    virtual bool Create(uint32_t iid, void **out) = 0;                 // slot 3 (ret 8)
    virtual uint32_t GetType() = 0;                                     // slot 4
    virtual uint32_t SetType(uint32_t type) = 0;                        // slot 5 (ret 4): returns type
    virtual bool IsEqual(cIGZMessage2 *other) = 0;                      // slot 6: the types equal
    virtual bool IsLess(cIGZMessage2 *other) = 0;                       // slot 7: this type below other's (unsigned)
};

class cIGZMessage2Standard : public cIGZMessage2 {
public:
    static constexpr uint32_t kIid = 0x65297976;
    virtual bool HasData1() = 0;                                        // slot 8
    virtual uint32_t GetData1() = 0;                                    // slot 9
    virtual void *GetVoid1() = 0;                                       // slot 10
    virtual void SetData1(uint32_t value) = 0;                          // slot 11
    virtual void SetVoid1(void *value) = 0;                             // slot 12
    virtual bool HasData2() = 0;                                        // slot 13
    virtual uint32_t GetData2() = 0;                                    // slot 14
    virtual void *GetVoid2() = 0;                                       // slot 15
    virtual void SetData2(uint32_t value) = 0;                          // slot 16
    virtual void SetVoid2(void *value) = 0;                             // slot 17
    virtual bool HasData3() = 0;                                        // slot 18
    virtual uint32_t GetData3() = 0;                                    // slot 19
    virtual void *GetVoid3() = 0;                                       // slot 20
    virtual void SetData3(uint32_t value) = 0;                          // slot 21
    virtual void SetVoid3(void *value) = 0;                             // slot 22
    virtual bool HasData4() = 0;                                        // slot 23
    virtual uint32_t GetData4() = 0;                                    // slot 24
    virtual void *GetVoid4() = 0;                                       // slot 25
    virtual void SetData4(uint32_t value) = 0;                          // slot 26
    virtual void SetVoid4(void *value) = 0;                             // slot 27
    virtual bool HasString() = 0;                                       // slot 28
    virtual bool GetString(uint32_t iid, void **out) = 0;               // slot 29 (ret 8): the string's QueryInterface
    virtual void SetString(const cIGZString *text) = 0;                 // slot 30
    virtual bool HasIGZUnknown() = 0;                                   // slot 31
    virtual cIGZUnknown *GetIGZUnknown() = 0;                           // slot 32: not AddRef'd
    virtual void SetIGZUnknown(cIGZUnknown *object) = 0;                // slot 33: counted
    virtual bool HasExtra() = 0;                                        // slot 34
    virtual bool GetExtra(uint32_t iid, void **out) = 0;                // slot 35 (ret 8): the object's QueryInterface
    virtual void SetExtra(cIGZUnknown *object) = 0;                     // slot 36
};
