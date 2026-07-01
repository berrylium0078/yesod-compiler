#include "backend/llvm.h"

#include "koopa/ir.h"
#include "utils.h"

#include <algorithm>
#include <functional>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yesod::backend {

namespace koopa_ir = yesod::koopa::ir;

namespace {

    constexpr std::string_view POLY_TYPE = "%struct.YesodPoly";
    constexpr std::string_view PV_TYPE = "%struct.YesodPointValues";
    constexpr int32_t INF_END = 2147483647;
    constexpr int32_t MINT_MOD = 998244353;
    constexpr int32_t MONT_U = 998244351;
    constexpr int32_t MONT_R2 = 932051910;

    int32_t montgomeryReduceConst(int64_t value)
    {
        const int32_t q = static_cast<int32_t>(value) * MONT_U;
        return static_cast<int32_t>(
            (value + static_cast<int64_t>(MINT_MOD) * q) >> 32);
    }

    int32_t intToMontgomeryConst(int32_t value)
    {
        return montgomeryReduceConst(static_cast<int64_t>(value) * MONT_R2);
    }

    // ─── Name helpers ─────────────────────────────────────────────────────

    std::string stripPrefix(const std::string& spelling)
    {
        if (!spelling.empty() && (spelling[0] == '@' || spelling[0] == '%')) {
            return spelling.substr(1);
        }
        return spelling;
    }

    const std::string& llvmValueName(const std::string& spelling)
    {
        return spelling;
    }

    // ─── Type emission ────────────────────────────────────────────────────

    std::string emitType(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        return MATCH(type) WITH(
            [](const koopa_ir::I32Type&) -> std::string { return "i32"; },
            [](const koopa_ir::MintType&) -> std::string { return "i32"; },
            [](const koopa_ir::PolyType&) -> std::string {
                return std::string(POLY_TYPE);
            },
            [&](const yesod::Ref<koopa_ir::ArrayType> arrayRef) -> std::string {
                const auto& arr = program[arrayRef];
                return "[" + std::to_string(arr.length) + " x "
                    + emitType(arr.elementType, program) + "]";
            },
            [](const yesod::Ref<koopa_ir::PointerType>&) -> std::string {
                return "ptr";
            },
            [&](const yesod::Ref<koopa_ir::FunctionType>& funcRef)
                -> std::string {
                const auto& ft = program[funcRef];
                std::string result;
                if (ft.returnType.has_value()) {
                    result = emitType(*ft.returnType, program);
                } else {
                    result = "void";
                }
                result += "(";
                for (size_t i = 0; i < ft.paramTypes.size(); ++i) {
                    if (i != 0) {
                        result += ", ";
                    }
                    result += emitType(ft.paramTypes[i], program);
                }
                result += ")";
                return result;
            });
    }

    // ─── Value operand emission ───────────────────────────────────────────

    std::string emitValueOperand(
        const koopa_ir::Value& value, const koopa_ir::Program& /*program*/)
    {
        return MATCH(value) WITH(
            [](const koopa_ir::Symbol& sym) -> std::string {
                return llvmValueName(sym.spelling);
            },
            [](const koopa_ir::IntegerLiteral& lit) -> std::string {
                return std::to_string(lit.value);
            },
            [](const koopa_ir::UndefValue&) -> std::string { return "undef"; });
    }

    // ─── Initializer emission ─────────────────────────────────────────────

    void emitInitializer(std::ostream& output,
        const koopa_ir::Initializer& initializer, const koopa_ir::Type& type,
        const koopa_ir::Program& program, bool emitLeadingType)
    {
        if (emitLeadingType) {
            output << emitType(type, program) << " ";
        }
        MATCH(initializer)
        WITH(
            [&](const koopa_ir::IntegerLiteral& lit) {
                if (std::holds_alternative<koopa_ir::MintType>(type)) {
                    output << intToMontgomeryConst(lit.value);
                } else {
                    output << lit.value;
                }
            },
            [&](const koopa_ir::UndefValue&) { output << "undef"; },
            [&](const koopa_ir::ZeroInit&) { output << "zeroinitializer"; },
            [&](const yesod::Ref<koopa_ir::AggregateInitializer>& aggRef) {
                const auto& agg = program[aggRef];
                koopa_ir::Type elementType = koopa_ir::I32Type { };
                if (const auto* arrayRef
                    = std::get_if<yesod::Ref<koopa_ir::ArrayType>>(&type)) {
                    elementType = program[*arrayRef].elementType;
                }
                output << "[";
                for (size_t i = 0; i < agg.elements.size(); ++i) {
                    if (i != 0) {
                        output << ", ";
                    }
                    emitInitializer(
                        output, agg.elements[i], elementType, program, true);
                }
                output << "]";
            });
    }

    // ─── Symbol type / pointee tracking (string-based) ────────────────────

    using PointeeMap = std::unordered_map<std::string, std::string>;
    using ValueTypeMap = std::unordered_map<std::string, std::string>;
    struct FuncSigInfo {
        std::string retType;
        std::vector<std::string> paramTypes;
    };

    using FuncSigMap = std::unordered_map<std::string, FuncSigInfo>;

    FuncSigMap buildFuncSigMap(const koopa_ir::Program& program)
    {
        FuncSigMap sigs;
        for (const auto& item : program.items) {
            std::visit(
                [&](auto itemRef) {
                    using Item
                        = std::remove_cvref_t<decltype(program[itemRef])>;
                    if constexpr (std::same_as<Item, koopa_ir::FunctionDef>) {
                        const auto& def = program[itemRef];
                        FuncSigInfo info;
                        if (def.returnType.has_value()) {
                            info.retType = emitType(*def.returnType, program);
                        } else {
                            info.retType = "void";
                        }
                        for (const auto& pref : def.params) {
                            info.paramTypes.push_back(
                                emitType(program[pref].type, program));
                        }
                        sigs[def.name.spelling] = info;
                    } else if constexpr (std::same_as<Item,
                                             koopa_ir::FunctionDecl>) {
                        const auto& decl = program[itemRef];
                        FuncSigInfo info;
                        if (decl.returnType.has_value()) {
                            info.retType = emitType(*decl.returnType, program);
                        } else {
                            info.retType = "void";
                        }
                        for (const auto& pt : decl.paramTypes) {
                            info.paramTypes.push_back(emitType(pt, program));
                        }
                        sigs[decl.name.spelling] = info;
                    }
                },
                item);
        }
        return sigs;
    }

