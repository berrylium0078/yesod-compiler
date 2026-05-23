#include "mykoopa.h"

namespace yesod::koopa {
void Int32Type::accept(TypeVisitor& visitor) { visitor.visitInt32(*this); }
void UnitType::accept(TypeVisitor& visitor) { visitor.visitUnit(*this); }
void ArrayType::accept(TypeVisitor& visitor) { visitor.visitArray(*this); }
void PointerType::accept(TypeVisitor& visitor) { visitor.visitPointer(*this); }
void FunctionType::accept(TypeVisitor& visitor)
{
    visitor.visitFunction(*this);
}

koopa_raw_type_t Type::dumpRaw(const Type* Ty)
{
    static map<const Type*, koopa_raw_type_t> Memo;
    koopa_raw_type_t& Entry = Memo[Ty];
    if (Entry == nullptr) {
        Entry = Ty->dumpRawImpl();
    }
    return Entry;
}

void Type::dumpSlice(koopa_raw_slice_t& Slice, const vector<Type*>& Vec)
{
    Slice.kind = KOOPA_RSIK_TYPE;
    Slice.len = Vec.size();
    koopa_raw_type_t* Buffer = new koopa_raw_type_t[Vec.size()];
    for (size_t i = 0; i != Vec.size(); ++i) {
        Buffer[i] = Type::dumpRaw(Vec[i]);
    }
    Slice.buffer = (const void**)Buffer;
}

Type* Type::fromRaw(koopa_raw_type_t RawType)
{
    static map<koopa_raw_type_t, Type*> Memo;
    Type*& Entry = Memo[RawType];
    if (Entry == nullptr) {
        switch (RawType->tag) {
        case KOOPA_RTT_INT32:
            Entry = Int32Type::get();
            break;
        case KOOPA_RTT_UNIT:
            Entry = UnitType::get();
            break;
        case KOOPA_RTT_ARRAY:
            Entry = ArrayType::get(
                fromRaw(RawType->data.array.base), RawType->data.array.len);
            break;
        case KOOPA_RTT_POINTER:
            Entry = PointerType::get(fromRaw(RawType->data.pointer.base));
            break;
        case KOOPA_RTT_FUNCTION: {
            Type* ResultType = fromRaw(RawType->data.function.ret);
            vector<Type*>& ParamTypes
                = fromSlice(RawType->data.function.params);
            Entry = FunctionType::get(ResultType, ParamTypes);
            break;
        }
        default:
            assert(false);
        }
    }
    return Entry;
}

vector<Type*>& Type::fromSlice(const koopa_raw_slice_t& Slice)
{
    static map<pair<const void**, size_t>, vector<Type*>> Memo;
    if (Memo.count({ Slice.buffer, Slice.len }) == 0) {
        vector<Type*>& Entry = Memo[{ Slice.buffer, Slice.len }];
        Entry.resize(Slice.len);
        for (size_t i = 0; i != Slice.len; ++i) {
            Entry[i] = fromRaw((koopa_raw_type_t)Slice.buffer[i]);
        }
        return Entry;
    } else {
        return Memo[{ Slice.buffer, Slice.len }];
    }
}

koopa_raw_type_t Int32Type::dumpRawImpl() const
{
    koopa_raw_type_kind_t* RawType = new koopa_raw_type_kind_t;
    RawType->tag = getTag();
    return RawType;
}

koopa_raw_type_t UnitType::dumpRawImpl() const
{
    koopa_raw_type_kind_t* RawType = new koopa_raw_type_kind_t;
    RawType->tag = getTag();
    return RawType;
}

koopa_raw_type_t ArrayType::dumpRawImpl() const
{
    koopa_raw_type_kind_t* RawType = new koopa_raw_type_kind_t;
    RawType->tag = getTag();
    RawType->data.array.base = Type::dumpRaw(ElementType);
    RawType->data.array.len = NumElements;
    return RawType;
}

koopa_raw_type_t PointerType::dumpRawImpl() const
{
    koopa_raw_type_kind_t* RawType = new koopa_raw_type_kind_t;
    RawType->tag = getTag();
    RawType->data.pointer.base = Type::dumpRaw(PointeeType);
    return RawType;
}

koopa_raw_type_t FunctionType::dumpRawImpl() const
{
    koopa_raw_type_kind_t* RawType = new koopa_raw_type_kind_t;
    RawType->tag = getTag();
    RawType->data.function.ret = Type::dumpRaw(getResultType());
    Type::dumpSlice(RawType->data.function.params, paramTypes());
    return RawType;
}

void IntegerValue::accept(ValueVisitor& visitor)
{
    visitor.visitInteger(*this);
}
void ZeroInitValue::accept(ValueVisitor& visitor)
{
    visitor.visitZeroInit(*this);
}
void UndefValue::accept(ValueVisitor& visitor) { visitor.visitUndef(*this); }
void AggregateValue::accept(ValueVisitor& visitor)
{
    visitor.visitAggregate(*this);
}
void FuncArgRefValue::accept(ValueVisitor& visitor)
{
    visitor.visitFuncArgRef(*this);
}
void BlockArgRefValue::accept(ValueVisitor& visitor)
{
    visitor.visitBlockArgRef(*this);
}
void AllocValue::accept(ValueVisitor& visitor) { visitor.visitAlloc(*this); }
void GlobalAllocValue::accept(ValueVisitor& visitor)
{
    visitor.visitGlobalAlloc(*this);
}
void LoadValue::accept(ValueVisitor& visitor) { visitor.visitLoad(*this); }
void StoreValue::accept(ValueVisitor& visitor) { visitor.visitStore(*this); }
void GetPtrValue::accept(ValueVisitor& visitor) { visitor.visitGetPtr(*this); }
void GetElemPtrValue::accept(ValueVisitor& visitor)
{
    visitor.visitGetElemPtr(*this);
}
void BinaryValue::accept(ValueVisitor& visitor) { visitor.visitBinary(*this); }
void BranchValue::accept(ValueVisitor& visitor) { visitor.visitBranch(*this); }
void JumpValue::accept(ValueVisitor& visitor) { visitor.visitJump(*this); }
void CallValue::accept(ValueVisitor& visitor) { visitor.visitCall(*this); }
void ReturnValue::accept(ValueVisitor& visitor) { visitor.visitReturn(*this); }

koopa_raw_value_data_t* Value::dumpMeta() const
{
    koopa_raw_value_data_t* RawValue = new koopa_raw_value_data_t;
    RawValue->ty = Type::dumpRaw(getVType());
    if (hasName()) {
        char* Buffer = new char[getName().size() + 1];
        copy(getName().begin(), getName().end(), Buffer);
        Buffer[getName().size()] = '\0';
        RawValue->name = Buffer;
    } else {
        RawValue->name = nullptr;
    }
    RawValue->kind.tag = getTag();
    return RawValue;
}

koopa_raw_value_t Value::dumpRaw(const Value* Val)
{
    static map<const Value*, koopa_raw_value_t> Memo;
    koopa_raw_value_t& Entry = Memo[Val];
    if (Entry == nullptr) {
        Entry = Val->dumpRawImpl();
    }
    return Entry;
}

void Value::dumpSlice(koopa_raw_slice_t& Slice, const vector<Value*>& Vec,
    koopa_raw_slice_item_kind kind)
{
    Slice.kind = KOOPA_RSIK_VALUE;
    Slice.len = Vec.size();
    koopa_raw_value_t* Buffer = new koopa_raw_value_t[Vec.size()];
    for (size_t i = 0; i != Vec.size(); ++i) {
        Buffer[i] = dumpRaw(Vec[i]);
    }
    Slice.buffer = (const void**)Buffer;
}

Value* Value::fromRaw(koopa_raw_value_t RawValue)
{
    static map<koopa_raw_value_t, Value*> Memo;
    Value*& Entry = Memo[RawValue];
    if (Entry == nullptr) {
        string Name = RawValue->name == nullptr ? "" : RawValue->name;
        switch (RawValue->kind.tag) {
        case KOOPA_RVT_INTEGER:
            Entry = IntegerValue::get(RawValue->kind.data.integer.value);
            break;
        case KOOPA_RVT_ZERO_INIT:
            Entry = ZeroInitValue::get(Type::fromRaw(RawValue->ty));
            break;
        case KOOPA_RVT_UNDEF:
            Entry = UndefValue::get(Type::fromRaw(RawValue->ty));
            break;
        case KOOPA_RVT_AGGREGATE:
            Entry = AggregateValue::get(
                fromSlice(RawValue->kind.data.aggregate.elems),
                Type::fromRaw(RawValue->ty));
            break;
        case KOOPA_RVT_FUNC_ARG_REF:
            Entry = FuncArgRefValue::get(RawValue->kind.data.func_arg_ref.index,
                Type::fromRaw(RawValue->ty));
            break;
        case KOOPA_RVT_BLOCK_ARG_REF:
            Entry
                = BlockArgRefValue::get(RawValue->kind.data.block_arg_ref.index,
                    Type::fromRaw(RawValue->ty));
            break;
        case KOOPA_RVT_ALLOC:
            Entry = AllocValue::get(
                Type::fromRaw(RawValue->ty->data.pointer.base),
                std::move(Name));
            break;
        case KOOPA_RVT_GLOBAL_ALLOC:
            Entry = GlobalAllocValue::get(
                fromRaw(RawValue->kind.data.global_alloc.init),
                std::move(Name));
            break;
        case KOOPA_RVT_LOAD:
            Entry = LoadValue::get(
                fromRaw(RawValue->kind.data.load.src), std::move(Name));
            break;
        case KOOPA_RVT_STORE:
            Entry = StoreValue::get(fromRaw(RawValue->kind.data.store.value),
                fromRaw(RawValue->kind.data.store.dest));
            break;
        case KOOPA_RVT_GET_PTR:
            Entry = GetPtrValue::get(fromRaw(RawValue->kind.data.get_ptr.src),
                fromRaw(RawValue->kind.data.get_ptr.index), std::move(Name));
            break;
        case KOOPA_RVT_GET_ELEM_PTR:
            Entry = GetElemPtrValue::get(
                fromRaw(RawValue->kind.data.get_elem_ptr.src),
                fromRaw(RawValue->kind.data.get_elem_ptr.index),
                std::move(Name));
            break;
        case KOOPA_RVT_BINARY:
            Entry = BinaryValue::get(
                (koopa_raw_binary_op)RawValue->kind.data.binary.op,
                fromRaw(RawValue->kind.data.binary.lhs),
                fromRaw(RawValue->kind.data.binary.rhs), std::move(Name));
            break;
        case KOOPA_RVT_BRANCH:
            Entry = BranchValue::get(fromRaw(RawValue->kind.data.branch.cond),
                BasicBlock::fromRaw(false, RawValue->kind.data.branch.true_bb),
                fromSlice(RawValue->kind.data.branch.true_args),
                BasicBlock::fromRaw(false, RawValue->kind.data.branch.false_bb),
                fromSlice(RawValue->kind.data.branch.false_args));
            break;
        case KOOPA_RVT_JUMP:
            Entry = JumpValue::get(
                BasicBlock::fromRaw(false, RawValue->kind.data.jump.target),
                fromSlice(RawValue->kind.data.jump.args));
            break;
        case KOOPA_RVT_CALL:
            Entry = CallValue::get(
                Function::fromRaw(RawValue->kind.data.call.callee),
                fromSlice(RawValue->kind.data.call.args), Name);
            break;
        case KOOPA_RVT_RETURN:
            if (RawValue->kind.data.ret.value == nullptr) {
                Entry = ReturnValue::get(nullptr);
            } else {
                Entry
                    = ReturnValue::get(fromRaw(RawValue->kind.data.ret.value));
            }
            break;
        default:
            assert(false);
        }
        Entry->setUsedBy(RawValue->used_by);
    }
    return Entry;
}

vector<Value*>& Value::fromSlice(const koopa_raw_slice_t& Slice)
{
    static map<pair<const void**, size_t>, vector<Value*>> Memo;
    if (Memo.count({ Slice.buffer, Slice.len }) == 0) {
        vector<Value*>& Entry = Memo[{ Slice.buffer, Slice.len }];
        Entry.resize(Slice.len);
        for (size_t i = 0; i != Slice.len; ++i) {
            Entry[i] = fromRaw((koopa_raw_value_t)Slice.buffer[i]);
        }
        return Entry;
    } else {
        return Memo[{ Slice.buffer, Slice.len }];
    }
}

koopa_raw_value_t IntegerValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.integer.value = getVal();
    return RawValue;
}

