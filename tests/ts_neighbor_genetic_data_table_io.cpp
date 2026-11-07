// Differential tests for the persistence side of cTSNeighborGeneticDataTable: Read/Write through a stream, Save into a
// DB segment, the holder wrappers and the two container slots (ExportTo / ImportFrom). The fake world is in
// ts_neighbor_genetic_data_table_world.cpp.
#include "ts_neighbor_genetic_data_table.h"
#include <cstdio>

using namespace ngdt;

namespace {
Seeded Plain(unsigned seed, unsigned entries) { return {seed, entries, (seed & 1) != 0, seed % 5 + 1}; }
constexpr uint8_t kResults[] = {0, 1, 0x80, 0xff, 3, 2, 0x7f};
}

DIFF_TEST(NeighborGeneticDataTable_Save, 0x4f1860)
{
    Globals globals(ctx);
    SweepReport report("Save");
    unsigned noService = 0, noSegment = 0, saved = 0, flushFailed = 0;
    for (unsigned i = 0; i < 280; ++i) {
        Config cfg;
        cfg.findSegmentFail = i % 5 == 1;
        cfg.saveResult[0] = kResults[i % 7];
        cfg.flushResult = kResults[i / 7 % 7];
        globals.SetService(i % 9 == 8);
        Pair(ctx, "Save", i, Plain(i, i % 4), cfg, [&](bool original, Blob &b) {
            using Fn = uint8_t(__thiscall *)(Table *);
            const uint8_t r = original ? ctx.Original<Fn>(0x4f1860)(T()) : T()->Save();
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0) {
                if (i % 9 == 8) ++noService; else if (cfg.findSegmentFail) ++noSegment;
                else if (r == 0) ++flushFailed; else ++saved;
            }
        });
    }
    globals.SetService(false);
    std::printf("  Save: 280 cases: %u without service, %u without segment, %u saved, %u failing (save/flush bytes AND-ed)\n", noService, noSegment, saved, flushFailed);
}

DIFF_TEST(NeighborGeneticDataTable_ReadFromHolder, 0x4f1760)
{
    Globals globals(ctx);
    SweepReport report("ReadFromHolder");
    unsigned queryFailed = 0;
    for (unsigned i = 0; i < 112; ++i) {
        Config cfg;
        cfg.holderFail = i % 4 == 3;
        cfg.tableReadResult = kResults[i % 7];
        Pair(ctx, "ReadFromHolder", i, Plain(i, i % 3), cfg, [&](bool original, Blob &b) {
            using Fn = uint8_t(__thiscall *)(Table *, void *);
            const uint8_t r = original ? ctx.Original<Fn>(0x4f1760)(T(), &W().holder) : T()->ReadFromHolder(&W().holder);
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0 && cfg.holderFail) ++queryFailed;
        });
    }
    std::printf("  ReadFromHolder: 112 cases, %u failing queries; reader id, virtual Read, stream release\n", queryFailed);
}

DIFF_TEST(NeighborGeneticDataTable_WriteToHolder, 0x4f17e0)
{
    Globals globals(ctx);
    SweepReport report("WriteToHolder");
    for (unsigned i = 0; i < 112; ++i) {
        Config cfg;
        cfg.holderFail = i % 4 == 3;
        cfg.tableWriteResult = kResults[i % 7];
        Pair(ctx, "WriteToHolder", i, Plain(i, i % 3), cfg, [&](bool original, Blob &b) {
            using Fn = uint8_t(__thiscall *)(Table *, void *);
            const uint8_t r = original ? ctx.Original<Fn>(0x4f17e0)(T(), &W().holder) : T()->WriteToHolder(&W().holder);
            b.Mark("result");
            b.Add(r);
        });
    }
    std::printf("  WriteToHolder: 112 cases; writer id, virtual Write, stream release\n");
}

