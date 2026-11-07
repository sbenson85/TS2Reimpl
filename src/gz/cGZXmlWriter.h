#pragma once
// The XML writers of the XmlUtils COM directory (standalone shape): cGZXmlWriter (game vtable 0xe616e8, 13 slots,
// 0x18 bytes, abstract: slot 12 writes raw text), cGZXmlBufferWriter (0x3c bytes, vtables 0xe61614 / +0x18 0xe61650;
// framework class 0xed50b6ac) collecting the text in a string, and cGZXmlFileWriter (0x20 bytes, vtables 0xe615c8 /
// +0x18 0xe61600; framework class 0x0c7e8d17) handing it to a stream.
//
// The writer keeps one open construct at a time (state: none, a start tag still open, a processing instruction, a
// CDATA section); every call first closes what is open ("close": ">", "?>" or "]]>"). Elements nest (depth); with
// indenting on, each start tag and each end tag after child elements begins a new line indented by depth x width.
// Attribute values and text are escaped (the five XML entities); comments and CDATA are written as given.
//
// Creation: only the XmlUtils director's creators 0x9c5261 (buffer) and 0x9c528e (file), registered by address, build
// them (through the constructors 0x9c515d and 0x9c518f); the creators and constructors are hooked to ours. The writers'
// slots are reached only through their vtables.
//
// ORIGINAL-BUG: Release (shared by all three) destroys the object as a cGZXmlWriter and frees 0x18 bytes, whatever the
// class: the buffer writer's string buffer and the file writer's stream reference are never released. Ours does the
// same (the base's destructor is not virtual, as the original's).
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// The writer's interface (iid 0x8c0f3d0d). Each call returns false when a write failed (or the call does not fit the
// open construct).
class cIGZXmlWriter : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x8c0f3d0d;
    // The XML declaration: a processing instruction "xml" with version and encoding attributes (the game's strings).
    virtual bool StartDocument() = 0;                                     // slot 3
    virtual bool StartElement(const char *name) = 0;                      // slot 4
    // "/>" when the element's start tag is still open, else (indented) "</name>".
    virtual bool EndElement(const char *name) = 0;                        // slot 5
    virtual bool StartProcessingInstruction(const char *target) = 0;      // slot 6
    virtual bool EndProcessingInstruction() = 0;                          // slot 7
    // Only while a start tag or a processing instruction is open.
    virtual bool Attribute(const char *name, const char *value) = 0;      // slot 8
    virtual bool Text(const char *text) = 0;                              // slot 9
    // Opens a CDATA section unless one is open, then writes the text as is.
    virtual bool CData(const char *text) = 0;                             // slot 10
    // "<!-- ", the text, " -->". ORIGINAL-BUG: meant to break up runs of '-', the loop never moves its start: at each
    // run of dashes it writes the text from its beginning through the character after the run's first dash (the
    // terminating NUL when the dash is last), and at the end the whole text again.
    virtual bool Comment(const char *text) = 0;                           // slot 11
};

// The buffer writer's face (iid 0xed50b6ac, the framework class id too).
class cIGZXmlBufferWriter : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xed50b6ac;
    virtual cIGZString *GetString() = 0;                                  // slot 3 (folded 0x18ea60)
    virtual const char *GetText() = 0;                                    // slot 4: the string's ToChar
    virtual uint32_t GetLength() = 0;                                     // slot 5: the string's Strlen
};

// The file writer's face (iid 0x2c7ea02c).
class cIGZXmlFileWriter : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x2c7ea02c;
    // Holds `stream` (counted, the old one released) and sets indenting.
    virtual void SetStream(cIGZUnknown *stream, bool indent) = 0;         // slot 3
};

class cGZXmlWriter : public cIGZXmlWriter {
public:
    static constexpr uint32_t kSize = 0x18;
    static constexpr uint32_t kIndentWidth = 2;
    static constexpr uint32_t kMaxIndentChunk = 0x41;  // the indent text's spaces, written in chunks of at most this

    // What is open.
    enum State : uint32_t {
        kNone = 0,
        kStartTag = 1,
        kProcessingInstruction = 2,
        kCData = 3,
    };

