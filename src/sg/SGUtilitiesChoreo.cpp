// nGZSceneGraph's free helpers, part 2 (lane 261111c): the choreography queue helpers. See the header.
#include "sg/SGUtilities.h"

#include "compgeom/ConvexMath.h" // the SSE operations in operand order
#include "game/vtable.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/SGNodeInterfaces.h"

namespace nGZSceneGraph {

namespace {

// A face's virtual base number `index`, the vbptr at `vbptrOffset` (the queue: +4; the handlers: +0).
void *VirtualBaseAt(void *face, int vbptrOffset, int index)
{
    char *p = static_cast<char *>(face) + vbptrOffset;
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}
cIExtension *QueueExtension(void *queue) { return static_cast<cIExtension *>(VirtualBaseAt(queue, 4, 1)); }
cIExtension *HandlerExtension(void *handler) { return static_cast<cIExtension *>(VirtualBaseAt(handler, 0, 1)); }
void *HandlerFace(void *handler) { return handler ? VirtualBaseAt(handler, 0, 2) : nullptr; }

constexpr uint32_t kQueueClass = 0xdaff3bda, kQueueIid = 0x1aff3bd4;
constexpr uint32_t kAnimHandlerClass = 0xa9de6c9c, kAnimHandlerIid = 0xa9de6c97;
constexpr uint32_t kEventHandlerClass = 0xc8ebf859, kEventHandlerIid = 0xc8ebf854;
constexpr uint32_t kCameraNodeIid = 0x5ca76db6;
constexpr int kFrameworkCreate = 3; // the framework's CreateInstance(class, iid, out)

// The cIChoreoQueue face's slots (cChoreoQueue's +332 table; arguments from the bodies' ret).
constexpr int kQueueSlot0 = 0;        // (1): 0x9d5390, ret 4
constexpr int kQueueAddHandler = 17;  // (handler face): 0x9d22d0, ret 4
constexpr int kQueueSlot40 = 40;      // (1): 0x9d3260, ret 4
constexpr int kQueueManager = 54;     // (): 0x9d29f0, the word at +24 of the object
constexpr int kQueueFindHandler = 59; // (iid, out, start): 0x9d4010, ret 0xc
// The anim manager's (cAnimManager's cIAnimManager face, +112; tentative) slots.
constexpr int kManagerAdd = 6, kManagerRemove = 7, kManagerIndexOf = 15; // each (queue), ret 4

bool Create(uint32_t classId, uint32_t iid, void **out)
{
    void *framework = GetDirectorFrameworkObject();
    // receiver: the framework (cIGZFrameworkW32's slot 3, CreateInstance).
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), kFrameworkCreate>(framework, classId,
                                                                                                    iid, out);
}

bool FindHandler(void *queue, uint32_t iid, void **out)
{
    // receiver: the cIChoreoQueue face.
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **, void *), kQueueFindHandler>(queue, iid, out,
                                                                                                  nullptr);
}

void AddHandler(void *queue, void *handler) { RawSlot<kQueueAddHandler, uint32_t>(queue, HandlerFace(handler)); }

} // namespace

// 0xae2dc0
void *CreateChoreoQueue(cICompositionTreeNode *node)
{
    void *queue = nullptr;
    Create(kQueueClass, kQueueIid, &queue);
    node->AddExtension(queue ? QueueExtension(queue) : nullptr);
    if (queue) QueueExtension(queue)->Release();
    return queue;
}

