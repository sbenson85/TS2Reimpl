#pragma once
// World construction, state capture and the compare loop of the interpreter tests (see simantics_treesim.h).
#include "simantics_treesim.h"

namespace tsw {

// The game globals the interpreter reads, kept per world and written into the mapped image before each run.
struct Globals {
    int32_t limit = 100000;
    int32_t maxDepth = 100;
    uint8_t runFlag = 0;
    uint8_t logEnabled = 0, logActive = 0, logShowText = 0;
    int32_t logDepth = 0;
    bool debugService = false;
    bool sinkPresent = true;
};
inline Globals gGlobals; // for the world being run

constexpr uint32_t kGlobalRunFlag = 0x1016910;
constexpr uint32_t kGlobalLimit = 0x1016914;
constexpr uint32_t kGlobalMaxDepth = 0x1016918;
constexpr uint32_t kGlobalDebugService = 0x10cc294;

inline void SetVt(void **vt, std::initializer_list<std::pair<int, void *>> entries)
{
    for (const auto &e : entries) vt[e.first] = e.second;
}

inline void BuildVtables(World &w)
{
    auto P = [](auto fn) { return reinterpret_cast<void *>(fn); };
    SetVt(w.vtSim, {{0, P(&SimQuery)}, {1, P(&SimAddRef)}, {5, P(&SimNewFrame)}, {6, P(&SimRunNode)}, {7, P(&SimReadOperand)}, {8, P(&SimHandleError)},
                    {9, P(&SimFramePopped)}, {12, P(&SimPushTree)}, {13, P(&SimDoNodeAction)}, {14, P(&SimResume)},
                    {20, P(&SimTopFrame)}, {22, P(&SimFrameAt)}, {28, P(&SimReturnedTrue)}});
    SetVt(w.vtStack, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {3, P(&StReset)}, {4, P(&StNewFrame)}, {5, P(&StPush3)}, {6, P(&StPop)},
                      {7, P(&StClear)}, {9, P(&StGetAt)}, {10, P(&StCount)}, {15, P(&StClearAll)}});
    SetVt(w.vtFrame, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {3, P(&FrGetW18)}, {4, P(&FrSetW18)}, {5, P(&FrGetW1a)},
                      {6, P(&FrSetW1a)}, {7, P(&FrGetW1c)}, {8, P(&FrSetW1c)}, {9, P(&FrGetTreeId)}, {10, P(&FrSetTreeId)},
                      {12, P(&FrGetStackObject)}, {13, P(&FrSetStackObject)}, {14, P(&FrGetNode)}, {15, P(&FrSetNode)},
                      {16, P(&FrGetFlag16)}, {17, P(&FrSetFlag16)}, {18, P(&FrGetResume)}, {19, P(&FrSetResume)},
                      {20, P(&FrGetOwner)}, {21, P(&FrSetOwner)}, {22, P(&FrIsPaused)}, {23, P(&FrSetPaused)},
                      {26, P(&FrGetArgCount)}, {27, P(&FrGetLocalCount)}, {28, P(&FrSetArgCount)}, {29, P(&FrSetLocalCount)},
                      {30, P(&FrGetArg)}, {33, P(&FrSetArgs)}, {41, P(&FrSelf)}});
    SetVt(w.vtTree, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {6, P(&TrVersion)}, {10, P(&TrType)}, {12, P(&TrArgCount)},
                     {14, P(&TrLocalCount)}, {16, P(&TrFlags)}, {18, P(&TrCache)}, {20, P(&TrNodeCount)}, {22, P(&TrGetNode)}});
    SetVt(w.vtNode, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {3, P(&NdOpcode)}, {6, P(&NdTrue)}, {8, P(&NdFalse)},
                     {16, P(&NdParam)}, {18, P(&NdFlag)}, {19, P(&NdSetFlag)}});
    SetVt(w.vtTemps, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {19, P(&TmGet)}, {20, P(&TmSet)}});
    SetVt(w.vtOwner, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {12, P(&OwKey)}, {17, P(&OwPrepare)}, {23, P(&OwGetTree)}, {34, P(&OwTreeName)}});
    SetVt(w.vtGate, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {15, P(&GtWord)}, {22, P(&GtRunState)}, {23, P(&GtSetRunState)},
                     {27, P(&GtHalt)}});
    SetVt(w.vtContext, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {3, P(&CxGate)}, {26, P(&CxTable)}});
    SetVt(w.vtTable, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {29, P(&TbFind)}});
    SetVt(w.vtObject, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {28, P(&ObFlags)}, {237, P(&ObCancel)}});
    SetVt(w.vtService, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {18, P(&SvEnabled)}});
    SetVt(w.vtDebug, {{1, P(&AddRefFake)}, {2, P(&ReleaseFake)}, {4, P(&DbNodeText)}});
    SetVt(w.vtSink, {{7, P(&SkWord)}, {9, P(&SkInt)}, {14, P(&SkText)}});
}

