// slang-ir-lower-cpu-resource-types.cpp

#include "slang-ir-lower-cpu-resource-types.h"

#include "slang-ir-inst-pass-base.h"
//#include "slang-ir-insts.h"
#include "slang-ir-layout.h"
#include "slang-ir-lower-buffer-element-type.h"
#include "slang-ir.h"

namespace Slang
{

struct ResourceTypeLoweringContext : InstPassBase
{
    DiagnosticSink* diagnosticSink;
    CodeGenContext* codeGenContext;
    Dictionary<IRType*, IRType*> loweredResourceTypes;

    ResourceTypeLoweringContext(CodeGenContext* codeGenContext, IRModule* inModule)
        : InstPassBase(inModule), codeGenContext(codeGenContext)
    {
    }

    IRType* lowerType(IRBuilder& builder, IRType* type)
    {
        if (loweredResourceTypes.containsKey(type))
            return loweredResourceTypes.getValue(type);

        codeGenContext->getTargetProgram();

        IRTypeLayoutRules* layoutRules = nullptr;
        IRType* loweredType = nullptr;
        // TODO: Make these (optionally) somehow user-configurable; this is
        // useful for supporting user-defined runtimes for the CPU targets.
        //
        // One option would be to look for specific named structs in the module
        // like `__CPUTexture2D` and use that struct to replace the
        // corresponding resource type here.
        switch (type->getOp())
        {
        case kIROp_ConstantBufferType:
        case kIROp_ParameterBlockType:
            {
                layoutRules = getTypeLayoutRuleForBuffer(codeGenContext->getTargetProgram(), type);
                loweredType = builder.getPtrType(as<IRType>(type->getOperand(0)));
            }
            break;
        case kIROp_HLSLStructuredBufferType:
        case kIROp_HLSLRWStructuredBufferType:
            {
                auto bufferType = as<IRHLSLStructuredBufferTypeBase>(type);

                layoutRules = getTypeLayoutRuleForBuffer(codeGenContext->getTargetProgram(), type);

                IRStructType* s = builder.createStructType();
                auto ptrKey = builder.createStructKey();
                auto sizeKey = builder.createStructKey();
                builder.createStructField(s, ptrKey, builder.getPtrType(bufferType->getElementType()));
                builder.createStructField(s, sizeKey, builder.getType(kIROp_UIntPtrType));
                loweredType = s;
            }
            break;
        case kIROp_HLSLByteAddressBufferType:
        case kIROp_HLSLRWByteAddressBufferType:
            {
                layoutRules = getTypeLayoutRuleForBuffer(codeGenContext->getTargetProgram(), type);

                IRStructType* s = builder.createStructType();
                auto ptrKey = builder.createStructKey();
                auto sizeKey = builder.createStructKey();
                builder.createStructField(s, ptrKey, builder.getPtrType(builder.getUInt8Type()));
                builder.createStructField(s, sizeKey, builder.getType(kIROp_UIntPtrType));
                loweredType = s;
            }
            break;
            // TODO: Texture & Sampler & TLAS types.
        default:
            break;
        }

        if (loweredType)
        {
            // We need to explicitly store the layout rules here; otherwise, the
            // LLVM emitter will have no idea what the buffer's layout should
            // be.
            // TODO: This won't work. Every time getSizeAndAlignment is called,
            // a new decoration is added for whatever was queried; we can't use
            // this to enforce a specific layout rule for a pointer like we'd
            // want to.
            if (layoutRules)
            {
                auto intType = builder.getIntType();
                auto int64Type = builder.getInt64Type();
                IRSizeAndAlignment sizeAndAlignment;
                layoutRules->calcSizeAndAlignment(codeGenContext->getProgram()->getOptionSet(), type, &sizeAndAlignment);
                builder.addDecoration(
                    loweredType,
                    kIROp_SizeAndAlignmentDecoration,
                    builder.getIntValue(intType, (IRIntegerValue)layoutRules->ruleName),
                    builder.getIntValue(int64Type, sizeAndAlignment.size),
                    builder.getIntValue(intType, sizeAndAlignment.alignment));
            }

            loweredResourceTypes[type] = loweredType;
        }
        return loweredType ? loweredType : type;
    }

    IRInst* getBufferPtr(IRBuilder& builder, IRInst* buffer)
    {
        auto structType = cast<IRStructType>(buffer->getDataType());
        return builder.emitFieldExtract(
            buffer,
            structType->getFields().getFirst()->getKey());
    }

    IRInst* getBufferSize(IRBuilder& builder, IRInst* buffer)
    {
        auto structType = cast<IRStructType>(buffer->getDataType());
        return builder.emitFieldExtract(
            buffer,
            structType->getFields().getLast()->getKey());
    }

    IRTypeLayoutRules* getBufferLayoutRules(IRInst* buffer)
    {
        auto type = as<IRType>(buffer);
        if (!type)
            type = buffer->getDataType();
        auto layout = type->findDecoration<IRSizeAndAlignmentDecoration>();
        SLANG_ASSERT(layout);

        return IRTypeLayoutRules::get(layout->getLayoutName());
    }

