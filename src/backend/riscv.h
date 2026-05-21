#ifndef _YESOD_BACKEND_RISCV_H_
#define _YESOD_BACKEND_RISCV_H_

#include <iosfwd>
#include <map>
#include <string>
#include "koopa/mykoopa.h"

namespace yesod::backend {

class RiscvGenerator {
  public:
    RiscvGenerator() = default;

    void generate(const yesod::koopa::Program& program, std::ostream& output);
    std::string genLabel(std::string hint);
  private:
    std::map<std::string, int> m_usedLabels;
};

} // namespace yesod::backend

#endif