inline uint32_t Pick(std::mt19937 &r, std::initializer_list<uint32_t> values)
{
    return *(values.begin() + r() % values.size());
}

inline void Put16(uint8_t *p, uint16_t v)
{
    std::memcpy(p, &v, 2);
}

// A random world. `stackDepth` frames sit on the stack (the last one is the top); frames are filled with plausible
// values (tree ids that resolve through the owners, node indices inside the trees or special targets, pause bits,
// resume states ...). Tests tweak the result before running.
inline void Build(World &w, uint32_t seed)
{
    std::memset(static_cast<void *>(&w), 0, sizeof(World));
    std::mt19937 r(seed * 2654435761u + 0x9e3779b9u);
    gSeed = seed;
    BuildVtables(w);

    cTSTreeSim *sim = Sim(w);
    sim->mVTable = w.vtSim;
    sim->mUnknown.mVTable = w.vtUnknown;
    sim->mStack.mVTable = w.vtStack;
    w.vtUnknown[1] = reinterpret_cast<void *>(&AddRefFake);
    new (&sim->mStack.mFrames) std::vector<TreeFrameRef>();

    for (int i = 0; i < kFrames; ++i) {
        reinterpret_cast<void ***>(w.frame[i].real)[0] = w.vtFrame;
        reinterpret_cast<uint32_t *>(w.frame[i].real)[1] = 1;
    }
    for (int i = 0; i < kTrees; ++i) {
        TreeCell &t = w.tree[i];
        t.vt = w.vtTree;
        t.refs = 1;
        t.version = static_cast<int16_t>(Pick(r, {0x8001u, 0x8002u, 0x8003u, 0x8004u, 0x8009u, 0x8003u, 0x8005u, 0x0007u, 0xffffu}));
        t.type = static_cast<uint8_t>(Pick(r, {0, 0, 0, 4, 1}));
        t.argCount = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 2, 3}));
        t.localCount = static_cast<uint8_t>(r() % 7);
        t.flags = static_cast<uint8_t>(Pick(r, {0, 0, 0, 8, 0x10, 0x18, 1, r() & 0xff}));
        t.cache = static_cast<uint8_t>(r() & 0xff);
        t.nodeCount = static_cast<int16_t>(Pick(r, {0, 1, 2, 3, 4, 5, 6, 3, 4}));
        t.nodeBase = i * kNodesPerTree;
    }
    for (int i = 0; i < kNodes; ++i) {
        NodeCell &n = w.node[i];
        n.vt = w.vtNode;
        n.refs = 1;
        uint32_t opcode = (r() % 10 < 3) ? 0x100 + r() % 8 : r() % 0x7f;
        if (r() % 20 == 0) opcode |= 0x8000;
        n.opcode = static_cast<uint16_t>(opcode);
        auto target = [&]() {
            switch (r() % 8) {
            case 0: return static_cast<uint16_t>(0xfffc);
            case 1: return static_cast<uint16_t>(0xfffd);
            case 2: return static_cast<uint16_t>(0xfffe);
            default: return static_cast<uint16_t>(r() % kNodesPerTree);
            }
        };
        n.tTrue = target();
        n.tFalse = target();
        for (uint8_t &b : n.param) b = static_cast<uint8_t>(r());
        n.param[0x10] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 0xff, 1, 2, 5, 0x80, r() & 0xff}));
        if (r() % 4 == 0) std::memset(n.param + 4, 0xff, 8);
        for (int k = 0; k < 4; ++k) n.param[4 + 3 * k] = static_cast<uint8_t>(r() % 0x30);
    }
    for (int i = 0; i < kOwners; ++i) {
        OwnerCell &o = w.owner[i];
        o.vt = w.vtOwner;
        o.refs = 1;
        o.salt = InjectWorld(r());
        o.nullRate = Pick(r, {0, 0, 1, 2, 4});
    }
    w.gate.vt = w.vtGate;
    w.gate.refs = 1;
    w.gate.runState = Pick(r, {0, 0, 1});
    w.gate.word33 = r() & 1;
    w.gate.halt = Pick(r, {0, 0, 1});
    w.context.vt = w.vtContext;
    w.context.refs = 1;
    w.context.gate = &w.gate;
    w.context.table = (r() % 8 == 0) ? nullptr : &w.table;
    w.table.vt = w.vtTable;
    w.table.refs = 1;
    for (int i = 0; i < kObjects; ++i) {
        w.object[i].vt = w.vtObject;
        w.object[i].refs = 1;
        w.object[i].flags = Pick(r, {0, 0x1000, 0x40, 0x800, 0x10000, 0x70000, r()});
        w.table.objects[i] = &w.object[i];
    }
    w.table.objects[kObjects] = nullptr;
    w.serviceC.vt = w.vtService;
    w.serviceC.refs = 1;
    w.serviceC.enabled = r() % 2;
    w.debug.vt = w.vtDebug;
    w.debug.refs = 1;
    std::snprintf(w.debug.text, sizeof(w.debug.text), "node text %u", r() % 100);
    w.sink.vt = w.vtSink;
    for (uint16_t &d : w.defaultArgs) d = static_cast<uint16_t>(r());

    // The sim.
    sim->mStepCount = Pick(r, {0, 5, 99990, 100000, 299999, r() % 400000});
    sim->mStepBase = r() % 1000;
    sim->mField14 = r();
    sim->mNextNode = Pick(r, {0xfffc, 0xfffd, 0xfffe, 3});
    sim->mLastBranch = static_cast<uint8_t>(r() % 2);
    sim->mDefaultArgs = w.defaultArgs;
    sim->mErrorCode = (r() % 10 == 0) ? static_cast<uint16_t>(0x400 + r() % 100) : 0;
    sim->mResult = static_cast<uint16_t>(r());
    sim->mCallDepth = static_cast<uint16_t>(r() % 4);
    sim->mContext = &w.context;
    sim->mDebugService = (r() % 3 == 0) ? nullptr : &w.debug;
    sim->mServiceC = &w.serviceC;
    sim->mGate = &w.gate;
    sim->mRecording = r() % 2;
    w.temps.vt = w.vtTemps;
    w.temps.refs = 1;
    for (uint16_t &t : w.temps.value) t = static_cast<uint16_t>(r());
    // The replay map: empty, built the way the constructor builds it.
    {
        TreeReplayNode *head = static_cast<TreeReplayNode *>(::operator new(0x20));
        head->next = head;
        head->prev = head;
        TreeReplayMap &m = sim->mReplayMap;
        m.mMaxLoadFactor = 1.0f;
        m.mList.mHead = head;
        m.mList.mSize = 0;
        new (&m.mBuckets) std::vector<TreeReplayNode *>(0x10, head);
        m.mMask = 7;
        m.mBucketCount = 8;
    }

    // The stack: some frames, bottom first.
    const int depth = 1 + static_cast<int>(r() % 4);
    for (int i = 0; i < depth; ++i) {
        cTSTreeStackElem *f = Fr(w, i);
        uint16_t treeWord = static_cast<uint16_t>(Pick(r, {0, 1, 2, 3, 4, 5, 0x100, 0x101, 0x1000, 7}));
        if (r() % 8 == 0) treeWord |= 0x8000;
        if (r() % 16 == 0) treeWord = 0xffff;
        f->mTreeWord = treeWord;
        f->mStackObjectId = static_cast<uint16_t>(r() % 6);
        f->mNodeIndex = (r() % 6 == 0) ? static_cast<uint16_t>(Pick(r, {0xfffb, 0xfffc, 0xfffd, 0xfffe, 0xffff, 0xfffd, 0xfffe}))
                                       : static_cast<uint16_t>(r() % (kNodesPerTree + 1));
        f->mResumeState = Pick(r, {0, 0, 1});
        f->mOwner = (r() % 12 == 0) ? nullptr : &w.owner[r() % kOwners];
        f->mArgCount = static_cast<uint8_t>(r() % 9);
        f->mLocalCount = static_cast<uint8_t>(r() % 5);
        f->mByte16 = static_cast<uint8_t>(r());
        f->mWord18 = static_cast<uint16_t>(r());
        f->mWord1a = static_cast<uint16_t>(r());
        f->mWord1c = static_cast<uint16_t>(r());
        f->mByte1e = static_cast<uint8_t>(r());
        for (uint16_t &a : w.frame[i].args) a = static_cast<uint16_t>(r());
        for (uint16_t &a : w.frame[i].locals) a = static_cast<uint16_t>(r());
        Frames(w).push_back(&w.frame[i]);
    }
    // The replay code (original, live when the record switch is on) dereferences the owner of the top frame without a test
    // (ORIGINAL-BUG), so worlds with the switch on give every frame an owner.
    if (sim->mRecording) {
        for (int i = 0; i < depth; ++i)
            if (!Fr(w, i)->mOwner) Fr(w, i)->mOwner = &w.owner[i % kOwners];
    }
    // The frame bytes the creator never writes keep random data (identical in both worlds of a seed).
    for (int i = 0; i < kFrames; ++i) {
        uint8_t *real = w.frame[i].real;
        real[0xe] = static_cast<uint8_t>(r());
        real[0xf] = static_cast<uint8_t>(r());
        real[0x17] = static_cast<uint8_t>(r());
        real[0x1f] = static_cast<uint8_t>(r());
    }

    Globals &g = gGlobals;
    g = Globals();
    g.limit = static_cast<int32_t>(Pick(r, {100000, 100000, 300000, 50, 6, 100001, 299999}));
    g.maxDepth = static_cast<int32_t>(Pick(r, {100, 100, 1, 2, 3, 5, 0}));
    g.runFlag = static_cast<uint8_t>(r() % 3);
    g.logEnabled = r() % 3 == 0;
    g.logActive = r() % 4 != 0;
    g.logShowText = r() % 2;
    g.logDepth = static_cast<int32_t>(r() % 3);
    g.debugService = r() % 2;
    g.sinkPresent = r() % 8 != 0;
    gStepsLeft = r() % 4;
    gStepMode = r() % 6 < 3 ? 0 : (r() % 2 ? 1 : 2);
    gThrowAt = (r() % 8 == 0) ? 1 + r() % 30 : 0;
    w.canary = 0xC0DEC0DEu;
}