    void processInst(IRBuilder& builder, IRInst* inst)
    {
        // TODO: These should also be user-definable, perhaps with specifically
        // named functions. If a user-defined implementation refers to an
        // external function, it should ideally be vectorizable somehow.
        // In LLVM, that can be achieved with the `vector-function-abi-variant`
        // attribute, which lists pre-vectorized variants of a function. We may
        // eventually need something similar.
        IRInst* loweredInst = nullptr;
        builder.setInsertBefore(inst);
        switch (inst->getOp())
        {
        case kIROp_RWStructuredBufferGetElementPtr:
            {
                auto gepInst = static_cast<IRRWStructuredBufferGetElementPtr*>(inst);
                auto index = gepInst->getIndex();
                auto ptr = getBufferPtr(builder, gepInst->getBase());
                loweredInst = builder.emitGetOffsetPtr(ptr, index);
            }
            break;
        case kIROp_StructuredBufferLoad:
        case kIROp_RWStructuredBufferLoad:
            {
                auto base = inst->getOperand(0);
                auto index = inst->getOperand(1);
                auto ptr = getBufferPtr(builder, base);
                auto offsetPtr = builder.emitGetOffsetPtr(ptr, index);
                loweredInst = builder.emitLoad(offsetPtr);
            }
            break;
        case kIROp_RWStructuredBufferStore:
            {
                auto base = inst->getOperand(0);
                auto index = inst->getOperand(1);
                auto val = inst->getOperand(2);
                auto ptr = getBufferPtr(builder, base);
                auto offsetPtr = builder.emitGetOffsetPtr(ptr, index);
                loweredInst = builder.emitStore(offsetPtr, val);
            }
            break;
        case kIROp_ByteAddressBufferLoad:
            {
                auto base = inst->getOperand(0);
                auto index = inst->getOperand(1);
                auto ptr = getBufferPtr(builder, base);
                auto offsetPtr = builder.emitGetOffsetPtr(ptr, index);
                auto typedPtr = builder.emitCast(builder.getPtrType(inst->getDataType()), offsetPtr);
                loweredInst = builder.emitLoad(inst->getDataType(), typedPtr);
            }
            break;
        case kIROp_ByteAddressBufferStore:
            {
                auto base = inst->getOperand(0);
                auto index = inst->getOperand(1);
                auto val = inst->getOperand(inst->getOperandCount() - 1);
                auto ptr = getBufferPtr(builder, base);
                auto offsetPtr = builder.emitGetOffsetPtr(ptr, index);
                auto typedPtr = builder.emitCast(builder.getPtrType(val->getDataType()), offsetPtr);
                loweredInst = builder.emitStore(typedPtr, val);
            }
            break;

        case kIROp_StructuredBufferGetDimensions:
            {
                auto getDimensionsInst = cast<IRStructuredBufferGetDimensions>(inst);
                auto buffer = getDimensionsInst->getBuffer();
                auto ptr = getBufferPtr(builder, buffer);
                auto size = getBufferSize(builder, buffer);
                auto intType = builder.getIntType();
                auto vecType = builder.getVectorType(intType, 2);

                auto rules = getBufferLayoutRules(buffer);
                IRSizeAndAlignment sizeAlignment;
                Slang::getSizeAndAlignment(
                    codeGenContext->getProgram()->getOptionSet(),
                    rules,
                    cast<IRPtrType>(ptr->getDataType())->getValueType(),
                    &sizeAlignment);

                loweredInst = builder.emitMakeVector(vecType, {
                    builder.emitCast(intType, size),
                    builder.getIntValue(sizeAlignment.size)
                });
            }
            break;

        case kIROp_GetEquivalentStructuredBuffer:
            {
                auto bufferType = inst->getDataType();
                auto byteBuffer = inst->getOperand(0);
                auto rules = getBufferLayoutRules(bufferType);

                auto structType = cast<IRStructType>(bufferType);
                auto ptrType = as<IRPtrType>(structType->getFields().getFirst()->getFieldType());
                auto elementType = ptrType->getValueType();

                IRSizeAndAlignment sizeAlignment;
                Slang::getSizeAndAlignment(
                    codeGenContext->getProgram()->getOptionSet(),
                    rules,
                    elementType,
                    &sizeAlignment);

                auto ptr = getBufferPtr(builder, byteBuffer);
                auto bytes = getBufferSize(builder, byteBuffer);

                auto size = builder.emitDiv(
                    builder.getIntPtrType(),
                    bytes,
                    builder.getIntValue(builder.getIntPtrType(), sizeAlignment.size));

                loweredInst = builder.emitMakeStruct(structType, {
                    builder.emitCast(ptrType, ptr),
                    size
                });
            }
            break;
        }

        if (loweredInst)
        {
            inst->replaceUsesWith(loweredInst);
            inst->removeAndDeallocate();
        }
    }

    void processModule()
    {
        IRBuilder builder(module);

        processAllInsts([&](IRInst* inst){
            if (IRType* type = as<IRType>(inst))
                lowerType(builder, type);
        });

        // Replace all resource types with lowered types.
        for (const auto& [type, loweredType] : loweredResourceTypes)
            type->replaceUsesWith(loweredType);

        processAllInsts([&](IRInst* inst){ processInst(builder, inst); });

    }
};

void lowerCPUResourceTypes(IRModule* module, CodeGenContext* codeGenContext)
{
    ResourceTypeLoweringContext context(codeGenContext, module);
    context.diagnosticSink = codeGenContext->getSink();
    context.processModule();
}

} // namespace Slang
