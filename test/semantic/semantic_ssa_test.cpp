#include <string>

#include "semantic_test_support.h"

using namespace yesod::test_support::semantic;

namespace {

struct SemanticSsaTest : SemanticTestBase {
    Ref<SemanticBasicBlock> requireBlockByName(
        Ref<FuncDef> funcDef, const std::string& name)
    {
        const auto& controlFlow = requireControlFlow(m_output, funcDef);
        for (auto block : controlFlow.blocks) {
            if (block(m_output.m_info.controlFlowArena()).nameHint == name) {
                return block;
            }
        }
        fail("expected semantic basic block named '" + name + "'");
    }

    void testJoinBlockGetsParameterAndAliases()
    {
        m_output = analyzeRoot(
            "int main(){int x = 0; if (x) { x = 1; } else { x = 2; } return x;}");

        const auto funcDef = requireFuncDefByName(root(), "main");
        const auto* ssa = m_output.m_info.findSSA(funcDef);
        require(ssa != nullptr, "expected semantic SSA for function");

        const auto entryBlock = requireBlockByName(funcDef, "entry");
        const auto thenBlock = requireBlockByName(funcDef, "if_then_1");
        const auto elseBlock = requireBlockByName(funcDef, "if_else_2");
        const auto joinBlock = requireBlockByName(funcDef, "if_end_3");

        const auto& body = funcDef(m_output.m_ast).body(m_output.m_ast);
        const auto decl = std::get<Ref<VarDecl>>(extractDeclNode(body.items[0]));
        const auto returnStmt = extractReturnStmt(body.items[2]);

        const auto symbol = requireSymbol(
            m_output, decl(m_output.m_ast).varDef.front().ptr());
        const auto declAlias = m_output.m_info.findAlias(
            decl(m_output.m_ast).varDef.front()(m_output.m_ast).identifier);
        const auto* returnLVal = std::get_if<Exp::LVal>(
            &returnStmt(m_output.m_ast).exp(m_output.m_ast).kind);
        require(returnLVal != nullptr, "return expression should be an lvalue");
        const auto returnAlias = m_output.m_info.findAlias(returnLVal->identifier);

        require(declAlias.has_value(), "declaration should define an SSA alias");
        require(returnAlias.has_value(), "return should read an SSA alias");

        require(declAlias->m_symbolId == symbol.m_id,
            "declaration alias should belong to the declared symbol");

        const auto& entryInfo = ssa->m_blockInfoByBlock.at(entryBlock);
        const auto& thenInfo = ssa->m_blockInfoByBlock.at(thenBlock);
        const auto& elseInfo = ssa->m_blockInfoByBlock.at(elseBlock);
        const auto& joinInfo = ssa->m_blockInfoByBlock.at(joinBlock);

        require(joinInfo.m_params.size() == 1,
            "if-join block should receive one block parameter for x");
        require(joinInfo.m_params[0].m_symbolId == symbol.m_id,
            "join block parameter should correspond to x");
        require(returnAlias == std::optional(joinInfo.m_params[0].m_alias),
            "join block parameter should be the alias read by the return");
        require(joinInfo.m_liveIn.contains(symbol.m_id),
            "x should be live at the join block entry");
        require(entryInfo.m_dominanceFrontier.empty(),
            "entry block should not have the join block in its dominance frontier");
        require(thenInfo.m_dominanceFrontier.size() == 1
                && thenInfo.m_dominanceFrontier.front() == joinBlock,
            "then block should have the join block in its dominance frontier");
        require(elseInfo.m_dominanceFrontier.size() == 1
                && elseInfo.m_dominanceFrontier.front() == joinBlock,
            "else block should have the join block in its dominance frontier");

        const auto thenArgsIt = thenInfo.m_outgoingArgsByTarget.find(joinBlock);
        const auto elseArgsIt = elseInfo.m_outgoingArgsByTarget.find(joinBlock);
        require(thenArgsIt != thenInfo.m_outgoingArgsByTarget.end()
                && thenArgsIt->second.size() == 1,
            "then block should pass one SSA argument into the join block");
        require(elseArgsIt != elseInfo.m_outgoingArgsByTarget.end()
                && elseArgsIt->second.size() == 1,
            "else block should pass one SSA argument into the join block");
        require(thenArgsIt->second.front().m_symbolId == symbol.m_id,
            "then edge should pass an alias of x");
        require(elseArgsIt->second.front().m_symbolId == symbol.m_id,
            "else edge should pass an alias of x");
        require(thenArgsIt->second.front() != elseArgsIt->second.front(),
            "then and else edges should pass different aliases");
        require(thenArgsIt->second.front() != *declAlias,
            "then edge should not pass the entry definition alias");
        require(elseArgsIt->second.front() != *declAlias,
            "else edge should not pass the entry definition alias");
    }