koopa_raw_value_t ZeroInitValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    return RawValue;
}

koopa_raw_value_t UndefValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    return RawValue;
}

koopa_raw_value_t AggregateValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    dumpSlice(RawValue->kind.data.aggregate.elems, elements());
    return RawValue;
}

koopa_raw_value_t FuncArgRefValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.func_arg_ref.index = getIndex();
    return RawValue;
}

koopa_raw_value_t BlockArgRefValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.block_arg_ref.index = getIndex();
    return RawValue;
}

koopa_raw_value_t AllocValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    return RawValue;
}

koopa_raw_value_t GlobalAllocValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.global_alloc.init = dumpRaw(getInitVal());
    return RawValue;
}

koopa_raw_value_t LoadValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.load.src = dumpRaw(getSource());
    return RawValue;
}

koopa_raw_value_t StoreValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.store.value = dumpRaw(getVal());
    RawValue->kind.data.store.dest = dumpRaw(getDestination());
    return RawValue;
}

koopa_raw_value_t GetPtrValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.get_ptr.src = dumpRaw(getSource());
    RawValue->kind.data.get_ptr.index = dumpRaw(getIndex());
    return RawValue;
}

koopa_raw_value_t GetElemPtrValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.get_elem_ptr.src = dumpRaw(getSource());
    RawValue->kind.data.get_elem_ptr.index = dumpRaw(getIndex());
    return RawValue;
}

