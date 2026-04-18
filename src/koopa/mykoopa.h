#ifndef _TOY_MYKOOPA_
#define _TOY_MYKOOPA_

#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "koopa.h"

namespace koopa {
using std::map;
using std::pair;
using std::string;
using std::vector;

class Type;
class Int32Type;
class UnitType;
class ArrayType;
class PointerType;
class FunctionType;
class TypeVisitor;

class Value;
class IntegerValue;
class ZeroInitValue;
class UndefValue;
class AggregateValue;
class FuncArgRefValue;
class BlockArgRefValue;
class AllocValue;
class GlobalAllocValue;
class LoadValue;
class StoreValue;
class GetPtrValue;
class GetElemPtrValue;
class BinaryValue;
class BranchValue;
class JumpValue;
class CallValue;
class ReturnValue;
class ValueVisitor;

class BasicBlock;
class Function;
class Program;

class Type {
    koopa_raw_type_tag_t Tag;

  protected:
    Type(koopa_raw_type_tag_t Tag)
        : Tag(Tag)
    {
    }

    koopa_raw_type_tag_t getTag() const { return Tag; }

  public:
    virtual ~Type() = default;

    bool isInt32Type() const { return getTag() == KOOPA_RTT_INT32; }
    bool isUnitType() const { return getTag() == KOOPA_RTT_UNIT; }
    bool isArrayType() const { return getTag() == KOOPA_RTT_ARRAY; }
    bool isPointerType() const { return getTag() == KOOPA_RTT_POINTER; }
    bool isFunctionType() const { return getTag() == KOOPA_RTT_FUNCTION; }

    virtual bool operator==(const Type&) const = 0;

    static koopa_raw_type_t dumpRaw(const Type*);
    static void dumpSlice(koopa_raw_slice_t&, const vector<Type*>&);

    static Type* fromRaw(koopa_raw_type_t);
    static vector<Type*>& fromSlice(const koopa_raw_slice_t&);

    virtual void accept(TypeVisitor&) = 0;

  private:
    virtual koopa_raw_type_t dumpRawImpl() const = 0;
};

class Int32Type : public Type {
    Int32Type()
        : Type(KOOPA_RTT_INT32)
    {
    }

  public:
    Int32Type(const Int32Type&) = delete;
    Int32Type& operator=(const Int32Type&) = delete;

    static Int32Type* get()
    {
        static Int32Type* Instance = nullptr;
        if (Instance == nullptr) {
            Instance = new Int32Type();
        }
        return Instance;
    }

    bool operator==(const Type& Other) const { return Other.isInt32Type(); }

    void accept(TypeVisitor&);

  private:
    koopa_raw_type_t dumpRawImpl() const;
};

class UnitType : public Type {
    UnitType()
        : Type(KOOPA_RTT_UNIT)
    {
    }

  public:
    UnitType(const UnitType&) = delete;
    UnitType& operator=(const UnitType&) = delete;

    static UnitType* get()
    {
        static UnitType* Instance = nullptr;
        if (Instance == nullptr) {
            Instance = new UnitType();
        }
        return Instance;
    }

    bool operator==(const Type& Other) const { return Other.isUnitType(); }

    void accept(TypeVisitor&);

  private:
    koopa_raw_type_t dumpRawImpl() const;
};

class ArrayType : public Type {
    Type* ElementType;
    size_t NumElements;

    ArrayType(Type* ElementType, size_t NumElements)
        : Type(KOOPA_RTT_ARRAY)
        , ElementType(ElementType)
        , NumElements(NumElements)
    {
    }

  public:
    ArrayType(const ArrayType&) = delete;
    ArrayType& operator=(const ArrayType&) = delete;

    Type* getElementType() const { return ElementType; }
    size_t getNumElements() const { return NumElements; }

    static ArrayType* get(Type* ElementType, size_t NumElements)
    {
        static map<pair<Type*, size_t>, ArrayType*> Memo;
        ArrayType*& Entry = Memo[{ ElementType, NumElements }];
        if (Entry == nullptr) {
            Entry = new ArrayType(ElementType, NumElements);
        }
        return Entry;
    }

    bool operator==(const Type& Other) const
    {
        if (Other.isArrayType()) {
            const ArrayType& OtherArray = dynamic_cast<const ArrayType&>(Other);
            return *getElementType() == *OtherArray.getElementType() && getNumElements() == OtherArray.getNumElements();
        } else {
            return false;
        }
    }