DIFF_TEST(NeighborGeneticDataTable_Read, 0x4f1130)
{
    Globals globals(ctx);
    SweepReport report("Read");
    unsigned noService = 0, added = 0, duplicates = 0, failedLookups = 0, nullObjects = 0;
    for (unsigned i = 0; i < 480; ++i) {
        std::mt19937 rng(i * 7919 + 1);
        Config cfg;
        cfg.version = i % 5 == 0 ? 0xffffffff : rng() % 4;
        cfg.count = i % 13;
        for (auto &id : cfg.ids) id = rng() % 3 == 0 ? rng() % 24 : rng() % 40;
        cfg.streamFail = i % 6 == 5 ? uint32_t(rng()) : 0;
        cfg.getResourceFail = i % 3 == 1 ? uint32_t(rng()) : 0;
        cfg.getResourceNull = i % 11 == 10;
        cfg.failWritesOut = i % 2 == 0;
        globals.SetService(i % 17 == 16);
        const Seeded s = Plain(i, i % 10);
        Pair(ctx, "Read", i, s, cfg, [&](bool original, Blob &b) {
            SetStreamMode(true);
            using Fn = uint8_t(__thiscall *)(Table *, void *);
            const uint8_t r = original ? ctx.Original<Fn>(0x4f1130)(T(), &W().stream) : T()->Read(&W().stream);
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0) {
                if (i % 17 == 16) ++noService;
                else {
                    added += uint32_t(T()->mData.size());
                    for (uint32_t k = 0; k < cfg.count && k < 16; ++k) {
                        if (cfg.getResourceFail >> (k % 32) & 1) ++failedLookups;
                    }
                    if (cfg.getResourceNull) ++nullObjects;
                    for (uint32_t k = 0; k < cfg.count && k + 1 < 16; ++k) for (uint32_t j = 0; j < k; ++j) if (cfg.ids[j] == cfg.ids[k]) { ++duplicates; break; }
                }
            }
        });
    }
    globals.SetService(false);
    std::printf("  Read: 480 cases: %u without service, %u failed lookups, %u repeated ids, %u null objects; entries ended with %u in total\n",
        noService, failedLookups, duplicates, nullObjects, added);
}

DIFF_TEST(NeighborGeneticDataTable_Write, 0x4f1950)
{
    Globals globals(ctx);
    SweepReport report("Write");
    unsigned noService = 0, entriesSaved = 0, refused = 0;
    for (unsigned i = 0; i < 360; ++i) {
        std::mt19937 rng(i * 104729 + 5);
        Config cfg;
        cfg.streamFail = i % 4 == 3 ? uint32_t(rng()) : 0;
        cfg.queryFail = i % 3 == 1 ? uint32_t(rng()) : 0;
        for (auto &r : cfg.saveResult) r = kResults[rng() % 7];
        globals.SetService(i % 19 == 18);
        const Seeded s = Plain(i, i % 14);
        Pair(ctx, "Write", i, s, cfg, [&](bool original, Blob &b) {
            SetStreamMode(false);
            using Fn = uint8_t(__thiscall *)(Table *, void *);
            const uint8_t r = original ? ctx.Original<Fn>(0x4f1950)(T(), &W().stream) : T()->Write(&W().stream);
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0) {
                if (i % 19 == 18) ++noService;
                else { entriesSaved += uint32_t(T()->mData.size()); if (r == 0) ++refused; }
            }
        });
    }
    globals.SetService(false);
    std::printf("  Write: 360 cases: %u without service, %u entries offered, %u results below 1 (save bytes AND-ed), keys restored\n", noService, entriesSaved, refused);
}

namespace {
void FillIds(Config &cfg, std::mt19937 &rng, unsigned count)
{
    cfg.idCount = count;
    for (unsigned k = 0; k < 24; ++k) cfg.idList[k] = rng() % 3 == 0 ? rng() % 40 : rng() % 16;
}
}

