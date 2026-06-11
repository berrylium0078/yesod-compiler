#ifndef _YESOD_KOOPA_IR_H_
#define _YESOD_KOOPA_IR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "utils.h"

namespace yesod::koopa::ir {

using yesod::Arena;
using yesod::Ptr;
using yesod::Ref;

/*
 * EBNF sketch preserved from doc/koopaIR.md:
 *
 * Type ::= "i32" | ArrayType | PointerType | FunType;
 * ArrayType ::= "[" Type "," INT "]";
 * PointerType ::= "*" Type;
 * FunType ::= "(" [Type {"," Type}] ")" [":" Type];
 *
 * Value ::= SYMBOL | INT | "undef";
 * Initializer ::= INT | "undef" | Aggregate | "zeroinit";
 * Aggregate ::= "{" Initializer {"," Initializer} "}";
 *
 * SymbolDef ::= SYMBOL "=" (MemoryDeclaration | Load | GetPointer |
 *     GetElementPointer | BinaryExpr | FunCall);
 * GlobalSymbolDef ::= "global" SYMBOL "=" GlobalMemoryDeclaration;
 * MemoryDeclaration ::= "alloc" Type;
 * GlobalMemoryDeclaration ::= "alloc" Type "," Initializer;
 * Load ::= "load" SYMBOL;
 * Store ::= "store" (Value | Initializer) "," SYMBOL;
 * GetPointer ::= "getptr" SYMBOL "," Value;
 * GetElementPointer ::= "getelemptr" SYMBOL "," Value;
 * BinaryExpr ::= BINARY_OP Value "," Value;
 * Branch ::= "br" Value "," SYMBOL [BlockArgList] "," SYMBOL [BlockArgList];
 * Jump ::= "jump" SYMBOL [BlockArgList];
 * Return ::= "ret" [Value];
 * BlockArgList ::= "(" Value {"," Value} ")";
 * Block ::= SYMBOL [BlockParamList] ":" {Statement} EndStatement;
 * BlockParamList ::= "(" SYMBOL ":" Type {"," SYMBOL ":" Type} ")";
 * FunDecl ::= "decl" SYMBOL "(" [Type {"," Type}] ")" [":" Type];
 * FunDef ::= "fun" SYMBOL "(" [SYMBOL ":" Type {"," SYMBOL ":" Type}] ")"
 *     [":" Type] "{" {Block} "}";
 *
 * Representation decisions:
 * - Recursive types and aggregate initializers use Ref<T> into Program-owned
 * arenas.
 * - Non-recursive leaves (symbols, literals, annotations) stay embedded by
 * value.
 * - Statements and top-level items use std::variant to preserve grammar
 * alternatives.
 * - Optional return types and return values use std::optional because they are
 * not AST links.
 */

struct SourcePos {
    constexpr SourcePos() = default;

    constexpr explicit SourcePos(int32_t offset)
        : m_offset(offset)
    {
    }