    explicit cGZXmlWriter(bool indent);  // 0x9c53de

    bool QueryInterface(uint32_t riid, void **ppv) override;              // 0x9c567e
    uint32_t AddRef() override;                                           // folded 0x148fb0
    uint32_t Release() override;                                          // 0x9c56b1
    bool StartDocument() override;                                        // 0x9c5906
    bool StartElement(const char *name) override;                         // 0x9c548c
    bool EndElement(const char *name) override;                           // 0x9c557f
    bool StartProcessingInstruction(const char *target) override;         // 0x9c54eb
    bool EndProcessingInstruction() override;                             // 0x9c561e
    bool Attribute(const char *name, const char *value) override;         // 0x9c540f
    bool Text(const char *text) override;                                 // 0x9c5739
    bool CData(const char *text) override;                                // 0x9c56ee
    bool Comment(const char *text) override;                              // 0x9c5761
    // Slot 12: `length` bytes of text to the output.
    virtual bool Write(const char *text, uint32_t length) = 0;

    // 0x9c553c: closes what is open; false when that write failed.
    bool CloseOpen();
    // 0x9c57e7: `text` with the five entities escaped.
    bool WriteEscaped(const char *text);
    // 0x9c58b3: a new line and depth x width spaces, when indenting.
    bool WriteIndent();

    uint32_t mRefCount;   // 0x04 starts at 0
    bool mIndent;         // 0x08
    bool mInline;         // 0x09 set by a start tag; an end tag right after it (only text between) is not indented
    State mState;         // 0x0c
    uint32_t mDepth;      // 0x10
    uint32_t mIndentWidth; // 0x14 kIndentWidth

protected:
    // 0x9c5408: not virtual (see ORIGINAL-BUG above).
    ~cGZXmlWriter();
};

static_assert(sizeof(cGZXmlWriter) == cGZXmlWriter::kSize, "cGZXmlWriter size");
static_assert(offsetof(cGZXmlWriter, mRefCount) == 0x4 && offsetof(cGZXmlWriter, mIndent) == 0x8 &&
                  offsetof(cGZXmlWriter, mInline) == 0x9 && offsetof(cGZXmlWriter, mState) == 0xc &&
                  offsetof(cGZXmlWriter, mDepth) == 0x10 && offsetof(cGZXmlWriter, mIndentWidth) == 0x14,
              "cGZXmlWriter layout");

class cGZXmlBufferWriter : public cGZXmlWriter, public cIGZXmlBufferWriter {
public:
    static constexpr uint32_t kSize = 0x3c;

    cGZXmlBufferWriter();            // 0x9c515d (indenting on)
    virtual ~cGZXmlBufferWriter();   // 0x9c5218; slot 13 the deleting destructor 0x9c523f

    cGZXmlBufferWriter(const cGZXmlBufferWriter &) = delete;
    cGZXmlBufferWriter &operator=(const cGZXmlBufferWriter &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override;  // 0x9c533a; +0x18 thunk 0x9c5367
    uint32_t AddRef() override;                               // folded 0x146a50; +0x18 thunk folded 0x242026
    uint32_t Release() override;                              // 0x9c5377; +0x18 thunk 0x9c537c
    bool Write(const char *text, uint32_t length) override;   // 0x9c5384: appended to the string
    cIGZString *GetString() override;                         // folded 0x18ea60
    const char *GetText() override;                           // 0x9c52bb
    uint32_t GetLength() override;                            // 0x9c52c3

    // 0x1c: the text written so far (a cRZString: the game's vtable, its count unused). In a union so that only the
    // explicit Construct / Destruct run, as in the original.
    union {
        cRZString mText;
    };
};

static_assert(sizeof(cGZXmlBufferWriter) == cGZXmlBufferWriter::kSize, "cGZXmlBufferWriter size");
static_assert(offsetof(cGZXmlBufferWriter, mText) == 0x1c, "cGZXmlBufferWriter layout");

class cGZXmlFileWriter : public cGZXmlWriter, public cIGZXmlFileWriter {
public:
    static constexpr uint32_t kSize = 0x20;
    // The stream is a cIGZFile (SetStream takes it as a cIGZUnknown); Write calls its Write, slot 16.
    static constexpr int kStreamWriteSlot = 16;