// Writes the world's globals into the mapped image (called before each run).
inline void ApplyGlobals(World &w)
{
    const Globals &g = gGlobals;
    *GameAddress<uint8_t>(kGlobalRunFlag) = g.runFlag;
    *GameAddress<int32_t>(kGlobalLimit) = g.limit;
    *GameAddress<int32_t>(kGlobalMaxDepth) = g.maxDepth;
    SimTraceLog *log = SimTraceLog::Get();
    log->mEnabled = g.logEnabled;
    log->mActive = g.logActive;
    log->mShowNodeText = g.logShowText;
    log->mDepth = g.logDepth;
    log->mSink = g.sinkPresent ? static_cast<void *>(&w.sink) : nullptr;
    *GameAddress<void *>(kGlobalDebugService) = g.debugService ? static_cast<void *>(&w.debug) : nullptr;
}

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1;     // the short thrown, or -1
    int kind = 0;        // 0 none, 1 short, 2 stack enum, 3 other
    std::vector<uint32_t> trace;
    std::vector<uint32_t> state;
};

// The shape of a replay table's buckets: the mask and count, and for every bucket entry the position in the list (0xffffffff
// for the list head, which an empty bucket holds).
inline void AppendReplayBuckets(const TreeReplayMap &m, std::vector<uint32_t> &s)
{
    s.push_back(m.mMask);
    s.push_back(m.mBucketCount);
    s.push_back(static_cast<uint32_t>(m.mBuckets.size()));
    std::vector<const StdHashNode *> order;
    for (const StdHashNode *node = m.mList.mHead->next; node != m.mList.mHead; node = node->next) order.push_back(node);
    for (const TreeReplayNode *entry : m.mBuckets) {
        uint32_t position = 0xffffffffu;
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == entry) position = static_cast<uint32_t>(i);
        s.push_back(position);
    }
}

