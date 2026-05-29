#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include <stdexcept>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "frontend/semantic.h"
#include "koopa/ir.h"

namespace yesod::koopa {

template <typename T> using Ptr = yesod::frontend::Ptr<T>;
template <typename T> using Ref = yesod::frontend::Ref<T>;

class Generator {

public:
    [[nodiscard]] std::unique_ptr<ir::Program> generateIr(
        const frontend::AST& ast, Ptr<frontend::CompUnit> compUnit,
        const frontend::SemanticInfo& semanticInfo) const;
};
} // namespace yesod::koopa

#endif
