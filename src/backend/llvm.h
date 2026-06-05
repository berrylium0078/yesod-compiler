#ifndef _YESOD_BACKEND_LLVM_H_
#define _YESOD_BACKEND_LLVM_H_

#include <iosfwd>

namespace yesod::koopa::ir {
struct Program;
}

namespace yesod::backend {

class LlvmGenerator {
  public:
    LlvmGenerator() = default;

    void generate(const yesod::koopa::ir::Program& program,
        std::ostream& output);
};

} // namespace yesod::backend

#endif