inline void Capture(World &w, Outcome &o)
{
    std::vector<uint32_t> &s = o.state;
    cTSTreeSim *sim = Sim(w);
    const uint32_t *words = reinterpret_cast<const uint32_t *>(w.sim);
    // The sim's plain fields; the vtable pointers and the vector internals are left out (the vector's contents follow).
    for (int off : {0x0c, 0x10, 0x14, 0x28}) s.push_back(words[off / 4]);
    s.push_back(sim->mLastBranch);
    s.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sim->mDefaultArgs))));
    s.push_back(sim->mErrorCode);
    s.push_back(sim->mResult);
    s.push_back(sim->mCallDepth);
    for (void *p : {sim->mContext, sim->mDebugService, sim->mServiceC, sim->mGate}) s.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))));
    s.push_back(sim->mRecording);
    s.push_back(static_cast<uint32_t>(sim->mStack.mFrames.size()));
    for (void *f : Frames(w)) s.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f))));
    for (int i = 0; i < kFrames; ++i) {
        const uint8_t *b = w.frame[i].real;
        for (int off = 4; off < 0x3c; off += 4) {
            uint32_t v;
            std::memcpy(&v, b + off, 4);
            if (off >= 0x24) continue; // the vectors (unused by the fakes)
            s.push_back(off == 0x20 ? Norm(v) : v);
        }
        for (uint16_t a : w.frame[i].args) s.push_back(a);
    }
    for (const TreeCell &t : w.tree) s.push_back(t.refs);
    for (const NodeCell &n : w.node) {
        s.push_back(n.refs);
        s.push_back(n.opcode);
    }
    for (const OwnerCell &t : w.owner) s.push_back(t.refs);
    s.push_back(w.debug.refs);
    s.insert(s.end(), {w.gate.refs, w.gate.runState, w.context.refs, w.table.refs, w.serviceC.refs, w.nextSpare, w.simRefs, w.canary, sim->mUnknown.mRefCount});
    for (const ObjectCell &t : w.object) s.push_back(t.refs);
    s.push_back(*GameAddress<uint8_t>(kGlobalRunFlag));
    s.push_back(static_cast<uint32_t>(*GameAddress<int32_t>(kGlobalLimit)));
    s.push_back(static_cast<uint32_t>(SimTraceLog::Get()->mDepth));
    s.push_back(gStepsLeft);
    // The replay map: its size and what the records hold (nodes in list order: key, rows, each row's result, arguments and temps).
    const TreeReplayMap &m = sim->mReplayMap;
    s.push_back(m.mList.mSize);
    AppendReplayBuckets(m, s);
    for (StdHashNode *node = m.mList.mHead->next; node != m.mList.mHead; node = node->next) {
        const uint32_t *words = reinterpret_cast<const uint32_t *>(node);
        s.push_back(words[2]);
        s.push_back(words[3]);
        const uint8_t *rowsFirst = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(words[4]));
        const uint8_t *rowsLast = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(words[5]));
        s.push_back(static_cast<uint32_t>((rowsLast - rowsFirst) / 0x1c));
        for (const uint8_t *row = rowsFirst; row != rowsLast; row += 0x1c) {
            const uint32_t *rw = reinterpret_cast<const uint32_t *>(row);
            s.push_back(rw[3]);
            for (int v : {0, 4}) {
                const uint16_t *first = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(rw[v]));
                const uint16_t *last = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(rw[v + 1]));
                s.push_back(static_cast<uint32_t>(last - first));
                for (const uint16_t *p = first; p != last; ++p) s.push_back(*p);
            }
        }
    }
    s.push_back(w.temps.refs);
    for (uint16_t t : w.temps.value) s.push_back(t);
}

