// SG_MissingAttribute_Records: the behaviour templates' "missing" event attribute (what GetEventAttribute, slot 14,
// returns by value for an unknown event) is one owned record, behavior_mt::gMissingRecord (lane SR, 2026-11-01). The
// tests that check which record is returned plant a pattern in it, so its own values are checked here: every image
// record it replaces (the per-class kMissingAttribute statics below) holds the same id and words, and all of them name
// one shared text, the empty string ours names too.
#include "difftest.h"

#include "gz/cAbstractBehaviorTypeMT.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr uint32_t kImageRecords[] = {0x1069fb0, 0x106a1f0, 0x106a48c, 0x106a4e0, 0x106ab04, 0x106ab68, 0x106ad88,
                                      0x106b120, 0x106b328, 0x106b418, 0x106b9c0, 0x106fad0, 0x1070058, 0xfeb690};
}

DIFF_TEST(SG_MissingAttribute_Records, 0x106a48c)
{
    const auto &ours = nGZEvent::behavior_mt::gMissingRecord;
    const uint32_t *o = reinterpret_cast<const uint32_t *>(&ours);
    if (!ours.mName || std::strcmp(ours.mName, "") != 0) ctx.Fail("ours names a non-empty text");
    const uint32_t firstName = ctx.image.At<uint32_t>(kImageRecords[0])[1];
    for (uint32_t at : kImageRecords) {
        const uint32_t *w = ctx.image.At<uint32_t>(at);
        for (int i : {0, 2, 3, 4, 5})
            if (w[i] != o[i]) ctx.Fail("record 0x%x word %d: image 0x%x, ours 0x%x", at, i, w[i], o[i]);
        if (w[1] != firstName) ctx.Fail("record 0x%x names another text (0x%x, first 0x%x)", at, w[1], firstName);
    }
}
