#ifndef _YESOD_FRONTEND_AST_H_
#define _YESOD_FRONTEND_AST_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "frontend/arena.h"

namespace yesod::frontend {

struct SourcePos {
    constexpr SourcePos() = default;

    explicit constexpr SourcePos(int32_t offset)
        : m_offset(offset)
    {
    }

    int32_t m_offset = 0;
};

enum class FuncTypeKeyword {
    voidKeyword,
    intKeyword,
};

enum class BTypeKeyword {
    intKeyword,
};

enum class UnaryOpKeyword {
    plus,
    minus,
    bang,
};

enum class BinaryOpKeyword {
    star,
    slash,
    percent,
    plus,
    minus,
    less,
    greater,
    lessEqual,
    greaterEqual,
    equal,
    notEqual,
    andAnd,
    orOr,
};

struct Exp;
struct LVal;
struct Number;
struct ConstInitVal;
struct InitVal;
struct ConstDef;
struct VarDef;
struct ConstDecl;
struct VarDecl;
struct DeclNode;
struct FuncFParam;
struct IfStmt;
struct WhileStmt;
struct BreakStmt;
struct ContinueStmt;
struct AssignStmt;
struct ExpStmt;
struct ReturnStmt;
struct StmtNode;
struct BlockItemNode;
struct Block;
struct TopLevelItemNode;

struct Identifier {
    Identifier() = default;

    Identifier(int32_t startOffset, std::string name)
        : m_sourcePos(startOffset)
        , m_name(std::move(name))
    {
    }

    Identifier(SourcePos sourcePos, std::string name)
        : m_sourcePos(sourcePos)
        , m_name(std::move(name))
    {
    }

    SourcePos m_sourcePos;
    std::string m_name;
};

struct Number {
    Number() = default;

    explicit Number(int32_t value)
        : m_value(value)
    {
    }

    explicit Number(SourcePos, int32_t value)
        : m_value(value)
    {
    }

    int32_t m_value = 0;
};

struct LVal {
    LVal() = default;

    explicit LVal(Handle<Identifier> identifier_nn)
        : m_identifier_nn(identifier_nn)
    {
    }

    explicit LVal(SourcePos, Handle<Identifier> identifier_nn)
        : m_identifier_nn(identifier_nn)
    {
    }

    Handle<Identifier> m_identifier_nn;
};

struct Exp {
    struct Binary {
        Handle<Exp> m_lhs_nn;
        Handle<Exp> m_rhs_nn;
        BinaryOpKeyword m_op = BinaryOpKeyword::plus;
    };

    struct Unary {
        Handle<Exp> m_lhs_nn;
        UnaryOpKeyword m_op = UnaryOpKeyword::plus;
    };

    struct Call {
        Handle<Identifier> m_func_nn;
        std::vector<Handle<Exp>> m_params;
    };

    using Kind = std::variant<Binary, Unary, Call, LVal, Number>;

    Exp()
        : m_kind(Number {})
    {
    }

    Exp(int32_t startOffset, Kind kind)
        : m_sourcePos(startOffset)
        , m_kind(std::move(kind))
    {
    }

    Exp(SourcePos sourcePos, Kind kind)
        : m_sourcePos(sourcePos)
        , m_kind(std::move(kind))
    {
    }

    SourcePos m_sourcePos;
    Kind m_kind;
};

struct ConstInitVal {
    ConstInitVal() = default;

