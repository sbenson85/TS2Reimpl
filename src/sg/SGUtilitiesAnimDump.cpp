// nGZSceneGraph's animation-channel dump (lane 261111k): 0xae3050. The release build keeps the dump's text building but
// not its output: every line goes into strings on the stack that are dropped at the end of each channel (the sink was
// compiled out). What stays observable is the getters it calls, in order, and the text handed to cRZString::Sprintf.
#include "sg/SGUtilities.h"

#include "game/vtable.h"
#include "rz/cRZString.h"

#include <cstdint>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kIidTransformNode = 0xe51c10f5; // cITransformNode
constexpr uint32_t kIidAnimated = 0x3afeef61;      // the node's animation owner's answer (slot 24's iid)
constexpr uint32_t kIidAnimClip = 0xc9de69ad;      // a channel playing a clip
constexpr uint32_t kIidLookAt = 0x0a6f2e8d;        // a look-at channel
constexpr uint32_t kIidLocomotion = 0xdc1d0085;    // a locomotion channel (MakeLocomotionRequest's request)

// The animatable (the argument): slots on its own vtable, and on its virtual base 1.
constexpr int kAnimChannelCount = 58; // (): the number of channels
constexpr int kAnimChannel = 9;       // (i): channel i, not AddRef'd
constexpr int kOwnerNode = 18;        // virtual base 1: the scene node it animates (or null)

// A channel: its own vtable.
constexpr int kChannelActive = 61;  // () bool
constexpr int kChannelId = 35;      // () int ("%d")
constexpr int kChannelDp = 62;      // () float ("dp")
constexpr int kChannelPc = 40;      // () float ("pc")
constexpr int kChannelEpc = 41;     // () float ("epc")
constexpr int kChannelP = 43;       // () int ("p")
constexpr int kChannelW = 44;       // () float ("w")
constexpr int kChannelS = 49;       // () float ("s")
constexpr int kChannelL = 47;       // () int ("l")

template <class R, int Slot, class... A> R Call(void *object, A... a)
{
    return VirtualCall<R(__thiscall *)(void *, A...), Slot>(object, a...);
}

void *VirtualBaseAt(void *face, int index)
{
    char *p = static_cast<char *>(face) + 4;
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}

bool Query(void *face, uint32_t iid, void **out)
{
    return Call<uint8_t, 0>(face, iid, out) != 0;
}

void AddRef(void *face) { Call<uint32_t, 1>(face); }
void Release(void *face) { Call<uint32_t, 2>(face); }

const char *Tag(void *node) { return GetNodeTag(static_cast<cICompositionTreeNode *>(node)); }

} // namespace

// 0xae3050 (lane 261111k)
void DumpAnimationChannels(void *animatable)
{
    cRZString nodeTag;
    nodeTag.Construct();

    // The animated node's tag, and its animation owner's clip (looked up, then dropped).
    if (Call<void *, kOwnerNode>(VirtualBaseAt(animatable, 1))) {
        void *node = nullptr;
        void *const owner = Call<void *, kOwnerNode>(VirtualBaseAt(animatable, 1));
        if (Query(owner, kIidTransformNode, &node)) {
            if (Tag(node)) nodeTag.mString.assign(Tag(node));
            void *animated = nullptr;
            if (Call<void *, 32>(node)) {
                void *const source = Call<void *, 32>(node);
                if (Call<uint8_t, 24>(source, kIidAnimated, &animated, 0u)) {
                    void *const clip = Call<void *, 14>(animated);
                    Call<uint32_t, 11>(clip);
                }
            }
            if (animated) Release(VirtualBaseAt(animated, 1));
        }
        if (node) Release(node);
    }

    const int32_t count = Call<int32_t, kAnimChannelCount>(animatable);
    for (int32_t i = 0; i < count; ++i) {
        cRZString line, piece, channelTag;
        line.Construct();
        piece.Construct();
        channelTag.Construct();

        void *const channel = Call<void *, kAnimChannel>(animatable, i);
        if (channel) AddRef(VirtualBaseAt(channel, 1));
        // ORIGINAL-BUG: a null channel is used regardless (its activity, its queries, its release).
        const bool active = Call<uint8_t, kChannelActive>(channel) != 0;
        line.Sprintf("*** %d [%d] ", i, static_cast<int>(active));

        void *clip = nullptr;
        float weight = 0.0f;
        if (Query(VirtualBaseAt(channel, 1), kIidAnimClip, &clip)) {
            void *lookAt = nullptr;
            if (Query(VirtualBaseAt(channel, 1), kIidLookAt, &lookAt)) {
                piece.Sprintf("look_at %s", Call<const char *, 9>(lookAt));
            } else {
                cRZString name;
                name.Construct();
                void *const player = VirtualBaseAt(clip, 2);
                if (Call<void *, 34>(player)) {
                    void *const animation = Call<void *, 34>(player);
                    name.mString.assign(Call<const char *, 17>(VirtualBaseAt(animation, 1)));
                    weight = Call<float, 36>(Call<void *, 34>(player));
                }
                piece.Sprintf("anim %s", name.ToChar());
                name.Destruct();
            }
            line.Append(piece.ToChar());
            if (Call<void *, 7>(clip) && Call<void *, 5>(clip, 0u) && Tag(Call<void *, 5>(clip, 0u)))
                channelTag.mString.assign(Tag(Call<void *, 5>(clip, 0u)));
            if (lookAt) Release(VirtualBaseAt(lookAt, 1));
        }

        void *locomotion = nullptr;
        if (Query(VirtualBaseAt(channel, 1), kIidLocomotion, &locomotion)) {
            const double second = Call<double, 27>(locomotion);
            const double first = Call<double, 23>(locomotion);
            piece.Sprintf("loco %4.2f %4.2f", first, second);
            line.Append(piece.ToChar());
        }

        // The arguments are fetched last to first, as the original pushes them.
        const char *const tag = channelTag.ToChar();
        const int32_t l = Call<int32_t, kChannelL>(channel);
        const double s = Call<double, kChannelS>(channel);
        const double w = Call<double, kChannelW>(channel);
        const int32_t p = Call<int32_t, kChannelP>(channel);
        const double epc = Call<double, kChannelEpc>(channel);
        const double pc = Call<double, kChannelPc>(channel);
        const double dp = Call<double, kChannelDp>(channel);
        const int32_t id = Call<int32_t, kChannelId>(channel);
        piece.Sprintf(" %d dp:%4.3f/%4.3f pc:%4.2f epc:%4.2f p:%d w:%5.3f s:%4.2f l:%d oo:%s\n", id, dp,
                      static_cast<double>(weight), pc, epc, p, w, s, l, tag);
        line.Append(piece.ToChar());

        if (locomotion) Release(VirtualBaseAt(locomotion, 1));
        if (clip) Release(VirtualBaseAt(clip, 1));
        Release(VirtualBaseAt(channel, 1));
        channelTag.Destruct();
        piece.Destruct();
        line.Destruct();
    }
    nodeTag.Destruct();
}

} // namespace nGZSceneGraph