    void testWhileHeaderGetsBackedgeParameters()
    {
        m_output = analyzeRoot(
            "int main(){int x = 0; int y = 0; while (x < 3) { y = y + x; x = x + 1; } return y;}");

        const auto funcDef = requireFuncDefByName(root(), "main");
        const auto* ssa = m_output.m_info.findSSA(funcDef);
        require(ssa != nullptr, "expected semantic SSA for function");

        const auto entryBlock = requireBlockByName(funcDef, "entry");
        const auto condBlock = requireBlockByName(funcDef, "while_cond_1");
        const auto bodyBlock = requireBlockByName(funcDef, "while_body_2");
        const auto endBlock = requireBlockByName(funcDef, "while_end_3");

        const auto& body = funcDef(m_output.m_ast).body(m_output.m_ast);
        const auto xDecl = std::get<Ref<VarDecl>>(extractDeclNode(body.items[0]));
        const auto yDecl = std::get<Ref<VarDecl>>(extractDeclNode(body.items[1]));
        const auto xSymbol = requireSymbol(
            m_output, xDecl(m_output.m_ast).varDef.front().ptr());
        const auto ySymbol = requireSymbol(
            m_output, yDecl(m_output.m_ast).varDef.front().ptr());

        const auto& entryInfo = ssa->m_blockInfoByBlock.at(entryBlock);
        const auto& condInfo = ssa->m_blockInfoByBlock.at(condBlock);
        const auto& bodyInfo = ssa->m_blockInfoByBlock.at(bodyBlock);
        const auto& endInfo = ssa->m_blockInfoByBlock.at(endBlock);

        require(condInfo.m_params.size() == 2,
            "while header should receive both x and y as block parameters");
        require(condInfo.m_params[0].m_symbolId == xSymbol.m_id,
            "while header first parameter should correspond to x");
        require(condInfo.m_params[1].m_symbolId == ySymbol.m_id,
            "while header second parameter should correspond to y");

        require(endInfo.m_params.empty(),
            "while end block should reuse the dominated y alias without extra parameters");

        const auto entryToCond = entryInfo.m_outgoingArgsByTarget.find(condBlock);
        require(entryToCond != entryInfo.m_outgoingArgsByTarget.end()
                && entryToCond->second.size() == 2,
            "entry block should seed both while-header parameters");
        const auto backedge = bodyInfo.m_outgoingArgsByTarget.find(condBlock);
        require(backedge != bodyInfo.m_outgoingArgsByTarget.end()
                && backedge->second.size() == 2,
            "while body should pass updated x and y aliases back to the header");
        require(backedge->second[0].m_symbolId == xSymbol.m_id,
            "backedge first argument should update x");
        require(backedge->second[1].m_symbolId == ySymbol.m_id,
            "backedge second argument should update y");
        require(backedge->second[0] != condInfo.m_params[0].m_alias,
            "backedge should pass a new x alias into the loop header");
        require(backedge->second[1] != condInfo.m_params[1].m_alias,
            "backedge should pass a new y alias into the loop header");

        const auto condToEnd = condInfo.m_outgoingArgsByTarget.find(endBlock);
        require(condToEnd == condInfo.m_outgoingArgsByTarget.end()
                || condToEnd->second.empty(),
            "while exit edge should not need explicit arguments when the end block has no parameters");
    }
};

} // namespace

int main()
{
    SemanticSsaTest test;
    test.testJoinBlockGetsParameterAndAliases();
    test.testWhileHeaderGetsBackedgeParameters();
    return 0;
}