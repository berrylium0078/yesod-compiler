#include "backend/riscv.h"

#include <cstdint>

#include <cctype>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>

namespace yesod::backend {

bool isAllDigits(const std::string& text)
{
    for (auto c : text) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

std::string normalizeIdentifierStem(std::string stem)
{
    if (!stem.empty() && std::isdigit(stem.front()) && !isAllDigits(stem)) {
        stem.insert(stem.begin(), '_');
    }
    return stem;
}

std::string RiscvGenerator::genLabel(std::string hint)
{
    hint = normalizeIdentifierStem(std::move(hint));
    std::string label = "_local" + hint;
    const int duplicateCount = ++m_usedLabels[label];
    if (duplicateCount > 1) {
        label += "_" + std::to_string(duplicateCount);
    }
    return label;
}
namespace {

    enum class GlobalSection {
        rodata,
        data,
        bss,
    };

    using yesod::koopa::AggregateValue;
    using yesod::koopa::AllocValue;
    using yesod::koopa::ArrayType;
    using yesod::koopa::BasicBlock;
    using yesod::koopa::BinaryValue;
    using yesod::koopa::BranchValue;
    using yesod::koopa::CallValue;
    using yesod::koopa::FuncArgRefValue;
    using yesod::koopa::Function;
    using yesod::koopa::FunctionType;
    using yesod::koopa::GetElemPtrValue;
    using yesod::koopa::GetPtrValue;
    using yesod::koopa::GlobalAllocValue;
    using yesod::koopa::IntegerValue;
    using yesod::koopa::JumpValue;
    using yesod::koopa::LoadValue;
    using yesod::koopa::PointerType;
    using yesod::koopa::Program;
    using yesod::koopa::ReturnValue;
    using yesod::koopa::StoreValue;
    using yesod::koopa::Type;
    using yesod::koopa::Value;
    using yesod::koopa::ZeroInitValue;

    std::string symbolName(const std::string& koopaName)
    {
        if (!koopaName.empty() && koopaName.front() == '@') {
            return koopaName.substr(1);
        }
        return koopaName;
    }

    int typeSize(const Type& type)
    {
        if (type.isInt32Type() || type.isPointerType()) {
            return 4;
        }
        if (type.isUnitType()) {
            return 0;
        }
        if (type.isArrayType()) {
            const auto& arrayType = dynamic_cast<const ArrayType&>(type);
            return static_cast<int>(arrayType.getNumElements())
                * typeSize(*arrayType.getElementType());
        }
        if (type.isFunctionType()) {
            return 0;
        }
        throw std::runtime_error("unsupported Koopa type in RISC-V backend");
    }

    bool isConstArraySymbolName(const std::string& name)
    {
        return name.rfind("c_", 0) == 0;
    }

    GlobalSection classifyGlobal(const GlobalAllocValue& globalAlloc)
    {
        if (globalAlloc.getInitVal()->isZeroInitValue()) {
            return GlobalSection::bss;
        }
        if (isConstArraySymbolName(symbolName(globalAlloc.getName()))) {
            return GlobalSection::rodata;
        }
        return GlobalSection::data;
    }

    void emitGlobalInitializer(std::ostream& output, const Value& initVal)
    {
        if (initVal.isIntegerValue()) {
            output << "  .word "
                   << dynamic_cast<const IntegerValue&>(initVal).getVal()
                   << "\n";
            return;
        }
        if (initVal.isZeroInitValue()) {
            output << "  .zero " << typeSize(*initVal.getVType()) << "\n";
            return;
        }
        if (initVal.isAggregateValue()) {
            const auto& aggregate
                = dynamic_cast<const AggregateValue&>(initVal);
            for (size_t i = 0; i < aggregate.getNumElements(); ++i) {
                emitGlobalInitializer(output, *aggregate.getElement(i));
            }
            return;
        }

        throw std::runtime_error(
            "RISC-V backend does not support this global initializer yet");
    }

    void emitGlobal(std::ostream& output, const GlobalAllocValue& globalAlloc)
    {
        const std::string name = symbolName(globalAlloc.getName());
        output << "  .globl " << name << "\n";
        output << name << ":\n";
        emitGlobalInitializer(output, *globalAlloc.getInitVal());
    }

    class FunctionEmitter {
        RiscvGenerator* m_parent;
        std::ostream& m_output;
        std::map<const Value*, int> m_stackSlots;
        std::map<const BasicBlock*, std::string> m_blockLabels;
        int m_outgoingArgAreaSize = 0;
        int m_frameSize = 0;
        int m_savedRaOffset = 0;
        bool m_savesReturnAddress = false;
        const Function* m_currentFunction_nn = nullptr;

      public:
        explicit FunctionEmitter(RiscvGenerator* parent, std::ostream& output)
            : m_parent(parent)
            , m_output(output)
        {
        }

        void emitFunction(const Function& function)
        {
            m_currentFunction_nn = &function;
            assignFrameLayout(function);
            assignBlockLabels(function);

            m_output << "  .globl " << symbolName(function.getName()) << "\n";
            m_output << symbolName(function.getName()) << ":\n";
            emitStackAdjustment(-m_frameSize);
            if (m_savesReturnAddress) {
                emitStoreToStackSlot("ra", m_savedRaOffset);
            }

            for (const BasicBlock* basicBlock : function.bbs()) {
                emitBasicBlock(*basicBlock);
            }
        }

      private:
        static std::string sanitizeBlockName(const BasicBlock& basicBlock)
        {
            std::string label;
            if (basicBlock.isEntry()) {
                label = "entry";
            }
            for (const char ch : basicBlock.getName()) {
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                    || (ch >= '0' && ch <= '9')) {
                    label.push_back(ch);
                } else if (ch == '%') {
                    continue;
                } else {
                    label.push_back('_');
                }
            }
            if (label.empty()) {
                label = basicBlock.isEntry() ? "entry" : "bb";
            }
            return normalizeIdentifierStem(std::move(label));
        }

        void assignBlockLabels(const Function& function)
        {
            m_blockLabels.clear();
            const std::string functionName = symbolName(function.getName());
            for (const BasicBlock* basicBlock : function.bbs()) {
                std::string label
                    = functionName + "_" + sanitizeBlockName(*basicBlock);
                m_blockLabels.emplace(basicBlock,
                    m_parent->genLabel(std::move(label)));
            }
        }

        const std::string& blockLabel(const BasicBlock& basicBlock) const
        {
            const auto it = m_blockLabels.find(&basicBlock);
            if (it == m_blockLabels.end()) {
                throw std::runtime_error(
                    "missing RISC-V label for basic body");
            }
            return it->second;
        }

        void assignFrameLayout(const Function& function)
        {
            m_stackSlots.clear();
            size_t maxCallArgs = 0;
            m_savesReturnAddress = false;

            for (const BasicBlock* basicBlock : function.bbs()) {
                for (const Value* instruction : basicBlock->insts()) {
                    if (instruction->isCallValue()) {
                        const auto& callValue
                            = dynamic_cast<const CallValue&>(*instruction);
                        m_savesReturnAddress = true;
                        if (callValue.getNumArgs() > maxCallArgs) {
                            maxCallArgs = callValue.getNumArgs();
                        }
                    }
                }
            }

            m_outgoingArgAreaSize
                = static_cast<int>(maxCallArgs > 8 ? (maxCallArgs - 8) * 4 : 0);

            int nextOffset = m_outgoingArgAreaSize;
            for (const BasicBlock* basicBlock : function.bbs()) {
                for (const Value* instruction : basicBlock->insts()) {
                    if (needsStackSlot(*instruction)) {
                        m_stackSlots.emplace(instruction, nextOffset);
                        nextOffset += valueStorageSize(*instruction);
                    }
                }
            }

            if (m_savesReturnAddress) {
                m_savedRaOffset = nextOffset;
                nextOffset += 4;
            } else {
                m_savedRaOffset = 0;
            }

            m_frameSize = align16(nextOffset);
        }

        static bool needsStackSlot(const Value& value)
        {
            return value.isAllocValue() || value.isLoadValue()
                || value.isBinaryValue() || value.isGetPtrValue()
                || value.isGetElemPtrValue()
                || (value.isCallValue() && !value.getVType()->isUnitType());
        }

        static int valueStorageSize(const Value& value)
        {
            if (value.isAllocValue()) {
                return typeSize(
                    *dynamic_cast<const PointerType*>(value.getVType())
                        ->getPointeeType());
            }
            return typeSize(*value.getVType());
        }

        static int align16(int value)
        {
            return value == 0 ? 0 : ((value + 15) / 16) * 16;
        }

        static bool fitsImm12(int value)
        {
            return value >= -2048 && value <= 2047;
        }

        int stackOffset(const Value& value) const
        {
            const auto it = m_stackSlots.find(&value);
            if (it == m_stackSlots.end()) {
                throw std::runtime_error("missing stack slot for Koopa value");
            }
            return it->second;
        }

        void emitStackAdjustment(int delta)
        {
            if (delta == 0) {
                return;
            }
            if (fitsImm12(delta)) {
                m_output << "  addi sp, sp, " << delta << "\n";
                return;
            }
            m_output << "  li t3, " << delta << "\n";
            m_output << "  add sp, sp, t3\n";
        }

        void emitAddressOfStackSlot(
            int offset, const std::string& targetRegister)
        {
            if (fitsImm12(offset)) {
                m_output << "  addi " << targetRegister << ", sp, " << offset
                         << "\n";
                return;
            }
            m_output << "  li " << targetRegister << ", " << offset << "\n";
            m_output << "  add " << targetRegister << ", sp, " << targetRegister
                     << "\n";
        }

        void emitLoadFromStackSlot(
            int offset, const std::string& targetRegister)
        {
            if (fitsImm12(offset)) {
                m_output << "  lw " << targetRegister << ", " << offset
                         << "(sp)\n";
                return;
            }
            emitAddressOfStackSlot(offset, "t3");
            m_output << "  lw " << targetRegister << ", 0(t3)\n";
        }

        void emitStoreToStackSlot(const std::string& sourceRegister, int offset)
        {
            if (fitsImm12(offset)) {
                m_output << "  sw " << sourceRegister << ", " << offset
                         << "(sp)\n";
                return;
            }
            emitAddressOfStackSlot(offset, "t3");
            m_output << "  sw " << sourceRegister << ", 0(t3)\n";
        }

        void emitBasicBlock(const BasicBlock& basicBlock)
        {
            if (!basicBlock.isEntry()) {
                m_output << blockLabel(basicBlock) << ":\n";
            }

            for (const Value* instruction : basicBlock.insts()) {
                emitInstruction(*instruction);
            }
        }

        void emitInstruction(const Value& instruction)
        {
            if (instruction.isAllocValue()) {
                return;
            }

            if (instruction.isStoreValue()) {
                emitStore(dynamic_cast<const StoreValue&>(instruction));
                return;
            }

            if (instruction.isLoadValue()) {
                emitLoad(dynamic_cast<const LoadValue&>(instruction));
                return;
            }

            if (instruction.isGetPtrValue()) {
                emitGetPtr(dynamic_cast<const GetPtrValue&>(instruction));
                return;
            }

            if (instruction.isGetElemPtrValue()) {
                emitGetElemPtr(
                    dynamic_cast<const GetElemPtrValue&>(instruction));
                return;
            }

            if (instruction.isBinaryValue()) {
                emitBinary(dynamic_cast<const BinaryValue&>(instruction));
                return;
            }

            if (instruction.isBranchValue()) {
                emitBranch(dynamic_cast<const BranchValue&>(instruction));
                return;
            }

            if (instruction.isReturnValue()) {
                emitReturn(dynamic_cast<const ReturnValue&>(instruction));
                return;
            }

            if (instruction.isCallValue()) {
                emitCall(dynamic_cast<const CallValue&>(instruction));
                return;
            }

            if (instruction.isJumpValue()) {
                emitJump(dynamic_cast<const JumpValue&>(instruction));
                return;
            }

            throw std::runtime_error(
                "unsupported Koopa instruction in RISC-V backend");
        }

        void loadPointerValueToRegister(
            const Value& value, const std::string& targetRegister)
        {
            if (value.isGlobalAllocValue()) {
                m_output << "  la " << targetRegister << ", "
                         << symbolName(value.getName()) << "\n";
                return;
            }

            if (value.isAllocValue()) {
                emitAddressOfStackSlot(stackOffset(value), targetRegister);
                return;
            }

            if (value.isFuncArgRefValue()) {
                const auto& funcArgRef
                    = dynamic_cast<const FuncArgRefValue&>(value);
                if (funcArgRef.getIndex() < 8) {
                    const std::string sourceRegister
                        = "a" + std::to_string(funcArgRef.getIndex());
                    if (sourceRegister != targetRegister) {
                        m_output << "  mv " << targetRegister << ", "
                                 << sourceRegister << "\n";
                    }
                } else {
                    const int offset = m_frameSize
                        + static_cast<int>((funcArgRef.getIndex() - 8) * 4);
                    emitLoadFromStackSlot(offset, targetRegister);
                }
                return;
            }

            emitLoadFromStackSlot(stackOffset(value), targetRegister);
        }

        void loadValueToRegister(
            const Value& value, const std::string& targetRegister)
        {
            if (value.isIntegerValue()) {
                m_output << "  li " << targetRegister << ", "
                         << dynamic_cast<const IntegerValue&>(value).getVal()
                         << "\n";
                return;
            }

            if (value.getVType()->isPointerType()) {
                loadPointerValueToRegister(value, targetRegister);
                return;
            }

            if (value.isFuncArgRefValue()) {
                const auto& funcArgRef
                    = dynamic_cast<const FuncArgRefValue&>(value);
                if (funcArgRef.getIndex() < 8) {
                    const std::string sourceRegister
                        = "a" + std::to_string(funcArgRef.getIndex());
                    if (sourceRegister != targetRegister) {
                        m_output << "  mv " << targetRegister << ", "
                                 << sourceRegister << "\n";
                    }
                } else {
                    const int offset = m_frameSize
                        + static_cast<int>((funcArgRef.getIndex() - 8) * 4);
                    emitLoadFromStackSlot(offset, targetRegister);
                }
                return;
            }

            emitLoadFromStackSlot(stackOffset(value), targetRegister);
        }

        void emitStore(const StoreValue& storeValue)
        {
            const Value* destination = storeValue.getDestination();
            loadValueToRegister(*storeValue.getVal(), "t2");
            if (destination->isAllocValue()) {
                emitStoreToStackSlot("t2", stackOffset(*destination));
                return;
            }
            loadPointerValueToRegister(*destination, "t0");
            m_output << "  sw t2, 0(t0)\n";
        }

        void emitLoad(const LoadValue& loadValue)
        {
            if (loadValue.getSource()->isAllocValue()) {
                emitLoadFromStackSlot(
                    stackOffset(*loadValue.getSource()), "t1");
                emitStoreToStackSlot("t1", stackOffset(loadValue));
                return;
            }
            loadPointerValueToRegister(*loadValue.getSource(), "t0");
            m_output << "  lw t1, 0(t0)\n";
            emitStoreToStackSlot("t1", stackOffset(loadValue));
        }

        void emitIndexedPointer(const Value& source, const Value& index,
            int stride, const Value& result)
        {
            loadPointerValueToRegister(source, "t0");
            loadValueToRegister(index, "t1");
            m_output << "  li t2, " << stride << "\n";
            m_output << "  mul t1, t1, t2\n";
            m_output << "  add t0, t0, t1\n";
            emitStoreToStackSlot("t0", stackOffset(result));
        }

        void emitGetPtr(const GetPtrValue& getPtrValue)
        {
            const auto* pointerType = dynamic_cast<const PointerType*>(
                getPtrValue.getSource()->getVType());
            emitIndexedPointer(*getPtrValue.getSource(),
                *getPtrValue.getIndex(),
                typeSize(*pointerType->getPointeeType()), getPtrValue);
        }

        void emitGetElemPtr(const GetElemPtrValue& getElemPtrValue)
        {
            const auto* pointerType = dynamic_cast<const PointerType*>(
                getElemPtrValue.getSource()->getVType());
            const auto* arrayType
                = dynamic_cast<const ArrayType*>(pointerType->getPointeeType());
            emitIndexedPointer(*getElemPtrValue.getSource(),
                *getElemPtrValue.getIndex(),
                typeSize(*arrayType->getElementType()), getElemPtrValue);
        }

        void emitBinary(const BinaryValue& binaryValue)
        {
            loadValueToRegister(*binaryValue.getLhs(), "t0");
            loadValueToRegister(*binaryValue.getRhs(), "t1");

            switch (binaryValue.getOp()) {
            case KOOPA_RBO_NOT_EQ:
                m_output << "  xor t2, t0, t1\n";
                m_output << "  snez t2, t2\n";
                break;
            case KOOPA_RBO_EQ:
                m_output << "  xor t2, t0, t1\n";
                m_output << "  seqz t2, t2\n";
                break;
            case KOOPA_RBO_GT:
                m_output << "  sgt t2, t0, t1\n";
                break;
            case KOOPA_RBO_LT:
                m_output << "  slt t2, t0, t1\n";
                break;
            case KOOPA_RBO_GE:
                m_output << "  slt t2, t0, t1\n";
                m_output << "  xori t2, t2, 1\n";
                break;
            case KOOPA_RBO_LE:
                m_output << "  sgt t2, t0, t1\n";
                m_output << "  xori t2, t2, 1\n";
                break;
            case KOOPA_RBO_ADD:
                m_output << "  add t2, t0, t1\n";
                break;
            case KOOPA_RBO_SUB:
                m_output << "  sub t2, t0, t1\n";
                break;
            case KOOPA_RBO_MUL:
                m_output << "  mul t2, t0, t1\n";
                break;
            case KOOPA_RBO_DIV:
                m_output << "  div t2, t0, t1\n";
                break;
            case KOOPA_RBO_MOD:
                m_output << "  rem t2, t0, t1\n";
                break;
            case KOOPA_RBO_AND:
                m_output << "  and t2, t0, t1\n";
                break;
            case KOOPA_RBO_OR:
                m_output << "  or t2, t0, t1\n";
                break;
            case KOOPA_RBO_XOR:
                m_output << "  xor t2, t0, t1\n";
                break;
            case KOOPA_RBO_SHL:
                m_output << "  sll t2, t0, t1\n";
                break;
            case KOOPA_RBO_SHR:
                m_output << "  srl t2, t0, t1\n";
                break;
            case KOOPA_RBO_SAR:
                m_output << "  sra t2, t0, t1\n";
                break;
            default:
                throw std::runtime_error(
                    "unsupported Koopa binary op in RISC-V backend");
            }

            emitStoreToStackSlot("t2", stackOffset(binaryValue));
        }

        void emitBranch(const BranchValue& branchValue)
        {
            if (branchValue.getNumTrueArgs() != 0
                || branchValue.getNumFalseArgs() != 0) {
                throw std::runtime_error(
                    "RISC-V backend does not support body arguments yet");
            }

            loadValueToRegister(*branchValue.getCondition(), "t0");
            std::string label = m_parent->genLabel("_local");
            m_output << "  bnez t0, " << label << "\n";
            m_output << "  j " << blockLabel(*branchValue.getFalseBB()) << "\n";
            m_output << label << ":\n";
            m_output << "  j " << blockLabel(*branchValue.getTrueBB()) << "\n";
        }

        void emitJump(const JumpValue& jumpValue)
        {
            if (jumpValue.getNumArgs() != 0) {
                throw std::runtime_error(
                    "RISC-V backend does not support body arguments yet");
            }
            m_output << "  j " << blockLabel(*jumpValue.getTargetBB()) << "\n";
        }

        void emitReturn(const ReturnValue& returnValue)
        {
            if (returnValue.getVal() != nullptr) {
                loadValueToRegister(*returnValue.getVal(), "a0");
            }
            if (m_savesReturnAddress) {
                emitLoadFromStackSlot(m_savedRaOffset, "ra");
            }
            emitStackAdjustment(m_frameSize);
            m_output << "  ret\n";
        }

        void emitCall(const CallValue& callValue)
        {
            for (size_t i = 0; i < callValue.getNumArgs(); ++i) {
                if (i < 8) {
                    loadValueToRegister(
                        *callValue.getArg(i), "a" + std::to_string(i));
                    continue;
                }

                loadValueToRegister(*callValue.getArg(i), "t0");
                const int offset = static_cast<int>((i - 8) * 4);
                emitStoreToStackSlot("t0", offset);
            }

            m_output << "  call "
                     << symbolName(callValue.getCallee()->getName()) << "\n";
            if (!callValue.getVType()->isUnitType()) {
                emitStoreToStackSlot("a0", stackOffset(callValue));
            }
        }
    };

} // namespace

void RiscvGenerator::generate(const Program& program, std::ostream& output)
{
    if (program.getNumFuncs() == 0) {
        throw std::runtime_error(
            "RISC-V backend expects at least one function");
    }

    if (program.getNumVals() != 0) {
        auto emitGlobalSection
            = [&](GlobalSection section, const char* header) {
                  bool emittedHeader = false;
                  for (const auto* value : program.vals()) {
                      const auto& globalAlloc
                          = dynamic_cast<const GlobalAllocValue&>(*value);
                      if (classifyGlobal(globalAlloc) != section) {
                          continue;
                      }
                      if (!emittedHeader) {
                          output << header;
                          emittedHeader = true;
                      }
                      emitGlobal(output, globalAlloc);
                  }
              };

        emitGlobalSection(GlobalSection::rodata, "  .section .rodata\n");
        emitGlobalSection(GlobalSection::data, "  .data\n");
        emitGlobalSection(GlobalSection::bss, "  .bss\n");
    }

    output << "  .text\n";
    FunctionEmitter functionEmitter(this, output);
    for (const auto* function : program.funcs()) {
        if (function->getNumBBs() == 0) {
            continue;
        }
        functionEmitter.emitFunction(*function);
    }
}

} // namespace yesod::backend