    std::string pointeeTypeString(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        if (const auto* pref
            = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&type)) {
            return emitType(program[*pref].pointeeType, program);
        }
        return { };
    }

    std::string loadedPointerPointeeTypeString(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        if (const auto* pref
            = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&type)) {
            const auto& pointerType = program[*pref];
            return emitType(pointerType.pointeeType, program);
        }
        return { };
    }

    bool typeContainsPoly(
        const koopa_ir::Type& type, const koopa_ir::Program& program)
    {
        return MATCH(type) WITH(
            [](const koopa_ir::PolyType&) -> bool { return true; },
            [](const koopa_ir::I32Type&) -> bool { return false; },
            [](const koopa_ir::MintType&) -> bool { return false; },
            [&](const yesod::Ref<koopa_ir::ArrayType> arrayRef) -> bool {
                return typeContainsPoly(program[arrayRef].elementType, program);
            },
            [&](const yesod::Ref<koopa_ir::PointerType> ptrRef) -> bool {
                return typeContainsPoly(program[ptrRef].pointeeType, program);
            },
            [](const yesod::Ref<koopa_ir::FunctionType>&) -> bool {
                return false;
            });
    }

    // ─── Phi-edge collection ──────────────────────────────────────────────

    struct IncomingEdge {
        std::string predLabel;
        koopa_ir::Value arg;
    };

    using IncomingMap = std::unordered_map<std::string,
        std::vector<std::vector<IncomingEdge>>>;
    using BlockLabelMap = std::unordered_map<std::string, std::string>;

    std::string edgeBlockLabel(
        const std::string& predLabel, const std::string& suffix)
    {
        return "__ssa_edge_" + predLabel + "_" + suffix;
    }

    bool needsSameTargetBranchSplit(const koopa_ir::BranchTerminator& branch)
    {
        return branch.trueTarget.spelling == branch.falseTarget.spelling
            && (!branch.trueArgs.empty() || !branch.falseArgs.empty());
    }

    void collectPhiEdges(const koopa_ir::FunctionDef& function,
        const koopa_ir::Program& program, IncomingMap& incoming,
        BlockLabelMap& blockLabels)
    {
        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];
            const std::string rawLabel = stripPrefix(block.label.spelling);
            const std::string label = rawLabel.empty() ? "__empty" : rawLabel;
            blockLabels[block.label.spelling] = label;
            incoming[block.label.spelling].resize(block.params.size());
        }

        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];
            const std::string predLabel = blockLabels[block.label.spelling];

            MATCH(block.terminator)
            WITH(
                [&](const yesod::Ref<koopa_ir::JumpTerminator>& jumpRef) {
                    const auto& jump = program[jumpRef];
                    const std::string targetLabel = jump.target.spelling;
                    auto& slots = incoming[targetLabel];
                    const std::string edgeLabel = jump.args.empty()
                        ? predLabel
                        : edgeBlockLabel(predLabel, "jump");
                    for (size_t i = 0; i < jump.args.size() && i < slots.size();
                        ++i) {
                        slots[i].push_back(
                            IncomingEdge { edgeLabel, jump.args[i] });
                    }
                },
                [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef) {
                    const auto& br = program[brRef];
                    if (needsSameTargetBranchSplit(br)) {
                        const std::string targetLabel = br.trueTarget.spelling;
                        auto& slots = incoming[targetLabel];
                        for (size_t i = 0;
                            i < br.trueArgs.size() && i < slots.size(); ++i) {
                            slots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "true"),
                                br.trueArgs[i],
                            });
                        }
                        for (size_t i = 0;
                            i < br.falseArgs.size() && i < slots.size(); ++i) {
                            slots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "false"),
                                br.falseArgs[i],
                            });
                        }
                        return;
                    }
                    {
                        const std::string tlabel = br.trueTarget.spelling;
                        auto& tslots = incoming[tlabel];
                        for (size_t i = 0;
                            i < br.trueArgs.size() && i < tslots.size(); ++i) {
                            tslots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "true"),
                                br.trueArgs[i],
                            });
                        }
                    }
                    {
                        const std::string flabel = br.falseTarget.spelling;
                        auto& fslots = incoming[flabel];
                        for (size_t i = 0;
                            i < br.falseArgs.size() && i < fslots.size(); ++i) {
                            fslots[i].push_back(IncomingEdge {
                                edgeBlockLabel(predLabel, "false"),
                                br.falseArgs[i],
                            });
                        }
                    }
                },
                [](const yesod::Ref<koopa_ir::ReturnTerminator>&) { });
        }
    }

    using SymbolSet = std::set<std::string>;

    struct EdgeInfo {
        size_t targetIndex = 0;
        std::vector<koopa_ir::Value> args;
        std::string suffix;
    };

    struct OwnershipAnalysis {
        std::vector<SymbolSet> liveIn;
        std::vector<SymbolSet> liveOut;
        std::vector<std::vector<SymbolSet>> liveAfterStmt;
        std::vector<std::vector<EdgeInfo>> edges;
        std::unordered_map<std::string, size_t> blockIndexByLabel;
    };

    struct ArgumentPlan {
        std::vector<std::string> operands;
        std::vector<std::pair<std::string, std::string>> clones;
        SymbolSet movedValues;
    };

    struct EdgeEmissionPlan {
        ArgumentPlan args;
        SymbolSet releases;
        std::string label;
        std::string targetLabel;
    };

    using EdgePlanMap
        = std::map<std::pair<size_t, std::string>, EdgeEmissionPlan>;

    bool isOwnedTypeString(const std::string& type)
    {
        return type == POLY_TYPE || type == PV_TYPE;
    }

    void addOwnedValueUse(const koopa_ir::Value& value,
        const ValueTypeMap& valueTypes, SymbolSet& uses)
    {
        if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
            const auto typeIt = valueTypes.find(symbol->spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                uses.insert(symbol->spelling);
            }
        }
    }

    void addPointwiseUses(yesod::Ref<koopa_ir::PointwiseNode> nodeRef,
        const koopa_ir::Program& program, const ValueTypeMap& valueTypes,
        SymbolSet& uses)
    {
        const auto& node = program[nodeRef];
        MATCH(node.kind)
        WITH(
            [&](const koopa_ir::PointwiseLeaf& leaf) -> void {
                addOwnedValueUse(leaf.value, valueTypes, uses);
            },
            [&](const koopa_ir::PointwiseBinary& binary) -> void {
                addPointwiseUses(binary.lhs, program, valueTypes, uses);
                addPointwiseUses(binary.rhs, program, valueTypes, uses);
            });
    }

    SymbolSet symbolDefUses(const koopa_ir::SymbolDef& def,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        const ValueTypeMap& valueTypes)
    {
        SymbolSet uses;
        MATCH(def.rhs)
        WITH([](const yesod::Ref<koopa_ir::MemoryDeclaration>&) -> void { },
            [&](const yesod::Ref<koopa_ir::LoadExpr>&) -> void { },
            [&](const yesod::Ref<koopa_ir::GetPointerExpr>& gpRef) -> void {
                addOwnedValueUse(program[gpRef].index, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::GetElementPointerExpr>& geRef)
                -> void {
                addOwnedValueUse(program[geRef].index, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::BinaryExpr>& binRef) -> void {
                const auto& bin = program[binRef];
                addOwnedValueUse(bin.lhs, valueTypes, uses);
                addOwnedValueUse(bin.rhs, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::CallExpr>& callRef) -> void {
                const auto& call = program[callRef];
                const auto sigIt = sigs.find(call.callee.spelling);
                for (size_t i = 0; i < call.args.size(); ++i) {
                    const std::string argType
                        = (sigIt != sigs.end()
                              && i < sigIt->second.paramTypes.size())
                        ? sigIt->second.paramTypes[i]
                        : "i32";
                    if (isOwnedTypeString(argType)) {
                        addOwnedValueUse(call.args[i], valueTypes, uses);
                    }
                }
            },
            [&](const yesod::Ref<koopa_ir::CopyExpr>& copyRef) -> void {
                addOwnedValueUse(program[copyRef].value, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::GetAttrExpr>& getAttrRef) -> void {
                addOwnedValueUse(program[getAttrRef].value, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::SetAttrExpr>& setAttrRef) -> void {
                const auto& setAttr = program[setAttrRef];
                addOwnedValueUse(setAttr.value, valueTypes, uses);
                addOwnedValueUse(setAttr.attrValue, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::SelectExpr>& selectRef) -> void {
                const auto& select = program[selectRef];
                addOwnedValueUse(select.condition, valueTypes, uses);
                addOwnedValueUse(select.trueValue, valueTypes, uses);
                addOwnedValueUse(select.falseValue, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::NextPow2Expr>& nextPow2Ref) -> void {
                addOwnedValueUse(program[nextPow2Ref].value, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::NttExpr>& nttRef) -> void {
                const auto& ntt = program[nttRef];
                addOwnedValueUse(ntt.value, valueTypes, uses);
                addOwnedValueUse(ntt.length, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::PointwiseExpr>& pointwiseRef)
                -> void {
                const auto& pointwise = program[pointwiseRef];
                addOwnedValueUse(pointwise.length, valueTypes, uses);
                addOwnedValueUse(pointwise.activeL, valueTypes, uses);
                addOwnedValueUse(pointwise.activeR, valueTypes, uses);
                addPointwiseUses(pointwise.root, program, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::CombineExpr>& combineRef) -> void {
                for (const auto& term : program[combineRef].terms) {
                    addOwnedValueUse(term.value, valueTypes, uses);
                    addOwnedValueUse(term.start, valueTypes, uses);
                    if (term.end.has_value()) {
                        addOwnedValueUse(*term.end, valueTypes, uses);
                    }
                    addOwnedValueUse(term.shift, valueTypes, uses);
                    addOwnedValueUse(term.scale, valueTypes, uses);
                }
            },
            [&](const yesod::Ref<koopa_ir::GetCoeffExpr>& getCoeffRef) -> void {
                const auto& getCoeff = program[getCoeffRef];
                addOwnedValueUse(getCoeff.value, valueTypes, uses);
                addOwnedValueUse(getCoeff.index, valueTypes, uses);
            },
            [&](const yesod::Ref<koopa_ir::PolyConstructExpr>& constructRef)
                -> void {
                for (const auto& element : program[constructRef].elements) {
                    addOwnedValueUse(element, valueTypes, uses);
                }
            },
            [&](const yesod::Ref<koopa_ir::ConversionExpr>& convRef) -> void {
                addOwnedValueUse(program[convRef].value, valueTypes, uses);
            });
        return uses;
    }

    SymbolSet storeUses(const koopa_ir::StoreStmt& store,
        const PointeeMap& pointees, const ValueTypeMap& valueTypes)
    {
        SymbolSet uses;
        const auto ptIt = pointees.find(store.destination.spelling);
        const std::string elemTy
            = (ptIt != pointees.end()) ? ptIt->second : "i32";
        if (elemTy == POLY_TYPE) {
            if (const auto* symbol
                = std::get_if<koopa_ir::Symbol>(&store.value)) {
                addOwnedValueUse(*symbol, valueTypes, uses);
            }
        }
        return uses;
    }

    SymbolSet callStmtUses(const koopa_ir::CallExpr& call,
        const FuncSigMap& sigs, const ValueTypeMap& valueTypes)
    {
        SymbolSet uses;
        const auto sigIt = sigs.find(call.callee.spelling);
        for (size_t i = 0; i < call.args.size(); ++i) {
            const std::string argType
                = (sigIt != sigs.end() && i < sigIt->second.paramTypes.size())
                ? sigIt->second.paramTypes[i]
                : "i32";
            if (isOwnedTypeString(argType)) {
                addOwnedValueUse(call.args[i], valueTypes, uses);
            }
        }
        return uses;
    }

    SymbolSet statementUses(const koopa_ir::Statement& statement,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        const PointeeMap& pointees, const ValueTypeMap& valueTypes)
    {
        return std::visit(
            [&](auto stmtRef) -> SymbolSet {
                using StmtNode
                    = std::remove_cvref_t<decltype(program[stmtRef])>;
                if constexpr (std::same_as<StmtNode, koopa_ir::SymbolDef>) {
                    return symbolDefUses(
                        program[stmtRef], program, sigs, valueTypes);
                } else if constexpr (std::same_as<StmtNode,
                                         koopa_ir::StoreStmt>) {
                    return storeUses(program[stmtRef], pointees, valueTypes);
                } else {
                    return callStmtUses(program[stmtRef], sigs, valueTypes);
                }
            },
            statement);
    }

    std::optional<std::string> statementDef(
        const koopa_ir::Statement& statement, const koopa_ir::Program& program,
        const ValueTypeMap& valueTypes)
    {
        if (const auto* defRef
            = std::get_if<yesod::Ref<koopa_ir::SymbolDef>>(&statement)) {
            const auto& def = program[*defRef];
            const auto typeIt = valueTypes.find(def.symbol.spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                return def.symbol.spelling;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> blockParamSymbols(
        const koopa_ir::BasicBlock& block, const koopa_ir::Program& program,
        const ValueTypeMap& valueTypes)
    {
        std::vector<std::string> result;
        result.reserve(block.params.size());
        for (const auto& paramRef : block.params) {
            const auto& param = program[paramRef];
            const auto typeIt = valueTypes.find(param.symbol.spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                result.push_back(param.symbol.spelling);
            } else {
                result.push_back({ });
            }
        }
        return result;
    }

    void addOwnedEdgeArgs(const std::vector<koopa_ir::Value>& args,
        const koopa_ir::BasicBlock& targetBlock,
        const koopa_ir::Program& program, const ValueTypeMap& valueTypes,
        SymbolSet& live)
    {
        for (size_t i = 0; i < args.size() && i < targetBlock.params.size();
            ++i) {
            const auto& param = program[targetBlock.params[i]];
            const auto typeIt = valueTypes.find(param.symbol.spelling);
            if (typeIt != valueTypes.end()
                && isOwnedTypeString(typeIt->second)) {
                addOwnedValueUse(args[i], valueTypes, live);
            }
        }
    }

    OwnershipAnalysis analyzeOwnership(const koopa_ir::FunctionDef& function,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        const PointeeMap& pointees, const ValueTypeMap& valueTypes)
    {
        OwnershipAnalysis analysis;
        const size_t blockCount = function.blocks.size();
        analysis.liveIn.resize(blockCount);
        analysis.liveOut.resize(blockCount);
        analysis.liveAfterStmt.resize(blockCount);
        analysis.edges.resize(blockCount);

        for (size_t i = 0; i < blockCount; ++i) {
            const auto& block = program[function.blocks[i]];
            analysis.blockIndexByLabel[block.label.spelling] = i;
            analysis.liveAfterStmt[i].resize(block.statements.size());
        }

        for (size_t i = 0; i < blockCount; ++i) {
            const auto& block = program[function.blocks[i]];
            const std::string predRaw = stripPrefix(block.label.spelling);
            const std::string predLabel = predRaw.empty() ? "__empty" : predRaw;
            MATCH(block.terminator)
            WITH(
                [&](const yesod::Ref<koopa_ir::JumpTerminator>& jumpRef)
                    -> void {
                    const auto& jump = program[jumpRef];
                    const auto targetIt
                        = analysis.blockIndexByLabel.find(jump.target.spelling);
                    if (targetIt != analysis.blockIndexByLabel.end()) {
                        analysis.edges[i].push_back(EdgeInfo {
                            .targetIndex = targetIt->second,
                            .args = jump.args,
                            .suffix = jump.args.empty() ? "" : "jump",
                        });
                    }
                },
                [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef)
                    -> void {
                    const auto& branch = program[brRef];
                    const auto trueIt = analysis.blockIndexByLabel.find(
                        branch.trueTarget.spelling);
                    if (trueIt != analysis.blockIndexByLabel.end()) {
                        analysis.edges[i].push_back(EdgeInfo {
                            .targetIndex = trueIt->second,
                            .args = branch.trueArgs,
                            .suffix = "true",
                        });
                    }
                    const auto falseIt = analysis.blockIndexByLabel.find(
                        branch.falseTarget.spelling);
                    if (falseIt != analysis.blockIndexByLabel.end()) {
                        analysis.edges[i].push_back(EdgeInfo {
                            .targetIndex = falseIt->second,
                            .args = branch.falseArgs,
                            .suffix = "false",
                        });
                    }
                },
                [](const yesod::Ref<koopa_ir::ReturnTerminator>&) -> void { });
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t reverseIndex = 0; reverseIndex < blockCount;
                ++reverseIndex) {
                const size_t blockIndex = blockCount - reverseIndex - 1;
                const auto& block = program[function.blocks[blockIndex]];

                SymbolSet liveOut;
                for (const auto& edge : analysis.edges[blockIndex]) {
                    const auto& targetBlock
                        = program[function.blocks[edge.targetIndex]];
                    SymbolSet edgeLive = analysis.liveIn[edge.targetIndex];
                    for (const auto& paramName :
                        blockParamSymbols(targetBlock, program, valueTypes)) {
                        if (!paramName.empty()) {
                            edgeLive.erase(paramName);
                        }
                    }
                    addOwnedEdgeArgs(
                        edge.args, targetBlock, program, valueTypes, edgeLive);
                    liveOut.insert(edgeLive.begin(), edgeLive.end());
                }

                SymbolSet live = liveOut;
                MATCH(block.terminator)
                WITH(
                    [&](const yesod::Ref<koopa_ir::ReturnTerminator>& retRef)
                        -> void {
                        const auto& ret = program[retRef];
                        if (ret.value.has_value()) {
                            addOwnedValueUse(*ret.value, valueTypes, live);
                        }
                    },
                    [](const yesod::Ref<koopa_ir::JumpTerminator>&) -> void { },
                    [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef)
                        -> void {
                        addOwnedValueUse(
                            program[brRef].condition, valueTypes, live);
                    });

                for (size_t reverseStmt = 0;
                    reverseStmt < block.statements.size(); ++reverseStmt) {
                    const size_t stmtIndex
                        = block.statements.size() - reverseStmt - 1;
                    analysis.liveAfterStmt[blockIndex][stmtIndex] = live;
                    if (const auto def = statementDef(
                            block.statements[stmtIndex], program, valueTypes);
                        def.has_value()) {
                        live.erase(*def);
                    }
                    const auto uses = statementUses(block.statements[stmtIndex],
                        program, sigs, pointees, valueTypes);
                    live.insert(uses.begin(), uses.end());
                }

                if (analysis.liveOut[blockIndex] != liveOut
                    || analysis.liveIn[blockIndex] != live) {
                    analysis.liveOut[blockIndex] = std::move(liveOut);
                    analysis.liveIn[blockIndex] = std::move(live);
                    changed = true;
                }
            }
        }

        return analysis;
    }

    // ─── Top-level emit helpers ───────────────────────────────────────────

    void emitModuleHeader(std::ostream& output)
    {
        output << "; ModuleID = 'KoopaIR'\n";
        output << "source_filename = \"KoopaIR\"\n";
        output << "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:"
                  "64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n";
        output << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
        output << "%struct.YesodPoly = type { ptr, ptr, i32, i32, i32 }\n";
        output << "%struct.YesodPointValues = type { ptr, i32 }\n\n";
    }

    void emitGlobalDecl(const koopa_ir::GlobalMemoryDef& global,
        const koopa_ir::Program& program, std::ostream& output)
    {
        const std::string name = stripPrefix(global.name.spelling);
        const std::string llvmType = emitType(global.allocType, program);
        output << "@" << name << " = global " << llvmType << " ";
        emitInitializer(
            output, global.initializer, global.allocType, program, false);
        output << "\n";
    }

    void emitFuncDecl(const koopa_ir::FunctionDecl& decl,
        const koopa_ir::Program& program, std::ostream& output)
    {
        const std::string name = stripPrefix(decl.name.spelling);
        std::string retType = "void";
        if (decl.returnType.has_value()) {
            retType = emitType(*decl.returnType, program);
        }
        output << "declare " << retType << " @" << name << "(";
        for (size_t i = 0; i < decl.paramTypes.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            output << emitType(decl.paramTypes[i], program);
        }
        output << ")\n";
    }

    // ─── Function body emitter ────────────────────────────────────────────

    void emitFunctionDef(const koopa_ir::FunctionDef& function,
        const koopa_ir::Program& program, const FuncSigMap& sigs,
        std::ostream& output)
    {
        const std::string name = stripPrefix(function.name.spelling);

        // ── Build per-function type maps ───────────────────────────────
        PointeeMap pointees;
        PointeeMap loadedPointees;
        ValueTypeMap valueTypes;
        ValueTypeMap logicalTypes;
        ValueTypeMap logicalPointees;
        std::vector<std::pair<std::string, koopa_ir::Type>>
            localPolyAllocations;
        std::vector<std::pair<std::string, koopa_ir::Type>>
            globalPolyAllocations;
        int32_t helperTempId = 0;

        auto nextHelperName = [&](const std::string& stem) -> std::string {
            return "%" + stem + "_" + std::to_string(helperTempId++);
        };

        auto emitMontgomeryReduce
            = [&](const std::string& value) -> std::string {
            const std::string truncated = nextHelperName("mont_trunc");
            output << "  " << truncated << " = trunc i64 " << value
                   << " to i32\n";
            const std::string q = nextHelperName("mont_q");
            output << "  " << q << " = mul i32 " << truncated << ", " << MONT_U
                   << "\n";
            const std::string q64 = nextHelperName("mont_q64");
            output << "  " << q64 << " = sext i32 " << q << " to i64\n";
            const std::string mq = nextHelperName("mont_mq");
            output << "  " << mq << " = mul i64 " << MINT_MOD << ", " << q64
                   << "\n";
            const std::string sum = nextHelperName("mont_sum");
            output << "  " << sum << " = add i64 " << value << ", " << mq
                   << "\n";
            const std::string shifted = nextHelperName("mont_shift");
            output << "  " << shifted << " = ashr i64 " << sum << ", 32\n";
            const std::string result = nextHelperName("mont_reduce");
            output << "  " << result << " = trunc i64 " << shifted
                   << " to i32\n";
            return result;
        };

        auto emitIntToMint = [&](const std::string& value) -> std::string {
            const std::string wide = nextHelperName("mint_i64");
            output << "  " << wide << " = sext i32 " << value << " to i64\n";
            const std::string scaled = nextHelperName("mint_r2");
            output << "  " << scaled << " = mul i64 " << wide << ", " << MONT_R2
                   << "\n";
            return emitMontgomeryReduce(scaled);
        };

        auto emitMintToInt = [&](const std::string& value) -> std::string {
            const std::string wide = nextHelperName("mint_to_i64");
            output << "  " << wide << " = sext i32 " << value << " to i64\n";
            const std::string reduced = emitMontgomeryReduce(wide);
            const std::string isNegative = nextHelperName("mint_neg");
            output << "  " << isNegative << " = icmp slt i32 " << reduced
                   << ", 0\n";
            const std::string adjusted = nextHelperName("mint_adjusted");
            output << "  " << adjusted << " = add i32 " << reduced << ", "
                   << MINT_MOD << "\n";
            const std::string result = nextHelperName("mint_int");
            output << "  " << result << " = select i1 " << isNegative
                   << ", i32 " << adjusted << ", i32 " << reduced << "\n";
            return result;
        };

        auto emitMintAddSub
            = [&](const std::string& lhs, const std::string& rhs,
                  bool isSub) -> std::string {
            const std::string raw
                = nextHelperName(isSub ? "mint_sub" : "mint_add");
            output << "  " << raw << " = " << (isSub ? "sub" : "add") << " i32 "
                   << lhs << ", " << rhs << "\n";
            const std::string isNegative = nextHelperName("mint_fold_neg");
            output << "  " << isNegative << " = icmp slt i32 " << raw
                   << ", 0\n";
            const std::string plus2m = nextHelperName("mint_plus_2m");
            output << "  " << plus2m << " = add i32 " << raw << ", "
                   << (2 * MINT_MOD) << "\n";
            const std::string nonNegative = nextHelperName("mint_nonneg");
            output << "  " << nonNegative << " = select i1 " << isNegative
                   << ", i32 " << plus2m << ", i32 " << raw << "\n";
            const std::string atLeastMod = nextHelperName("mint_ge_mod");
            output << "  " << atLeastMod << " = icmp sge i32 " << nonNegative
                   << ", " << MINT_MOD << "\n";
            const std::string minusMod = nextHelperName("mint_minus_mod");
            output << "  " << minusMod << " = sub i32 " << nonNegative << ", "
                   << MINT_MOD << "\n";
            const std::string result = nextHelperName("mint_folded");
            output << "  " << result << " = select i1 " << atLeastMod
                   << ", i32 " << minusMod << ", i32 " << nonNegative << "\n";
            return result;
        };

        auto emitMintMul = [&](const std::string& lhs,
                               const std::string& rhs) -> std::string {
            const std::string lhs64 = nextHelperName("mint_lhs64");
            output << "  " << lhs64 << " = sext i32 " << lhs << " to i64\n";
            const std::string rhs64 = nextHelperName("mint_rhs64");
            output << "  " << rhs64 << " = sext i32 " << rhs << " to i64\n";
            const std::string product = nextHelperName("mint_product");
            output << "  " << product << " = mul i64 " << lhs64 << ", " << rhs64
                   << "\n";
            return emitMontgomeryReduce(product);
        };

        auto emitMintPowConst
            = [&](const std::string& base, int32_t exponent) -> std::string {
            std::string result = std::to_string(301989884);
            std::string power = base;
            int32_t exp = exponent;
            while (exp > 0) {
                if ((exp & 1) != 0) {
                    result = emitMintMul(result, power);
                }
                exp >>= 1;
                if (exp > 0) {
                    power = emitMintMul(power, power);
                }
            }
            return result;
        };

        auto valueTypeOf = [&](const koopa_ir::Value& value) -> std::string {
            if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
                const auto typeIt = valueTypes.find(symbol->spelling);
                return typeIt == valueTypes.end() ? "i32" : typeIt->second;
            }
            return "i32";
        };

        auto logicalTypeOfIrType
            = [&](const koopa_ir::Type& type) -> std::string {
            if (std::holds_alternative<koopa_ir::MintType>(type)) {
                return "mint";
            }
            if (std::holds_alternative<koopa_ir::PolyType>(type)) {
                return "poly";
            }
            if (std::holds_alternative<koopa_ir::I32Type>(type)) {
                return "int";
            }
            if (std::holds_alternative<yesod::Ref<koopa_ir::PointerType>>(
                    type)) {
                return "ptr";
            }
            return emitType(type, program);
        };

        auto logicalTypeOf = [&](const koopa_ir::Value& value) -> std::string {
            if (const auto* symbol = std::get_if<koopa_ir::Symbol>(&value)) {
                const auto typeIt = logicalTypes.find(symbol->spelling);
                return typeIt == logicalTypes.end() ? "int" : typeIt->second;
            }
            return "int";
        };

        auto emitMintOperand
            = [&](const koopa_ir::Value& value) -> std::string {
            if (logicalTypeOf(value) == "mint") {
                return emitValueOperand(value, program);
            }
            if (const auto* literal
                = std::get_if<koopa_ir::IntegerLiteral>(&value)) {
                return std::to_string(intToMontgomeryConst(literal->value));
            }
            return emitIntToMint(emitValueOperand(value, program));
        };

        auto recordPointer
            = [&](const std::string& sym, const koopa_ir::Type& type) {
                  const std::string pt = pointeeTypeString(type, program);
                  if (!pt.empty()) {
                      valueTypes[sym] = "ptr";
                      pointees[sym] = pt;
                      logicalTypes[sym] = "ptr";
                  }
              };

        // Pre-populate global symbols from the program's GlobalMemoryDef items.
        for (const auto& item : program.items) {
            std::visit(
                [&](auto itemRef) {
                    using Item
                        = std::remove_cvref_t<decltype(program[itemRef])>;
                    if constexpr (std::same_as<Item,
                                      koopa_ir::GlobalMemoryDef>) {
                        const auto& g = program[itemRef];
                        valueTypes[g.name.spelling] = "ptr";
                        pointees[g.name.spelling]
                            = emitType(g.allocType, program);
                        logicalTypes[g.name.spelling] = "ptr";
                        logicalPointees[g.name.spelling]
                            = logicalTypeOfIrType(g.allocType);
                        if (typeContainsPoly(g.allocType, program)) {
                            globalPolyAllocations.emplace_back(
                                g.name.spelling, g.allocType);
                        }
                    }
                },
                item);
        }

        // Function parameter types
        for (const auto& pref : function.params) {
            const auto& param = program[pref];
            const std::string ps = emitType(param.type, program);
            valueTypes[param.symbol.spelling] = ps;
            logicalTypes[param.symbol.spelling]
                = logicalTypeOfIrType(param.type);
            if (const auto* prefType
                = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&param.type)) {
                pointees[param.symbol.spelling]
                    = emitType(program[*prefType].pointeeType, program);
                logicalPointees[param.symbol.spelling]
                    = logicalTypeOfIrType(program[*prefType].pointeeType);
            }
        }

        // Walk all blocks to build type maps
        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];

            for (const auto& ppref : block.params) {
                const auto& bp = program[ppref];
                const std::string ps = emitType(bp.type, program);
                valueTypes[bp.symbol.spelling] = ps;
                logicalTypes[bp.symbol.spelling] = logicalTypeOfIrType(bp.type);
                recordPointer(bp.symbol.spelling, bp.type);
            }

            for (const auto& stmt : block.statements) {
                std::visit(
                    [&](auto stmtRef) {
                        using StmtNode
                            = std::remove_cvref_t<decltype(program[stmtRef])>;
                        if constexpr (std::same_as<StmtNode,
                                          koopa_ir::SymbolDef>) {
                            const auto& sd = program[stmtRef];
                            const std::string& sname = sd.symbol.spelling;

                            MATCH(sd.rhs)
                            WITH(
                                [&](const yesod::Ref<
                                    koopa_ir::MemoryDeclaration>& memRef) {
                                    const auto& mem = program[memRef];
                                    valueTypes[sname] = "ptr";
                                    logicalTypes[sname] = "ptr";
                                    pointees[sname]
                                        = emitType(mem.allocType, program);
                                    logicalPointees[sname]
                                        = logicalTypeOfIrType(mem.allocType);
                                    const std::string loadedPointee
                                        = loadedPointerPointeeTypeString(
                                            mem.allocType, program);
                                    if (!loadedPointee.empty()) {
                                        loadedPointees[sname] = loadedPointee;
                                    }
                                    if (typeContainsPoly(
                                            mem.allocType, program)) {
                                        localPolyAllocations.emplace_back(
                                            sname, mem.allocType);
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::LoadExpr>&
                                        loadRef) {
                                    const auto& ld = program[loadRef];
                                    const auto it
                                        = pointees.find(ld.source.spelling);
                                    valueTypes[sname] = (it != pointees.end())
                                        ? it->second
                                        : "i32";
                                    const auto logicalIt = logicalPointees.find(
                                        ld.source.spelling);
                                    logicalTypes[sname]
                                        = logicalIt == logicalPointees.end()
                                        ? "int"
                                        : logicalIt->second;
                                    if (valueTypes[sname] == "ptr") {
                                        const auto loadedPointeeIt
                                            = loadedPointees.find(
                                                ld.source.spelling);
                                        if (loadedPointeeIt
                                            != loadedPointees.end()) {
                                            pointees[sname]
                                                = loadedPointeeIt->second;
                                        }
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::GetPointerExpr>&
                                        getptrRef) {
                                    const auto& gp = program[getptrRef];
                                    const auto vtIt
                                        = valueTypes.find(gp.source.spelling);
                                    valueTypes[sname]
                                        = (vtIt != valueTypes.end())
                                        ? vtIt->second
                                        : "ptr";
                                    logicalTypes[sname] = "ptr";
                                    const auto ptIt
                                        = pointees.find(gp.source.spelling);
                                    if (ptIt != pointees.end()) {
                                        pointees[sname] = ptIt->second;
                                    }
                                    const auto logicalPtIt
                                        = logicalPointees.find(
                                            gp.source.spelling);
                                    if (logicalPtIt != logicalPointees.end()) {
                                        logicalPointees[sname]
                                            = logicalPtIt->second;
                                    }
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::GetElementPointerExpr>&
                                        gelemRef) {
                                    const auto& ge = program[gelemRef];
                                    valueTypes[sname] = "ptr";
                                    logicalTypes[sname] = "ptr";
                                    const auto ptIt
                                        = pointees.find(ge.source.spelling);
                                    if (ptIt != pointees.end()) {
                                        const std::string& arrType
                                            = ptIt->second;
                                        auto xpos = arrType.find(" x ");
                                        if (xpos != std::string::npos) {
                                            std::string elemTy
                                                = arrType.substr(xpos + 3);
                                            if (!elemTy.empty()
                                                && elemTy.back() == ']') {
                                                elemTy.pop_back();
                                            }
                                            pointees[sname] = elemTy;
                                        } else {
                                            pointees[sname] = ptIt->second;
                                        }
                                    }
                                    const auto logicalPtIt
                                        = logicalPointees.find(
                                            ge.source.spelling);
                                    if (logicalPtIt != logicalPointees.end()) {
                                        const std::string& arrType
                                            = logicalPtIt->second;
                                        auto xpos = arrType.find(" x ");
                                        if (xpos != std::string::npos) {
                                            std::string elemTy
                                                = arrType.substr(xpos + 3);
                                            if (!elemTy.empty()
                                                && elemTy.back() == ']') {
                                                elemTy.pop_back();
                                            }
                                            logicalPointees[sname] = elemTy;
                                        } else {
                                            logicalPointees[sname]
                                                = logicalPtIt->second;
                                        }
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::BinaryExpr>&
                                        binRef) {
                                    const auto& bin = program[binRef];
                                    valueTypes[sname] = "i32";
                                    switch (bin.op) {
                                    case koopa_ir::BinaryOp::eq:
                                    case koopa_ir::BinaryOp::ne:
                                    case koopa_ir::BinaryOp::gt:
                                    case koopa_ir::BinaryOp::lt:
                                    case koopa_ir::BinaryOp::ge:
                                    case koopa_ir::BinaryOp::le:
                                    case koopa_ir::BinaryOp::mod:
                                    case koopa_ir::BinaryOp::bitAnd:
                                    case koopa_ir::BinaryOp::bitOr:
                                    case koopa_ir::BinaryOp::bitXor:
                                    case koopa_ir::BinaryOp::shl:
                                    case koopa_ir::BinaryOp::shr:
                                    case koopa_ir::BinaryOp::sar:
                                        logicalTypes[sname] = "int";
                                        break;
                                    default:
                                        logicalTypes[sname]
                                            = logicalTypeOf(bin.lhs) == "mint"
                                                || logicalTypeOf(bin.rhs)
                                                    == "mint"
                                            ? "mint"
                                            : "int";
                                        break;
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::CallExpr>&
                                        callRef) {
                                    const auto& call = program[callRef];
                                    const auto sigIt
                                        = sigs.find(call.callee.spelling);
                                    if (sigIt != sigs.end()
                                        && sigIt->second.retType != "void") {
                                        valueTypes[sname]
                                            = sigIt->second.retType;
                                        logicalTypes[sname] = "int";
                                    } else {
                                        valueTypes[sname] = "i32";
                                        logicalTypes[sname] = "int";
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::CopyExpr>&
                                        copyRef) {
                                    const auto& copy = program[copyRef];
                                    valueTypes[sname] = valueTypeOf(copy.value);
                                    logicalTypes[sname]
                                        = logicalTypeOf(copy.value);
                                },
                                [&](const yesod::Ref<koopa_ir::GetAttrExpr>&
                                        getAttrRef) {
                                    const auto& getAttr = program[getAttrRef];
                                    valueTypes[sname]
                                        = (getAttr.attr
                                                  == koopa_ir::PolyAttr::base
                                              || getAttr.attr
                                                  == koopa_ir::PolyAttr::addr)
                                        ? "ptr"
                                        : "i32";
                                    logicalTypes[sname]
                                        = valueTypes[sname] == "ptr" ? "ptr"
                                                                     : "int";
                                },
                                [&](const yesod::Ref<koopa_ir::SetAttrExpr>&) {
                                    valueTypes[sname] = std::string(POLY_TYPE);
                                    logicalTypes[sname] = "poly";
                                },
                                [&](const yesod::Ref<koopa_ir::SelectExpr>&
                                        selectRef) {
                                    const auto& select = program[selectRef];
                                    valueTypes[sname]
                                        = valueTypeOf(select.trueValue);
                                    logicalTypes[sname]
                                        = logicalTypeOf(select.trueValue);
                                },
                                [&](const yesod::Ref<koopa_ir::NextPow2Expr>&) {
                                    valueTypes[sname] = "i32";
                                    logicalTypes[sname] = "int";
                                },
                                [&](const yesod::Ref<koopa_ir::NttExpr>&) {
                                    valueTypes[sname] = std::string(PV_TYPE);
                                    logicalTypes[sname] = "pv";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::PointwiseExpr>&) {
                                    valueTypes[sname] = std::string(POLY_TYPE);
                                    logicalTypes[sname] = "poly";
                                },
                                [&](const yesod::Ref<koopa_ir::CombineExpr>&) {
                                    valueTypes[sname] = std::string(POLY_TYPE);
                                    logicalTypes[sname] = "poly";
                                },
                                [&](const yesod::Ref<koopa_ir::GetCoeffExpr>&) {
                                    valueTypes[sname] = "i32";
                                    logicalTypes[sname] = "mint";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::PolyConstructExpr>&) {
                                    valueTypes[sname] = std::string(POLY_TYPE);
                                    logicalTypes[sname] = "poly";
                                },
                                [&](const yesod::Ref<koopa_ir::ConversionExpr>&
                                        convRef) {
                                    valueTypes[sname] = "i32";
                                    logicalTypes[sname] = program[convRef].op
                                            == koopa_ir::ConversionOp::int2mint
                                        ? "mint"
                                        : "int";
                                },
                                [&](const auto&) {
                                    throw std::runtime_error(
                                        "LLVM backend does not support native "
                                        "poly/pv pseudo-instructions");
                                });
                        }
                    },
                    stmt);
            }
        }

        // ── Collect phi edges ──────────────────────────────────────────
        IncomingMap incoming;
        BlockLabelMap blockLabels;
        collectPhiEdges(function, program, incoming, blockLabels);
        const OwnershipAnalysis ownership
            = analyzeOwnership(function, program, sigs, pointees, valueTypes);
        // ── Function signature ─────────────────────────────────────────
        std::string retType = "void";
        if (function.returnType.has_value()) {
            retType = emitType(*function.returnType, program);
        }

        output << "define " << retType << " @" << name << "(";
        for (size_t i = 0; i < function.params.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            const auto& param = program[function.params[i]];
            const std::string paramType = emitType(param.type, program);
            output << paramType << " ";
            if (paramType == POLY_TYPE) {
                output << llvmValueName(param.symbol.spelling) << "_param";
            } else {
                output << llvmValueName(param.symbol.spelling);
            }
        }
        output << ") {\n";

        struct PolyFields {
            std::string coeffs;
            std::string addr;
            std::string n;
            std::string l;
            std::string r;
        };

        struct PvFields {
            std::string values;
            std::string len;
        };

        auto emitPolyFromFields
            = [&](const std::string& result, const PolyFields& fields) -> void {
            const std::string s0 = nextHelperName("poly_make");
            const std::string s1 = nextHelperName("poly_make");
            const std::string s2 = nextHelperName("poly_make");
            const std::string s3 = nextHelperName("poly_make");
            output << "  " << s0 << " = insertvalue " << POLY_TYPE
                   << " undef, ptr " << fields.coeffs << ", 0\n";
            output << "  " << s1 << " = insertvalue " << POLY_TYPE << " " << s0
                   << ", ptr " << fields.addr << ", 1\n";
            output << "  " << s2 << " = insertvalue " << POLY_TYPE << " " << s1
                   << ", i32 " << fields.n << ", 2\n";
            output << "  " << s3 << " = insertvalue " << POLY_TYPE << " " << s2
                   << ", i32 " << fields.l << ", 3\n";
            output << "  " << result << " = insertvalue " << POLY_TYPE << " "
                   << s3 << ", i32 " << fields.r << ", 4\n";
        };

        auto emitPvFromFields
            = [&](const std::string& result, const PvFields& fields) -> void {
            const std::string s0 = nextHelperName("pv_make");
            output << "  " << s0 << " = insertvalue " << PV_TYPE
                   << " undef, ptr " << fields.values << ", 0\n";
            output << "  " << result << " = insertvalue " << PV_TYPE << " "
                   << s0 << ", i32 " << fields.len << ", 1\n";
        };

        auto emitPolyFields = [&](const std::string& value) -> PolyFields {
            PolyFields fields {
                .coeffs = nextHelperName("poly_coeffs"),
                .addr = nextHelperName("poly_addr"),
                .n = nextHelperName("poly_n"),
                .l = nextHelperName("poly_l"),
                .r = nextHelperName("poly_r"),
            };
            output << "  " << fields.coeffs << " = extractvalue " << POLY_TYPE
                   << " " << value << ", 0\n";
            output << "  " << fields.addr << " = extractvalue " << POLY_TYPE
                   << " " << value << ", 1\n";
            output << "  " << fields.n << " = extractvalue " << POLY_TYPE << " "
                   << value << ", 2\n";
            output << "  " << fields.l << " = extractvalue " << POLY_TYPE << " "
                   << value << ", 3\n";
            output << "  " << fields.r << " = extractvalue " << POLY_TYPE << " "
                   << value << ", 4\n";
            return fields;
        };

        auto emitPvFields = [&](const std::string& value) -> PvFields {
            PvFields fields {
                .values = nextHelperName("pv_values"),
                .len = nextHelperName("pv_len"),
            };
            output << "  " << fields.values << " = extractvalue " << PV_TYPE
                   << " " << value << ", 0\n";
            output << "  " << fields.len << " = extractvalue " << PV_TYPE << " "
                   << value << ", 1\n";
            return fields;
        };

        auto emitPolyCoeffPtrForAddr
            = [&](const std::string& addr, const std::string& l,
                  const std::string& isEmpty) -> std::string {
            const std::string negL = nextHelperName("poly_neg_l");
            output << "  " << negL << " = sub i32 0, " << l << "\n";
            const std::string raw = nextHelperName("poly_coeffs_from_addr");
            output << "  " << raw << " = getelementptr i32, ptr " << addr
                   << ", i32 " << negL << "\n";
            const std::string coeffs = nextHelperName("poly_coeffs_select");
            output << "  " << coeffs << " = select i1 " << isEmpty
                   << ", ptr null, ptr " << raw << "\n";
            return coeffs;
        };

        auto emitPolyCloneTo
            = [&](const std::string& result, const std::string& value) -> void {
            const PolyFields input = emitPolyFields(value);
            const std::string clonedAddr = nextHelperName("poly_clone_addr");
            output << "  " << clonedAddr
                   << " = call ptr @__yesod_poly_clone_data(ptr " << input.addr
                   << ", i32 " << input.n << ")\n";
            const std::string inputCoeffsInt = nextHelperName("poly_coeff_i");
            const std::string inputAddrInt = nextHelperName("poly_addr_i");
            const std::string byteOffset = nextHelperName("poly_byte_offset");
            const std::string elemOffset = nextHelperName("poly_elem_offset");
            const std::string clonedCoeffs
                = nextHelperName("poly_clone_coeffs");
            output << "  " << inputCoeffsInt << " = ptrtoint ptr "
                   << input.coeffs << " to i64\n";
            output << "  " << inputAddrInt << " = ptrtoint ptr " << input.addr
                   << " to i64\n";
            output << "  " << byteOffset << " = sub i64 " << inputCoeffsInt
                   << ", " << inputAddrInt << "\n";
            output << "  " << elemOffset << " = sdiv i64 " << byteOffset
                   << ", 4\n";
            output << "  " << clonedCoeffs << " = getelementptr i32, ptr "
                   << clonedAddr << ", i64 " << elemOffset << "\n";
            emitPolyFromFields(result,
                PolyFields { .coeffs = clonedCoeffs,
                    .addr = clonedAddr,
                    .n = input.n,
                    .l = input.l,
                    .r = input.r });
        };

        auto emitPolyCloneValue = [&](const std::string& value) -> std::string {
            const std::string result = nextHelperName("poly_clone");
            emitPolyCloneTo(result, value);
            return result;
        };

        auto emitPolyZeroTo = [&](const std::string& result) -> void {
            const std::string s0 = nextHelperName("poly_zero");
            const std::string s1 = nextHelperName("poly_zero");
            const std::string s2 = nextHelperName("poly_zero");
            const std::string s3 = nextHelperName("poly_zero");
            output << "  " << s0 << " = insertvalue " << POLY_TYPE
                   << " undef, ptr null, 0\n";
            output << "  " << s1 << " = insertvalue " << POLY_TYPE << " " << s0
                   << ", ptr null, 1\n";
            output << "  " << s2 << " = insertvalue " << POLY_TYPE << " " << s1
                   << ", i32 0, 2\n";
            output << "  " << s3 << " = insertvalue " << POLY_TYPE << " " << s2
                   << ", i32 0, 3\n";
            output << "  " << result << " = insertvalue " << POLY_TYPE << " "
                   << s3 << ", i32 0, 4\n";
        };

        auto emitOwnedValueDropByType
            = [&](const std::string& type, const std::string& value) -> void {
            if (type == POLY_TYPE) {
                const PolyFields fields = emitPolyFields(value);
                output << "  call void @__yesod_rt_free_ints(ptr "
                       << fields.addr << ")\n";
            } else if (type == PV_TYPE) {
                const PvFields fields = emitPvFields(value);
                output << "  call void @__yesod_rt_free_ints(ptr "
                       << fields.values << ")\n";
            }
        };

        auto emitOwnedValueDrop = [&](const std::string& symbolName) -> void {
            const auto typeIt = valueTypes.find(symbolName);
            if (typeIt == valueTypes.end()
                || !isOwnedTypeString(typeIt->second)) {
                return;
            }
            emitOwnedValueDropByType(typeIt->second, llvmValueName(symbolName));
        };

        auto emitOwnedValueDrops = [&](const SymbolSet& values) -> void {
            for (const auto& value : values) {
                emitOwnedValueDrop(value);
            }
        };

        std::function<void(const koopa_ir::Type&, const std::string&)>
            emitStorageDrop
            = [&](const koopa_ir::Type& type, const std::string& ptr) -> void {
            MATCH(type)
            WITH(
                [&](const koopa_ir::PolyType&) -> void {
                    const std::string value = nextHelperName("poly_storage");
                    output << "  " << value << " = load " << POLY_TYPE
                           << ", ptr " << ptr << "\n";
                    emitOwnedValueDropByType(std::string(POLY_TYPE), value);
                },
                [](const koopa_ir::I32Type&) -> void { },
                [](const koopa_ir::MintType&) -> void { },
                [&](const yesod::Ref<koopa_ir::ArrayType> arrayRef) -> void {
                    const auto& arrayType = program[arrayRef];
                    const std::string llvmArrayType = emitType(type, program);
                    for (int32_t i = 0; i < arrayType.length; ++i) {
                        const std::string elemPtr
                            = nextHelperName("poly_elem_drop");
                        output << "  " << elemPtr << " = getelementptr "
                               << llvmArrayType << ", ptr " << ptr
                               << ", i32 0, i32 " << i << "\n";
                        emitStorageDrop(arrayType.elementType, elemPtr);
                    }
                },
                [](const yesod::Ref<koopa_ir::PointerType>&) -> void { },
                [](const yesod::Ref<koopa_ir::FunctionType>&) -> void { });
        };

        auto emitFunctionStorageCleanup = [&]() -> void {
            for (const auto& allocation : localPolyAllocations) {
                emitStorageDrop(
                    allocation.second, llvmValueName(allocation.first));
            }
            if (name == "main") {
                for (const auto& allocation : globalPolyAllocations) {
                    emitStorageDrop(
                        allocation.second, llvmValueName(allocation.first));
                }
            }
        };

        auto edgeReleaseSet = [&](const SymbolSet& beforeTerm,
                                  const SymbolSet& edgeLive) -> SymbolSet {
            SymbolSet result;
            for (const auto& value : beforeTerm) {
                if (!edgeLive.contains(value)) {
                    result.insert(value);
                }
            }
            return result;
        };

        auto planOwnedArguments
            = [&](const std::vector<koopa_ir::Value>& values,
                  const std::vector<std::string>& types,
                  const SymbolSet& liveAfter,
                  const std::string& cloneStem) -> ArgumentPlan {
            ArgumentPlan plan;
            plan.operands.reserve(values.size());
            std::map<std::string, size_t> remainingUses;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i >= types.size() || !isOwnedTypeString(types[i])) {
                    continue;
                }
                if (const auto* symbol
                    = std::get_if<koopa_ir::Symbol>(&values[i])) {
                    remainingUses[symbol->spelling] += 1;
                }
            }
            for (size_t i = 0; i < values.size(); ++i) {
                const std::string operand
                    = emitValueOperand(values[i], program);
                if (i >= types.size() || !isOwnedTypeString(types[i])) {
                    plan.operands.push_back(operand);
                    continue;
                }
                const auto* symbol = std::get_if<koopa_ir::Symbol>(&values[i]);
                if (symbol == nullptr) {
                    plan.operands.push_back(operand);
                    continue;
                }
                size_t& remaining = remainingUses[symbol->spelling];
                const bool mustKeepOriginal
                    = liveAfter.contains(symbol->spelling);
                const bool needsClone = mustKeepOriginal || remaining > 1;
                if (needsClone) {
                    const std::string cloneName = nextHelperName(cloneStem);
                    plan.clones.emplace_back(cloneName, operand);
                    plan.operands.push_back(cloneName);
                } else {
                    plan.movedValues.insert(symbol->spelling);
                    plan.operands.push_back(operand);
                }
                remaining -= 1;
            }
            return plan;
        };

        auto emitArgumentPlanClones = [&](const ArgumentPlan& plan) -> void {
            for (const auto& clone : plan.clones) {
                emitPolyCloneTo(clone.first, clone.second);
            }
        };

        EdgePlanMap edgePlans;
        for (size_t predIndex = 0; predIndex < function.blocks.size();
            ++predIndex) {
            const auto& predBlock = program[function.blocks[predIndex]];
            const std::string predLabel = blockLabels[predBlock.label.spelling];
            for (const auto& edge : ownership.edges[predIndex]) {
                const auto& targetBlock
                    = program[function.blocks[edge.targetIndex]];
                std::vector<std::string> targetParamTypes;
                targetParamTypes.reserve(edge.args.size());
                for (size_t i = 0; i < edge.args.size(); ++i) {
                    if (i < targetBlock.params.size()) {
                        targetParamTypes.push_back(emitType(
                            program[targetBlock.params[i]].type, program));
                    } else {
                        targetParamTypes.push_back("i32");
                    }
                }

                SymbolSet liveAfterEdge = ownership.liveIn[edge.targetIndex];
                for (const auto& paramName :
                    blockParamSymbols(targetBlock, program, valueTypes)) {
                    if (!paramName.empty()) {
                        liveAfterEdge.erase(paramName);
                    }
                }

                ArgumentPlan argPlan = planOwnedArguments(
                    edge.args, targetParamTypes, liveAfterEdge, "edge_arg");
                SymbolSet edgeLive = liveAfterEdge;
                for (const auto& moved : argPlan.movedValues) {
                    edgeLive.insert(moved);
                }
                for (const auto& clone : argPlan.clones) {
                    edgeLive.insert(clone.first);
                }
                SymbolSet releases
                    = edgeReleaseSet(ownership.liveOut[predIndex], edgeLive);
                for (const auto& moved : argPlan.movedValues) {
                    releases.erase(moved);
                }

                const std::string edgeLabel = edge.suffix.empty()
                    ? predLabel
                    : edgeBlockLabel(predLabel, edge.suffix);
                edgePlans[{ predIndex, edge.suffix }] = EdgeEmissionPlan {
                    .args = std::move(argPlan),
                    .releases = std::move(releases),
                    .label = edgeLabel,
                    .targetLabel = blockLabels[targetBlock.label.spelling],
                };
            }
        }

        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];
            auto incomingIt = incoming.find(block.label.spelling);
            if (incomingIt == incoming.end()) {
                continue;
            }
            for (size_t paramIndex = 0; paramIndex < block.params.size();
                ++paramIndex) {
                if (paramIndex >= incomingIt->second.size()) {
                    continue;
                }
                const auto& param = program[block.params[paramIndex]];
                if (emitType(param.type, program) != POLY_TYPE) {
                    continue;
                }
                for (auto& edge : incomingIt->second[paramIndex]) {
                    for (const auto& planItem : edgePlans) {
                        const auto& plan = planItem.second;
                        if (plan.label == edge.predLabel
                            && paramIndex < plan.args.operands.size()) {
                            edge.arg = koopa_ir::Symbol {
                                .sourcePos = { },
                                .spelling = plan.args.operands[paramIndex],
                            };
                            break;
                        }
                    }
                }
            }
        }

        auto nextLabelName = [&](const std::string& stem) -> std::string {
            return stripPrefix(nextHelperName(stem));
        };

        std::function<std::string(
            yesod::Ref<koopa_ir::PointwiseNode>, const std::string&)>
            emitPointwiseScalar
            = [&](yesod::Ref<koopa_ir::PointwiseNode> nodeRef,
                  const std::string& index) -> std::string {
            const auto& node = program[nodeRef];
            return MATCH(node.kind) WITH(
                [&](const koopa_ir::PointwiseLeaf& leaf) -> std::string {
                    if (leaf.kind == koopa_ir::PointwiseLeafKind::mint) {
                        return emitMintOperand(leaf.value);
                    }
                    const std::string pv
                        = emitValueOperand(leaf.value, program);
                    const std::string values = nextHelperName("pv_values");
                    output << "  " << values << " = extractvalue " << PV_TYPE
                           << " " << pv << ", 0\n";
                    const std::string elementPtr = nextHelperName("pv_elem");
                    output << "  " << elementPtr << " = getelementptr i32, ptr "
                           << values << ", i32 " << index << "\n";
                    const std::string value = nextHelperName("pv_value");
                    output << "  " << value << " = load i32, ptr " << elementPtr
                           << "\n";
                    return value;
                },
                [&](const koopa_ir::PointwiseBinary& binary) -> std::string {
                    const std::string lhs
                        = emitPointwiseScalar(binary.lhs, index);
                    const std::string rhs
                        = emitPointwiseScalar(binary.rhs, index);
                    if (binary.op == koopa_ir::PvBinaryOp::times) {
                        return emitMintMul(lhs, rhs);
                    }
                    if (binary.op == koopa_ir::PvBinaryOp::sub) {
                        return emitMintAddSub(lhs, rhs, true);
                    }
                    if (binary.op == koopa_ir::PvBinaryOp::mul) {
                        return emitMintMul(lhs, rhs);
                    }
                    return emitMintAddSub(lhs, rhs, false);
                });
        };

        // ── Emit blocks ────────────────────────────────────────────────
        for (size_t blockIndex = 0; blockIndex < function.blocks.size();
            ++blockIndex) {
            const auto& blockRef = function.blocks[blockIndex];
            const auto& block = program[blockRef];
            const std::string llvmLabel = blockLabels[block.label.spelling];
            output << llvmLabel << ":\n";

            // Phi nodes from block parameters
            const auto& slots = incoming[block.label.spelling];
            for (size_t pi = 0; pi < block.params.size(); ++pi) {
                const auto& bp = program[block.params[pi]];
                const std::string paramType = emitType(bp.type, program);
                const std::string paramName = llvmValueName(bp.symbol.spelling);

                output << "  " << paramName << " = phi " << paramType;
                for (size_t ei = 0; ei < slots[pi].size(); ++ei) {
                    const auto& edge = slots[pi][ei];
                    if (ei != 0) {
                        output << ",";
                    }
                    output << " [ " << emitValueOperand(edge.arg, program)
                           << ", %" << edge.predLabel << " ]";
                }
                output << "\n";
            }
            if (blockIndex == 0) {
                for (const auto& paramRef : function.params) {
                    const auto& param = program[paramRef];
                    if (emitType(param.type, program) == POLY_TYPE) {
                        emitPolyCloneTo(llvmValueName(param.symbol.spelling),
                            llvmValueName(param.symbol.spelling) + "_param");
                    }
                }
            }
            for (const auto& paramRef : block.params) {
                const auto& param = program[paramRef];
                const auto typeIt = valueTypes.find(param.symbol.spelling);
                if (typeIt != valueTypes.end()
                    && isOwnedTypeString(typeIt->second)
                    && !ownership.liveIn[blockIndex].contains(
                        param.symbol.spelling)) {
                    emitOwnedValueDrop(param.symbol.spelling);
                }
            }
            if (blockIndex == 0) {
                for (const auto& paramRef : function.params) {
                    const auto& param = program[paramRef];
                    const auto typeIt = valueTypes.find(param.symbol.spelling);
                    if (typeIt != valueTypes.end()
                        && isOwnedTypeString(typeIt->second)
                        && !ownership.liveIn[blockIndex].contains(
                            param.symbol.spelling)) {
                        emitOwnedValueDrop(param.symbol.spelling);
                    }
                }
            }

            // Statements
            for (size_t stmtIndex = 0; stmtIndex < block.statements.size();
                ++stmtIndex) {
                const auto& stmt = block.statements[stmtIndex];
                SymbolSet movedValuesInStatement;
                std::visit(
                    [&](auto stmtRef) {
                        using StmtNode
                            = std::remove_cvref_t<decltype(program[stmtRef])>;
                        if constexpr (std::same_as<StmtNode,
                                          koopa_ir::SymbolDef>) {
                            const auto& sd = program[stmtRef];
                            const std::string& sname = sd.symbol.spelling;

                            MATCH(sd.rhs)
                            WITH(
                                [&](const yesod::Ref<
                                    koopa_ir::MemoryDeclaration>& memRef) {
                                    const auto& mem = program[memRef];
                                    output << "  " << llvmValueName(sname)
                                           << " = alloca "
                                           << emitType(mem.allocType, program)
                                           << "\n";
                                    if (typeContainsPoly(
                                            mem.allocType, program)) {
                                        output
                                            << "  store "
                                            << emitType(mem.allocType, program)
                                            << " zeroinitializer, ptr "
                                            << llvmValueName(sname) << "\n";
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::LoadExpr>&
                                        loadRef) {
                                    const auto& ld = program[loadRef];
                                    const auto ptIt
                                        = pointees.find(ld.source.spelling);
                                    const std::string elemTy
                                        = (ptIt != pointees.end())
                                        ? ptIt->second
                                        : "i32";
                                    if (elemTy == POLY_TYPE) {
                                        const std::string rawName
                                            = llvmValueName(sname) + "_raw";
                                        output
                                            << "  " << rawName << " = load "
                                            << elemTy << ", ptr "
                                            << llvmValueName(ld.source.spelling)
                                            << "\n";
                                        emitPolyCloneTo(
                                            llvmValueName(sname), rawName);
                                    } else {
                                        output
                                            << "  " << llvmValueName(sname)
                                            << " = load " << elemTy << ", ptr "
                                            << llvmValueName(ld.source.spelling)
                                            << "\n";
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::GetPointerExpr>&
                                        getptrRef) {
                                    const auto& gp = program[getptrRef];
                                    const auto ptIt
                                        = pointees.find(gp.source.spelling);
                                    const std::string gepElemTy
                                        = (ptIt != pointees.end())
                                        ? ptIt->second
                                        : "i32";
                                    output
                                        << "  " << llvmValueName(sname)
                                        << " = getelementptr " << gepElemTy
                                        << ", ptr "
                                        << llvmValueName(gp.source.spelling)
                                        << ", i32 "
                                        << emitValueOperand(gp.index, program)
                                        << "\n";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::GetElementPointerExpr>&
                                        gelemRef) {
                                    const auto& ge = program[gelemRef];
                                    const auto ptIt
                                        = pointees.find(ge.source.spelling);
                                    const std::string gepTy
                                        = (ptIt != pointees.end())
                                        ? ptIt->second
                                        : "[0 x i32]";
                                    output
                                        << "  " << llvmValueName(sname)
                                        << " = getelementptr " << gepTy
                                        << ", ptr "
                                        << llvmValueName(ge.source.spelling)
                                        << ", i32 0, i32 "
                                        << emitValueOperand(ge.index, program)
                                        << "\n";
                                },
                                [&](const yesod::Ref<koopa_ir::BinaryExpr>&
                                        binRef) {
                                    const auto& bin = program[binRef];
                                    const std::string lhs_typed = "i32 "
                                        + emitValueOperand(bin.lhs, program);
                                    const std::string lhs_bare
                                        = emitValueOperand(bin.lhs, program);
                                    const std::string rhs_bare
                                        = emitValueOperand(bin.rhs, program);
                                    const bool lhsIsMint
                                        = logicalTypeOf(bin.lhs) == "mint";
                                    const bool rhsIsMint
                                        = logicalTypeOf(bin.rhs) == "mint";
                                    const bool isMintBinary
                                        = lhsIsMint || rhsIsMint;
                                    const std::string lhs_mint
                                        = !isMintBinary || lhsIsMint
                                        ? lhs_bare
                                        : emitIntToMint(lhs_bare);
                                    const std::string rhs_mint
                                        = !isMintBinary || rhsIsMint
                                        ? rhs_bare
                                        : emitIntToMint(rhs_bare);
                                    using BOp = koopa_ir::BinaryOp;
                                    switch (bin.op) {
                                    case BOp::add:
                                        if (isMintBinary) {
                                            const std::string result
                                                = emitMintAddSub(
                                                    lhs_mint, rhs_mint, false);
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = add i32 " << result
                                                   << ", 0\n";
                                        } else {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = add " << lhs_typed
                                                   << ", " << rhs_bare << "\n";
                                        }
                                        break;
                                    case BOp::sub:
                                        if (isMintBinary) {
                                            const std::string result
                                                = emitMintAddSub(
                                                    lhs_mint, rhs_mint, true);
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = add i32 " << result
                                                   << ", 0\n";
                                        } else {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = sub " << lhs_typed
                                                   << ", " << rhs_bare << "\n";
                                        }
                                        break;
                                    case BOp::mul:
                                        if (isMintBinary) {
                                            const std::string result
                                                = emitMintMul(
                                                    lhs_mint, rhs_mint);
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = add i32 " << result
                                                   << ", 0\n";
                                        } else {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = mul " << lhs_typed
                                                   << ", " << rhs_bare << "\n";
                                        }
                                        break;
                                    case BOp::div:
                                        if (isMintBinary) {
                                            const std::string inverse
                                                = emitMintPowConst(
                                                    rhs_mint, MINT_MOD - 2);
                                            const std::string result
                                                = emitMintMul(
                                                    lhs_mint, inverse);
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = add i32 " << result
                                                   << ", 0\n";
                                        } else {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = sdiv " << lhs_typed
                                                   << ", " << rhs_bare << "\n";
                                        }
                                        break;
                                    case BOp::mod:
                                        output << "  " << llvmValueName(sname)
                                               << " = srem " << lhs_typed
                                               << ", " << rhs_bare << "\n";
                                        break;
                                    case BOp::bitAnd:
                                        output << "  " << llvmValueName(sname)
                                               << " = and " << lhs_typed << ", "
                                               << rhs_bare << "\n";
                                        break;
                                    case BOp::bitOr:
                                        output << "  " << llvmValueName(sname)
                                               << " = or " << lhs_typed << ", "
                                               << rhs_bare << "\n";
                                        break;
                                    case BOp::bitXor:
                                        output << "  " << llvmValueName(sname)
                                               << " = xor " << lhs_typed << ", "
                                               << rhs_bare << "\n";
                                        break;
                                    case BOp::shl:
                                        output << "  " << llvmValueName(sname)
                                               << " = shl " << lhs_typed << ", "
                                               << rhs_bare << "\n";
                                        break;
                                    case BOp::shr:
                                        output << "  " << llvmValueName(sname)
                                               << " = lshr " << lhs_typed
                                               << ", " << rhs_bare << "\n";
                                        break;
                                    case BOp::sar:
                                        output << "  " << llvmValueName(sname)
                                               << " = ashr " << lhs_typed
                                               << ", " << rhs_bare << "\n";
                                        break;
                                    case BOp::eq: {
                                        const std::string cn = sname + "_cmp";
                                        const std::string rhs_bare
                                            = emitValueOperand(
                                                bin.rhs, program);
                                        output << "  " << cn << " = icmp eq "
                                               << lhs_typed << ", " << rhs_bare
                                               << "\n";
                                        output << "  " << llvmValueName(sname)
                                               << " = zext i1 " << cn
                                               << " to i32\n";
                                        break;
                                    }
                                    case BOp::ne: {
                                        const std::string cn = sname + "_cmp";
                                        const std::string rhs_bare
                                            = emitValueOperand(
                                                bin.rhs, program);
                                        output << "  " << cn << " = icmp ne "
                                               << lhs_typed << ", " << rhs_bare
                                               << "\n";
                                        output << "  " << llvmValueName(sname)
                                               << " = zext i1 " << cn
                                               << " to i32\n";
                                        break;
                                    }
                                    case BOp::gt: {
                                        const std::string cn = sname + "_cmp";
                                        const std::string rhs_bare
                                            = emitValueOperand(
                                                bin.rhs, program);
                                        output << "  " << cn << " = icmp sgt "
                                               << lhs_typed << ", " << rhs_bare
                                               << "\n";
                                        output << "  " << llvmValueName(sname)
                                               << " = zext i1 " << cn
                                               << " to i32\n";
                                        break;
                                    }
                                    case BOp::lt: {
                                        const std::string cn = sname + "_cmp";
                                        const std::string rhs_bare
                                            = emitValueOperand(
                                                bin.rhs, program);
                                        output << "  " << cn << " = icmp slt "
                                               << lhs_typed << ", " << rhs_bare
                                               << "\n";
                                        output << "  " << llvmValueName(sname)
                                               << " = zext i1 " << cn
                                               << " to i32\n";
                                        break;
                                    }
                                    case BOp::ge: {
                                        const std::string cn = sname + "_cmp";
                                        const std::string rhs_bare
                                            = emitValueOperand(
                                                bin.rhs, program);
                                        output << "  " << cn << " = icmp sge "
                                               << lhs_typed << ", " << rhs_bare
                                               << "\n";
                                        output << "  " << llvmValueName(sname)
                                               << " = zext i1 " << cn
                                               << " to i32\n";
                                        break;
                                    }
                                    case BOp::le: {
                                        const std::string cn = sname + "_cmp";
                                        const std::string rhs_bare
                                            = emitValueOperand(
                                                bin.rhs, program);
                                        output << "  " << cn << " = icmp sle "
                                               << lhs_typed << ", " << rhs_bare
                                               << "\n";
                                        output << "  " << llvmValueName(sname)
                                               << " = zext i1 " << cn
                                               << " to i32\n";
                                        break;
                                    }
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::CallExpr>&
                                        callRef) {
                                    const auto& call = program[callRef];
                                    const std::string callee
                                        = stripPrefix(call.callee.spelling);
                                    const auto sigIt
                                        = sigs.find(call.callee.spelling);
                                    const std::string callRetTy
                                        = (sigIt != sigs.end())
                                        ? sigIt->second.retType
                                        : "void";
                                    // Mark the value type for the result of the
                                    // call.
                                    if (callRetTy != "void") {
                                        valueTypes[sname] = callRetTy;
                                    }
                                    std::vector<std::string> argTypes;
                                    argTypes.reserve(call.args.size());
                                    for (size_t ai = 0; ai < call.args.size();
                                        ++ai) {
                                        argTypes.push_back(
                                            (sigIt != sigs.end()
                                                && ai < sigIt->second.paramTypes
                                                        .size())
                                                ? sigIt->second.paramTypes[ai]
                                                : "i32");
                                    }
                                    const ArgumentPlan argPlan
                                        = planOwnedArguments(call.args,
                                            argTypes,
                                            ownership.liveAfterStmt[blockIndex]
                                                                   [stmtIndex],
                                            "call_arg");
                                    emitArgumentPlanClones(argPlan);
                                    movedValuesInStatement.insert(
                                        argPlan.movedValues.begin(),
                                        argPlan.movedValues.end());
                                    output << "  " << llvmValueName(sname)
                                           << " = call " << callRetTy << " @"
                                           << callee << "(";
                                    for (size_t ai = 0;
                                        ai < argPlan.operands.size(); ++ai) {
                                        if (ai != 0) {
                                            output << ", ";
                                        }
                                        output << argTypes[ai] << " "
                                               << argPlan.operands[ai];
                                    }
                                    output << ")\n";
                                },
                                [&](const yesod::Ref<koopa_ir::ConversionExpr>&
                                        convRef) {
                                    const auto& conv = program[convRef];
                                    const std::string input
                                        = emitValueOperand(conv.value, program);
                                    const std::string converted = conv.op
                                            == koopa_ir::ConversionOp::int2mint
                                        ? emitIntToMint(input)
                                        : emitMintToInt(input);
                                    output << "  " << llvmValueName(sname)
                                           << " = add i32 " << converted
                                           << ", 0\n";
                                },
                                [&](const yesod::Ref<koopa_ir::CopyExpr>&
                                        copyRef) {
                                    const auto& copy = program[copyRef];
                                    const std::string valueType
                                        = valueTypeOf(copy.value);
                                    if (valueType == POLY_TYPE) {
                                        emitPolyCloneTo(llvmValueName(sname),
                                            emitValueOperand(
                                                copy.value, program));
                                    } else {
                                        output << "  " << llvmValueName(sname)
                                               << " = add i32 "
                                               << emitValueOperand(
                                                      copy.value, program)
                                               << ", 0\n";
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::GetAttrExpr>&
                                        getAttrRef) {
                                    const auto& getAttr = program[getAttrRef];
                                    const int32_t index = getAttr.attr
                                            == koopa_ir::PolyAttr::base
                                        ? 0
                                        : getAttr.attr
                                            == koopa_ir::PolyAttr::addr
                                        ? 1
                                        : getAttr.attr == koopa_ir::PolyAttr::l
                                        ? 3
                                        : 4;
                                    output << "  " << llvmValueName(sname)
                                           << " = extractvalue " << POLY_TYPE
                                           << " "
                                           << emitValueOperand(
                                                  getAttr.value, program)
                                           << ", " << index << "\n";
                                },
                                [&](const yesod::Ref<koopa_ir::SetAttrExpr>&
                                        setAttrRef) {
                                    const auto& setAttr = program[setAttrRef];
                                    if (const auto* symbol
                                        = std::get_if<koopa_ir::Symbol>(
                                            &setAttr.value)) {
                                        movedValuesInStatement.insert(
                                            symbol->spelling);
                                    }
                                    const std::string input = emitValueOperand(
                                        setAttr.value, program);
                                    const std::string value = emitValueOperand(
                                        setAttr.attrValue, program);
                                    const int32_t index = setAttr.attr
                                            == koopa_ir::PolyAttr::base
                                        ? 0
                                        : setAttr.attr
                                            == koopa_ir::PolyAttr::addr
                                        ? 1
                                        : setAttr.attr == koopa_ir::PolyAttr::l
                                        ? 3
                                        : 4;
                                    const std::string valueType
                                        = (setAttr.attr
                                                  == koopa_ir::PolyAttr::base
                                              || setAttr.attr
                                                  == koopa_ir::PolyAttr::addr)
                                        ? "ptr"
                                        : "i32";
                                    output << "  " << llvmValueName(sname)
                                           << " = insertvalue " << POLY_TYPE
                                           << " " << input << ", " << valueType
                                           << " " << value << ", " << index
                                           << "\n";
                                },
                                [&](const yesod::Ref<koopa_ir::SelectExpr>&
                                        selectRef) {
                                    const auto& select = program[selectRef];
                                    const std::string resultType
                                        = valueTypeOf(select.trueValue);
                                    const std::string cond
                                        = nextHelperName("select_cond");
                                    output << "  " << cond << " = icmp ne i32 "
                                           << emitValueOperand(
                                                  select.condition, program)
                                           << ", 0\n";
                                    const std::string raw
                                        = resultType == POLY_TYPE
                                        ? nextHelperName("select_poly")
                                        : llvmValueName(sname);
                                    output << "  " << raw << " = select i1 "
                                           << cond << ", " << resultType << " "
                                           << emitValueOperand(
                                                  select.trueValue, program)
                                           << ", " << resultType << " "
                                           << emitValueOperand(
                                                  select.falseValue, program)
                                           << "\n";
                                    if (resultType == POLY_TYPE) {
                                        emitPolyCloneTo(
                                            llvmValueName(sname), raw);
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::NextPow2Expr>&
                                        nextPow2Ref) {
                                    const auto& nextPow2 = program[nextPow2Ref];
                                    output << "  " << llvmValueName(sname)
                                           << " = call i32 @__yesod_next_pow2("
                                           << "i32 "
                                           << emitValueOperand(
                                                  nextPow2.value, program)
                                           << ")\n";
                                },
                                [&](const yesod::Ref<koopa_ir::NttExpr>&
                                        nttRef) {
                                    const auto& ntt = program[nttRef];
                                    const PolyFields input = emitPolyFields(
                                        emitValueOperand(ntt.value, program));
                                    const std::string length
                                        = emitValueOperand(ntt.length, program);
                                    const std::string values
                                        = nextHelperName("pv_ntt_values");
                                    output << "  " << values
                                           << " = call ptr "
                                              "@__yesod_poly_ntt_data(ptr "
                                           << input.coeffs << ", i32 "
                                           << input.l << ", i32 " << input.r
                                           << ", i32 " << length << ")\n";
                                    emitPvFromFields(llvmValueName(sname),
                                        PvFields {
                                            .values = values, .len = length });
                                },
                                [&](const yesod::Ref<koopa_ir::PointwiseExpr>&
                                        pointwiseRef) {
                                    const auto& pointwise
                                        = program[pointwiseRef];
                                    const std::string length = emitValueOperand(
                                        pointwise.length, program);
                                    const std::string outputPv
                                        = nextHelperName("pv_fused");
                                    const std::string outputValues
                                        = nextHelperName("pv_values");
                                    output << "  " << outputValues
                                           << " = call ptr "
                                              "@__yesod_rt_alloc_ints(i32 "
                                           << length << ")\n";
                                    emitPvFromFields(outputPv,
                                        PvFields { .values = outputValues,
                                            .len = length });
                                    const std::string preheaderLabel
                                        = nextLabelName("pointwise_preheader");
                                    const std::string condLabel
                                        = nextLabelName("pointwise_cond");
                                    const std::string bodyLabel
                                        = nextLabelName("pointwise_body");
                                    const std::string endLabel
                                        = nextLabelName("pointwise_end");
                                    output << "  br label %" << preheaderLabel
                                           << "\n";
                                    output << preheaderLabel << ":\n";
                                    output << "  br label %" << condLabel
                                           << "\n";
                                    output << condLabel << ":\n";
                                    const std::string index
                                        = nextHelperName("pointwise_i");
                                    const std::string next
                                        = nextHelperName("pointwise_next");
                                    output << "  " << index
                                           << " = phi i32 [ 0, %"
                                           << preheaderLabel << " ], [ " << next
                                           << ", %" << bodyLabel << " ]\n";
                                    const std::string keep
                                        = nextHelperName("pointwise_keep");
                                    output << "  " << keep << " = icmp slt i32 "
                                           << index << ", " << length << "\n";
                                    output << "  br i1 " << keep << ", label %"
                                           << bodyLabel << ", label %"
                                           << endLabel << "\n";
                                    output << bodyLabel << ":\n";
                                    const std::string value
                                        = emitPointwiseScalar(
                                            pointwise.root, index);
                                    const std::string outputElement
                                        = nextHelperName("pv_out_elem");
                                    output << "  " << outputElement
                                           << " = getelementptr i32, ptr "
                                           << outputValues << ", i32 " << index
                                           << "\n";
                                    output << "  store i32 " << value
                                           << ", ptr " << outputElement << "\n";
                                    output << "  " << next << " = add i32 "
                                           << index << ", 1\n";
                                    output << "  br label %" << condLabel
                                           << "\n";
                                    output << endLabel << ":\n";
                                    const std::string activeL
                                        = emitValueOperand(
                                            pointwise.activeL, program);
                                    const std::string activeR
                                        = emitValueOperand(
                                            pointwise.activeR, program);
                                    const std::string resultAddr
                                        = nextHelperName("poly_from_pv_addr");
                                    output
                                        << "  " << resultAddr
                                        << " = call ptr "
                                           "@__yesod_poly_from_pointwise_data"
                                           "(ptr "
                                        << outputValues << ", i32 " << length
                                        << ", i32 " << activeL << ", i32 "
                                        << activeR << ")\n";
                                    const std::string activeLen
                                        = nextHelperName("poly_from_pv_len");
                                    output << "  " << activeLen << " = sub i32 "
                                           << activeR << ", " << activeL
                                           << "\n";
                                    const std::string isEmpty
                                        = nextHelperName("poly_from_pv_empty");
                                    output << "  " << isEmpty
                                           << " = icmp sge i32 " << activeL
                                           << ", " << activeR << "\n";
                                    const std::string rawN
                                        = nextHelperName("poly_from_pv_n_raw");
                                    output << "  " << rawN
                                           << " = call i32 @__yesod_next_pow2("
                                              "i32 "
                                           << activeLen << ")\n";
                                    const std::string resultN
                                        = nextHelperName("poly_from_pv_n");
                                    output << "  " << resultN << " = select i1 "
                                           << isEmpty << ", i32 0, i32 " << rawN
                                           << "\n";
                                    const std::string resultCoeffs
                                        = emitPolyCoeffPtrForAddr(
                                            resultAddr, activeL, isEmpty);
                                    emitPolyFromFields(llvmValueName(sname),
                                        PolyFields { .coeffs = resultCoeffs,
                                            .addr = resultAddr,
                                            .n = resultN,
                                            .l = activeL,
                                            .r = activeR });
                                    output
                                        << "  call void @__yesod_rt_free_ints"
                                           "(ptr "
                                        << outputValues << ")\n";
                                },
                                [&](const yesod::Ref<koopa_ir::CombineExpr>&
                                        combineRef) {
                                    const auto& combine = program[combineRef];
                                    struct CombineTermState {
                                        std::string srcCoeffs;
                                        std::string lower;
                                        std::string upper;
                                        std::string shift;
                                        std::string scale;
                                    };
                                    std::vector<CombineTermState> termStates;
                                    termStates.reserve(combine.terms.size());
                                    std::string resultL = "0";
                                    std::string resultR = "0";
                                    std::string hasAny = "false";
                                    for (const auto& term : combine.terms) {
                                        if (const auto* scaleLiteral
                                            = std::get_if<
                                                koopa_ir::IntegerLiteral>(
                                                &term.scale);
                                            scaleLiteral != nullptr
                                            && scaleLiteral->value == 0) {
                                            continue;
                                        }
                                        const std::string src
                                            = emitValueOperand(
                                                term.value, program);
                                        const std::string srcCoeffs
                                            = nextHelperName(
                                                "combine_src_coeffs");
                                        output << "  " << srcCoeffs
                                               << " = extractvalue "
                                               << POLY_TYPE << " " << src
                                               << ", 0\n";
                                        const std::string srcL
                                            = nextHelperName("combine_src_l");
                                        output << "  " << srcL
                                               << " = extractvalue "
                                               << POLY_TYPE << " " << src
                                               << ", 3\n";
                                        const std::string srcR
                                            = nextHelperName("combine_src_r");
                                        output << "  " << srcR
                                               << " = extractvalue "
                                               << POLY_TYPE << " " << src
                                               << ", 4\n";
                                        const std::string start
                                            = emitValueOperand(
                                                term.start, program);
                                        const std::string endValue
                                            = term.end.has_value()
                                            ? emitValueOperand(
                                                  *term.end, program)
                                            : std::to_string(INF_END);
                                        const std::string endBeforeSrcR
                                            = nextHelperName(
                                                "combine_end_before_src_r");
                                        output << "  " << endBeforeSrcR
                                               << " = icmp slt i32 " << endValue
                                               << ", " << srcR << "\n";
                                        const std::string upper
                                            = nextHelperName("combine_upper");
                                        output << "  " << upper
                                               << " = select i1 "
                                               << endBeforeSrcR << ", i32 "
                                               << endValue << ", i32 " << srcR
                                               << "\n";
                                        const std::string startAfterSrcL
                                            = nextHelperName(
                                                "combine_start_after_src_l");
                                        output << "  " << startAfterSrcL
                                               << " = icmp sgt i32 " << start
                                               << ", " << srcL << "\n";
                                        const std::string lower
                                            = nextHelperName("combine_lower");
                                        output << "  " << lower
                                               << " = select i1 "
                                               << startAfterSrcL << ", i32 "
                                               << start << ", i32 " << srcL
                                               << "\n";
                                        const std::string hasTerm
                                            = nextHelperName(
                                                "combine_has_term");
                                        output << "  " << hasTerm
                                               << " = icmp slt i32 " << lower
                                               << ", " << upper << "\n";
                                        const std::string shift
                                            = emitValueOperand(
                                                term.shift, program);
                                        const std::string termL
                                            = nextHelperName("combine_term_l");
                                        output << "  " << termL << " = sub i32 "
                                               << lower << ", " << shift
                                               << "\n";
                                        const std::string termR
                                            = nextHelperName("combine_term_r");
                                        output << "  " << termR << " = sub i32 "
                                               << upper << ", " << shift
                                               << "\n";
                                        const std::string isSmallerL
                                            = nextHelperName(
                                                "combine_l_smaller");
                                        output << "  " << isSmallerL
                                               << " = icmp slt i32 " << termL
                                               << ", " << resultL << "\n";
                                        const std::string minL
                                            = nextHelperName("combine_min_l");
                                        output << "  " << minL
                                               << " = select i1 " << isSmallerL
                                               << ", i32 " << termL << ", i32 "
                                               << resultL << "\n";
                                        const std::string isLargerR
                                            = nextHelperName(
                                                "combine_r_larger");
                                        output << "  " << isLargerR
                                               << " = icmp sgt i32 " << termR
                                               << ", " << resultR << "\n";
                                        const std::string maxR
                                            = nextHelperName("combine_max_r");
                                        output << "  " << maxR
                                               << " = select i1 " << isLargerR
                                               << ", i32 " << termR << ", i32 "
                                               << resultR << "\n";
                                        const std::string mergedL
                                            = nextHelperName(
                                                "combine_merged_l");
                                        output << "  " << mergedL
                                               << " = select i1 " << hasAny
                                               << ", i32 " << minL << ", i32 "
                                               << termL << "\n";
                                        const std::string mergedR
                                            = nextHelperName(
                                                "combine_merged_r");
                                        output << "  " << mergedR
                                               << " = select i1 " << hasAny
                                               << ", i32 " << maxR << ", i32 "
                                               << termR << "\n";
                                        const std::string nextResultL
                                            = nextHelperName(
                                                "combine_result_l");
                                        output << "  " << nextResultL
                                               << " = select i1 " << hasTerm
                                               << ", i32 " << mergedL
                                               << ", i32 " << resultL << "\n";
                                        const std::string nextResultR
                                            = nextHelperName(
                                                "combine_result_r");
                                        output << "  " << nextResultR
                                               << " = select i1 " << hasTerm
                                               << ", i32 " << mergedR
                                               << ", i32 " << resultR << "\n";
                                        const std::string nextHasAny
                                            = nextHelperName("combine_any");
                                        output << "  " << nextHasAny
                                               << " = or i1 " << hasAny << ", "
                                               << hasTerm << "\n";
                                        resultL = nextResultL;
                                        resultR = nextResultR;
                                        hasAny = nextHasAny;
                                        termStates.push_back(CombineTermState {
                                            .srcCoeffs = srcCoeffs,
                                            .lower = termL,
                                            .upper = termR,
                                            .shift = shift,
                                            .scale
                                            = emitMintOperand(term.scale),
                                        });
                                    }
                                    if (termStates.empty()) {
                                        emitPolyZeroTo(llvmValueName(sname));
                                        return;
                                    }
                                    const std::string resultAddr
                                        = nextHelperName("combine_result_addr");
                                    output << "  " << resultAddr
                                           << " = call ptr "
                                              "@__yesod_poly_alloc_zero_data("
                                              "i32 "
                                           << resultL << ", i32 " << resultR
                                           << ")\n";
                                    const std::string resultLen
                                        = nextHelperName("combine_result_len");
                                    output << "  " << resultLen << " = sub i32 "
                                           << resultR << ", " << resultL
                                           << "\n";
                                    const std::string resultEmpty
                                        = nextHelperName(
                                            "combine_result_empty");
                                    output << "  " << resultEmpty
                                           << " = icmp sge i32 " << resultL
                                           << ", " << resultR << "\n";
                                    const std::string resultRawN
                                        = nextHelperName(
                                            "combine_result_n_raw");
                                    output << "  " << resultRawN
                                           << " = call i32 @__yesod_next_pow2("
                                              "i32 "
                                           << resultLen << ")\n";
                                    const std::string resultN
                                        = nextHelperName("combine_result_n");
                                    output << "  " << resultN << " = select i1 "
                                           << resultEmpty << ", i32 0, i32 "
                                           << resultRawN << "\n";
                                    const std::string resultPolyCoeffs
                                        = emitPolyCoeffPtrForAddr(
                                            resultAddr, resultL, resultEmpty);
                                    emitPolyFromFields(llvmValueName(sname),
                                        PolyFields { .coeffs = resultPolyCoeffs,
                                            .addr = resultAddr,
                                            .n = resultN,
                                            .l = resultL,
                                            .r = resultR });
                                    const std::string resultCoeffs
                                        = nextHelperName(
                                            "combine_result_coeffs");
                                    output << "  " << resultCoeffs
                                           << " = extractvalue " << POLY_TYPE
                                           << " " << llvmValueName(sname)
                                           << ", 0\n";
                                    const std::string montOne = std::to_string(
                                        intToMontgomeryConst(1));
                                    const std::string montMinusOne
                                        = std::to_string(
                                            intToMontgomeryConst(-1));
                                    for (const auto& term : termStates) {
                                        const std::string loopStartIsNegative
                                            = nextHelperName(
                                                "combine_start_neg");
                                        output << "  " << loopStartIsNegative
                                               << " = icmp slt i32 "
                                               << term.lower << ", 0\n";
                                        const std::string loopStart
                                            = nextHelperName(
                                                "combine_loop_start");
                                        output << "  " << loopStart
                                               << " = select i1 "
                                               << loopStartIsNegative
                                               << ", i32 0, i32 " << term.lower
                                               << "\n";
                                        const std::string preheaderLabel
                                            = nextLabelName(
                                                "combine_preheader");
                                        const std::string condLabel
                                            = nextLabelName("combine_cond");
                                        const std::string bodyLabel
                                            = nextLabelName("combine_body");
                                        const std::string endLabel
                                            = nextLabelName("combine_end");
                                        output << "  br label %"
                                               << preheaderLabel << "\n";
                                        output << preheaderLabel << ":\n";
                                        output << "  br label %" << condLabel
                                               << "\n";
                                        output << condLabel << ":\n";
                                        const std::string index
                                            = nextHelperName("combine_i");
                                        const std::string next
                                            = nextHelperName("combine_next");
                                        output << "  " << index
                                               << " = phi i32 [ " << loopStart
                                               << ", %" << preheaderLabel
                                               << " ], [ " << next << ", %"
                                               << bodyLabel << " ]\n";
                                        const std::string keep
                                            = nextHelperName("combine_keep");
                                        output << "  " << keep
                                               << " = icmp slt i32 " << index
                                               << ", " << term.upper << "\n";
                                        output << "  br i1 " << keep
                                               << ", label %" << bodyLabel
                                               << ", label %" << endLabel
                                               << "\n";
                                        output << bodyLabel << ":\n";
                                        const std::string dstPtr
                                            = nextHelperName("combine_dst");
                                        output << "  " << dstPtr
                                               << " = getelementptr i32, ptr "
                                               << resultCoeffs << ", i32 "
                                               << index << "\n";
                                        const std::string oldValue
                                            = nextHelperName("combine_old");
                                        output << "  " << oldValue
                                               << " = load i32, ptr " << dstPtr
                                               << "\n";
                                        const std::string srcIndex
                                            = nextHelperName(
                                                "combine_src_index");
                                        output << "  " << srcIndex
                                               << " = add i32 " << index << ", "
                                               << term.shift << "\n";
                                        const std::string srcPtr
                                            = nextHelperName("combine_src");
                                        output << "  " << srcPtr
                                               << " = getelementptr i32, ptr "
                                               << term.srcCoeffs << ", i32 "
                                               << srcIndex << "\n";
                                        const std::string srcValue
                                            = nextHelperName("combine_src_val");
                                        output << "  " << srcValue
                                               << " = load i32, ptr " << srcPtr
                                               << "\n";
                                        std::string combined;
                                        if (term.scale == montOne) {
                                            combined = emitMintAddSub(
                                                oldValue, srcValue, false);
                                        } else if (term.scale == montMinusOne) {
                                            combined = emitMintAddSub(
                                                oldValue, srcValue, true);
                                        } else {
                                            const std::string scaled
                                                = emitMintMul(
                                                    srcValue, term.scale);
                                            combined = emitMintAddSub(
                                                oldValue, scaled, false);
                                        }
                                        output << "  store i32 " << combined
                                               << ", ptr " << dstPtr << "\n";
                                        output << "  " << next << " = add i32 "
                                               << index << ", 1\n";
                                        output << "  br label %" << condLabel
                                               << "\n";
                                        output << endLabel << ":\n";
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::GetCoeffExpr>&
                                        getCoeffRef) {
                                    const auto& getCoeff = program[getCoeffRef];
                                    const PolyFields input
                                        = emitPolyFields(emitValueOperand(
                                            getCoeff.value, program));
                                    output << "  " << llvmValueName(sname)
                                           << " = call i32 "
                                              "@__yesod_poly_getcoeff_data(ptr "
                                           << input.coeffs << ", i32 "
                                           << input.l << ", i32 " << input.r
                                           << ", i32 "
                                           << emitValueOperand(
                                                  getCoeff.index, program)
                                           << ")\n";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::PolyConstructExpr>&
                                        constructRef) {
                                    const auto& construct
                                        = program[constructRef];
                                    if (construct.elements.empty()) {
                                        emitPolyZeroTo(llvmValueName(sname));
                                        return;
                                    }
                                    const std::string count = std::to_string(
                                        construct.elements.size());
                                    const std::string addr
                                        = nextHelperName("poly_construct_addr");
                                    output << "  " << addr
                                           << " = call ptr "
                                              "@__yesod_poly_alloc_zero_data("
                                              "i32 0, i32 "
                                           << count << ")\n";
                                    for (size_t i = 0;
                                        i < construct.elements.size(); ++i) {
                                        const std::string elementPtr
                                            = nextHelperName("poly_coeff_ptr");
                                        output << "  " << elementPtr
                                               << " = getelementptr i32, ptr "
                                               << addr << ", i32 " << i << "\n";
                                        const std::string element
                                            = emitMintOperand(
                                                construct.elements[i]);
                                        output << "  store i32 " << element
                                               << ", ptr " << elementPtr
                                               << "\n";
                                    }
                                    size_t constructN = 1;
                                    while (constructN
                                        < construct.elements.size()) {
                                        constructN <<= 1;
                                    }
                                    emitPolyFromFields(llvmValueName(sname),
                                        PolyFields { .coeffs = addr,
                                            .addr = addr,
                                            .n = std::to_string(constructN),
                                            .l = "0",
                                            .r = count });
                                },
                                [&](const auto&) {
                                    throw std::runtime_error(
                                        "LLVM backend does not support native "
                                        "poly/pv pseudo-instructions");
                                });
                        } else if constexpr (std::same_as<StmtNode,
                                                 koopa_ir::StoreStmt>) {
                            const auto& store = program[stmtRef];
                            const auto ptIt
                                = pointees.find(store.destination.spelling);
                            const std::string elemTy = (ptIt != pointees.end())
                                ? ptIt->second
                                : "i32";
                            std::string storeValue;
                            std::string storeLogicalType = "int";
                            MATCH(store.value)
                            WITH(
                                [&](const koopa_ir::Symbol& sv) {
                                    storeValue = llvmValueName(sv.spelling);
                                    const auto logicalIt
                                        = logicalTypes.find(sv.spelling);
                                    storeLogicalType
                                        = logicalIt == logicalTypes.end()
                                        ? "int"
                                        : logicalIt->second;
                                },
                                [&](const koopa_ir::IntegerLiteral& sv) {
                                    storeValue = std::to_string(sv.value);
                                },
                                [&](const koopa_ir::UndefValue&) {
                                    storeValue = "undef";
                                },
                                [&](const koopa_ir::ZeroInit&) {
                                    storeValue = "zeroinitializer";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::AggregateInitializer>&) {
                                    storeValue = "zeroinitializer";
                                });
                            if (elemTy == POLY_TYPE) {
                                const std::string oldValue
                                    = nextHelperName("poly_store_old");
                                output
                                    << "  " << oldValue << " = load "
                                    << POLY_TYPE << ", ptr "
                                    << llvmValueName(store.destination.spelling)
                                    << "\n";
                                emitOwnedValueDropByType(
                                    std::string(POLY_TYPE), oldValue);
                                storeValue = emitPolyCloneValue(storeValue);
                            } else {
                                const auto logicalPtIt = logicalPointees.find(
                                    store.destination.spelling);
                                const std::string logicalElemTy
                                    = logicalPtIt == logicalPointees.end()
                                    ? "int"
                                    : logicalPtIt->second;
                                if (elemTy == "i32" && logicalElemTy == "mint"
                                    && storeLogicalType != "mint") {
                                    if (storeValue != "undef"
                                        && storeValue != "zeroinitializer") {
                                        storeValue = emitIntToMint(storeValue);
                                    }
                                }
                            }
                            output << "  store " << elemTy << " " << storeValue
                                   << ", ptr "
                                   << llvmValueName(store.destination.spelling)
                                   << "\n";
                        } else if constexpr (std::same_as<StmtNode,
                                                 koopa_ir::CallExpr>) {
                            const auto& call = program[stmtRef];
                            const std::string callee
                                = stripPrefix(call.callee.spelling);
                            const auto sigIt = sigs.find(call.callee.spelling);
                            const std::string callRetTy = (sigIt != sigs.end())
                                ? sigIt->second.retType
                                : "void";
                            std::vector<std::string> argTypes;
                            argTypes.reserve(call.args.size());
                            for (size_t ai = 0; ai < call.args.size(); ++ai) {
                                argTypes.push_back(
                                    (sigIt != sigs.end()
                                        && ai < sigIt->second.paramTypes.size())
                                        ? sigIt->second.paramTypes[ai]
                                        : "i32");
                            }
                            const ArgumentPlan argPlan = planOwnedArguments(
                                call.args, argTypes,
                                ownership.liveAfterStmt[blockIndex][stmtIndex],
                                "call_arg");
                            emitArgumentPlanClones(argPlan);
                            movedValuesInStatement.insert(
                                argPlan.movedValues.begin(),
                                argPlan.movedValues.end());
                            output << "  call " << callRetTy << " @" << callee
                                   << "(";
                            for (size_t ai = 0; ai < argPlan.operands.size();
                                ++ai) {
                                if (ai != 0) {
                                    output << ", ";
                                }
                                output << argTypes[ai] << " "
                                       << argPlan.operands[ai];
                            }
                            output << ")\n";
                        }
                    },
                    stmt);
                const auto uses
                    = statementUses(stmt, program, sigs, pointees, valueTypes);
                SymbolSet valuesToDrop;
                for (const auto& use : uses) {
                    if (!ownership.liveAfterStmt[blockIndex][stmtIndex]
                            .contains(use)
                        && !movedValuesInStatement.contains(use)) {
                        valuesToDrop.insert(use);
                    }
                }
                const auto def = statementDef(stmt, program, valueTypes);
                if (def.has_value()
                    && !ownership.liveAfterStmt[blockIndex][stmtIndex].contains(
                        *def)) {
                    valuesToDrop.insert(*def);
                }
                emitOwnedValueDrops(valuesToDrop);
            }

            // ── Terminator ─────────────────────────────────────────────
            MATCH(block.terminator)
            WITH(
                [&](const yesod::Ref<koopa_ir::JumpTerminator>& jumpRef) {
                    const auto& jump = program[jumpRef];
                    const auto planIt = edgePlans.find({ blockIndex,
                        jump.args.empty() ? std::string("") : "jump" });
                    const bool splitJump = planIt != edgePlans.end()
                        && planIt->second.label != llvmLabel;
                    if (!splitJump && planIt != edgePlans.end()) {
                        emitArgumentPlanClones(planIt->second.args);
                        emitOwnedValueDrops(planIt->second.releases);
                    }
                    if (splitJump) {
                        output << "  br label %" << planIt->second.label
                               << "\n";
                        output << planIt->second.label << ":\n";
                        emitArgumentPlanClones(planIt->second.args);
                        emitOwnedValueDrops(planIt->second.releases);
                    }
                    output << "  br label %"
                           << blockLabels[jump.target.spelling] << "\n";
                },
                [&](const yesod::Ref<koopa_ir::BranchTerminator>& brRef) {
                    const auto& br = program[brRef];
                    const std::string condName
                        = "_cond_bool_" + stripPrefix(block.label.spelling);
                    output << "  %" << condName << " = icmp ne i32 "
                           << emitValueOperand(br.condition, program)
                           << ", 0\n";
                    const auto truePlanIt
                        = edgePlans.find({ blockIndex, "true" });
                    const auto falsePlanIt
                        = edgePlans.find({ blockIndex, "false" });
                    const SymbolSet trueReleases = truePlanIt == edgePlans.end()
                        ? SymbolSet { }
                        : truePlanIt->second.releases;
                    const SymbolSet falseReleases
                        = falsePlanIt == edgePlans.end()
                        ? SymbolSet { }
                        : falsePlanIt->second.releases;
                    const bool trueHasClones = truePlanIt != edgePlans.end()
                        && !truePlanIt->second.args.clones.empty();
                    const bool falseHasClones = falsePlanIt != edgePlans.end()
                        && !falsePlanIt->second.args.clones.empty();
                    const bool splitTrue = !br.trueArgs.empty()
                        || !trueReleases.empty() || trueHasClones;
                    const bool splitFalse = !br.falseArgs.empty()
                        || !falseReleases.empty() || falseHasClones;
                    if (splitTrue || splitFalse
                        || needsSameTargetBranchSplit(br)) {
                        const std::string trueEdgeLabel
                            = edgeBlockLabel(llvmLabel, "true");
                        const std::string falseEdgeLabel
                            = edgeBlockLabel(llvmLabel, "false");
                        output << "  br i1 %" << condName << ", label %";
                        output << (splitTrue
                                ? trueEdgeLabel
                                : blockLabels[br.trueTarget.spelling]);
                        output << ", label %";
                        output << (splitFalse
                                ? falseEdgeLabel
                                : blockLabels[br.falseTarget.spelling])
                               << "\n";
                        if (splitTrue) {
                            output << trueEdgeLabel << ":\n";
                            if (truePlanIt != edgePlans.end()) {
                                emitArgumentPlanClones(truePlanIt->second.args);
                            }
                            emitOwnedValueDrops(trueReleases);
                            output << "  br label %"
                                   << blockLabels[br.trueTarget.spelling]
                                   << "\n";
                        }
                        if (splitFalse) {
                            output << falseEdgeLabel << ":\n";
                            if (falsePlanIt != edgePlans.end()) {
                                emitArgumentPlanClones(
                                    falsePlanIt->second.args);
                            }
                            emitOwnedValueDrops(falseReleases);
                            output << "  br label %"
                                   << blockLabels[br.falseTarget.spelling]
                                   << "\n";
                        }
                        return;
                    }
                    output << "  br i1 %" << condName << ", label %"
                           << blockLabels[br.trueTarget.spelling] << ", label %"
                           << blockLabels[br.falseTarget.spelling] << "\n";
                },
                [&](const yesod::Ref<koopa_ir::ReturnTerminator>& retRef) {
                    const auto& ret = program[retRef];
                    SymbolSet beforeReturn;
                    if (ret.value.has_value()) {
                        addOwnedValueUse(*ret.value, valueTypes, beforeReturn);
                    }
                    SymbolSet returnReleases = beforeReturn;
                    if (ret.value.has_value()) {
                        if (const auto* symbol
                            = std::get_if<koopa_ir::Symbol>(&*ret.value)) {
                            returnReleases.erase(symbol->spelling);
                        }
                    }
                    emitOwnedValueDrops(returnReleases);
                    emitFunctionStorageCleanup();
                    if (ret.value.has_value()) {
                        const std::string valueType = valueTypeOf(*ret.value);
                        output << "  ret " << valueType << " "
                               << emitValueOperand(*ret.value, program) << "\n";
                    } else {
                        output << "  ret void\n";
                    }
                });
        }

        output << "}\n\n";
    }

} // anonymous namespace

// ─── Public API ───────────────────────────────────────────────────────

void LlvmGenerator::generate(
    const koopa_ir::Program& program, std::ostream& output)
{
    emitModuleHeader(output);
    const FuncSigMap sigs = buildFuncSigMap(program);

    // Emit globals and function declarations first.
    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, koopa_ir::GlobalMemoryDef>) {
                    emitGlobalDecl(program[itemRef], program, output);
                } else if constexpr (std::same_as<Item,
                                         koopa_ir::FunctionDecl>) {
                    emitFuncDecl(program[itemRef], program, output);
                }
            },
            item);
    }

    // Emit function definitions.
    for (const auto& item : program.items) {
        std::visit(
            [&](auto itemRef) {
                using Item = std::remove_cvref_t<decltype(program[itemRef])>;
                if constexpr (std::same_as<Item, koopa_ir::FunctionDef>) {
                    emitFunctionDef(program[itemRef], program, sigs, output);
                }
            },
            item);
    }
}

} // namespace yesod::backend