DIFF_TEST(NeighborGeneticDataTable_ExportTo, 0x4f1b60)
{
    Globals globals(ctx);
    SweepReport report("ExportTo");
    unsigned noContainer = 0, noIdSource = 0, noSegment = 0, exported = 0, withLong = 0, skipped = 0;
    for (unsigned i = 0; i < 480; ++i) {
        std::mt19937 rng(i * 15485863u + 3);
        Config cfg;
        cfg.idSourceFail = i % 11 == 3;
        cfg.segmentFail = i % 13 == 5;
        FillIds(cfg, rng, i % 9);
        cfg.idListEmptyBlock = i % 7 == 6;
        cfg.group = rng();
        cfg.dataMissing = i % 3 == 1 ? uint32_t(rng()) : 0;
        cfg.emptyProps = i % 4 == 2 ? uint32_t(rng()) : 0;
        for (auto &r : cfg.saveResult) r = kResults[rng() % 7];
        const bool nullContainer = i % 29 == 28;
        const Seeded s = Plain(i, i % 12);
        Pair(ctx, "ExportTo", i, s, cfg, [&](bool original, Blob &b) {
            using Fn = uint8_t(__thiscall *)(NeighborContainerFace *, void *);
            void *container = nullContainer ? nullptr : &W().container;
            const uint8_t r = original ? ctx.Original<Fn>(0x4f1b60)(&T()->mContainer, container) : T()->mContainer.ExportTo(container);
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0) {
                if (nullContainer) ++noContainer; else if (cfg.idSourceFail) ++noIdSource; else if (cfg.segmentFail) ++noSegment;
                else {
                    ++exported;
                    for (const auto &e : W().events) (void)e;
                    for (unsigned k = 0; k < cfg.idCount; ++k) {
                        if (cfg.dataMissing >> (cfg.idList[k] % 32) & 1) ++skipped;
                        if (!W().props[cfg.idList[k] % 24].empty() && W().props[cfg.idList[k] % 24][0].first.size() > 15) ++withLong;
                    }
                }
            }
        });
    }
    std::printf("  ExportTo: 480 cases: %u null containers, %u without id source, %u without segment, %u exports (%u ids skipped as missing, %u long property names freed)\n",
        noContainer, noIdSource, noSegment, exported, skipped, withLong);
}

DIFF_TEST(NeighborGeneticDataTable_ImportFrom, 0x4f13b0)
{
    Globals globals(ctx);
    SweepReport report("ImportFrom");
    unsigned noContainer = 0, noIdSource = 0, noSegment = 0, notFound = 0, fallback = 0, imported = 0, unmapped = 0;
    for (unsigned i = 0; i < 480; ++i) {
        std::mt19937 rng(i * 32452843u + 9);
        Config cfg;
        cfg.idSourceFail = i % 11 == 3;
        cfg.segmentFail = i % 13 == 5;
        FillIds(cfg, rng, i % 9);
        cfg.idListEmptyBlock = i % 7 == 6;
        cfg.group = rng();
        cfg.getResourceFail = i % 4 == 1 ? 1 : i % 4 == 2 ? 3 : 0; // first lookup fails / both fail / none
        cfg.mapFail = i % 3 == 1 ? uint32_t(rng()) : 0;
        cfg.emptyProps = i % 4 == 2 ? uint32_t(rng()) : 0;
        cfg.mapOffset = rng() % 1000;
        cfg.failWritesOut = i % 3 != 0;
        const bool nullContainer = i % 29 == 28;
        const Seeded s = Plain(i, i % 6);
        Pair(ctx, "ImportFrom", i, s, cfg, [&](bool original, Blob &b) {
            using Fn = uint8_t(__thiscall *)(NeighborContainerFace *, void *);
            void *container = nullContainer ? nullptr : &W().container;
            const uint8_t r = original ? ctx.Original<Fn>(0x4f13b0)(&T()->mContainer, container) : T()->mContainer.ImportFrom(container);
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0) {
                if (nullContainer) ++noContainer; else if (cfg.idSourceFail) ++noIdSource; else if (cfg.segmentFail) ++noSegment;
                else if (cfg.getResourceFail == 3) ++notFound;
                else {
                    if (cfg.getResourceFail == 1) ++fallback;
                    if (r == 1) ++imported; else ++unmapped;
                }
            }
        });
    }
    std::printf("  ImportFrom: 480 cases: %u null containers, %u without id source, %u without segment, %u tables not found, %u found at instance 0, %u fully imported, %u with an unmapped id\n",
        noContainer, noIdSource, noSegment, notFound, fallback, imported, unmapped);
}
