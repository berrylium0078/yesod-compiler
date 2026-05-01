#ifndef _YESOD_KOOPA_AST_TO_KOOPA_H_
#define _YESOD_KOOPA_AST_TO_KOOPA_H_

#include "frontend/ast.h"
#include "koopa/mykoopa.h"

namespace yesod::koopa_ir {

class Generator {
public:
    [[nodiscard]] ::koopa::Program* generate(const frontend::CompUnit& compUnit) const;

private:
    [[nodiscard]] ::koopa::Function* generateFuncDef(const frontend::FuncDef& funcDef) const;
    [[nodiscard]] ::koopa::BasicBlock* generateBlock(const frontend::Block& block) const;
    [[nodiscard]] ::koopa::Value* generateStmt(const frontend::StmtNode& stmtNode) const;
    [[nodiscard]] ::koopa::ReturnValue* generateReturnStmt(const frontend::ReturnStmt& returnStmt) const;
    [[nodiscard]] ::koopa::Value* generateNumber(const frontend::Number& number) const;
};

}  // namespace yesod::koopa_ir

#endif