// 0xae39c0
void *AttachChoreoQueue(cICompositionTreeNode *node, void *animManager)
{
    cIGZUnknown *camera = nullptr;
    if (node->QueryInterface(kCameraNodeIid, reinterpret_cast<void **>(&camera))) SetNodeTag(node, "cinecam");
    void *queue = AnimatedFaceOf(node);
    if (queue) QueueExtension(queue)->AddRef();
    if (queue) {
        RawSlot<kQueueSlot0, uint32_t>(queue, 1u); // receiver: the cIChoreoQueue face
    } else {
        void *made = CreateChoreoQueue(node);
        if (made) {
            QueueExtension(made)->AddRef();
            queue = made;
        }
        // receiver: the anim manager the cTSSGSystem holds (its slot 6 takes the new queue).
        // ORIGINAL-BUG: a failed creation is handed on and then called through null (0xae3a69, 0xae3a78), as here.
        RawSlot<kManagerAdd, uint32_t>(animManager, queue);
    }
    RawSlot<kQueueSlot40, uint32_t>(queue, 1u);

    void *anim = nullptr;
    if (!FindHandler(queue, kAnimHandlerIid, &anim)) {
        void *framework = GetDirectorFrameworkObject();
        if (anim) { // a miss that wrote one: dropped before the creation writes the same word
            void *old = anim;
            anim = nullptr;
            HandlerExtension(old)->Release();
        }
        // receiver: the framework (slot 3, CreateInstance).
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), kFrameworkCreate>(
            framework, kAnimHandlerClass, kAnimHandlerIid, &anim);
        AddHandler(queue, anim);
    }

    void *event = nullptr;
    if (!FindHandler(queue, kEventHandlerIid, &event)) {
        void *made = nullptr;
        Create(kEventHandlerClass, kEventHandlerIid, &made);
        AddHandler(queue, made);
        if (made) HandlerExtension(made)->Release();
    }

    if (event) HandlerExtension(event)->Release();
    if (anim) HandlerExtension(anim)->Release();
    if (queue) QueueExtension(queue)->Release();
    if (camera) camera->Release();
    return queue;
}

// 0xae3c00
void DetachChoreoQueue(cICompositionTreeNode *node)
{
    void *queue = AnimatedFaceOf(node);
    if (!queue) return;
    QueueExtension(queue)->AddRef();
    RawSlot<kQueueSlot0, uint32_t>(queue, 1u); // receiver: the cIChoreoQueue face
    if (QueueExtension(queue)->GetOwner()) {
        cIExtension *extension = QueueExtension(queue);
        extension->GetOwner()->RemoveExtension(extension);
    }
    // receiver: the queue's anim manager (cIAnimManager face; tentative).
    void *manager = RawSlot<kQueueManager, void *>(queue);
    if (manager && RawSlot<kManagerIndexOf, int32_t>(manager, queue) >= 0) RawSlot<kManagerRemove, uint32_t>(manager, queue);
    QueueExtension(queue)->Release();
}

// ---- animations --------------------------------------------------------------------------------------------------

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;

constexpr uint32_t kBlendClass = 0x8b969d64, kBlendIid = 0x8b969d59;
constexpr uint32_t kAnimationClass = 0xc9de69b6, kAnimationIid = 0xc9de69ad;
constexpr int kAnimationDuration = 36;  // the animation's (): its length, an x87 float
constexpr int kAnimationMetadata = 24;  // the animation's (0): an object asked for kinds (slot 16), or null
constexpr int kMetadataHasKind = 16;    // (kind, group): nonzero when it has one
constexpr int kBlendCurveDuration = 19; // the blend's (elapsed): a float (x87)

// An unsigned count as a float, as the original converts it (int to double, 2^32 added when negative, rounded once).
float CountAsFloat(uint32_t count) { return float(double(count)); }

// The animation instance (class 0xc9de69b6): its own vtable at +0 (slot 0 (scene, placement), slot 2 (1)), a vbptr at
// +4: entry 1 its cIGZUnknown, entry 2 its controller face (slots 9-26 the settings).
void *Unknown(void *instance) { return VirtualBaseAt(instance, 4, 1); }
void *Controller(void *instance) { return VirtualBaseAt(instance, 4, 2); }

// The instance's reference, released on the way out (normal or not), as the original's frame does.
struct InstanceRef {
    void *instance = nullptr;
    ~InstanceRef()
    {
        if (instance) RawSlot<2, uint32_t>(Unknown(instance)); // receiver: the instance's cIGZUnknown
    }
};

template <int kSlot, typename... A> void SetOn(void *controller, A... args)
{
    // receiver: the animation instance's controller face (its settings; arguments: one word each, from the pushes).
    VirtualCall<void(__thiscall *)(void *, A...), kSlot>(controller, args...);
}

} // namespace

