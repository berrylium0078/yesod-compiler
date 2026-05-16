#include "backend/riscv.h"

#include <cstdint>

#include <map>
#include <ostream>
#include <stdexcept>
#include <string>

namespace yesod::backend {
namespace {

    using yesod::koopa::AllocValue;
    using yesod::koopa::BasicBlock;
    using yesod::koopa::BinaryValue;
    using yesod::koopa::BranchValue;
    using yesod::koopa::Function;
    using yesod::koopa::IntegerValue;
    using yesod::koopa::JumpValue;
    using yesod::koopa::LoadValue;
    using yesod::koopa::Program;
    using yesod::koopa::ReturnValue;
    using yesod::koopa::StoreValue;
    using yesod::koopa::Value;

    class FunctionEmitter {
        std::ostream& m_output;
        std::map<const Value*, int> m_stackSlots;
        std::map<const BasicBlock*, std::string> m_blockLabels;
        int m_stackSize = 0;
        const Function* m_currentFunction_nn = nullptr;

      public:
        explicit FunctionEmitter(std::ostream& output)
            : m_output(output)
        {
        }

        void emitFunction(const Function& function)
        {
            if (function.getNumParams() != 0) {
                throw std::runtime_error(
                    "RISC-V backend does not support function parameters yet");
            }

            assignStackSlots(function);
            m_currentFunction_nn = &function;
            assignBlockLabels(function);

            m_output << "  .globl " << symbolName(function.getName()) << "\n";
            m_output << symbolName(function.getName()) << ":\n";
            emitStackAdjustment(-m_stackSize);

            for (const BasicBlock* basicBlock : function.bbs()) {
                emitBasicBlock(*basicBlock);
            }
        }

      private:
        static std::string symbolName(const std::string& koopaName)
        {
            if (!koopaName.empty() && koopaName.front() == '@') {
                return koopaName.substr(1);
            }
            return koopaName;
        }

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
            return label;
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

        void assignStackSlots(const Function& function)
        {
            m_stackSlots.clear();
            int nextOffset = 0;
            for (const BasicBlock* basicBlock : function.bbs()) {
                for (const Value* instruction : basicBlock->insts()) {
                    if (needsStackSlot(*instruction)) {
                        nextOffset += 4;
                        m_stackSlots.emplace(instruction, nextOffset);
                    }
                }
            }

            m_stackSize = align16(nextOffset);
        }

        static bool needsStackSlot(const Value& value)
        {
            return value.isAllocValue() || value.isLoadValue()
                || value.isBinaryValue();
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

            m_output << "  lw " << targetRegister << ", -" << stackOffset(value)
                     << "(sp)\n";
        }

        void emitStore(const StoreValue& storeValue)
        {
            loadValueToRegister(*storeValue.getVal(), "t0");
            const Value* destination = storeValue.getDestination();
            m_output << "  sw t0, -" << stackOffset(*destination) << "(sp)\n";
        }

        void emitLoad(const LoadValue& loadValue)
        {
            const int sourceOffset = stackOffset(*loadValue.getSource());
            const int targetOffset = stackOffset(loadValue);
            m_output << "  lw t0, -" << sourceOffset << "(sp)\n";
            m_output << "  sw t0, -" << targetOffset << "(sp)\n";
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

            m_output << "  sw t2, -" << stackOffset(binaryValue) << "(sp)\n";
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
            emitStackAdjustment(m_stackSize);
            m_output << "  ret\n";
        }
    };

} // namespace

void RiscvGenerator::generate(const Program& program, std::ostream& output)
{
    if (program.getNumVals() != 0) {
        throw std::runtime_error("RISC-V backend does not support globals yet");
    }
    if (program.getNumFuncs() != 1) {
        throw std::runtime_error("RISC-V backend expects exactly one function");
    }

    output << "  .text\n";
    FunctionEmitter functionEmitter(output);
    functionEmitter.emitFunction(*program.getFunc(0));
}

} // namespace yesod::backend