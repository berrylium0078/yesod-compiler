#ifndef _YESOD_BACKEND_RISCV_H_
#define _YESOD_BACKEND_RISCV_H_

#include <iosfwd>

#include "koopa/mykoopa.h"

namespace yesod::backend {

class RiscvGenerator {
  public:
    RiscvGenerator() = default;

    void generate(const yesod::koopa::Program& program, std::ostream& output);
};

} // namespace yesod::backend

#endif