koopa_raw_value_t BinaryValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.binary.op = getOp();
    RawValue->kind.data.binary.lhs = dumpRaw(getLhs());
    RawValue->kind.data.binary.rhs = dumpRaw(getRhs());
    return RawValue;
}

BranchValue* BranchValue::get(Value* Condition, BasicBlock* TrueBB,
    vector<Value*>&& TrueArgs, BasicBlock* FalseBB, vector<Value*>&& FalseArgs)
{
    assert(Condition->getVType()->isInt32Type()
        && "the condition of branch value should have integer type");
    assert(!TrueBB->isEntry() && !FalseBB->isEntry()
        && "branch value should not jump to entry body");
    assert(TrueBB->getNumParams() == TrueArgs.size() && "arity should match");
    for (size_t i = 0; i != TrueBB->getNumParams(); ++i) {
        assert(*TrueBB->getParam(i)->getVType() == *TrueArgs[i]->getVType()
            && "signature should match");
    }
    assert(FalseBB->getNumParams() == FalseArgs.size() && "arity should match");
    for (size_t i = 0; i != FalseBB->getNumParams(); ++i) {
        assert(*FalseBB->getParam(i)->getVType() == *FalseArgs[i]->getVType()
            && "signature should match");
    }
    return new BranchValue(
        Condition, TrueBB, std::move(TrueArgs), FalseBB, std::move(FalseArgs));
}

