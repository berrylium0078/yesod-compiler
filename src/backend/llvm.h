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

    void setMinify(bool v) { m_minify = v; }
    bool isMinify() const { return m_minify; }

    void generate(const yesod::koopa::ir::Program& program,
        std::ostream& output);

  private:
    bool m_minify = false;
};

} // namespace yesod::backend

#endif
