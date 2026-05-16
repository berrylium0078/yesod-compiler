#include "backend/riscv.h"

#include <cstdint>

#include <map>
#include <ostream>
#include <stdexcept>
#include <string>

namespace yesod::backend {
namespace {

    bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

    bool isAllDigits(const std::string& text)
    {
        if (text.empty()) {
            return false;
        }
        for (const char ch : text) {
            if (!isDigit(ch)) {
                return false;
            }
        }
        return true;
    }

    std::string normalizeIdentifierStem(std::string stem)
    {
        if (!stem.empty() && isDigit(stem.front()) && !isAllDigits(stem)) {
            stem.insert(stem.begin(), '_');
        }
        return stem;
    }

    using yesod::koopa::AllocValue;
    using yesod::koopa::BasicBlock;
    using yesod::koopa::BinaryValue;
    using yesod::koopa::BranchValue;
    using yesod::koopa::CallValue;
    using yesod::koopa::FuncArgRefValue;
    using yesod::koopa::Function;
    using yesod::koopa::GlobalAllocValue;
    using yesod::koopa::IntegerValue;
    using yesod::koopa::JumpValue;
    using yesod::koopa::LoadValue;
    using yesod::koopa::Program;
    using yesod::koopa::ReturnValue;
    using yesod::koopa::StoreValue;
    using yesod::koopa::Value;

    std::string symbolName(const std::string& koopaName)
    {
        if (!koopaName.empty() && koopaName.front() == '@') {
            return koopaName.substr(1);
        }
        return koopaName;
    }

    void emitGlobal(std::ostream& output, const GlobalAllocValue& globalAlloc)
    {
        const std::string name = symbolName(globalAlloc.getName());
        output << "  .globl " << name << "\n";
        output << name << ":\n";

        const Value* initVal = globalAlloc.getInitVal();
        if (initVal->isIntegerValue()) {
            output << "  .word "
                   << dynamic_cast<const IntegerValue&>(*initVal).getVal()
                   << "\n";
            return;
        }
        if (initVal->isZeroInitValue()) {
            output << "  .zero 4\n";
            return;
        }

        throw std::runtime_error(
            "RISC-V backend does not support this global initializer yet");
    }

    class FunctionEmitter {
        std::ostream& m_output;
        std::map<const Value*, int> m_stackSlots;
        std::map<const BasicBlock*, std::string> m_blockLabels;
        int m_outgoingArgAreaSize = 0;
        int m_frameSize = 0;
        int m_savedRaOffset = 0;
        bool m_savesReturnAddress = false;
        const Function* m_currentFunction_nn = nullptr;

      public:
        explicit FunctionEmitter(std::ostream& output)
            : m_output(output)
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
                m_output << "  sw ra, " << m_savedRaOffset << "(sp)\n";
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

            std::map<std::string, int> usedLabels;
            const std::string functionName = symbolName(function.getName());
            for (const BasicBlock* basicBlock : function.bbs()) {
                std::string label
                    = functionName + "_" + sanitizeBlockName(*basicBlock);
                const int duplicateCount = ++usedLabels[label];
                if (duplicateCount > 1) {
                    label += "_" + std::to_string(duplicateCount);
                }
                m_blockLabels.emplace(basicBlock, std::move(label));
            }
        }

        const std::string& blockLabel(const BasicBlock& basicBlock) const
        {
            const auto it = m_blockLabels.find(&basicBlock);
            if (it == m_blockLabels.end()) {
                throw std::runtime_error(
                    "missing RISC-V label for basic block");
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
                        nextOffset += 4;
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
                || value.isBinaryValue()
                || (value.isCallValue() && !value.getVType()->isUnitType());
        }

        static int align16(int value)
        {
            return value == 0 ? 0 : ((value + 15) / 16) * 16;
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
            m_output << "  addi sp, sp, " << delta << "\n";
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

        void loadValueToRegister(
            const Value& value, const std::string& targetRegister)
        {
            if (value.isIntegerValue()) {
                m_output << "  li " << targetRegister << ", "
                         << dynamic_cast<const IntegerValue&>(value).getVal()
                         << "\n";
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
                    m_output << "  lw " << targetRegister << ", " << offset
                             << "(sp)\n";
                }
                return;
            }

            m_output << "  lw " << targetRegister << ", " << stackOffset(value)
                     << "(sp)\n";
        }

        void emitStore(const StoreValue& storeValue)
        {
            const Value* destination = storeValue.getDestination();
            if (destination->isGlobalAllocValue()) {
                loadValueToRegister(*storeValue.getVal(), "t2");
                m_output << "  la t0, "
                         << symbolName(destination->getName()) << "\n";
                m_output << "  sw t2, 0(t0)\n";
                return;
            }

            loadValueToRegister(*storeValue.getVal(), "t0");
            m_output << "  sw t0, " << stackOffset(*destination) << "(sp)\n";
        }

        void emitLoad(const LoadValue& loadValue)
        {
            if (loadValue.getSource()->isGlobalAllocValue()) {
                m_output << "  la t0, "
                         << symbolName(loadValue.getSource()->getName())
                         << "\n";
                m_output << "  lw t1, 0(t0)\n";
                m_output << "  sw t1, " << stackOffset(loadValue) << "(sp)\n";
                return;
            }

            const int sourceOffset = stackOffset(*loadValue.getSource());
            const int targetOffset = stackOffset(loadValue);
            m_output << "  lw t0, " << sourceOffset << "(sp)\n";
            m_output << "  sw t0, " << targetOffset << "(sp)\n";
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

            m_output << "  sw t2, " << stackOffset(binaryValue) << "(sp)\n";
        }

        void emitBranch(const BranchValue& branchValue)
        {
            if (branchValue.getNumTrueArgs() != 0
                || branchValue.getNumFalseArgs() != 0) {
                throw std::runtime_error(
                    "RISC-V backend does not support block arguments yet");
            }

            loadValueToRegister(*branchValue.getCondition(), "t0");
            m_output << "  bnez t0, " << blockLabel(*branchValue.getTrueBB())
                     << "\n";
            m_output << "  j " << blockLabel(*branchValue.getFalseBB()) << "\n";
        }

        void emitJump(const JumpValue& jumpValue)
        {
            if (jumpValue.getNumArgs() != 0) {
                throw std::runtime_error(
                    "RISC-V backend does not support block arguments yet");
            }
            m_output << "  j " << blockLabel(*jumpValue.getTargetBB()) << "\n";
        }

        void emitReturn(const ReturnValue& returnValue)
        {
            if (returnValue.getVal() != nullptr) {
                loadValueToRegister(*returnValue.getVal(), "a0");
            }
            if (m_savesReturnAddress) {
                m_output << "  lw ra, " << m_savedRaOffset << "(sp)\n";
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
                m_output << "  sw t0, " << offset << "(sp)\n";
            }

            m_output << "  call "
                     << symbolName(callValue.getCallee()->getName()) << "\n";
            if (!callValue.getVType()->isUnitType()) {
                m_output << "  sw a0, " << stackOffset(callValue) << "(sp)\n";
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
        output << "  .data\n";
        for (const auto* value : program.vals()) {
            emitGlobal(output, dynamic_cast<const GlobalAllocValue&>(*value));
        }
    }

    output << "  .text\n";
    FunctionEmitter functionEmitter(output);
    for (const auto* function : program.funcs()) {
        if (function->getNumBBs() == 0) {
            continue;
        }
        functionEmitter.emitFunction(*function);
    }
}

} // namespace yesod::backend