    cGZXmlFileWriter();  // 0x9c518f (indenting on, no stream)

    cGZXmlFileWriter(const cGZXmlFileWriter &) = delete;
    cGZXmlFileWriter &operator=(const cGZXmlFileWriter &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override;  // 0x9c564f; +0x18 thunk 0x9c536f
    uint32_t AddRef() override;                               // folded 0x146a50; +0x18 thunk folded 0x242026
    uint32_t Release() override;                              // 0x9c5377; +0x18 thunk 0x9c537c
    // 0x9c53d2: the stream's cIGZFile::Write (slot 16) with (text, length), its result.
    bool Write(const char *text, uint32_t length) override;
    void SetStream(cIGZUnknown *stream, bool indent) override; // 0x9c56d2

    cIGZUnknown *mStream;  // 0x1c counted (RZUnknownSlot::Assign)
};

static_assert(sizeof(cGZXmlFileWriter) == cGZXmlFileWriter::kSize, "cGZXmlFileWriter size");
static_assert(offsetof(cGZXmlFileWriter, mStream) == 0x1c, "cGZXmlFileWriter layout");

// The hooks' entry points (docs/standalone-shape.md).
struct cGZXmlWriterAdapters {
    // 0x9c5261 / 0x9c528e (__cdecl, no arguments): the XmlUtils director's creators, `new` of each class (null when
    // the allocation fails).
    static cGZXmlBufferWriter *__cdecl CreateBufferWriter();
    static cGZXmlFileWriter *__cdecl CreateFileWriter();
    // 0x9c515d / 0x9c518f: the constructors on `memory` (only the creators call them).
    static cGZXmlBufferWriter *__fastcall ConstructBufferWriter(void *memory, void *edx);
    static cGZXmlFileWriter *__fastcall ConstructFileWriter(void *memory, void *edx);
};

// The game's strings the writer emits (RVAs of their bytes).
namespace gzxml {
constexpr uint32_t kOpenTag = 0xdcb1bc;          // "<"
constexpr uint32_t kCloseTag = 0xdcb1c0;         // ">"
constexpr uint32_t kCloseEmptyTag = 0xdcb204;    // "/>"
constexpr uint32_t kOpenEndTag = 0xe61768;       // "</"
constexpr uint32_t kOpenPI = 0xe6176c;           // "<?"
constexpr uint32_t kClosePI = 0xe61764;          // "?>"
constexpr uint32_t kOpenCData = 0xda4eb0;        // "<![CDATA["
constexpr uint32_t kCloseCData = 0xda4ebc;       // "]]>"
constexpr uint32_t kOpenComment = 0xe61770;      // "<!-- "
constexpr uint32_t kCloseComment = 0xe61778;     // " -->"
constexpr uint32_t kSpace = 0xda4ca4;            // " "
constexpr uint32_t kEqualsQuote = 0xe4a088;      // "=\""
constexpr uint32_t kQuote = 0xda296c;            // "\""
constexpr uint32_t kQuotEntity = 0xe4a080;       // "&quot;"
constexpr uint32_t kAposEntity = 0xe4a078;       // "&apos;"
constexpr uint32_t kGtEntity = 0xe4a070;         // "&gt;"
constexpr uint32_t kLtEntity = 0xe4a068;         // "&lt;"
constexpr uint32_t kAmpEntity = 0xe4a060;        // "&amp;"
// A new line and 64 spaces (0xe61721 the spaces alone), then its NUL. ORIGINAL-BUG: the indent is written in chunks
// of up to 0x41 spaces, so an indent of 0x41 or more also writes the NUL after the spaces.
constexpr uint32_t kNewlineIndent = 0xe61720;
constexpr uint32_t kIndentSpaces = 0xe61721;
constexpr uint32_t kXmlTarget = 0xda9c30;        // the declaration's target
constexpr uint32_t kVersionName = 0xdbc1a4;
constexpr uint32_t kVersionValue = 0xe61780;
constexpr uint32_t kEncodingName = 0xe61784;
constexpr uint32_t kEncodingValue = 0xda2604;
} // namespace gzxml