    ConstInitVal(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_exp_nn(exp_nn)
    {
    }

    ConstInitVal(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

struct InitVal {
    InitVal() = default;

    InitVal(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_exp_nn(exp_nn)
    {
    }

    InitVal(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

struct ConstDef {
    ConstDef() = default;

    ConstDef(int32_t startOffset, Handle<Identifier> identifier_nn,
        Handle<ConstInitVal> constInitVal_nn)
        : m_sourcePos(startOffset)
        , m_identifier_nn(identifier_nn)
        , m_constInitVal_nn(constInitVal_nn)
    {
    }

    ConstDef(SourcePos sourcePos, Handle<Identifier> identifier_nn,
        Handle<ConstInitVal> constInitVal_nn)
        : m_sourcePos(sourcePos)
        , m_identifier_nn(identifier_nn)
        , m_constInitVal_nn(constInitVal_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Identifier> m_identifier_nn;
    Handle<ConstInitVal> m_constInitVal_nn;
};

struct VarDef {
    VarDef() = default;

    VarDef(int32_t startOffset, Handle<Identifier> identifier_nn,
        Handle<InitVal> initVal_nn)
        : m_sourcePos(startOffset)
        , m_identifier_nn(identifier_nn)
        , m_initVal_nn(initVal_nn)
    {
    }

    VarDef(SourcePos sourcePos, Handle<Identifier> identifier_nn,
        Handle<InitVal> initVal_nn)
        : m_sourcePos(sourcePos)
        , m_identifier_nn(identifier_nn)
        , m_initVal_nn(initVal_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Identifier> m_identifier_nn;
    Handle<InitVal> m_initVal_nn;
};

struct ConstDecl {
    ConstDecl() = default;

    ConstDecl(int32_t startOffset, BTypeKeyword bType,
        std::vector<Handle<ConstDef>> constDefs)
        : m_sourcePos(startOffset)
        , m_bType(bType)
        , m_constDefs(std::move(constDefs))
    {
    }

    ConstDecl(SourcePos sourcePos, BTypeKeyword bType,
        std::vector<Handle<ConstDef>> constDefs)
        : m_sourcePos(sourcePos)
        , m_bType(bType)
        , m_constDefs(std::move(constDefs))
    {
    }

    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    std::vector<Handle<ConstDef>> m_constDefs;
};

struct VarDecl {
    VarDecl() = default;

    VarDecl(int32_t startOffset, BTypeKeyword bType,
        std::vector<Handle<VarDef>> varDefs)
        : m_sourcePos(startOffset)
        , m_bType(bType)
        , m_varDefs(std::move(varDefs))
    {
    }

    VarDecl(SourcePos sourcePos, BTypeKeyword bType,
        std::vector<Handle<VarDef>> varDefs)
        : m_sourcePos(sourcePos)
        , m_bType(bType)
        , m_varDefs(std::move(varDefs))
    {
    }

    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    std::vector<Handle<VarDef>> m_varDefs;
};

using Decl = std::variant<Handle<ConstDecl>, Handle<VarDecl>>;

struct DeclNode {
    DeclNode() = default;

    DeclNode(int32_t startOffset, Decl decl)
        : m_sourcePos(startOffset)
        , m_decl(std::move(decl))
    {
    }

    DeclNode(SourcePos sourcePos, Decl decl)
        : m_sourcePos(sourcePos)
        , m_decl(std::move(decl))
    {
    }

    SourcePos m_sourcePos;
    Decl m_decl;
};

struct IfStmt {
    IfStmt() = default;

    IfStmt(int32_t startOffset, Handle<Exp> condExp_nn,
        Handle<StmtNode> thenStmt_nn, Handle<StmtNode> elseStmt_nn)
        : m_sourcePos(startOffset)
        , m_condExp_nn(condExp_nn)
        , m_thenStmt_nn(thenStmt_nn)
        , m_elseStmt_nn(elseStmt_nn)
    {
    }

    IfStmt(SourcePos sourcePos, Handle<Exp> condExp_nn,
        Handle<StmtNode> thenStmt_nn, Handle<StmtNode> elseStmt_nn)
        : m_sourcePos(sourcePos)
        , m_condExp_nn(condExp_nn)
        , m_thenStmt_nn(thenStmt_nn)
        , m_elseStmt_nn(elseStmt_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_condExp_nn;
    Handle<StmtNode> m_thenStmt_nn;
    Handle<StmtNode> m_elseStmt_nn;
};

struct WhileStmt {
    WhileStmt() = default;

    WhileStmt(int32_t startOffset, Handle<Exp> condExp_nn,
        Handle<StmtNode> bodyStmt_nn)
        : m_sourcePos(startOffset)
        , m_condExp_nn(condExp_nn)
        , m_bodyStmt_nn(bodyStmt_nn)
    {
    }

    WhileStmt(SourcePos sourcePos, Handle<Exp> condExp_nn,
        Handle<StmtNode> bodyStmt_nn)
        : m_sourcePos(sourcePos)
        , m_condExp_nn(condExp_nn)
        , m_bodyStmt_nn(bodyStmt_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_condExp_nn;
    Handle<StmtNode> m_bodyStmt_nn;
};

struct BreakStmt {
    BreakStmt() = default;

    explicit BreakStmt(int32_t startOffset)
        : m_sourcePos(startOffset)
    {
    }

    explicit BreakStmt(SourcePos sourcePos)
        : m_sourcePos(sourcePos)
    {
    }

    SourcePos m_sourcePos;
};

struct ContinueStmt {
    ContinueStmt() = default;

    explicit ContinueStmt(int32_t startOffset)
        : m_sourcePos(startOffset)
    {
    }

    explicit ContinueStmt(SourcePos sourcePos)
        : m_sourcePos(sourcePos)
    {
    }

    SourcePos m_sourcePos;
};

struct AssignStmt {
    AssignStmt() = default;

    AssignStmt(int32_t startOffset, Handle<Exp> lVal_nn, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_lVal_nn(lVal_nn)
        , m_exp_nn(exp_nn)
    {
    }

    AssignStmt(SourcePos sourcePos, Handle<Exp> lVal_nn, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_lVal_nn(lVal_nn)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_lVal_nn;
    Handle<Exp> m_exp_nn;
};

struct ExpStmt {
    ExpStmt() = default;

    ExpStmt(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_exp_nn(exp_nn)
    {
    }

    ExpStmt(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

struct ReturnStmt {
    ReturnStmt() = default;

    ReturnStmt(int32_t startOffset, Handle<Exp> exp_nn)
        : m_sourcePos(startOffset)
        , m_exp_nn(exp_nn)
    {
    }

    ReturnStmt(SourcePos sourcePos, Handle<Exp> exp_nn)
        : m_sourcePos(sourcePos)
        , m_exp_nn(exp_nn)
    {
    }

    SourcePos m_sourcePos;
    Handle<Exp> m_exp_nn;
};

using Stmt = std::variant<Handle<IfStmt>, Handle<WhileStmt>, Handle<BreakStmt>,
    Handle<ContinueStmt>, Handle<AssignStmt>, Handle<Block>, Handle<ReturnStmt>,
    Handle<ExpStmt>>;

struct StmtNode {
    StmtNode() = default;

    StmtNode(int32_t startOffset, Stmt stmt)
        : m_sourcePos(startOffset)
        , m_stmt(std::move(stmt))
    {
    }

    StmtNode(SourcePos sourcePos, Stmt stmt)
        : m_sourcePos(sourcePos)
        , m_stmt(std::move(stmt))
    {
    }

    SourcePos m_sourcePos;
    Stmt m_stmt;
};

using BlockItem = std::variant<Handle<DeclNode>, Handle<StmtNode>>;

struct BlockItemNode {
    BlockItemNode() = default;

    BlockItemNode(int32_t startOffset, BlockItem blockItem)
        : m_sourcePos(startOffset)
        , m_blockItem(std::move(blockItem))
    {
    }

    BlockItemNode(SourcePos sourcePos, BlockItem blockItem)
        : m_sourcePos(sourcePos)
        , m_blockItem(std::move(blockItem))
    {
    }

    SourcePos m_sourcePos;
    BlockItem m_blockItem;
};

struct Block {
    Block() = default;

    Block(int32_t startOffset, std::vector<Handle<BlockItemNode>> blockItems)
        : m_sourcePos(startOffset)
        , m_blockItems(std::move(blockItems))
    {
    }

    Block(SourcePos sourcePos, std::vector<Handle<BlockItemNode>> blockItems)
        : m_sourcePos(sourcePos)
        , m_blockItems(std::move(blockItems))
    {
    }

    SourcePos m_sourcePos;
    std::vector<Handle<BlockItemNode>> m_blockItems;
};

struct FuncFParam {
    FuncFParam() = default;

    FuncFParam(int32_t startOffset, BTypeKeyword bType,
        Handle<Identifier> identifier_nn)
        : m_sourcePos(startOffset)
        , m_bType(bType)
        , m_identifier_nn(identifier_nn)
    {
    }

    FuncFParam(SourcePos sourcePos, BTypeKeyword bType,
        Handle<Identifier> identifier_nn)
        : m_sourcePos(sourcePos)
        , m_bType(bType)
        , m_identifier_nn(identifier_nn)
    {
    }

    SourcePos m_sourcePos;
    BTypeKeyword m_bType = BTypeKeyword::intKeyword;
    Handle<Identifier> m_identifier_nn;
};

struct FuncDef {
    FuncDef() = default;

    FuncDef(int32_t startOffset, FuncTypeKeyword funcType,
        Handle<Identifier> identifier_nn,
        std::vector<Handle<FuncFParam>> funcFParams, Handle<Block> block_nn)
        : m_sourcePos(startOffset)
        , m_funcType(funcType)
        , m_identifier_nn(identifier_nn)
        , m_funcFParams(std::move(funcFParams))
        , m_block_nn(block_nn)
    {
    }

    FuncDef(SourcePos sourcePos, FuncTypeKeyword funcType,
        Handle<Identifier> identifier_nn,
        std::vector<Handle<FuncFParam>> funcFParams, Handle<Block> block_nn)
        : m_sourcePos(sourcePos)
        , m_funcType(funcType)
        , m_identifier_nn(identifier_nn)
        , m_funcFParams(std::move(funcFParams))
        , m_block_nn(block_nn)
    {
    }

    SourcePos m_sourcePos;
    FuncTypeKeyword m_funcType = FuncTypeKeyword::intKeyword;
    Handle<Identifier> m_identifier_nn;
    std::vector<Handle<FuncFParam>> m_funcFParams;
    Handle<Block> m_block_nn;
};

using TopLevelItem = std::variant<Handle<DeclNode>, Handle<FuncDef>>;

struct TopLevelItemNode {
    TopLevelItemNode() = default;

    TopLevelItemNode(int32_t startOffset, TopLevelItem topLevelItem)
        : m_sourcePos(startOffset)
        , m_topLevelItem(std::move(topLevelItem))
    {
    }

    TopLevelItemNode(SourcePos sourcePos, TopLevelItem topLevelItem)
        : m_sourcePos(sourcePos)
        , m_topLevelItem(std::move(topLevelItem))
    {
    }

    SourcePos m_sourcePos;
    TopLevelItem m_topLevelItem;
};

struct CompUnit {
    CompUnit() = default;

    CompUnit(int32_t startOffset,
        std::vector<Handle<TopLevelItemNode>> topLevelItems)
        : m_sourcePos(startOffset)
        , m_topLevelItems(std::move(topLevelItems))
    {
    }

    CompUnit(SourcePos sourcePos,
        std::vector<Handle<TopLevelItemNode>> topLevelItems)
        : m_sourcePos(sourcePos)
        , m_topLevelItems(std::move(topLevelItems))
    {
    }

    SourcePos m_sourcePos;
    std::vector<Handle<TopLevelItemNode>> m_topLevelItems;
};

class AST {
  public:
    class ScopedCurrent {
      public:
        ScopedCurrent() = default;

        explicit ScopedCurrent(AST& ast_nn)
            : m_previousAst_nn(s_currentAst_nn)
            , m_previousConstAst_nn(s_currentConstAst_nn)
            , m_active(true)
        {
            s_currentAst_nn = &ast_nn;
            s_currentConstAst_nn = &ast_nn;
        }

        explicit ScopedCurrent(const AST& ast_nn)
            : m_previousAst_nn(s_currentAst_nn)
            , m_previousConstAst_nn(s_currentConstAst_nn)
            , m_active(true)
        {
            s_currentAst_nn = nullptr;
            s_currentConstAst_nn = &ast_nn;
        }

        ScopedCurrent(const ScopedCurrent&) = delete;
        ScopedCurrent& operator=(const ScopedCurrent&) = delete;

        ScopedCurrent(ScopedCurrent&& other) noexcept
            : m_previousAst_nn(other.m_previousAst_nn)
            , m_previousConstAst_nn(other.m_previousConstAst_nn)
            , m_active(other.m_active)
        {
            other.m_active = false;
        }

        ScopedCurrent& operator=(ScopedCurrent&& other) noexcept
        {
            if (this == &other) {
                return *this;
            }

            reset();
            m_previousAst_nn = other.m_previousAst_nn;
            m_previousConstAst_nn = other.m_previousConstAst_nn;
            m_active = other.m_active;
            other.m_active = false;
            return *this;
        }

        ~ScopedCurrent() { reset(); }

        void rebind(AST& ast_nn)
        {
            if (!m_active) {
                m_previousAst_nn = s_currentAst_nn;
                m_previousConstAst_nn = s_currentConstAst_nn;
                m_active = true;
            }

            s_currentAst_nn = &ast_nn;
            s_currentConstAst_nn = &ast_nn;
        }

      private:
        void reset()
        {
            if (!m_active) {
                return;
            }

            s_currentAst_nn = m_previousAst_nn;
            s_currentConstAst_nn = m_previousConstAst_nn;
            m_active = false;
        }

        AST* m_previousAst_nn = nullptr;
        const AST* m_previousConstAst_nn = nullptr;
        bool m_active = false;
    };

    template <typename T, typename TAst> class BasicRef {
      public:
        using Pointer = typename std::conditional<std::is_const<TAst>::value,
            const T*, T*>::type;
        using Reference = typename std::conditional<std::is_const<TAst>::value,
            const T&, T&>::type;

        BasicRef() = default;

        BasicRef(TAst* ast_nn, Handle<T> handle)
            : m_ast_nn(ast_nn)
            , m_handle(handle)
        {
        }

        [[nodiscard]] explicit operator bool() const
        {
            return m_ast_nn != nullptr && static_cast<bool>(m_handle);
        }

        [[nodiscard]] Pointer operator->() const
        {
            return &m_ast_nn->get(m_handle);
        }

        [[nodiscard]] Reference operator*() const
        {
            return m_ast_nn->get(m_handle);
        }

        [[nodiscard]] Handle<T> handle() const { return m_handle; }

      private:
        TAst* m_ast_nn = nullptr;
        Handle<T> m_handle {};
    };

    template <typename T> using Ref = BasicRef<T, AST>;
    template <typename T> using ConstRef = BasicRef<T, const AST>;

    using Arenas = std::tuple<Arena<Identifier>, Arena<Exp>,
        Arena<ConstInitVal>, Arena<InitVal>, Arena<ConstDef>, Arena<VarDef>,
        Arena<ConstDecl>, Arena<VarDecl>, Arena<DeclNode>, Arena<FuncFParam>,
        Arena<IfStmt>, Arena<WhileStmt>, Arena<BreakStmt>,
        Arena<ContinueStmt>, Arena<AssignStmt>, Arena<ExpStmt>,
        Arena<ReturnStmt>, Arena<StmtNode>, Arena<BlockItemNode>, Arena<Block>,
        Arena<FuncDef>, Arena<TopLevelItemNode>, Arena<CompUnit>>;

    template <typename T> [[nodiscard]] Arena<T>& arena()
    {
        return std::get<Arena<T>>(m_arenas);
    }

    template <typename T> [[nodiscard]] const Arena<T>& arena() const
    {
        return std::get<Arena<T>>(m_arenas);
    }

    template <typename T, typename... Args>
    [[nodiscard]] Handle<T> emplace(Args&&... args)
    {
        return arena<T>().emplace(std::forward<Args>(args)...);
    }

    template <typename T> [[nodiscard]] T& get(Handle<T> handle)
    {
        return handle(m_arenas);
    }

    template <typename T> [[nodiscard]] const T& get(Handle<T> handle) const
    {
        return handle(m_arenas);
    }

    template <typename T> [[nodiscard]] Ref<T> ref(Handle<T> handle)
    {
        return Ref<T>(this, handle);
    }

    template <typename T> [[nodiscard]] ConstRef<T> ref(Handle<T> handle) const
    {
        return ConstRef<T>(this, handle);
    }

    [[nodiscard]] Arenas& arenas() { return m_arenas; }

    [[nodiscard]] const Arenas& arenas() const { return m_arenas; }

    void clear() { m_arenas = Arenas {}; }

    [[nodiscard]] ScopedCurrent bindCurrent() { return ScopedCurrent(*this); }

    [[nodiscard]] ScopedCurrent bindCurrent() const
    {
        return ScopedCurrent(*this);
    }

  private:
    template <typename T> friend T& detail::currentAstGet(int32_t index);
    template <typename T>
    friend const T& detail::currentAstGetConst(int32_t index);

    inline static thread_local AST* s_currentAst_nn = nullptr;
    inline static thread_local const AST* s_currentConstAst_nn = nullptr;

    Arenas m_arenas;
};

namespace detail {

    template <typename T> T& currentAstGet(int32_t index)
    {
        if (AST::s_currentAst_nn == nullptr || index < 0) {
            throw std::runtime_error(
                "handle dereference requires a bound mutable AST");
        }
        return AST::s_currentAst_nn->get(Handle<T>(index));
    }

    template <typename T> const T& currentAstGetConst(int32_t index)
    {
        if (AST::s_currentConstAst_nn == nullptr || index < 0) {
            throw std::runtime_error("handle dereference requires a bound AST");
        }
        return AST::s_currentConstAst_nn->get(Handle<T>(index));
    }

} // namespace detail

} // namespace yesod::frontend

#endif