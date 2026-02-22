#ifndef SLANG_IR_LOWER_BUFFER_TYPE_H
#define SLANG_IR_LOWER_BUFFER_TYPE_H

namespace Slang
{
struct IRModule;
class TargetProgram;
class DiagnosticSink;

// Lowers each unsupported buffer type into an ordinary struct type.
// This pass is particularly intended to allow using a variety of buffer types
// on the CPU targets, where they have no inherent equivalent.
//
void lowerBufferTypes(IRModule* module, TargetProgram* target, DiagnosticSink* sink);

} // namespace Slang

#endif