koopa_raw_value_t BranchValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.branch.cond = dumpRaw(getCondition());
    RawValue->kind.data.branch.true_bb = BasicBlock::dumpRaw(getTrueBB());
    dumpSlice(RawValue->kind.data.branch.true_args, trueArgs());
    RawValue->kind.data.branch.false_bb = BasicBlock::dumpRaw(getFalseBB());
    dumpSlice(RawValue->kind.data.branch.false_args, falseArgs());
    return RawValue;
}

JumpValue* JumpValue::get(BasicBlock* TargetBB, vector<Value*>&& Args)
{
    assert(!TargetBB->isEntry() && "jump value should not jump to entry body");
    assert(TargetBB->getNumParams() == Args.size() && "arity should match");
    for (size_t i = 0; i != TargetBB->getNumParams(); ++i) {
        assert(*TargetBB->getParam(i)->getVType() == *Args[i]->getVType()
            && "signature should match");
    }
    return new JumpValue(TargetBB, std::move(Args));
}

koopa_raw_value_t JumpValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.jump.target = BasicBlock::dumpRaw(getTargetBB());
    dumpSlice(RawValue->kind.data.jump.args, args());
    return RawValue;
}

CallValue::CallValue(Function* Callee, vector<Value*>&& Args, string&& Name)
    : Value(KOOPA_RVT_CALL,
          dynamic_cast<FunctionType*>(Callee->getFuncType())->getResultType(),
          std::move(Name))
    , Callee(Callee)
    , Args(std::move(Args))
{
}

