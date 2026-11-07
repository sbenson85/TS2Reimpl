#include "app/AppDebugger.h"
#include "game/game.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimS.h"
#include "simantics/PrimXCommon.h"
#include "ts/cTSString.h"

#include <cstring>

using namespace primX;
using appdebugger::LogServiceHolder;

// SimAntics opcode 0x73, Debug (0x586560).

namespace {

constexpr uint32_t kTextKey = cTSString::kStringTableType; // the last argument of every string-table loader

// A string-table loader object (vtable 0xda5128, 0x34 bytes): a cRZString that fills itself from the string tables
// when it is constructed. The destructor is the cRZString
// one, run only for an object that was constructed.
class Loader {
public:
    Loader() = default;
    Loader(const Loader &) = delete;
    Loader &operator=(const Loader &) = delete;
    ~Loader() noexcept
    {
        if (mAlive) text().Destruct();
    }
    // cTSString::Construct (0x1d0df0): (table, entry, group, flag).
    void Construct(uint32_t table, uint32_t entry, uint32_t group, uint32_t flag)
    {
        loader().Construct(static_cast<uint16_t>(table), static_cast<uint16_t>(entry), group, static_cast<uint8_t>(flag), kTextKey);
        mAlive = true;
    }
    cTSString &loader() { return *reinterpret_cast<cTSString *>(mBytes); }
    cRZString &text() { return loader().mString; }

private:
    alignas(4) uint8_t mBytes[0x34] = {};
    bool mAlive = false;
};

// The debug console: the cached service getter 0xb38f10 (cache game global 0x10faf68, ids 0x2a4915ea / 0x4a490e00);
// its slot 7 runs one console command line.
inline void *DebugConsole()
{
    return CachedServiceGetter<0x10faf68, 0x2a4915ea, 0x4a490e00>();
}

inline void RunCommand(void *console, const Text &command)
{
    Call<uint32_t>(console, 7, command.c_str());
}

// The log service local: 0x1bf8fb fills it; leaving the frame releases it (0x1be5d1), found or not.
struct LogService {
    LogServiceHolder mHolder;
    LogService() { mHolder.Acquire(); }
    LogService(const LogService &) = delete;
    LogService &operator=(const LogService &) = delete;
    ~LogService() noexcept(false) { mHolder.Release(); }
};

// Loads the string-table text (group 0x132, flag 1) of `entry` for the language object `languageEntry` into `text`:
// the group comes from its slot 11; the loader object is a temporary.
void LoadInto(Text &text, void *languageEntry, uint32_t entry)
{
    const uint32_t group = Call<uint32_t>(languageEntry, 11);
    Loader loaded;
    loaded.Construct(0x132, entry, group, 1);
    reinterpret_cast<SString *>(&text.mText)->AssignText(reinterpret_cast<const SString *>(&loaded.text()));
}

// The four operands in param bytes +4/+5, +7/+8, +0xa/+0xb, +0xd/+0xe (scope byte, id word), each sign-extended.
struct FourOperands {
    int32_t v[4];
};

FourOperands ReadFour(PrimXObject *self, const uint8_t *p)
{
    FourOperands r;
    r.v[0] = Read(self, p[4], Word(p, 5));
    r.v[1] = Read(self, p[7], Word(p, 8));
    r.v[2] = Read(self, p[0xa], Word(p, 0xb));
    r.v[3] = Read(self, p[0xd], Word(p, 0xe));
    return r;
}

void DumpHappy(PrimXObject *self, void *console, void *person)
{
    Text line;
    line.AppendLiteral("dumpHappy \"");
    void *named = Call<void *>(person ? person : static_cast<void *>(self), 61);
    const char *name = Call<const char *>(named, 8);
    line.Append(name, std::strlen(name));
    line.AppendLiteral("\"\n");
    RunCommand(console, line);
}

} // namespace