template <typename Call> Outcome Run(World &w, Call call)
{
    gW = &w;
    gBase = &w;
    gBaseSize = sizeof(World);
    gHitCount = 0;
    gStepCalls = 0;
    gTrace.clear();
    ApplyGlobals(w);
    Outcome o;
    try {
        o.ret = call(w);
    } catch (short code) {
        o.thrown = code;
        o.kind = 1;
    } catch (decltype(cTSTreeStack::kStackErrorNoFrame)) {
        o.kind = 2;
    } catch (...) {
        o.kind = 3;
    }
    o.trace = gTrace;
    Capture(w, o);
    gW = nullptr;
    gBase = nullptr;
    gBaseSize = 0;
    return o;
}

struct Stats {
    uint32_t cases = 0, shorts = 0, enums = 0, returns = 0;
    uint32_t byRet[8] = {};
    std::vector<uint32_t> codes;
};

struct Trial {
    bool bad = false;
    Outcome oa, ob;
};

inline void(__thiscall *gMapDestroy)(void *) = nullptr; // the original map destructor (0x5757a0), set by the tests

inline void FreeWorld(World &w)
{
    Frames(w).~vector();
    TreeReplayMap &m = Sim(w)->mReplayMap;
    if (gMapDestroy) {
        gMapDestroy(&m); // frees the nodes, the bucket storage and (here) leaves the sentinel node to us
    }
}