CallValue* CallValue::get(
    Function* Callee, vector<Value*>&& Args, string&& Name)
{
    assert(Callee->getFuncType()->isFunctionType()
        && "the callee of call value should have function type");
    assert(Callee->getNumParams() == Args.size() && "arity should match");
    for (size_t i = 0; i != Callee->getNumParams(); ++i) {
        assert(*Callee->getParam(i)->getVType() == *Args[i]->getVType()
            && "signature should match");
    }
    return new CallValue(Callee, std::move(Args), std::move(Name));
}

koopa_raw_value_t CallValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    RawValue->kind.data.call.callee = Function::dumpRaw(getCallee());
    dumpSlice(RawValue->kind.data.call.args, args());
    return RawValue;
}

koopa_raw_value_t ReturnValue::dumpRawImpl() const
{
    koopa_raw_value_data_t* RawValue = dumpMeta();
    if (getVal() == nullptr) {
        RawValue->kind.data.ret.value = nullptr;
    } else {
        RawValue->kind.data.ret.value = dumpRaw(getVal());
    }
    return RawValue;
}

void BasicBlock::validate() const
{
    assert(insts().size() > 0 && "basic body should not be empty");
    assert(insts().back()->canTerminateBlock()
        && "basic body should end with terminator");
    for (size_t i = 0; i < insts().size() - 1; ++i) {
        assert(!getInst(i)->canTerminateBlock()
            && "basic body should contain only one terminator");
    }
}

koopa_raw_basic_block_data_t* BasicBlock::dumpMeta() const
{
    koopa_raw_basic_block_data_t* RawBB = new koopa_raw_basic_block_data_t;
    if (hasName()) {
        char* Buffer = new char[getName().size() + 1];
        copy(getName().begin(), getName().end(), Buffer);
        Buffer[getName().size()] = '\0';
        RawBB->name = Buffer;
    } else {
        RawBB->name = nullptr;
    }
    return RawBB;
}

koopa_raw_basic_block_t BasicBlock::dumpRaw(const BasicBlock* BB)
{
    static map<const BasicBlock*, koopa_raw_basic_block_t> Memo;
    koopa_raw_basic_block_t& Entry = Memo[BB];
    if (Entry == nullptr) {
        koopa_raw_basic_block_data_t* RawBB = BB->dumpMeta();
        Entry = RawBB;
        Value::dumpSlice(RawBB->params, BB->params());
        Value::dumpSlice(RawBB->insts, BB->insts());
    }
    return Entry;
}

void BasicBlock::dumpSlice(
    koopa_raw_slice_t& Slice, const vector<BasicBlock*>& Vec)
{
    Slice.kind = KOOPA_RSIK_BASIC_BLOCK;
    Slice.len = Vec.size();
    koopa_raw_basic_block_t* Buffer = new koopa_raw_basic_block_t[Vec.size()];
    for (size_t i = 0; i != Vec.size(); ++i) {
        Buffer[i] = dumpRaw(Vec[i]);
    }
    Slice.buffer = (const void**)Buffer;
}

BasicBlock* BasicBlock::fromRaw(bool IsEntry, koopa_raw_basic_block_t RawBB)
{
    static map<koopa_raw_basic_block_t, BasicBlock*> Memo;
    BasicBlock*& Entry = Memo[RawBB];
    if (Entry == nullptr) {
        string Name = RawBB->name == nullptr ? "" : RawBB->name;
        Entry = BasicBlock::get(IsEntry, Value::fromSlice(RawBB->params),
            Value::fromSlice(RawBB->insts), Name);
        Entry->setUsedBy(RawBB->used_by);
    }
    assert(IsEntry == Entry->IsEntry && "entry should not have predecessor");
    return Entry;
}

vector<BasicBlock*>& BasicBlock::fromSlice(const koopa_raw_slice_t& Slice)
{
    static map<pair<const void**, size_t>, vector<BasicBlock*>> Memo;
    if (Memo.count({ Slice.buffer, Slice.len }) == 0) {
        vector<BasicBlock*>& Entry = Memo[{ Slice.buffer, Slice.len }];
        Entry.resize(Slice.len);
        for (size_t i = 0; i != Slice.len; ++i) {
            Entry[i]
                = fromRaw(i == 0, (koopa_raw_basic_block_t)Slice.buffer[i]);
        }
        return Entry;
    } else {
        return Memo[{ Slice.buffer, Slice.len }];
    }
}

