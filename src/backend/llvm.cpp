#include "backend/llvm.h"

#include "koopa/ir.h"
#include "utils.h"

#include <algorithm>
#include <functional>
#include <map>
#include <ostream>
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
        output << "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:"
                  "64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n";
        output << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
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
        int32_t helperTempId = 0;

        auto nextHelperName = [&](const std::string& stem) -> std::string {
            return "%" + stem + "_" + std::to_string(helperTempId++);
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

        const bool enableFieldHelpers
            = std::ranges::any_of(valueTypes, [](const auto& item) -> bool {
                  return item.second == POLY_TYPE || item.second == PV_TYPE;
              });

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
            const std::string paramType = emitType(param.type, program);
            output << paramType << " ";
            if (paramType == POLY_TYPE) {
                output << llvmValueName(param.symbol.spelling) << "_param";
            } else {
                output << llvmValueName(param.symbol.spelling);
            }
        }
        output << ") {\n";

        auto emitStackValue
            = [&](const std::string& type, const std::string& value,
                  const std::string& stem) -> std::string {
            const std::string slot = nextHelperName(stem + "_slot");
            output << "  " << slot << " = alloca " << type << "\n";
            output << "  store " << type << " " << value << ", ptr " << slot
                   << "\n";
            return slot;
        };

        auto emitAggregateLoad
            = [&](const std::string& result, const std::string& type,
                  const std::string& slot) -> void {
            output << "  " << result << " = load " << type << ", ptr " << slot
                   << "\n";
        };

        auto emitPolyCloneTo
            = [&](const std::string& result, const std::string& value) -> void {
            const std::string input
                = emitStackValue(std::string(POLY_TYPE), value, "poly_in");
            const std::string outputSlot = nextHelperName("poly_out");
            output << "  " << outputSlot << " = alloca " << POLY_TYPE << "\n";
            output << "  call void @__yesod_poly_clone(ptr " << outputSlot
                   << ", ptr " << input << ")\n";
            emitAggregateLoad(result, std::string(POLY_TYPE), outputSlot);
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
            output << "  " << s0 << " = insertvalue " << POLY_TYPE
                   << " undef, ptr null, 0\n";
            output << "  " << s1 << " = insertvalue " << POLY_TYPE << " " << s0
                   << ", i32 0, 1\n";
            output << "  " << s2 << " = insertvalue " << POLY_TYPE << " " << s1
                   << ", i32 0, 2\n";
            output << "  " << result << " = insertvalue " << POLY_TYPE << " "
                   << s2 << ", i32 0, 3\n";
        };

        auto emitPolyHelperTo
            = [&](const std::string& result, const std::string& callee,
                  const std::string& args) -> void {
            const std::string outputSlot = nextHelperName("poly_out");
            output << "  " << outputSlot << " = alloca " << POLY_TYPE << "\n";
            output << "  call void @" << callee << "(ptr " << outputSlot;
            if (!args.empty()) {
                output << ", " << args;
            }
            output << ")\n";
            emitAggregateLoad(result, std::string(POLY_TYPE), outputSlot);
        };

        auto emitPvHelperTo
            = [&](const std::string& result, const std::string& callee,
                  const std::string& args) -> void {
            const std::string outputSlot = nextHelperName("pv_out");
            output << "  " << outputSlot << " = alloca " << PV_TYPE << "\n";
            output << "  call void @" << callee << "(ptr " << outputSlot;
            if (!args.empty()) {
                output << ", " << args;
            }
            output << ")\n";
            emitAggregateLoad(result, std::string(PV_TYPE), outputSlot);
        };

        auto emitPolyPtr = [&](const std::string& value) -> std::string {
            return emitStackValue(std::string(POLY_TYPE), value, "poly_arg");
        };

        auto emitPvPtr = [&](const std::string& value) -> std::string {
            return emitStackValue(std::string(PV_TYPE), value, "pv_arg");
        };

        auto cloneCallArgument = [&](const koopa_ir::Value& value,
                                     const std::string& type) -> std::string {
            const std::string operand = emitValueOperand(value, program);
            if (type == POLY_TYPE) {
                return emitPolyCloneValue(operand);
            }
            return operand;
        };

        std::function<std::pair<std::string, std::string>(
            yesod::Ref<koopa_ir::PointwiseNode>)>
            emitPointwiseNode = [&](yesod::Ref<koopa_ir::PointwiseNode> nodeRef)
            -> std::pair<std::string, std::string> {
            const auto& node = program[nodeRef];
            return MATCH(node.kind) WITH(
                [&](const koopa_ir::PointwiseLeaf& leaf)
                    -> std::pair<std::string, std::string> {
                    if (leaf.kind == koopa_ir::PointwiseLeafKind::mint) {
                        return { "i32", emitValueOperand(leaf.value, program) };
                    }
                    return { std::string(PV_TYPE),
                        emitValueOperand(leaf.value, program) };
                },
                [&](const koopa_ir::PointwiseBinary& binary)
                    -> std::pair<std::string, std::string> {
                    const auto lhs = emitPointwiseNode(binary.lhs);
                    const auto rhs = emitPointwiseNode(binary.rhs);
                    const std::string result = nextHelperName("pv");
                    const std::string lhsPtr = emitPvPtr(lhs.second);
                    if (binary.op == koopa_ir::PvBinaryOp::times) {
                        emitPvHelperTo(result, "__yesod_pv_times",
                            "ptr " + lhsPtr + ", i32 " + rhs.second);
                        return { std::string(PV_TYPE), result };
                    }
                    const std::string rhsPtr = emitPvPtr(rhs.second);
                    const char* callee = "__yesod_pv_add";
                    if (binary.op == koopa_ir::PvBinaryOp::sub) {
                        callee = "__yesod_pv_sub";
                    } else if (binary.op == koopa_ir::PvBinaryOp::mul) {
                        callee = "__yesod_pv_mul";
                    }
                    emitPvHelperTo(
                        result, callee, "ptr " + lhsPtr + ", ptr " + rhsPtr);
                    return { std::string(PV_TYPE), result };
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
            std::vector<std::pair<std::string, std::string>> phiClones;
            for (size_t pi = 0; pi < block.params.size(); ++pi) {
                const auto& bp = program[block.params[pi]];
                const std::string paramType = emitType(bp.type, program);
                const std::string paramName = llvmValueName(bp.symbol.spelling);
                const bool clonePhi = paramType == POLY_TYPE;
                const std::string phiName
                    = clonePhi ? paramName + "_phi" : paramName;

                output << "  " << phiName << " = phi " << paramType;
                for (size_t ei = 0; ei < slots[pi].size(); ++ei) {
                    const auto& edge = slots[pi][ei];
                    if (ei != 0) {
                        output << ",";
                    }
                    output << " [ " << emitValueOperand(edge.arg, program)
                           << ", %" << edge.predLabel << " ]";
                }
                output << "\n";
                if (clonePhi) {
                    phiClones.emplace_back(paramName, phiName);
                }
            }
            for (const auto& clone : phiClones) {
                emitPolyCloneTo(clone.first, clone.second);
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
                                    const std::string rhs_bare
                                        = emitValueOperand(bin.rhs, program);
                                    using BOp = koopa_ir::BinaryOp;
                                    switch (bin.op) {
                                    case BOp::add:
                                        if (enableFieldHelpers
                                            && (logicalTypeOf(bin.lhs) == "mint"
                                                || logicalTypeOf(bin.rhs)
                                                    == "mint")) {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = call i32 "
                                                      "@__yesod_field_add("
                                                   << lhs_typed << ", i32 "
                                                   << rhs_bare << ")\n";
                                        } else {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = add " << lhs_typed
                                                   << ", " << rhs_bare << "\n";
                                        }
                                        break;
                                    case BOp::sub:
                                        if (enableFieldHelpers
                                            && (logicalTypeOf(bin.lhs) == "mint"
                                                || logicalTypeOf(bin.rhs)
                                                    == "mint")) {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = call i32 "
                                                      "@__yesod_field_sub("
                                                   << lhs_typed << ", i32 "
                                                   << rhs_bare << ")\n";
                                        } else {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = sub " << lhs_typed
                                                   << ", " << rhs_bare << "\n";
                                        }
                                        break;
                                    case BOp::mul:
                                        if (enableFieldHelpers
                                            && (logicalTypeOf(bin.lhs) == "mint"
                                                || logicalTypeOf(bin.rhs)
                                                    == "mint")) {
                                            output << "  " << sname
                                                   << "_raw_mint_mul = mul "
                                                   << lhs_typed << ", "
                                                   << rhs_bare << "\n";
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = call i32 "
                                                      "@__yesod_field_mul("
                                                   << lhs_typed << ", i32 "
                                                   << rhs_bare << ")\n";
                                        } else {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = mul " << lhs_typed
                                                   << ", " << rhs_bare << "\n";
                                        }
                                        break;
                                    case BOp::div:
                                        if (enableFieldHelpers
                                            && (logicalTypeOf(bin.lhs) == "mint"
                                                || logicalTypeOf(bin.rhs)
                                                    == "mint")) {
                                            output << "  "
                                                   << llvmValueName(sname)
                                                   << " = call i32 "
                                                      "@__yesod_field_div("
                                                   << lhs_typed << ", i32 "
                                                   << rhs_bare << ")\n";
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
                                    std::vector<
                                        std::pair<std::string, std::string>>
                                        args;
                                    args.reserve(call.args.size());
                                    for (size_t ai = 0; ai < call.args.size();
                                        ++ai) {
                                        const std::string argType
                                            = (sigIt != sigs.end()
                                                  && ai < sigIt->second
                                                          .paramTypes.size())
                                            ? sigIt->second.paramTypes[ai]
                                            : "i32";
                                        args.emplace_back(argType,
                                            cloneCallArgument(
                                                call.args[ai], argType));
                                    }
                                    output << "  " << llvmValueName(sname)
                                           << " = call " << callRetTy << " @"
                                           << callee << "(";
                                    for (size_t ai = 0; ai < args.size();
                                        ++ai) {
                                        if (ai != 0) {
                                            output << ", ";
                                        }
                                        output << args[ai].first << " "
                                               << args[ai].second;
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
                                    const int32_t index
                                        = (getAttr.attr
                                                  == koopa_ir::PolyAttr::base
                                              || getAttr.attr
                                                  == koopa_ir::PolyAttr::addr)
                                        ? 0
                                        : (getAttr.attr == koopa_ir::PolyAttr::l
                                                  ? 2
                                                  : 3);
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
                                    const std::string input
                                        = emitPolyPtr(emitValueOperand(
                                            setAttr.value, program));
                                    const std::string value = emitValueOperand(
                                        setAttr.attrValue, program);
                                    if (setAttr.attr == koopa_ir::PolyAttr::l) {
                                        emitPolyHelperTo(llvmValueName(sname),
                                            "__yesod_poly_set_l",
                                            "ptr " + input + ", i32 " + value);
                                    } else if (setAttr.attr
                                        == koopa_ir::PolyAttr::r) {
                                        emitPolyHelperTo(llvmValueName(sname),
                                            "__yesod_poly_set_r",
                                            "ptr " + input + ", i32 " + value);
                                    } else {
                                        emitPolyHelperTo(llvmValueName(sname),
                                            "__yesod_poly_set_ptr",
                                            "ptr " + input + ", ptr " + value);
                                    }
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
                                    const std::string input = emitPolyPtr(
                                        emitValueOperand(ntt.value, program));
                                    emitPvHelperTo(llvmValueName(sname),
                                        "__yesod_poly_ntt",
                                        "ptr " + input + ", i32 "
                                            + emitValueOperand(
                                                ntt.length, program));
                                },
                                [&](const yesod::Ref<koopa_ir::PointwiseExpr>&
                                        pointwiseRef) {
                                    const auto& pointwise
                                        = program[pointwiseRef];
                                    const auto pv
                                        = emitPointwiseNode(pointwise.root);
                                    const std::string pvPtr
                                        = emitPvPtr(pv.second);
                                    emitPolyHelperTo(llvmValueName(sname),
                                        "__yesod_poly_from_pointwise",
                                        "ptr " + pvPtr + ", i32 "
                                            + emitValueOperand(
                                                pointwise.activeL, program)
                                            + ", i32 "
                                            + emitValueOperand(
                                                pointwise.activeR, program));
                                },
                                [&](const yesod::Ref<koopa_ir::CombineExpr>&
                                        combineRef) {
                                    const auto& combine = program[combineRef];
                                    std::string acc
                                        = nextHelperName("poly_acc");
                                    emitPolyZeroTo(acc);
                                    for (const auto& term : combine.terms) {
                                        const std::string result
                                            = nextHelperName("poly_acc");
                                        const std::string accPtr
                                            = emitPolyPtr(acc);
                                        const std::string srcPtr
                                            = emitPolyPtr(emitValueOperand(
                                                term.value, program));
                                        const std::string endValue
                                            = term.end.has_value()
                                            ? emitValueOperand(
                                                  *term.end, program)
                                            : std::to_string(INF_END);
                                        emitPolyHelperTo(result,
                                            "__yesod_poly_combine_term",
                                            "ptr " + accPtr + ", ptr " + srcPtr
                                                + ", i32 "
                                                + emitValueOperand(
                                                    term.start, program)
                                                + ", i32 " + endValue + ", i32 "
                                                + emitValueOperand(
                                                    term.shift, program)
                                                + ", i32 "
                                                + emitValueOperand(
                                                    term.scale, program));
                                        acc = result;
                                    }
                                    emitPolyCloneTo(llvmValueName(sname), acc);
                                },
                                [&](const yesod::Ref<koopa_ir::GetCoeffExpr>&
                                        getCoeffRef) {
                                    const auto& getCoeff = program[getCoeffRef];
                                    const std::string input
                                        = emitPolyPtr(emitValueOperand(
                                            getCoeff.value, program));
                                    output << "  " << llvmValueName(sname)
                                           << " = call i32 "
                                              "@__yesod_poly_getcoeff(ptr "
                                           << input << ", i32 "
                                           << emitValueOperand(
                                                  getCoeff.index, program)
                                           << ")\n";
                                },
                                [&](const yesod::Ref<
                                    koopa_ir::PolyConstructExpr>&
                                        constructRef) {
                                    const auto& construct
                                        = program[constructRef];
                                    const std::string arrayType = "["
                                        + std::to_string(
                                            construct.elements.size())
                                        + " x i32]";
                                    const std::string slot
                                        = nextHelperName("poly_construct");
                                    output << "  " << slot << " = alloca "
                                           << arrayType << "\n";
                                    for (size_t i = 0;
                                        i < construct.elements.size(); ++i) {
                                        const std::string elementPtr
                                            = nextHelperName("poly_coeff_ptr");
                                        output << "  " << elementPtr
                                               << " = getelementptr "
                                               << arrayType << ", ptr " << slot
                                               << ", i32 0, i32 " << i << "\n";
                                        output << "  store i32 "
                                               << emitValueOperand(
                                                      construct.elements[i],
                                                      program)
                                               << ", ptr " << elementPtr
                                               << "\n";
                                    }
                                    const std::string dataPtr
                                        = construct.elements.empty()
                                        ? "null"
                                        : nextHelperName("poly_data");
                                    if (!construct.elements.empty()) {
                                        output << "  " << dataPtr
                                               << " = getelementptr "
                                               << arrayType << ", ptr " << slot
                                               << ", i32 0, i32 0\n";
                                    }
                                    emitPolyHelperTo(llvmValueName(sname),
                                        "__yesod_poly_construct",
                                        "ptr " + dataPtr + ", i32 "
                                            + std::to_string(
                                                construct.elements.size()));
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
                            MATCH(store.value)
                            WITH(
                                [&](const koopa_ir::Symbol& sv) {
                                    storeValue = llvmValueName(sv.spelling);
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
                                storeValue = emitPolyCloneValue(storeValue);
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
                            std::vector<std::pair<std::string, std::string>>
                                args;
                            args.reserve(call.args.size());
                            for (size_t ai = 0; ai < call.args.size(); ++ai) {
                                const std::string argType
                                    = (sigIt != sigs.end()
                                          && ai
                                              < sigIt->second.paramTypes.size())
                                    ? sigIt->second.paramTypes[ai]
                                    : "i32";
                                args.emplace_back(argType,
                                    cloneCallArgument(call.args[ai], argType));
                            }
                            output << "  call " << callRetTy << " @" << callee
                                   << "(";
                            for (size_t ai = 0; ai < args.size(); ++ai) {
                                if (ai != 0) {
                                    output << ", ";
                                }
                                output << args[ai].first << " "
                                       << args[ai].second;
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