// Set the environment variable TSW_DUMP to print the traces of the first failures.
inline bool DumpRequested()
{
    char *value = nullptr;
    size_t size = 0;
    const bool set = _dupenv_s(&value, &size, "TSW_DUMP") == 0 && value != nullptr;
    std::free(value);
    return set;
}

// Both sides run in this one world, built afresh from the seed before each run (Build zeroes it first, FreeWorld gives back
// what a run left in it), so they see it at the same address (as in tests/simantics_prims_aa.h, lane EH). Why: the trace
// and the captured state hold words that may be pointers into the world (Norm turns those into world offsets) next to the
// fakes' random answers (operand reads, gate words, owner salts). With two worlds at different addresses, a random word that
// happened to fall inside one world's range was normalised on that side only, and the seed differed although nothing
// diverged; the old retry on two more worlds made that rarer but not impossible. At one address Norm is the same function on
// both sides, so equal words stay equal and different words stay different: no comparison is weakened (a word the original
// logs as a world pointer must still be that same pointer in ours), and none depends on where the world sits. The outcome
// copies everything it compares out of the world before it is rebuilt.
inline World &SharedWorld()
{
    static World *const w = [] {
        World *p = static_cast<World *>(::operator new(sizeof(World))); // (raw memory, kept for the process; Build zeroes it)
        gInjectBase = reinterpret_cast<uintptr_t>(p);
        gInjectSize = static_cast<uint32_t>(sizeof(World));
        return p;
    }();
    return *w;
}

template <typename Setup, typename Orig, typename Ours>
Trial Try(World &w, uint32_t seed, Setup setup, Orig original, Ours ours, bool compareRet)
{
    Trial t;
    // The original runs first, then ours, each on the world built from the same seed (the stepping script globals too: the
    // second build sets them again, and they are put back to what the first one set, as when each side had its own world).
    Build(w, seed);
    setup(w, seed);
    const Globals ga = gGlobals;
    const uint32_t stepsA = gStepsLeft, modeA = gStepMode, throwA = gThrowAt;
    t.oa = Run(w, [&](World &x) { return original(x); });
    FreeWorld(w);
    Build(w, seed);
    setup(w, seed);
    gGlobals = ga;
    gStepsLeft = stepsA;
    gStepMode = modeA;
    gThrowAt = throwA;
    t.ob = Run(w, [&](World &x) { return ours(x); });
    FreeWorld(w);
    t.bad = t.oa.thrown != t.ob.thrown || t.oa.kind != t.ob.kind || t.oa.trace != t.ob.trace || t.oa.state != t.ob.state ||
            (compareRet && t.oa.kind == 0 && t.oa.ret != t.ob.ret);
    return t;
}