    void accept(TypeVisitor&);

  private:
    koopa_raw_type_t dumpRawImpl() const;
};

class PointerType : public Type {
    Type* PointeeType;

    PointerType(Type* PointeeType)
        : Type(KOOPA_RTT_POINTER)
        , PointeeType(PointeeType)
    {
    }

  public:
    PointerType(const PointerType&) = delete;
    PointerType& operator=(const PointerType&) = delete;

    Type* getPointeeType() const { return PointeeType; }

    static PointerType* get(Type* PointeeType)
    {
        static map<Type*, PointerType*> Memo;
        PointerType*& Entry = Memo[PointeeType];
        if (Entry == nullptr) {
            Entry = new PointerType(PointeeType);
        }
        return Entry;
    }

    bool operator==(const Type& Other) const
    {
        if (Other.isPointerType()) {
            const PointerType& OtherPointer = dynamic_cast<const PointerType&>(Other);
            return *getPointeeType() == *OtherPointer.getPointeeType();
        } else {
            return false;
        }
    }

    void accept(TypeVisitor&);

  private:
    koopa_raw_type_t dumpRawImpl() const;
};

class FunctionType : public Type {
    Type* ResultType;
    vector<Type*> ParamTypes;

    FunctionType(Type* ResultType, const vector<Type*>& ParamTypes)
        : Type(KOOPA_RTT_FUNCTION)
        , ResultType(ResultType)
        , ParamTypes(ParamTypes)
    {
    }

  public:
    FunctionType(const FunctionType&) = delete;
    FunctionType& operator=(const FunctionType&) = delete;

    Type* getResultType() const { return ResultType; }
    size_t getNumParams() const { return ParamTypes.size(); }
    Type* getParamType(size_t i) const { return ParamTypes[i]; }
    const vector<Type*>& paramTypes() const { return ParamTypes; }

    static FunctionType* get(Type* ResultType, const vector<Type*>& ParamTypes)
    {
        static map<pair<Type*, vector<Type*>>, FunctionType*> Memo;
        FunctionType*& Entry = Memo[{ ResultType, ParamTypes }];
        if (Entry == nullptr) {
            Entry = new FunctionType(ResultType, ParamTypes);
        }
        return Entry;
    }
    static FunctionType* get(Type* ResultType,
        const vector<Type*>&& ParamTypes)
    {
        return get(ResultType, ParamTypes);
    }

