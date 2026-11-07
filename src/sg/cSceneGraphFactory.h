#pragma once
// nGZSceneGraph::cSceneGraphFactory (lane RC, scenegraph N1 rest): the scene graph's persist resource factory, the singleton
// the scene graph system registers its resource classes with and reads / writes scene graph resources through. A persist
// resource factory (cIGZPersistResourceFactory) for the scene graph's record types, and, through a virtual base, a
// cISceneGraphFactory (the scene graph's own face of it). Standalone shape (docs/standalone-shape.md): a real C++ class
// with virtual inheritance, so the compiler lays out what the game's compiler laid out:
//
//   +0x00 cGZPersistResourceFactory part (vtable 0xe6c568, 12 slots; cIGZPersistResourceFactory at +0): refcount +4, lock +8
//   +0x10 vbptr (vbtable 0xe6c5cc: entry 1 = 0x20 to the virtual cIGZUnknown, entry 2 = 0x28 to the face)
//   +0x14 mOption14 (uint32_t)  +0x18 mOption18 (uint32_t)
//   +0x1c mUnused (counted)     +0x20 mWriterSource (counted: the writer slot 7 makes)  +0x24 mReader  +0x28 mWriter
//   +0x2c vtordisp, +0x30 virtual base cIGZUnknown (vtable 0xe6c59c, 3 slots), +0x34 vtordisp,
//   +0x38 virtual base cISceneGraphFactory (vtable 0xe6c5ac, 8 slots) with its own vbptr at +0x3c (vbtable 0xe6c5d8)
//
// 0x40 bytes. Made only by GetSceneGraphFactory (0xa4d660: a static, global 0x10f61b0, new 0x40 and the constructor
// 0xa4d310), whose caller is the system's constructor and Init; the creator 0xa4d600 (registered under class id 0xe5259c1b
// with the COM director) hands out the object's virtual cIGZUnknown. The destructor body 0xa4d3e0 clears the global and
// releases the four counted fields (+0x28, +0x24, +0x20, +0x1c in that order).
//
// The class ids Init registers come from sixteen `mov eax, imm32; ret` statics the director also hands out as the keys of its
// class creators; they are cSceneGraphClassIds here (kept together: each is the class's id, nothing else).
//
// Read (0xa4d750) wraps a record's cIGZIStream in a cIStreamStringBehaviourAdaptor and has the binary reader read the
// resource; Write (0xa4da90) the same with a cOStreamStringBehaviourAdaptor and the binary writer. RVAs are 1.18.0.187.
#include "game/owned.h"
#include "gz/cIGZPersistResourceFactory.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>

namespace ifaces {
class cIGZPersistDBRecord;
} // namespace ifaces

namespace nGZSceneGraph {

class cSceneGraphFactory;

// The binary reader / writer the factory asks the COM director for (classes 0x5b101e6b and 0xbb100367, cGZPersistBinaryReader /
// cGZPersistBinaryWriter): the one slot the factory uses.
class cIBinaryReader : public cIGZUnknown {
public:
    virtual void ReadRoot(void *root, void *stream, void *context) = 0; // slot 3
};
class cIBinaryWriter : public cIGZUnknown {
public:
    virtual void WriteRoot(void *root, void *stream, void *context) = 0; // slot 3
};

// The factory's cISceneGraphFactory face (virtual base at +0x38, vtable 0xe6c5ac): its cIGZUnknown is a virtual base too.
class cISceneGraphFactory : public virtual cIGZUnknown {
public:
    virtual void RegisterClass(uint32_t classId) = 0;       // 0: the resource manager's slot 20 (0xe5259c1b, classId, 0)
    virtual void SetBinaryReader(cIBinaryReader *reader) = 0; // 1: the counted reader at +0x24
    virtual void SetBinaryWriter(cIBinaryWriter *writer) = 0; // 2: the counted writer at +0x28
    virtual void SetOption14(uint32_t value) = 0;           // 3
    virtual void SetOption18(uint32_t value) = 0;           // 4: with 0 the writer follows the writer source
    virtual cIGZPersistResourceFactory *GetResourceFactory() = 0; // 5: the factory itself, as its primary face
    virtual cIBinaryReader *GetBinaryReader() = 0;          // 6: made on first use
    virtual cIBinaryWriter *GetBinaryWriter() = 0;          // 7: made on first use
};

// The part at +0 (the game's cGZPersistResourceFactory part): refcount at +4 and the lock at +8 before the class's vbptr.
class cSceneGraphFactoryBase : public cIGZPersistResourceFactory {
public:
    cSceneGraphFactoryBase();  // the base constructor 0x9b6010 inline: count 0, the lock built
    ~cSceneGraphFactoryBase(); // the base destructor 0x9b6040: the lock

protected:
    uint32_t mRefCount;
    cRZCriticalSection mLock;
};

class cSceneGraphFactory : public cSceneGraphFactoryBase, public virtual cISceneGraphFactory {
public:
    static constexpr uint32_t kClassId = 0xe5259c1b;       // the factory's own class id (the creator 0xa4d600's key)
    static constexpr uint32_t kFaceId = 0x85259c23;        // QueryInterface answers it with the cISceneGraphFactory face
    static constexpr uint32_t kIStreamId = 0x051b11bd;     // cIGZIStream: asked of the record by Read
    static constexpr uint32_t kSerialIdRead = 0x9b11bf70;  // asked of the resource by Read
    static constexpr uint32_t kContextId = 0x656b8f0c;     // asked of the record by Read: the reader's context
    static constexpr uint32_t kOStreamId = 0xc51b11cb;     // cIGZOStream: asked of the record by Write
    static constexpr uint32_t kSerialIdWrite = 0x9b4bbca3; // asked of the resource by Write
    static constexpr uint32_t kReaderClass = 0x5b101e6b, kReaderIid = 0xfb0c4344;
    static constexpr uint32_t kWriterClass = 0xbb100367, kWriterIid = 0x1b0aab0e;