// Runs every seed on the shared world (see SharedWorld); a mismatch is reported as it is, without a retry.
template <typename Setup, typename Orig, typename Ours>
void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Setup setup, Orig original, Ours ours, Stats &st,
             bool compareRet = true)
{
    gMapDestroy = ctx.Original<void(__thiscall *)(void *)>(0x5757a0);
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Trial t = Try(SharedWorld(), seed, setup, original, ours, compareRet);
        ++ctx.cases;
        ++st.cases;
        const Outcome &oa = t.oa, &ob = t.ob;
        if (oa.kind == 1) {
            ++st.shorts;
            st.codes.push_back(static_cast<uint32_t>(oa.thrown));
        } else if (oa.kind == 2) {
            ++st.enums;
        } else {
            ++st.returns;
            if (oa.ret < 8) ++st.byRet[oa.ret];
        }
        if (t.bad) {
            size_t k = 0;
            while (k < oa.trace.size() && k < ob.trace.size() && oa.trace[k] == ob.trace[k]) ++k;
            size_t j = 0;
            while (j < oa.state.size() && j < ob.state.size() && oa.state[j] == ob.state[j]) ++j;
            if (DumpRequested() && ctx.failures.size() < 2) {
                std::printf("globals: limit %d depth %d run %u log %u/%u/%u depth %d sink %d dbg %d throwAt %u steps %u mode %u\n", gGlobals.limit,
                            gGlobals.maxDepth, gGlobals.runFlag, gGlobals.logEnabled, gGlobals.logActive, gGlobals.logShowText,
                            gGlobals.logDepth, gGlobals.sinkPresent, gGlobals.debugService, gThrowAt, gStepsLeft, gStepMode);
                for (int side = 0; side < 2; ++side) {
                    const Outcome &o = side ? ob : oa;
                    std::printf("seed %u %s throwAt trace:", seed, side ? "ours" : "orig");
                    for (size_t q = 0; q < o.trace.size() && q < 120; ++q) std::printf(" %x", o.trace[q]);
                    std::printf("\n");
                }
            }
            ctx.Fail("%s seed %u: thrown %d/%d kind %d/%d ret 0x%x/0x%x trace %zu/%zu (first diff at %zu: 0x%x/0x%x) state %s (first diff at %zu: 0x%x/0x%x)",
                     name, seed, oa.thrown, ob.thrown, oa.kind, ob.kind, oa.ret, ob.ret, oa.trace.size(), ob.trace.size(), k,
                     k < oa.trace.size() ? oa.trace[k] : 0xdeadu, k < ob.trace.size() ? ob.trace[k] : 0xdeadu,
                     oa.state == ob.state ? "same" : "DIFFERENT", j, j < oa.state.size() ? oa.state[j] : 0xdeadu,
                     j < ob.state.size() ? ob.state[j] : 0xdeadu);
        }
    }
    if (gInjectWorld) std::printf("      %s TREESIM_INJECT_WORLD: %u world-range words injected\n", name, gInjected);
    gInjected = 0;
}

// A one-line summary printed by the tests (how varied the runs were).
inline void Report(const char *name, const Stats &st)
{
    static const char *const kClasses[] = {"sim", "stack", "frame", "tree", "node", "owner", "gate", "context", "table", "object", "svc", "dbg", "sink", "?", "?", "?"};
    std::printf("  [treesim] %s: %u cases, %u returned (0:%u 1:%u 2:%u), %u shorts, %u stack errors\n", name, st.cases, st.returns,
                st.byRet[0], st.byRet[1], st.byRet[2], st.shorts, st.enums);
    std::string line = "    calls:";
    for (const auto &e : gSlotHits) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), " %s.%u=%u", kClasses[e.first >> 8], e.first & 0xff, e.second);
        line += buf;
    }
    std::printf("%s\n", line.c_str());
    std::vector<uint32_t> codes = st.codes;
    std::sort(codes.begin(), codes.end());
    line = "    thrown:";
    for (size_t i = 0; i < codes.size();) {
        size_t j = i;
        while (j < codes.size() && codes[j] == codes[i]) ++j;
        char buf[48];
        std::snprintf(buf, sizeof(buf), " 0x%x=%zu", codes[i], j - i);
        line += buf;
        i = j;
    }
    std::printf("%s\n", line.c_str());
    gSlotHits.clear();
}

} // namespace tsw
