#pragma once
// Diagnostics: lets the host's SimAntics trace (host/control_simtrace.cpp) watch our interpreter step
// (cTSTreeSim::DoNodeAction, src/simantics/TreeSimStep.cpp) at the two points it patches in the
// original's 0x5762d0: the opcode read that decides between running a primitive and calling a tree
// (0x576544), and the primitive call through the object's slot 6 (0x5768cd). Both are null unless a
// trace or the simulator-frame counter is installed; they don't change what the step does.
namespace stepobserver {

// (object, frame, node) as the original's call sites have them: `object` is the cTSTreeSim interface whose slot 6
// runs the node.
using NodeFn = int(__cdecl *)(void *object, void *frame, void *node);

// Returns the node's opcode (node slot 3), recording a tree call if it is one.
extern NodeFn gOpcodeRead;
// Runs the primitive (object slot 6 with frame and node) and returns its result, recording it.
extern NodeFn gRunPrimitive;

} // namespace stepobserver