uint32_t PrimXObject::Prim73(void *frame, const PrimParamView *param)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(param);

    void *singleton = GetCachedSingleton();
    if ((Call<uint32_t>(singleton, 31) & 0xff) == 0) return 1;

    void *settings = Call<void *>(mContext, 3);

    Text scratch, objectText, personText;
    {
        // The object's description: its slot-61 object's name and ", ID n".
        void *named = Call<void *>(this, 61);
        const char *name = Call<const char *>(named, 8);
        objectText.str().assign(name);
        scratch.mText.Sprintf(", ID %d", static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(this, 47))));
        objectText.AppendText(scratch);
    }
    void *people = Call<void *>(mContext, 26);
    void *lists = Call<void *>(mContext, 24);
    const int32_t personId = static_cast<int16_t>(Call<uint32_t>(frame, 12));
    void *person = Call<void *>(people, 29, personId);
    if (person) {
        void *named = Call<void *>(person, 61);
        const char *name = Call<const char *>(named, 8);
        personText.str().assign(name);
        scratch.mText.Sprintf(", ID %d", static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(person, 47))));
        personText.AppendText(scratch);
    }

    void *console = DebugConsole();
    const bool driven = mField44 != 0;
    if (driven && (static_cast<uint16_t>(Call<uint32_t>(settings, 15, 0x21u)) & 8) != 0) {
        switch (p[0x11]) {
        case 0: {
            // Format a string-table text with the four operands; the result is built and dropped (not sent).
            Text format;
            void *owner = Call<void *>(frame, 20);
            void *entryOwner = Call<void *>(lists, 14, Call<uint32_t>(owner, 38));
            void *group18 = Call<void *>(lists, 18);
            const uint32_t entry = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(p[0x10])));
            if (p[0x12] & 2) {
                if (group18 && Call<void *>(group18, 67)) LoadInto(format, Call<void *>(group18, 67), entry);
            } else if (p[0x12] & 1) {
                if (entryOwner && Call<void *>(entryOwner, 70)) LoadInto(format, Call<void *>(entryOwner, 70), entry);
            } else {
                if (entryOwner && Call<void *>(entryOwner, 18)) LoadInto(format, Call<void *>(entryOwner, 18), entry);
            }
            const FourOperands ops = ReadFour(this, p);
            Text out;
            out.mText.Sprintf(format.c_str(), ops.v[0], ops.v[1], ops.v[2], ops.v[3]);
            out.AppendLiteral("\n");
            break;
        }
        case 1: {
            LogService log;
            if (log.mHolder.mService != nullptr) {
                Text quoted;
                quoted.mText.Sprintf("\"%s\"", personText.c_str());
                Text command;
                HeldRef found;
                found.Reset();
                if ((Call<uint32_t>(log.mHolder.mService, 12, personText.c_str(), &found.mObject) & 0xff) == 0) {
                    command.mText.Sprintf("logFilter %s", quoted.c_str());
                    RunCommand(console, command);
                    Call<uint32_t>(log.mHolder.mService, 9, personText.c_str(), "NoGroup", 0u);
                    Call<uint32_t>(log.mHolder.mService, 9, personText.c_str(), personText.c_str(), 7u);
                }
                command.mText.Sprintf("logWindow %s", quoted.c_str());
                RunCommand(console, command);
            }
            break;
        }
        case 2: {
            // ORIGINAL-BUG: the person is not checked (no person is a null dereference).
            Text line;
            if ((Call<uint32_t>(person, 25, 0x8000u) & 0xff) != 0) {
                Call<uint32_t>(person, 26, 0x8000u, 0u);
                line.str().assign("displayAnimBillboard off  \"");
            } else {
                line.str().assign("displayAnimBillboard on  \"");
                Call<uint32_t>(person, 26, 0x8000u, 1u);
            }
            line.AppendText(personText);
            line.AppendLiteral("\"\n");
            RunCommand(console, line);
            break;
        }
        case 4: {
            // ORIGINAL-BUG: the person is not checked.
            Text number;
            number.mText.Sprintf("%d", static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(person, 47))));
            Text line("showbones ");
            line.AppendText(number);
            line.AppendLiteral("\n");
            RunCommand(console, line);
            break;
        }
        case 5:
            if (person) {
                if ((Call<uint32_t>(person, 28) & 0x4000) == 0) {
                    Call<uint32_t>(person, 27, Call<uint32_t>(person, 28) | 0x4000u);
                } else {
                    Call<uint32_t>(person, 27, Call<uint32_t>(person, 28) & 0xffffbfffu);
                }
            }
            break;
        case 6: {
            Text format;
            void *owner = Call<void *>(frame, 20);
            void *entryOwner = Call<void *>(lists, 14, Call<uint32_t>(owner, 38));
            void *group18 = Call<void *>(lists, 18);
            const uint32_t entry = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(p[0x10])) + 1);
            if (p[0x12] & 2) {
                if (group18 && Call<void *>(group18, 67)) LoadInto(format, Call<void *>(group18, 67), entry);
            } else if (p[0x12] & 1) {
                // ORIGINAL-BUG: the language entry is not checked here (it is in the other branches).
                void *found = Call<void *>(lists, 19, Call<uint32_t>(entryOwner, 73));
                if (found && Call<void *>(found, 67)) LoadInto(format, Call<void *>(found, 67), entry);
            } else {
                if (entryOwner && Call<void *>(entryOwner, 18)) LoadInto(format, Call<void *>(entryOwner, 18), entry);
            }
            const FourOperands ops = ReadFour(this, p);
            Text out;
            out.mText.Sprintf(format.c_str(), ops.v[0], ops.v[1], ops.v[2], ops.v[3]);
            out.AppendLiteral("\n");
            RunCommand(console, out);
            break;
        }
        case 7: DumpHappy(this, console, person); break;
        default: break;
        }
    } else if (p[0x11] == 7) {
        DumpHappy(this, console, person);
    }
    return 1;
}