void Function::validate() const
{
    assert(getFuncType()->isFunctionType()
        && "function should have function type");
    FunctionType* FType = dynamic_cast<FunctionType*>(getFuncType());
    assert(FType->getNumParams() == getNumParams() && "arity should match");
    for (size_t i = 0; i < FType->getNumParams(); ++i) {
        assert(*FType->getParamType(i) == *getParam(i)->getVType()
            && "signature should match");
    }
    assert(getNumBBs() > 0 && "function body should not be empty");
    for (BasicBlock* BB : bbs()) {
        BB->validate();
    }
}

koopa_raw_function_data_t* Function::dumpMeta() const
{
    koopa_raw_function_data_t* RawFunc = new koopa_raw_function_data_t;
    RawFunc->ty = Type::dumpRaw(FuncType);
    if (hasName()) {
        char* Buffer = new char[getName().size() + 1];
        copy(getName().begin(), getName().end(), Buffer);
        Buffer[getName().size()] = '\0';
        RawFunc->name = Buffer;
    } else {
        RawFunc->name = nullptr;
    }
    return RawFunc;
}

koopa_raw_function_t Function::dumpRaw(const Function* Func)
{
    static map<const Function*, koopa_raw_function_t> Memo;
    koopa_raw_function_t& Entry = Memo[Func];
    if (Entry == nullptr) {
        koopa_raw_function_data_t* RawFunc = Func->dumpMeta();
        Entry = RawFunc;
        Value::dumpSlice(RawFunc->params, Func->params());
        BasicBlock::dumpSlice(RawFunc->bbs, Func->bbs());
    }
    return Entry;
}

void Function::dumpSlice(koopa_raw_slice_t& Slice, const vector<Function*>& Vec)
{
    Slice.kind = KOOPA_RSIK_FUNCTION;
    Slice.len = Vec.size();
    koopa_raw_function_t* Buffer = new koopa_raw_function_t[Vec.size()];
    for (size_t i = 0; i != Vec.size(); ++i) {
        Buffer[i] = dumpRaw(Vec[i]);
    }
    Slice.buffer = (const void**)Buffer;
}

Function* Function::fromRaw(koopa_raw_function_t RawFunc)
{
    static map<koopa_raw_function_t, Function*> Memo;
    Function*& Entry = Memo[RawFunc];
    if (Entry == nullptr) {
        string Name = RawFunc->name == nullptr ? "" : RawFunc->name;
        Entry = Function::get(Type::fromRaw(RawFunc->ty),
            Value::fromSlice(RawFunc->params),
            BasicBlock::fromSlice(RawFunc->bbs), Name);
    }
    return Entry;
}

vector<Function*>& Function::fromSlice(const koopa_raw_slice_t& Slice)
{
    static map<pair<const void**, size_t>, vector<Function*>> Memo;
    if (Memo.count({ Slice.buffer, Slice.len }) == 0) {
        vector<Function*>& Entry = Memo[{ Slice.buffer, Slice.len }];
        Entry.resize(Slice.len);
        for (size_t i = 0; i != Slice.len; ++i) {
            Entry[i] = fromRaw((koopa_raw_function_t)Slice.buffer[i]);
        }
        return Entry;
    } else {
        return Memo[{ Slice.buffer, Slice.len }];
    }
}

Program* Program::validate()
{
    for (Function* Func : funcs()) {
        Func->validate();
    }
    koopa_raw_program_t RawProg = dumpRaw(this);
    koopa_program_t Prog;
    auto err = koopa_generate_raw_to_koopa(&RawProg, &Prog);
    (void)err;
    assert(err == KOOPA_EC_SUCCESS && "koopa validation is not passed");
    koopa_dump_to_stdout(Prog);
    koopa_raw_program_builder_t Builder = koopa_new_raw_program_builder();
    RawProg = koopa_build_raw_program(Builder, Prog);
    koopa_delete_program(Prog);
    return fromRaw(Builder, RawProg);
}

koopa_raw_program_t Program::dumpRaw(const Program* Prog)
{
    koopa_raw_program_t RawProg;
    Value::dumpSlice(RawProg.values, Prog->vals());
    Function::dumpSlice(RawProg.funcs, Prog->funcs());
    return RawProg;
}

Program* Program::fromRaw(
    koopa_raw_program_builder_t Builder, koopa_raw_program_t RawProg)
{
    return get(Builder, Value::fromSlice(RawProg.values),
        Function::fromSlice(RawProg.funcs));
}
} // namespace koopa
