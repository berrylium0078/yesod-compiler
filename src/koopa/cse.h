#ifndef _YESOD_KOOPA_CSE_H_
#define _YESOD_KOOPA_CSE_H_

#include "koopa/ir.h"

namespace yesod::koopa::ir {

void eliminateCommonSubexpressions(Program& program);
void eliminateCommonSubexpressions(Program& program, FunctionDef& function);

} // namespace yesod::koopa::ir

#endif // _YESOD_KOOPA_CSE_H_