    // 0xa4d660 (cdecl): the one factory, made on first use (global 0x10f61b0); null when the allocation fails.
    static cSceneGraphFactory *Get();
    // 0xa4d600 (cdecl): the class creator: the factory's virtual cIGZUnknown, or null without a factory.
    static cIGZUnknown *Create();

    cSceneGraphFactory();  // 0xa4d310: the base part, the counted fields zero
    virtual ~cSceneGraphFactory(); // slot 7 (0xa4d570) with the body 0xa4d3e0: the global cleared, the four fields released
    cSceneGraphFactory(const cSceneGraphFactory &) = delete;
    cSceneGraphFactory &operator=(const cSceneGraphFactory &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0xa4d700
    uint32_t AddRef() override;                              // slot 1 (the base's 0x9b60b0)
    uint32_t Release() override;                             // slot 2 (the base's 0x9b6290)
    bool CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                        uint32_t a5) override;                // slot 3 (the base's 0x9b60e0)
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4,
                              uint32_t a5) override;          // slot 4 (the base's 0x9b61b0)
    bool Read(cIGZUnknown *resource, cIGZUnknown *record) override;  // slot 5, 0xa4d750
    bool Write(cIGZUnknown *resource, cIGZUnknown *record) override; // slot 6, 0xa4da90
    virtual bool CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out); // slot 8 (shared 0x1f62f0)
    virtual uint32_t CreateResource(uint32_t type, uint32_t riid, void **out);                       // slot 9, 0xa4d5c0
    virtual bool ReadRecord(cIGZUnknown *resource, cIGZUnknown *record);                         // slot 10 (shared 0x1474b0)
    virtual bool WriteRecord(cIGZUnknown *resource, cIGZUnknown *record);                        // slot 11 (shared 0x1474b0)

    void RegisterClass(uint32_t classId) override;               // face 0, 0xa4d900
    void SetBinaryReader(cIBinaryReader *reader) override;       // face 1, 0xa4d9b0
    void SetBinaryWriter(cIBinaryWriter *writer) override;       // face 2, 0xa4da47
    void SetOption14(uint32_t value) override;                   // face 3, 0xa4d97a
    void SetOption18(uint32_t value) override;                   // face 4, 0xa4d9df
    cIGZPersistResourceFactory *GetResourceFactory() override;   // face 5, 0xa4d5b0
    cIBinaryReader *GetBinaryReader() override;                  // face 6, 0xa4d8c7
    cIBinaryWriter *GetBinaryWriter() override;                  // face 7, 0xa4dbf0

    uint32_t mOption14;         // +0x14
    uint32_t mOption18;         // +0x18
    cIGZUnknown *mUnused;       // +0x1c (counted; nothing sets it)
    cIBinaryWriter *mWriterSource; // +0x20 (counted)
    cIBinaryReader *mReader;    // +0x24 (counted)
    cIBinaryWriter *mWriter;    // +0x28 (counted)
};

// The class ids the director registers creators under and Init registers with the factory (cdecl, no arguments): the
// statics 0xa5d410 ... 0xab1670 (each `mov eax, imm32; ret`).
struct cSceneGraphClassIds {
    static uint32_t __cdecl Factory();             // 0xa4d630: the factory's own
    static uint32_t __cdecl Shape();               // 0xa5d410
    static uint32_t __cdecl AmbientLight();        // 0xa47d50
    static uint32_t __cdecl DirectionalLight();    // 0xa47d60
    static uint32_t __cdecl PointLight();          // 0xa47da0
    static uint32_t __cdecl SpotLight();           // 0xa47db0
    static uint32_t __cdecl View();                // 0xa66080
    static uint32_t __cdecl GeometryNode();        // 0xa425f0
    static uint32_t __cdecl IndexedMeshBuilder();  // 0xa80020
    static uint32_t __cdecl CompactorBuilder();    // 0xa839b0
    static uint32_t __cdecl BoundingVolumeBuilder(); // 0xa85110
    static uint32_t __cdecl TangentSpaceBuilder(); // 0xa86c70
    static uint32_t __cdecl OldPart();             // 0xa8f2f0
    static uint32_t __cdecl Unknown925d0();        // 0xa925d0
    static uint32_t __cdecl MaterialDefinition();  // 0xaadf80 (TXMT)
    static uint32_t __cdecl Texture();             // 0xab1660 (TXTR)
    static uint32_t __cdecl LevelOfDetail();       // 0xab1670 (LIFO)
};

} // namespace nGZSceneGraph

// The singleton: global 0x10f61b0 (null until the first Get; the destructor clears it).
TS2_OWNED_STATIC(nGZSceneGraph::cSceneGraphFactory *, 10f61b0);

namespace nGZSceneGraph {
static_assert(sizeof(cSceneGraphFactory) == 0x40, "cSceneGraphFactory is 0x40 bytes (new 0x40 in the getter)");
} // namespace nGZSceneGraph
