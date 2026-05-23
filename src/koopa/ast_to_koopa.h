#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "frontend/semantic.h"
#include "koopa/mykoopa.h"

namespace yesod::koopa {

template <typename T> using Ptr = yesod::frontend::Ptr<T>;
template <typename T> using Ref = yesod::frontend::Ref<T>;

class Generator {

public:
    [[nodiscard]] Program* generate(const frontend::AST& ast,
        Ptr<frontend::CompUnit> compUnit,
        const frontend::SemanticInfo& semanticInfo) const;

private:
    [[nodiscard]] Function* createFunctionDecl(const frontend::AST& ast,
        Ptr<frontend::FuncDef> funcDef,
        const frontend::SemanticInfo& semanticInfo) const;
    [[nodiscard]] Function* createExternalFunctionDecl(
        const frontend::SemanticSymbol& symbol) const;
    [[nodiscard]] Function* generateFuncDef(const frontend::AST& ast,
        Ptr<frontend::FuncDef> funcDef,
        const frontend::SemanticInfo& semanticInfo,
        const std::unordered_map<int32_t, Value*>& globalStorageBySymbolId,
        const std::unordered_map<int32_t, Function*>& functionBySymbolId,
        Function* function_nn) const;
    void generateGlobalDecl(frontend::Decl declNode, Program& program,
        const frontend::AST& ast, const frontend::SemanticInfo& semanticInfo,
        std::unordered_map<int32_t, Value*>& globalStorageBySymbolId) const;
    [[nodiscard]] Type* lowerSemanticType(const frontend::SemanticType& type,
        bool decayUnsizedArrayToPointer = true) const;
    [[nodiscard]] Value* generateGlobalArrayInitializer(
        const frontend::SemanticType& type,
        const std::vector<Ptr<frontend::Exp>>& scalarExprs,
        size_t& nextScalarIndex,
        const frontend::SemanticInfo& semanticInfo) const;
};
} // namespace yesod::koopa

#endif