    int32_t m_offset = 0;
};

struct AnnotationField {
    SourcePos sourcePos;
    std::string name;
    std::optional<std::string> value;
};

using AnnotationList = std::vector<AnnotationField>;

struct Symbol {
    SourcePos sourcePos;
    std::string spelling;
};

struct IntegerLiteral {
    SourcePos sourcePos;
    int32_t value = 0;
};

struct UndefValue {
    SourcePos sourcePos;
};

struct ZeroInit {
    SourcePos sourcePos;
};

struct I32Type {
    SourcePos sourcePos;
};

struct MintType {
    SourcePos sourcePos;
};

struct PolyType {
    SourcePos sourcePos;
};

struct PvType {
    SourcePos sourcePos;
};

struct ArrayType;
struct PointerType;
struct FunctionType;
struct AggregateInitializer;
struct MemoryDeclaration;
struct LoadExpr;
struct GetPointerExpr;
struct GetElementPointerExpr;
struct BinaryExpr;
struct CallExpr;
struct UnaryPolyExpr;
struct PvBinaryExpr;
struct CombineExpr;
struct GetCoeffExpr;
struct PolyConstructExpr;
struct ConversionExpr;
struct SymbolDef;
struct StoreStmt;
struct BranchTerminator;
struct JumpTerminator;
struct ReturnTerminator;
struct BlockParameter;
struct BasicBlock;
struct FunctionParameter;
struct FunctionDecl;
struct FunctionDef;
struct GlobalMemoryDef;
struct Program;

using Type = std::variant<I32Type, MintType, PolyType, PvType, Ref<ArrayType>,
    Ref<PointerType>, Ref<FunctionType>>;
using Value = std::variant<Symbol, IntegerLiteral, UndefValue>;
using Initializer = std::variant<IntegerLiteral, UndefValue, ZeroInit,
    Ref<AggregateInitializer>>;
using StoreValue = std::variant<Symbol, IntegerLiteral, UndefValue, ZeroInit,
    Ref<AggregateInitializer>>;

enum class UnaryPolyOp {
    ntt,
    intt,
};

enum class PvBinaryOp {
    add,
    sub,
    mul,
};

enum class ConversionOp {
    int2mint,
    mint2int,
};

enum class BinaryOp {
    ne,
    eq,
    gt,
    lt,
    ge,
    le,
    add,
    sub,
    mul,
    div,
    mod,
    bitAnd,
    bitOr,
    bitXor,
    shl,
    shr,
    sar,
};

struct ArrayType {
    SourcePos sourcePos;
    Type elementType;
    int32_t length = 0;
};

struct PointerType {
    SourcePos sourcePos;
    Type pointeeType;
};

struct FunctionType {
    SourcePos sourcePos;
    std::vector<Type> paramTypes;
    std::optional<Type> returnType;
};

struct AggregateInitializer {
    SourcePos sourcePos;
    std::vector<Initializer> elements;
};

struct MemoryDeclaration {
    SourcePos sourcePos;
    Type allocType;
    AnnotationList annotations;
};

struct LoadExpr {
    SourcePos sourcePos;
    Symbol source;
    AnnotationList annotations;
};

struct GetPointerExpr {
    SourcePos sourcePos;
    Symbol source;
    Value index;
    AnnotationList annotations;
};

struct GetElementPointerExpr {
    SourcePos sourcePos;
    Symbol source;
    Value index;
    AnnotationList annotations;
};

struct BinaryExpr {
    SourcePos sourcePos;
    BinaryOp op = BinaryOp::add;
    Value lhs;
    Value rhs;
    AnnotationList annotations;
};

struct CallExpr {
    SourcePos sourcePos;
    Symbol callee;
    std::vector<Value> args;
    AnnotationList annotations;
};

struct UnaryPolyExpr {
    SourcePos sourcePos;
    UnaryPolyOp op = UnaryPolyOp::ntt;
    Value value;
    AnnotationList annotations;
};

struct PvBinaryExpr {
    SourcePos sourcePos;
    PvBinaryOp op = PvBinaryOp::add;
    Value lhs;
    Value rhs;
    AnnotationList annotations;
};

struct CombineTerm {
    Value value;
    Value start;
    std::optional<Value> end;
    Value shift;
    Value scale;
};

struct CombineExpr {
    SourcePos sourcePos;
    std::vector<CombineTerm> terms;
    AnnotationList annotations;
};

struct GetCoeffExpr {
    SourcePos sourcePos;
    Value value;
    Value index;
    AnnotationList annotations;
};

struct PolyConstructExpr {
    SourcePos sourcePos;
    std::vector<Value> elements;
    AnnotationList annotations;
};

struct ConversionExpr {
    SourcePos sourcePos;
    ConversionOp op = ConversionOp::int2mint;
    Value value;
    AnnotationList annotations;
};

using SymbolRhs = std::variant<Ref<MemoryDeclaration>, Ref<LoadExpr>,
    Ref<GetPointerExpr>, Ref<GetElementPointerExpr>, Ref<BinaryExpr>,
    Ref<CallExpr>, Ref<UnaryPolyExpr>, Ref<PvBinaryExpr>, Ref<CombineExpr>,
    Ref<GetCoeffExpr>, Ref<PolyConstructExpr>, Ref<ConversionExpr>>;

struct SymbolDef {
    SourcePos sourcePos;
    Symbol symbol;
    SymbolRhs rhs;
    AnnotationList annotations;
};

struct StoreStmt {
    SourcePos sourcePos;
    StoreValue value;
    Symbol destination;
    AnnotationList annotations;
};

using Statement = std::variant<Ref<SymbolDef>, Ref<StoreStmt>, Ref<CallExpr>>;

struct BranchTerminator {
    SourcePos sourcePos;
    Value condition;
    Symbol trueTarget;
    std::vector<Value> trueArgs;
    Symbol falseTarget;
    std::vector<Value> falseArgs;
    AnnotationList annotations;
};

struct JumpTerminator {
    SourcePos sourcePos;
    Symbol target;
    std::vector<Value> args;
    AnnotationList annotations;
};

struct ReturnTerminator {
    SourcePos sourcePos;
    std::optional<Value> value;
    AnnotationList annotations;
};

using Terminator = std::variant<Ref<BranchTerminator>, Ref<JumpTerminator>,
    Ref<ReturnTerminator>>;

struct BlockParameter {
    SourcePos sourcePos;
    Symbol symbol;
    Type type;
    AnnotationList annotations;
};

struct BasicBlock {
    SourcePos sourcePos;
    Symbol label;
    std::vector<Ref<BlockParameter>> params;
    std::vector<Statement> statements;
    Terminator terminator;
    AnnotationList annotations;
};

struct FunctionParameter {
    SourcePos sourcePos;
    Symbol symbol;
    Type type;
    AnnotationList annotations;
};

struct FunctionDecl {
    SourcePos sourcePos;
    Symbol name;
    std::vector<Type> paramTypes;
    std::optional<Type> returnType;
    AnnotationList annotations;
};

struct FunctionDef {
    SourcePos sourcePos;
    Symbol name;
    std::vector<Ref<FunctionParameter>> params;
    std::optional<Type> returnType;
    std::vector<Ref<BasicBlock>> blocks;
    AnnotationList annotations;
};

struct GlobalMemoryDef {
    SourcePos sourcePos;
    Symbol name;
    Type allocType;
    Initializer initializer;
    AnnotationList annotations;
};

using TopLevelItem
    = std::variant<Ref<GlobalMemoryDef>, Ref<FunctionDecl>, Ref<FunctionDef>>;

struct Program {
    using NodeArena = Arena<ArrayType, PointerType, FunctionType,
        AggregateInitializer, MemoryDeclaration, LoadExpr, GetPointerExpr,
        GetElementPointerExpr, BinaryExpr, CallExpr, UnaryPolyExpr,
        PvBinaryExpr, CombineExpr, GetCoeffExpr, PolyConstructExpr,
        ConversionExpr, SymbolDef, StoreStmt, BranchTerminator, JumpTerminator,
        ReturnTerminator, BlockParameter, BasicBlock, FunctionParameter,
        FunctionDecl, FunctionDef, GlobalMemoryDef>;

    SourcePos sourcePos;
    AnnotationList annotations;
    std::vector<TopLevelItem> items;

    template <typename T, typename... Args> Ref<T> alloc(Args&&... args)
    {
        return m_nodes.alloc<T>(std::forward<Args>(args)...);
    }

    template <typename T> T& operator[](Ref<T> handle)
    {
        return m_nodes[handle];
    }

    template <typename T> const T& operator[](Ref<T> handle) const
    {
        return m_nodes[handle];
    }

    void clear();

private:
    NodeArena m_nodes;
};

std::string_view toString(BinaryOp op);
std::string_view toString(UnaryPolyOp op);
std::string_view toString(PvBinaryOp op);
std::string_view toString(ConversionOp op);
bool hasReturnType(const FunctionType& type);
bool hasReturnValue(const ReturnTerminator& terminator);
bool usesSsaExtension(const BranchTerminator& terminator);
bool usesSsaExtension(const JumpTerminator& terminator);
bool usesSsaExtension(const BasicBlock& block, const Program& program);
void validate(const Program& program);
std::string serializeToKoopa(const Program& program);

} // namespace yesod::koopa::ir

#endif // _YESOD_KOOPA_IR_H_