    bool operator==(const Type& Other) const
    {
        if (Other.isFunctionType()) {
            const FunctionType& OtherFunction = dynamic_cast<const FunctionType&>(Other);
            if (*getResultType() == *OtherFunction.getResultType()) {
                if (getNumParams() == OtherFunction.getNumParams()) {
                    for (size_t i = 0; i != getNumParams(); ++i) {
                        if (!(*getParamType(i) == *OtherFunction.getParamType(i))) {
                            return false;
                        }
                    }
                    return true;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    void accept(TypeVisitor&);

  private:
    koopa_raw_type_t dumpRawImpl() const;
};

class TypeVisitor {
  public:
    virtual ~TypeVisitor() = default;

    virtual void visitInt32(Int32Type&) = 0;
    virtual void visitUnit(UnitType&) = 0;
    virtual void visitArray(ArrayType&) = 0;
    virtual void visitPointer(PointerType&) = 0;
    virtual void visitFunction(FunctionType&) = 0;
};

class Value {
    koopa_raw_value_tag_t Tag;
    Type* VType;
    string Name;
    vector<Value*> UsedBy;
    koopa_raw_slice_t RawUsedBy;
    bool UsedByReady;

  protected:
    Value(koopa_raw_value_tag_t Tag, Type* VType, string&& Name)
        : Tag(Tag)
        , VType(VType)
        , Name(std::move(Name))
        , UsedBy()
        , RawUsedBy()
        , UsedByReady(true)
    {
    }

    koopa_raw_value_tag_t getTag() const { return Tag; }

    koopa_raw_value_data_t* dumpMeta() const;

  public:
    virtual ~Value() = default;

    Type* getVType() const { return VType; }
    const string& getName() const { return Name; }
    bool hasName() const { return !getName().empty(); }
    void setUsedBy(koopa_raw_slice_t RawUsedBy)
    {
        this->RawUsedBy = RawUsedBy;
        this->UsedByReady = false;
    }
    vector<Value*>& usedBy()
    {
        if (!UsedByReady) {
            UsedBy = fromSlice(RawUsedBy);
            UsedByReady = true;
        }
        return UsedBy;
    }

    bool isIntegerValue() const { return getTag() == KOOPA_RVT_INTEGER; }
    bool isZeroInitValue() const { return getTag() == KOOPA_RVT_ZERO_INIT; }
    bool isUndefValue() const { return getTag() == KOOPA_RVT_UNDEF; }
    bool isAggregateValue() const { return getTag() == KOOPA_RVT_AGGREGATE; }
    bool isFuncArgRefValue() const { return getTag() == KOOPA_RVT_FUNC_ARG_REF; }
    bool isBlockArgRefValue() const
    {
        return getTag() == KOOPA_RVT_BLOCK_ARG_REF;
    }
    bool isAllocValue() const { return getTag() == KOOPA_RVT_ALLOC; }
    bool isGlobalAllocValue() const { return getTag() == KOOPA_RVT_GLOBAL_ALLOC; }
    bool isLoadValue() const { return getTag() == KOOPA_RVT_LOAD; }
    bool isStoreValue() const { return getTag() == KOOPA_RVT_STORE; }
    bool isGetPtrValue() const { return getTag() == KOOPA_RVT_GET_PTR; }
    bool isGetElemPtrValue() const { return getTag() == KOOPA_RVT_GET_ELEM_PTR; }
    bool isBinaryValue() const { return getTag() == KOOPA_RVT_BINARY; }
    bool isBranchValue() const { return getTag() == KOOPA_RVT_BRANCH; }
    bool isJumpValue() const { return getTag() == KOOPA_RVT_JUMP; }
    bool isCallValue() const { return getTag() == KOOPA_RVT_CALL; }
    bool isReturnValue() const { return getTag() == KOOPA_RVT_RETURN; }

    virtual bool canBeInitializer() const
    {
        return isIntegerValue() || isUndefValue() || isZeroInitValue();
    }

    bool canTerminateBlock() const
    {
        return isBranchValue() || isJumpValue() || isReturnValue();
    }

    static koopa_raw_value_t dumpRaw(const Value* Val);
    static void dumpSlice(koopa_raw_slice_t&, const vector<Value*>&,
        koopa_raw_slice_item_kind = KOOPA_RSIK_VALUE);

    static Value* fromRaw(koopa_raw_value_t);
    static vector<Value*>& fromSlice(const koopa_raw_slice_t&);

    virtual void accept(ValueVisitor&) = 0;

  private:
    virtual koopa_raw_value_t dumpRawImpl() const = 0;
};

class IntegerValue : public Value {
    int32_t Val;

    IntegerValue(int32_t Val)
        : Value(KOOPA_RVT_INTEGER, Int32Type::get(), "")
        , Val(Val)
    {
    }

  public:
    IntegerValue(const IntegerValue&) = delete;
    IntegerValue& operator=(const IntegerValue&) = delete;

    int32_t getVal() const { return Val; }

    static IntegerValue* get(int32_t Val) { return new IntegerValue(Val); }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class ZeroInitValue : public Value {
    ZeroInitValue(Type* VType)
        : Value(KOOPA_RVT_ZERO_INIT, VType, "")
    {
    }

  public:
    ZeroInitValue(const ZeroInitValue&) = delete;
    ZeroInitValue& operator=(const ZeroInitValue&) = delete;

    static ZeroInitValue* get(Type* VType) { return new ZeroInitValue(VType); }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class UndefValue : public Value {
    UndefValue(Type* VType)
        : Value(KOOPA_RVT_UNDEF, VType, "")
    {
    }

  public:
    UndefValue(const UndefValue&) = delete;
    UndefValue& operator=(const UndefValue&) = delete;

    static UndefValue* get(Type* VType) { return new UndefValue(VType); }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class AggregateValue : public Value {
    vector<Value*> Elements;

    AggregateValue(vector<Value*>&& Elements, Type* VType)
        : Value(KOOPA_RVT_AGGREGATE, VType, "")
        , Elements(std::move(Elements))
    {
    }

  public:
    AggregateValue(const AggregateValue&) = delete;
    AggregateValue& operator=(const AggregateValue&) = delete;

    size_t getNumElements() const { return Elements.size(); }
    Value* getElement(size_t i) const { return Elements[i]; }
    const vector<Value*>& elements() const { return Elements; }

    bool canBeInitializer() const
    {
        if (getNumElements() == 0) {
            return false;
        } else {
            for (Value* Element : elements()) {
                if (!Element->canBeInitializer()) {
                    return false;
                }
            }
            return true;
        }
    }

    static AggregateValue* get(vector<Value*>&& Elements, Type* VType)
    {
        assert(VType->isArrayType() && "aggregate value should have array type");
        return new AggregateValue(std::move(Elements), VType);
    }
    static AggregateValue* get(const vector<Value*>& Elements, Type* VType)
    {
        return get(vector<Value*>(Elements), VType);
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class FuncArgRefValue : public Value {
    size_t Index;

    FuncArgRefValue(size_t Index, Type* VType)
        : Value(KOOPA_RVT_FUNC_ARG_REF, VType, "")
        , Index(Index)
    {
    }

  public:
    FuncArgRefValue(const FuncArgRefValue&) = delete;
    FuncArgRefValue& operator=(const FuncArgRefValue&) = delete;

    size_t getIndex() const { return Index; }

    static FuncArgRefValue* get(size_t Index, Type* VType)
    {
        return new FuncArgRefValue(Index, VType);
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class BlockArgRefValue : public Value {
    size_t Index;

    BlockArgRefValue(size_t Index, Type* VType)
        : Value(KOOPA_RVT_BLOCK_ARG_REF, VType, "")
        , Index(Index)
    {
    }

  public:
    BlockArgRefValue(const BlockArgRefValue&) = delete;
    BlockArgRefValue& operator=(const BlockArgRefValue&) = delete;

    size_t getIndex() const { return Index; }

    static BlockArgRefValue* get(size_t Index, Type* VType)
    {
        return new BlockArgRefValue(Index, VType);
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class AllocValue : public Value {
    AllocValue(Type* PointeeType, string&& Name)
        : Value(KOOPA_RVT_ALLOC, PointerType::get(PointeeType), std::move(Name))
    {
    }

  public:
    AllocValue(const AllocValue&) = delete;
    AllocValue& operator=(const AllocValue&) = delete;

    static AllocValue* get(Type* PointeeType, string&& Name = "")
    {
        return new AllocValue(PointeeType, std::move(Name));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class GlobalAllocValue : public Value {
    Value* InitVal;

    GlobalAllocValue(Value* InitVal, string&& Name)
        : Value(KOOPA_RVT_GLOBAL_ALLOC, PointerType::get(InitVal->getVType()),
              std::move(Name))
        , InitVal(InitVal)
    {
    }

  public:
    GlobalAllocValue(const GlobalAllocValue&) = delete;
    GlobalAllocValue& operator=(const GlobalAllocValue&) = delete;

    Value* getInitVal() const { return InitVal; }

    static GlobalAllocValue* get(Value* InitVal, string&& Name = "")
    {
        assert(InitVal->canBeInitializer() && "the init val of global alloc value must be initializer");
        return new GlobalAllocValue(InitVal, std::move(Name));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class LoadValue : public Value {
    Value* Source;

    LoadValue(Value* Source, string&& Name)
        : Value(KOOPA_RVT_LOAD,
              dynamic_cast<PointerType*>(Source->getVType())->getPointeeType(),
              std::move(Name))
        , Source(Source)
    {
    }

  public:
    LoadValue(const LoadValue&) = delete;
    LoadValue& operator=(const LoadValue&) = delete;

    Value* getSource() const { return Source; }

    static LoadValue* get(Value* Source, string&& Name = "")
    {
        assert(Source->getVType()->isPointerType() && "the source of load value should have pointer type");
        return new LoadValue(Source, std::move(Name));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class StoreValue : public Value {
    Value* Val;
    Value* Destination;

    StoreValue(Value* Val, Value* Destination)
        : Value(KOOPA_RVT_STORE, UnitType::get(), "")
        , Val(Val)
        , Destination(Destination)
    {
    }

  public:
    StoreValue(const StoreValue&) = delete;
    StoreValue& operator=(const StoreValue&) = delete;

    Value* getVal() const { return Val; }
    Value* getDestination() const { return Destination; }

    static StoreValue* get(Value* Val, Value* Destination)
    {
        assert(Destination->getVType()->isPointerType() && "the destination of store value should have pointer type");
        Type* PointeeType = dynamic_cast<PointerType*>(Destination->getVType())->getPointeeType();
        assert(*PointeeType == *Val->getVType() && "the value of store value should have compatible type");
        return new StoreValue(Val, Destination);
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class GetPtrValue : public Value {
    Value* Source;
    Value* Index;

    GetPtrValue(Value* Source, Value* Index, string&& Name)
        : Value(KOOPA_RVT_GET_PTR, Source->getVType(), std::move(Name))
        , Source(Source)
        , Index(Index)
    {
    }

  public:
    GetPtrValue(const GetPtrValue&) = delete;
    GetPtrValue& operator=(const GetPtrValue&) = delete;

    Value* getSource() const { return Source; }
    Value* getIndex() const { return Index; }

    static GetPtrValue* get(Value* Source, Value* Index, string&& Name = "")
    {
        assert(Source->getVType()->isPointerType() && "the source of getptr value should have pointer type");
        assert(Index->getVType()->isInt32Type() && "the index of getptr value should have integer type");
        return new GetPtrValue(Source, Index, std::move(Name));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class GetElemPtrValue : public Value {
    Value* Source;
    Value* Index;

    GetElemPtrValue(Value* Source, Value* Index, string&& Name)
        : Value(
              KOOPA_RVT_GET_ELEM_PTR,
              PointerType::get(dynamic_cast<ArrayType*>(
                  dynamic_cast<PointerType*>(Source->getVType())
                      ->getPointeeType())
                                   ->getElementType()),
              std::move(Name))
        , Source(Source)
        , Index(Index)
    {
    }

  public:
    GetElemPtrValue(const GetElemPtrValue&) = delete;
    GetElemPtrValue& operator=(const GetElemPtrValue&) = delete;

    Value* getSource() const { return Source; }
    Value* getIndex() const { return Index; }

    static GetElemPtrValue* get(Value* Source, Value* Index, string&& Name = "")
    {
        assert(Source->getVType()->isPointerType() && "the source of getelemptr value should have pointer-of-array type");
        Type* PointeeType = dynamic_cast<PointerType*>(Source->getVType())->getPointeeType();
        assert(PointeeType->isArrayType() && "the source of getelemptr value should have pointer-of-array type");
        assert(Index->getVType()->isInt32Type() && "the index of getelemptr value should have integer type");
        return new GetElemPtrValue(Source, Index, std::move(Name));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class BinaryValue : public Value {
    koopa_raw_binary_op Op;
    Value* Lhs;
    Value* Rhs;

    BinaryValue(koopa_raw_binary_op Op, Value* Lhs, Value* Rhs, string&& Name)
        : Value(KOOPA_RVT_BINARY, Int32Type::get(), std::move(Name))
        , Op(Op)
        , Lhs(Lhs)
        , Rhs(Rhs)
    {
    }

  public:
    BinaryValue(const BinaryValue&) = delete;
    BinaryValue& operator=(const BinaryValue&) = delete;

    koopa_raw_binary_op getOp() const { return Op; }
    Value* getLhs() const { return Lhs; }
    Value* getRhs() const { return Rhs; }

    static BinaryValue* get(koopa_raw_binary_op Op, Value* Lhs, Value* Rhs,
        string&& Name)
    {
        assert(Lhs->getVType()->isInt32Type() && Rhs->getVType()->isInt32Type() && "the lhs and rhs of binary value should have integer type");
        return new BinaryValue(Op, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getNotEqual(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_NOT_EQ, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getEqual(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_EQ, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getGreater(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_GT, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getLess(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_LT, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getGreaterEqual(Value* Lhs, Value* Rhs,
        string&& Name = "")
    {
        return get(KOOPA_RBO_GE, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getLessEqual(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_LE, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getAdd(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_ADD, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getSub(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_SUB, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getMul(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_MUL, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getDiv(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_DIV, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getMod(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_MOD, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getBitwiseAnd(Value* Lhs, Value* Rhs,
        string&& Name = "")
    {
        return get(KOOPA_RBO_AND, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getBitwiseOr(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_OR, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getBitwiseXor(Value* Lhs, Value* Rhs,
        string&& Name = "")
    {
        return get(KOOPA_RBO_XOR, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getShl(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_SHL, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getShr(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_SHR, Lhs, Rhs, std::move(Name));
    }
    static BinaryValue* getSar(Value* Lhs, Value* Rhs, string&& Name = "")
    {
        return get(KOOPA_RBO_SAR, Lhs, Rhs, std::move(Name));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class BranchValue : public Value {
    Value* Condition;
    BasicBlock* TrueBB;
    vector<Value*> TrueArgs;
    BasicBlock* FalseBB;
    vector<Value*> FalseArgs;

    BranchValue(Value* Condition, BasicBlock* TrueBB, vector<Value*>&& TrueArgs,
        BasicBlock* FalseBB, vector<Value*>&& FalseArgs)
        : Value(KOOPA_RVT_BRANCH, UnitType::get(), "")
        , Condition(Condition)
        , TrueBB(TrueBB)
        , TrueArgs(std::move(TrueArgs))
        , FalseBB(FalseBB)
        , FalseArgs(std::move(FalseArgs))
    {
    }

  public:
    BranchValue(const BranchValue&) = delete;
    BranchValue& operator=(const BranchValue&) = delete;

    Value* getCondition() const { return Condition; }
    BasicBlock* getTrueBB() const { return TrueBB; }
    size_t getNumTrueArgs() const { return TrueArgs.size(); }
    Value* getTrueArg(size_t i) const { return TrueArgs[i]; }
    const vector<Value*>& trueArgs() const { return TrueArgs; }
    BasicBlock* getFalseBB() const { return FalseBB; }
    size_t getNumFalseArgs() const { return FalseArgs.size(); }
    Value* getFalseArg(size_t i) const { return FalseArgs[i]; }
    const vector<Value*>& falseArgs() const { return FalseArgs; }

    static BranchValue* get(Value*, BasicBlock*, vector<Value*>&&,
        BasicBlock*, vector<Value*>&&);
    static BranchValue* get(Value* Condition, BasicBlock* TrueBB,
        const vector<Value*>& TrueArgs, BasicBlock* FalseBB,
        const vector<Value*>& FalseArgs)
    {
        return get(Condition, TrueBB, vector<Value*>(TrueArgs), FalseBB,
            vector<Value*>(FalseArgs));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class JumpValue : public Value {
    BasicBlock* TargetBB;
    vector<Value*> Args;

    JumpValue(BasicBlock* TargetBB, vector<Value*>&& Args)
        : Value(KOOPA_RVT_JUMP, UnitType::get(), "")
        , TargetBB(TargetBB)
        , Args(std::move(Args))
    {
    }

  public:
    JumpValue(const JumpValue&) = delete;
    JumpValue& operator=(const JumpValue&) = delete;

    BasicBlock* getTargetBB() const { return TargetBB; }
    size_t getNumArgs() const { return Args.size(); }
    Value* getArg(size_t i) const { return Args[i]; }
    const vector<Value*>& args() const { return Args; }

    static JumpValue* get(BasicBlock*, vector<Value*>&&);
    static JumpValue* get(BasicBlock* TargetBB, const vector<Value*>& Args)
    {
        return get(TargetBB, vector<Value*>(Args));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class CallValue : public Value {
    Function* Callee;
    vector<Value*> Args;

    CallValue(Function*, vector<Value*>&&, string&&);

  public:
    CallValue(const CallValue&) = delete;
    CallValue& operator=(const CallValue&) = delete;

    Function* getCallee() const { return Callee; }
    size_t getNumArgs() const { return Args.size(); }
    Value* getArg(size_t i) const { return Args[i]; }
    const vector<Value*>& args() const { return Args; }

    static CallValue* get(Function*, vector<Value*>&&, string&& = "");
    static CallValue* get(Function* Callee, const vector<Value*>& Args,
        const string& Name = "")
    {
        return get(Callee, vector<Value*>(Args), string(Name));
    }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class ReturnValue : public Value {
    Value* Val;

    ReturnValue(Value* Val)
        : Value(KOOPA_RVT_RETURN, UnitType::get(), "")
        , Val(Val)
    {
    }

  public:
    ReturnValue(const ReturnValue&) = delete;
    ReturnValue& operator=(const ReturnValue&) = delete;

    Value* getVal() const { return Val; }

    static ReturnValue* get(Value* Val) { return new ReturnValue(Val); }

    void accept(ValueVisitor&);

  private:
    koopa_raw_value_t dumpRawImpl() const;
};

class ValueVisitor {
  public:
    virtual ~ValueVisitor() = default;

    virtual void visitInteger(IntegerValue&) = 0;
    virtual void visitZeroInit(ZeroInitValue&) = 0;
    virtual void visitUndef(UndefValue&) = 0;
    virtual void visitAggregate(AggregateValue&) = 0;
    virtual void visitFuncArgRef(FuncArgRefValue&) = 0;
    virtual void visitBlockArgRef(BlockArgRefValue&) = 0;
    virtual void visitAlloc(AllocValue&) = 0;
    virtual void visitGlobalAlloc(GlobalAllocValue&) = 0;
    virtual void visitLoad(LoadValue&) = 0;
    virtual void visitStore(StoreValue&) = 0;
    virtual void visitGetPtr(GetPtrValue&) = 0;
    virtual void visitGetElemPtr(GetElemPtrValue&) = 0;
    virtual void visitBinary(BinaryValue&) = 0;
    virtual void visitBranch(BranchValue&) = 0;
    virtual void visitJump(JumpValue&) = 0;
    virtual void visitCall(CallValue&) = 0;
    virtual void visitReturn(ReturnValue&) = 0;
};

class BasicBlock {
    bool IsEntry;
    vector<Value*> Params;
    vector<Value*> Insts;
    string Name;
    vector<Value*> UsedBy;
    koopa_raw_slice_t RawUsedBy;
    bool UsedByReady;

    BasicBlock(bool IsEntry, vector<Value*>&& Params, vector<Value*>&& Insts,
        string&& Name)
        : IsEntry(IsEntry)
        , Params(std::move(Params))
        , Insts(std::move(Insts))
        , Name(std::move(Name))
        , UsedBy()
        , RawUsedBy()
        , UsedByReady(true)
    {
    }

  public:
    BasicBlock(const BasicBlock&) = delete;
    BasicBlock& operator=(const BasicBlock&) = delete;
    ~BasicBlock() = default;

    bool isEntry() const { return IsEntry; }
    size_t getNumParams() const { return Params.size(); }
    Value* getParam(size_t i) const { return Params[i]; }
    const vector<Value*>& params() const { return Params; }
    size_t getNumInsts() const { return Insts.size(); }
    Value* getInst(size_t i) const { return Insts[i]; }
    const vector<Value*>& insts() const { return Insts; }
    const string& getName() const { return Name; }
    bool hasName() const { return !getName().empty(); }
    void setUsedBy(koopa_raw_slice_t RawUsedBy)
    {
        this->RawUsedBy = RawUsedBy;
        this->UsedByReady = false;
    }
    vector<Value*>& usedBy()
    {
        if (!UsedByReady) {
            UsedBy = Value::fromSlice(RawUsedBy);
            UsedByReady = true;
        }
        return UsedBy;
    }

    static BasicBlock* get(bool IsEntry, vector<Value*>&& Params,
        vector<Value*>&& Insts, string&& Name = "")
    {
        BasicBlock* BB = new BasicBlock(IsEntry, std::move(Params),
            std::move(Insts), std::move(Name));
        BB->validate();
        return BB;
    }
    static BasicBlock* get(bool IsEntry, const vector<Value*>& Params,
        const vector<Value*>& Insts,
        const string& Name = "")
    {
        return get(IsEntry, vector<Value*>(Params), vector<Value*>(Insts),
            string(Name));
    }

    static BasicBlock* createEntry(string&& Name = "")
    {
        return new BasicBlock(true, {}, {}, std::move(Name));
    }
    static BasicBlock* createNonEntry(string&& Name = "")
    {
        return new BasicBlock(false, {}, {}, std::move(Name));
    }
    void pushParam(Value* Param) { Params.push_back(Param); }
    void pushInst(Value* Inst) { Insts.push_back(Inst); }
    void validate() const;

    static koopa_raw_basic_block_t dumpRaw(const BasicBlock*);
    static void dumpSlice(koopa_raw_slice_t&, const vector<BasicBlock*>&);

    static BasicBlock* fromRaw(bool, koopa_raw_basic_block_t);
    static vector<BasicBlock*>& fromSlice(const koopa_raw_slice_t&);

  private:
    koopa_raw_basic_block_data_t* dumpMeta() const;
};

class Function {
    Type* FuncType;
    vector<Value*> Params;
    vector<BasicBlock*> BBs;
    string Name;

    Function(Type* FuncType, vector<Value*>&& Params, vector<BasicBlock*>&& BBs,
        string&& Name)
        : FuncType(FuncType)
        , Params(std::move(Params))
        , BBs(std::move(BBs))
        , Name(std::move(Name))
    {
    }

  public:
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;
    ~Function() = default;

    Type* getFuncType() const { return FuncType; }
    size_t getNumParams() const { return Params.size(); }
    Value* getParam(size_t i) const { return Params[i]; }
    const vector<Value*>& params() const { return Params; }
    size_t getNumBBs() const { return BBs.size(); }
    BasicBlock* getBB(size_t i) const { return BBs[i]; }
    const vector<BasicBlock*>& bbs() const { return BBs; }
    const string& getName() const { return Name; }
    bool hasName() const { return !getName().empty(); }

    static Function* get(Type* FuncType, vector<Value*>&& Params,
        vector<BasicBlock*>&& BBs, string&& Name = "")
    {
        Function* Func = new Function(FuncType, std::move(Params), std::move(BBs),
            std::move(Name));
        Func->validate();
        return Func;
    }
    static Function* get(Type* FuncType, const vector<Value*>& Params,
        const vector<BasicBlock*>& BBs,
        const string& Name = "")
    {
        return get(FuncType, vector<Value*>(Params), vector<BasicBlock*>(BBs),
            string(Name));
    }

    static Function* create(Type* FuncType, string&& Name = "")
    {
        return new Function(FuncType, {}, {}, std::move(Name));
    }
    void pushParam(Value* Param) { Params.push_back(Param); }
    void pushBB(BasicBlock* BB) { BBs.push_back(BB); }
    void validate() const;

    static koopa_raw_function_t dumpRaw(const Function*);
    static void dumpSlice(koopa_raw_slice_t&, const vector<Function*>&);

    static Function* fromRaw(koopa_raw_function_t);
    static vector<Function*>& fromSlice(const koopa_raw_slice_t&);

  private:
    koopa_raw_function_data_t* dumpMeta() const;
};

class Program {
    koopa_raw_program_builder_t Builder;
    vector<Value*> Vals;
    vector<Function*> Funcs;

    Program(koopa_raw_program_builder_t Builder, vector<Value*>&& Vals,
        vector<Function*>&& Funcs)
        : Builder(Builder)
        , Vals(std::move(Vals))
        , Funcs(std::move(Funcs))
    {
    }

  public:
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    ~Program()
    {
        if (Builder != nullptr) {
            koopa_delete_raw_program_builder(Builder);
        }
    }

    size_t getNumVals() const { return Vals.size(); }
    Value* getVal(size_t i) const { return Vals[i]; }
    const vector<Value*>& vals() const { return Vals; }
    size_t getNumFuncs() const { return Funcs.size(); }
    Function* getFunc(size_t i) const { return Funcs[i]; }
    const vector<Function*>& funcs() const { return Funcs; }

    static Program* get(koopa_raw_program_builder_t Builder,
        vector<Value*>&& Vals, vector<Function*>&& Funcs)
    {
        for (Value* Val : Vals) {
            assert(Val->isGlobalAllocValue() && "global value should be global alloc");
        }
        return new Program(Builder, std::move(Vals), std::move(Funcs));
    }
    static Program* get(koopa_raw_program_builder_t Builder,
        const vector<Value*>& Vals,
        const vector<Function*>& Funcs)
    {
        return get(Builder, vector<Value*>(Vals), vector<Function*>(Funcs));
    }

    static Program* create() { return get(nullptr, {}, {}); }
    void pushVal(Value* Val)
    {
        assert(Val->isGlobalAllocValue() && "global value should be global alloc");
        Vals.push_back(Val);
    }
    void pushFunc(Function* Func) { Funcs.push_back(Func); }
    Program* validate();

    static koopa_raw_program_t dumpRaw(const Program*);
    static Program* fromRaw(koopa_raw_program_builder_t, koopa_raw_program_t);
};
} // namespace koopa

#endif
