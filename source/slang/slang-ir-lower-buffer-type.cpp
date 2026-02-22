#include "slang-ir-lower-buffer-type.h"

#include "slang-ir-insts.h"
#include "slang-ir-inst-pass-base.h"
#include "slang-ir-lower-buffer-element-type.h"
#include "slang-ir-clone.h"
#include "slang-ir.h"

namespace Slang
{

struct BufferTypeLoweringContext: InstPassBase
{
    DiagnosticSink* sink;
    TargetProgram* target;
    Dictionary<IRType*, IRType*> loweredBufferTypes;

    BufferTypeLoweringContext(IRModule* module, TargetProgram* target)
        : InstPassBase(module), target(target)
    {
    }

    IRType* specializeBufferType(
        IRBuilder& builder,
        IRType* elementType,
        IRType* dataLayout,
        IRWitnessTable* originalWitnessTable)
    {
        IRWitnessTable* witness = originalWitnessTable;
        if (originalWitnessTable->getConcreteType() != dataLayout)
        {
            // If we can't use the original witness table, we'll need to create
            // a copy for the correct data layout.
            IRCloneEnv env;
            env.mapOldValToNew[originalWitnessTable->getConcreteType()] = dataLayout;
            witness = as<IRWitnessTable>(cloneInstAndOperands(&env, &builder, originalWitnessTable));
        }

        // TODO: Find implementation. 
        return nullptr;
    }

    IRType* lowerType(IRBuilder& builder, IRType* type)
    {
        if (loweredBufferTypes.containsKey(type))
            return loweredBufferTypes.getValue(type);

        IRType* loweredType = nullptr;
        switch (type->getOp())
        {
        case kIROp_HLSLStructuredBufferType:
            {
                auto bufferType = as<IRHLSLStructuredBufferTypeBase>(type);

                IRType* dataLayout = getTypeLayoutTypeForBuffer(target, builder, bufferType);

                specializeBufferType(
                    builder,
                    bufferType->getElementType(),
                    dataLayout,
                    cast<IRWitnessTable>(bufferType->getOperand(2)));
                //bufferType->getElementType();
                //bufferType->getDataLayout();
            }
            break;
        }

        if (loweredType)
        {
            loweredBufferTypes[type] = loweredType;
            return loweredType;
        }
        else
        {
            return type;
        }
    }

    void processInst(IRBuilder& builder, IRInst* inst)
    {
    }

    void processModule()
    {
        IRBuilder builder(module);

        // Look for all lowerable buffer types.
        processAllInsts([&](IRInst* inst){
            if (IRType* type = as<IRType>(inst))
                lowerType(builder, type);
        });

        for (const auto& [type, loweredType] : loweredBufferTypes)
            type->replaceUsesWith(loweredType);

        // Look for buffer-related instructions and replace them with the
        // corresponding function calls.
        processAllInsts([&](IRInst* inst){
            processInst(builder, inst);
        });
    }
};

void lowerBufferTypes(IRModule* module, TargetProgram* target, DiagnosticSink* sink)
{
    BufferTypeLoweringContext context(module, target);
    context.sink = sink;
    context.processModule();
}

} // namespace Slang