// 0xae4ef0
bool MakeAnimationBlend(void **out, void *animation, float speed, int32_t loops, float start, float end, int32_t mode,
                        float blendIn, float blendOut, float a9, float a10, float a11)
{
    *out = nullptr;
    Create(kBlendClass, kBlendIid, out);
    float length = 2147483392.0f; // the largest float under 2^31
    if (loops != -1) {
        // receiver: the animation (its slot 36, no arguments).
        const float duration = VirtualCall<float(__thiscall *)(void *), kAnimationDuration>(animation);
        // SSE, in the original's operand order (the first operand's NaN wins).
        if (duration == 0.0f) {
            length = CountAsFloat(uint32_t(loops));
            if (start > end - 1e-6f) length = length - 1.0f;
        } else {
            const float sum = AddS(MulS(SubS(end, start), duration), MulS(CountAsFloat(uint32_t(loops) - 1u), duration));
            length = 0.0f > sum ? 0.0f : sum; // maxss 0, sum
        }
        length = DivS(length, speed);
        if (0.0f > length) length = Negated(length);
    }
    float total = 0.0f;
    if (mode == 0)
        total = AddS(blendIn, blendOut);
    else if (mode == 1)
        total = MulS(AddS(blendIn, blendOut), 0.5f);
    bool fits = true;
    if (total > length + 1e-6f) {
        const float scale = MulS(DivS(length, total), 0.9999f);
        fits = !(total > length + 1e-5f);
        blendOut = MulS(blendOut, scale);
        blendIn = MulS(blendIn, scale);
    }
    if (1.0f > blendIn) blendIn = 0.0f;
    if (1.0f > blendOut) blendOut = 0.0f;
    // receiver: the new blend (its own slot 2, 13 words). ORIGINAL-BUG: a failed creation is called through null.
    VirtualCall<void(__thiscall *)(void *, uint32_t, int32_t, float, float, float, float, float, float, uint32_t,
                                   uint32_t, uint32_t, uint32_t, uint32_t),
                2>(*out, 1u, mode, a9, a10, a11, blendIn, blendOut, length, 0u, 0u, 0u, 0u, 0u);
    return fits;
}

// 0xae1f50
bool AttachAnimation(void **out, void *target, void *scene, const void *placement, void *animation, int32_t now,
                     int32_t start, float a7, float a8, uint32_t a9, uint32_t a10, float a11, void *blend, uint32_t a13,
                     uint32_t a14, float a15, float a16, uint32_t a17, uint32_t a18, void *a19, uint32_t a20,
                     uint32_t a21)
{
    InstanceRef ref;
    // ORIGINAL-BUG: a failed creation is not tested; the instance is then used through null.
    Create(kAnimationClass, kAnimationIid, &ref.instance);
    void *const instance = ref.instance;
    // receiver: the animation (its slot 36, no arguments).
    const float duration = VirtualCall<float(__thiscall *)(void *), kAnimationDuration>(animation);
    float rate = MulS(duration, a7);
    if (0.0f > a15) rate = MulS(SubS(1.0f, a7), duration);
    if (blend && now != int32_t(0x80000100))
        // receiver: the blend (its slot 19 (elapsed)).
        a11 = VirtualCall<float(__thiscall *)(void *, float), kBlendCurveDuration>(blend, float(now - start));
    // receiver: the instance's own face (slot 0 (scene, placement)).
    if (scene) VirtualCall<void(__thiscall *)(void *, void *, const void *), 0>(instance, scene, placement);
    void *const controller = Controller(instance);
    SetOn<9>(controller, animation);
    SetOn<10>(controller, start);
    SetOn<11>(controller, a7);
    SetOn<12>(controller, a8);
    SetOn<13>(controller, a9);
    SetOn<14>(controller, a10);
    SetOn<15>(controller, a11);
    SetOn<16>(controller, blend);
    SetOn<17>(controller, a13);
    SetOn<18>(controller, a14);
    SetOn<19>(controller, a15);
    SetOn<20>(controller, a16);
    SetOn<24>(controller, a19);
    RawSlot<2, uint32_t>(instance, a20); // receiver: the instance's own face (slot 2 (1))
    SetOn<25>(controller, a21);
    SetOn<26>(controller, rate);
    if (a17) {
        // receiver: the animation (slot 24 (0)), then what it answers (slot 16 (kind, group)).
        void *metadata = VirtualCall<void *(__thiscall *)(void *, uint32_t), kAnimationMetadata>(animation, 0u);
        using HasKind = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
        if (metadata && (VirtualCall<HasKind, kMetadataHasKind>(metadata, 0xe6e97a71u, 0xfb5b3383u) ||
                         VirtualCall<HasKind, kMetadataHasKind>(metadata, 0x7af75043u, 0xfb5b3383u)))
            SetOn<21>(controller, a17, a18);
    }
    // receiver: the target (its slot 10 (controller)).
    if (target) RawSlot<10, uint32_t>(target, instance ? Controller(instance) : nullptr);
    *out = instance;
    RawSlot<1, uint32_t>(Unknown(instance)); // receiver: the instance's cIGZUnknown (AddRef)
    return true;
}

} // namespace nGZSceneGraph
