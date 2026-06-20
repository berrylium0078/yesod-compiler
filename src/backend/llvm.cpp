#include "backend/llvm.h"

#include "koopa/ir.h"
#include "utils.h"

#include <algorithm>
#include <map>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace yesod::backend {

namespace koopa_ir = yesod::koopa::ir;

namespace {

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
                throw std::runtime_error(
                    "LLVM backend does not support poly yet");
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
        WITH([&](const koopa_ir::IntegerLiteral& lit) { output << lit.value; },
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
                    for (size_t i = 0; i < jump.args.size() && i < slots.size();
                        ++i) {
                        slots[i].push_back(
                            IncomingEdge { predLabel, jump.args[i] });
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
                            tslots[i].push_back(
                                IncomingEdge { predLabel, br.trueArgs[i] });
                        }
                    }
                    {
                        const std::string flabel = br.falseTarget.spelling;
                        auto& fslots = incoming[flabel];
                        for (size_t i = 0;
                            i < br.falseArgs.size() && i < fslots.size(); ++i) {
                            fslots[i].push_back(
                                IncomingEdge { predLabel, br.falseArgs[i] });
                        }
                    }
                },
                [](const yesod::Ref<koopa_ir::ReturnTerminator>&) { });
        }
    }

    // ─── Top-level emit helpers ───────────────────────────────────────────

    void emitModuleHeader(std::ostream& output)
    {
        output << "; ModuleID = 'KoopaIR'\n";
        output << "source_filename = \"KoopaIR\"\n";
        output << "target datalayout = \"e-m:e-p:32:32-p270:32:32-f64:32:64"
                  "-f80:128-n8:16:32-S128\"\n";
        output << "target triple = \"riscv32-unknown-linux-elf\"\n\n";
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

        auto recordPointer
            = [&](const std::string& sym, const koopa_ir::Type& type) {
                  valueTypes[sym] = "ptr";
                  const std::string pt = pointeeTypeString(type, program);
                  if (!pt.empty()) {
                      pointees[sym] = pt;
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
                    }
                },
                item);
        }

        // Function parameter types
        for (const auto& pref : function.params) {
            const auto& param = program[pref];
            const std::string ps = emitType(param.type, program);
            valueTypes[param.symbol.spelling] = ps;
            if (const auto* prefType
                = std::get_if<yesod::Ref<koopa_ir::PointerType>>(&param.type)) {
                pointees[param.symbol.spelling]
                    = emitType(program[*prefType].pointeeType, program);
            }
        }

        // Walk all blocks to build type maps
        for (const auto& blockRef : function.blocks) {
            const auto& block = program[blockRef];

            for (const auto& ppref : block.params) {
                const auto& bp = program[ppref];
                const std::string ps = emitType(bp.type, program);
                valueTypes[bp.symbol.spelling] = ps;
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
                                    pointees[sname]
                                        = emitType(mem.allocType, program);
                                    const std::string loadedPointee
                                        = loadedPointerPointeeTypeString(
                                            mem.allocType, program);
                                    if (!loadedPointee.empty()) {
                                        loadedPointees[sname] = loadedPointee;
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
                                    const auto ptIt
                                        = pointees.find(gp.source.spelling);
                                    if (ptIt != pointees.end()) {
                                        pointees[sname] = ptIt->second;
                                    }
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::GetElementPointerExpr>&
                                        gelemRef) {
                                    const auto& ge = program[gelemRef];
                                    valueTypes[sname] = "ptr";
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
                                },
                                [&](const yesod::Ref<koopa_ir::BinaryExpr>&) {
                                    valueTypes[sname] = "i32";
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
                                    } else {
                                        valueTypes[sname] = "i32";
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::CopyExpr>&
                                        copyRef) {
                                    const auto& copy = program[copyRef];
                                    if (const auto* sym
                                        = std::get_if<koopa_ir::Symbol>(
                                            &copy.value)) {
                                        const auto typeIt
                                            = valueTypes.find(sym->spelling);
                                        valueTypes[sname]
                                            = typeIt == valueTypes.end()
                                            ? "i32"
                                            : typeIt->second;
                                    } else {
                                        valueTypes[sname] = "i32";
                                    }
                                },
                                [&](const yesod::Ref<koopa_ir::PolyLenExpr>&) {
                                    valueTypes[sname] = "i32";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::ConversionExpr>&) {
                                    valueTypes[sname] = "i32";
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
            output << emitType(param.type, program) << " "
                   << llvmValueName(param.symbol.spelling);
        }
        output << ") {\n";

        // ── Emit blocks ────────────────────────────────────────────────
        for (const auto& blockRef : function.blocks) {
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

            // Statements
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
                                    output << "  " << llvmValueName(sname)
                                           << " = alloca "
                                           << emitType(mem.allocType, program)
                                           << "\n";
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
                                    output << "  " << llvmValueName(sname)
                                           << " = load " << elemTy << ", ptr "
                                           << llvmValueName(ld.source.spelling)
                                           << "\n";
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
                                    const std::string rhs_bare
                                        = emitValueOperand(bin.rhs, program);
                                    using BOp = koopa_ir::BinaryOp;
                                    switch (bin.op) {
                                    case BOp::add:
                                        output << "  " << llvmValueName(sname)
                                               << " = add " << lhs_typed << ", "
                                               << rhs_bare << "\n";
                                        break;
                                    case BOp::sub:
                                        output << "  " << llvmValueName(sname)
                                               << " = sub " << lhs_typed << ", "
                                               << rhs_bare << "\n";
                                        break;
                                    case BOp::mul:
                                        output << "  " << llvmValueName(sname)
                                               << " = mul " << lhs_typed << ", "
                                               << rhs_bare << "\n";
                                        break;
                                    case BOp::div:
                                        output << "  " << llvmValueName(sname)
                                               << " = sdiv " << lhs_typed
                                               << ", " << rhs_bare << "\n";
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
                                    output << "  " << llvmValueName(sname)
                                           << " = call " << callRetTy << " @"
                                           << callee << "(";
                                    for (size_t ai = 0; ai < call.args.size();
                                        ++ai) {
                                        if (ai != 0) {
                                            output << ", ";
                                        }
                                        const std::string argType
                                            = (sigIt != sigs.end()
                                                  && ai < sigIt->second
                                                          .paramTypes.size())
                                            ? sigIt->second.paramTypes[ai]
                                            : "i32";
                                        output << argType << " "
                                               << emitValueOperand(
                                                      call.args[ai], program);
                                    }
                                    output << ")\n";
                                },
                                [&](const yesod::Ref<koopa_ir::ConversionExpr>&
                                        convRef) {
                                    const auto& conv = program[convRef];
                                    output
                                        << "  " << llvmValueName(sname)
                                        << " = add i32 "
                                        << emitValueOperand(conv.value, program)
                                        << ", 0\n";
                                },
                                [&](const yesod::Ref<koopa_ir::CopyExpr>&
                                        copyRef) {
                                    const auto& copy = program[copyRef];
                                    output
                                        << "  " << llvmValueName(sname)
                                        << " = add i32 "
                                        << emitValueOperand(copy.value, program)
                                        << ", 0\n";
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
                            output << "  store " << elemTy << " ";
                            MATCH(store.value)
                            WITH(
                                [&](const koopa_ir::Symbol& sv) {
                                    output << llvmValueName(sv.spelling);
                                },
                                [&](const koopa_ir::IntegerLiteral& sv) {
                                    output << sv.value;
                                },
                                [&](const koopa_ir::UndefValue&) {
                                    output << "undef";
                                },
                                [&](const koopa_ir::ZeroInit&) {
                                    output << "zeroinitializer";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::AggregateInitializer>&) {
                                    output << "zeroinitializer";
                                });
                            output << ", ptr "
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
                            output << "  call " << callRetTy << " @" << callee
                                   << "(";
                            for (size_t ai = 0; ai < call.args.size(); ++ai) {
                                if (ai != 0) {
                                    output << ", ";
                                }
                                const std::string argType
                                    = (sigIt != sigs.end()
                                          && ai
                                              < sigIt->second.paramTypes.size())
                                    ? sigIt->second.paramTypes[ai]
                                    : "i32";
                                output
                                    << argType << " "
                                    << emitValueOperand(call.args[ai], program);
                            }
                            output << ")\n";
                        }
                    },
                    stmt);
            }

            // ── Terminator ─────────────────────────────────────────────
            MATCH(block.terminator)
            WITH(
                [&](const yesod::Ref<koopa_ir::JumpTerminator>& jumpRef) {
                    const auto& jump = program[jumpRef];
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
                    if (needsSameTargetBranchSplit(br)) {
                        const std::string trueEdgeLabel
                            = edgeBlockLabel(llvmLabel, "true");
                        const std::string falseEdgeLabel
                            = edgeBlockLabel(llvmLabel, "false");
                        output << "  br i1 %" << condName << ", label %"
                               << trueEdgeLabel << ", label %" << falseEdgeLabel
                               << "\n";
                        output << trueEdgeLabel << ":\n";
                        output << "  br label %"
                               << blockLabels[br.trueTarget.spelling] << "\n";
                        output << falseEdgeLabel << ":\n";
                        output << "  br label %"
                               << blockLabels[br.falseTarget.spelling] << "\n";
                        return;
                    }
                    output << "  br i1 %" << condName << ", label %"
                           << blockLabels[br.trueTarget.spelling] << ", label %"
                           << blockLabels[br.falseTarget.spelling] << "\n";
                },
                [&](const yesod::Ref<koopa_ir::ReturnTerminator>& retRef) {
                    const auto& ret = program[retRef];
                    if (ret.value.has_value()) {
                        output << "  ret i32 "
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
