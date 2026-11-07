// Host diagnostic for milestone N-f (docs/subsystems/scenegraph.md, "Milestone N", N-f): with TS2HOST_GMDC set to a non-empty
// value other than 0, every GMDC resource the hooked game decodes (cGeometryDataContainer::Read, ours) writes one line to
// the host log in the format of tests/gmdc_decode_acceptance.cpp's expected lines,
//   gmdc-expected - <instance> <group> <resource> <summary>
// (the package path is not known where the container is decoded, so it is "-"; tools/gmdc_compare.py pairs by key).
// The summary is standalone/gmdc_summary.h's. Off (the default), the observer pointer stays null: the decode path does one
// load and one test, and nothing here runs or allocates.
#include "../../standalone/gmdc_summary.h"
#include "hook/log.h"

#include <cstdlib>

namespace {

void OnDecoded(const nGZSceneGraph::cGeometryDataContainer &container, const cGZPersistResourceKey &key)
{
    Log("gmdc-expected - %08x %08x %08x %s", unsigned(key.instance), unsigned(key.group), unsigned(key.resource),
        gmdcsum::Summarize(container).Line().c_str());
}

struct Installer {
    Installer()
    {
        char value[8];
        size_t len = 0;
        if (getenv_s(&len, value, "TS2HOST_GMDC") != 0 || len <= 1 || value[0] == '0') return;
        nGZSceneGraph::gDecodeObserver = &OnDecoded;
    }
} gInstaller;

} // namespace
