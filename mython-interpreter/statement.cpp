#include "statement.h"

#include <cassert>
#include <iostream>
#include <sstream>

using namespace std;

namespace ast {

using runtime::Bool;
using runtime::IsTrue;
using runtime::Class;
using runtime::ClassInstance;
using runtime::Closure;
using runtime::Context;
using runtime::Number;
using runtime::ObjectHolder;
using runtime::String;

namespace {
const string ADD_METHOD = "__add__"s;
const string INIT_METHOD = "__init__"s;
const string STR_METHOD = "__str__"s;
}  // namespace

ObjectHolder Assignment::Execute(Closure& closure, Context& context) {
    ObjectHolder rv_obj = right_value_->Execute(closure, context);
    closure.insert_or_assign(var_name_, std::move(rv_obj));
    return closure.at(var_name_);
}

Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv)
    : var_name_{std::move(var)}, right_value_{std::move(rv)} {
}

VariableValue::VariableValue(const std::string& var_name)
    : dotted_ids_{var_name} {
}

VariableValue::VariableValue(std::vector<std::string> dotted_ids)
    : dotted_ids_(std::move(dotted_ids)) {
}

ObjectHolder VariableValue::Execute(Closure& closure, Context& /*context*/) {
    assert(!dotted_ids_.empty());
    auto it = closure.find(dotted_ids_[0]);
    if (it == closure.end()) {
        throw std::runtime_error("There is not a variable with a name: " + dotted_ids_[0]);
    }
    ObjectHolder obj = it->second;
    for (size_t i = 1; i < dotted_ids_.size(); ++i) {
        ClassInstance* cli = obj.TryAs<ClassInstance>();
        assert(cli);
        obj = cli->Fields().at(dotted_ids_[i]);
    }
    return obj;
}

unique_ptr<Print> Print::Variable(const std::string& name) {
    return make_unique<Print>(Print{make_unique<VariableValue>(VariableValue{name})});
}

Print::Print(unique_ptr<Statement> arg) : args_{} {
    args_.push_back(std::move(arg));
}

Print::Print(vector<unique_ptr<Statement>> args) : args_{std::move(args)} {
}

ObjectHolder Print::Execute(Closure& closure, Context& context) {
    bool is_first = true;
    auto& os = context.GetOutputStream();
    for (const auto& arg : args_) {
        if (!is_first) {
            os << ' ';
        }
        is_first = false;
        arg->Execute(closure, context).Print(os, context);
    }
    os << '\n';
    return {};
}

MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                       std::vector<std::unique_ptr<Statement>> args)
    : object_{std::move(object)}, method_name_{std::move(method)}, args_{std::move(args)} {
}

ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
    ObjectHolder obj = object_->Execute(closure, context);
    ClassInstance* cli = obj.TryAs<ClassInstance>();
    if (!cli) {
        throw runtime_error("Cannot call a method, not a ClassInstance"s);
    }
    assert(cli->HasMethod(method_name_, args_.size()));
    std::vector<ObjectHolder> actual_args;
    for (auto& arg : args_) {
        actual_args.push_back(std::move(arg->Execute(closure, context)));
    }
    return cli->Call(method_name_, actual_args, context);
}

const std::unique_ptr<Statement>& UnaryOperation::GetArgument() const {
    return argument_;
}

ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
    ostringstream os;
    GetArgument()->Execute(closure, context).Print(os, context);
    return ObjectHolder::Own(String{os.str()});
}

const std::unique_ptr<Statement>& BinaryOperation::GetLeftArgument() const {
    return lhs_;
}

const std::unique_ptr<Statement>& BinaryOperation::GetRightArgument() const {
    return rhs_;
}

ObjectHolder Add::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs = GetLeftArgument()->Execute(closure, context);
    ObjectHolder rhs = GetRightArgument()->Execute(closure, context);
    if (Number *l = lhs.TryAs<Number>(), *r = rhs.TryAs<Number>(); l && r) {
        return ObjectHolder::Own(Number{l->GetValue() + r->GetValue()});
    }
    if (String *l = lhs.TryAs<String>(), *r = rhs.TryAs<String>(); l && r) {
        return ObjectHolder::Own(String{l->GetValue() + r->GetValue()});
    }
    if (ClassInstance *l = lhs.TryAs<ClassInstance>(); rhs && l && l->HasMethod(ADD_METHOD, 1U)) {
        return l->Call(ADD_METHOD, {ObjectHolder::Share(*rhs)}, context);
    }
    throw runtime_error("Bad addition"s);
}

ObjectHolder Sub::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs = GetLeftArgument()->Execute(closure, context);
    ObjectHolder rhs = GetRightArgument()->Execute(closure, context);
    if (Number *l = lhs.TryAs<Number>(), *r = rhs.TryAs<Number>(); l && r) {
        return ObjectHolder::Own(Number{l->GetValue() - r->GetValue()});
    }
    throw runtime_error("Bad subtraction"s);
}

ObjectHolder Mult::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs = GetLeftArgument()->Execute(closure, context);
    ObjectHolder rhs = GetRightArgument()->Execute(closure, context);
    if (Number *l = lhs.TryAs<Number>(), *r = rhs.TryAs<Number>(); l && r) {
        return ObjectHolder::Own(Number{l->GetValue() * r->GetValue()});
    }
    throw runtime_error("Bad multiplication"s);
}

ObjectHolder Div::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs = GetLeftArgument()->Execute(closure, context);
    ObjectHolder rhs = GetRightArgument()->Execute(closure, context);
    if (Number *l = lhs.TryAs<Number>(), *r = rhs.TryAs<Number>(); l && r && r->GetValue() != 0) {
        return ObjectHolder::Own(Number{l->GetValue() / r->GetValue()});
    }
    throw runtime_error("Bad division"s);
}

void Compound::AddStatement(std::unique_ptr<Statement> stmt) {
    args_.push_back(std::move(stmt));
}

ObjectHolder Compound::Execute(Closure& closure, Context& context) {
    for (auto& arg : args_) {
        arg->Execute(closure, context);
    }
    return {};
}

Return::Return(std::unique_ptr<Statement> statement) : statement_{std::move(statement)} {
}

ObjectHolder Return::Execute(Closure& closure, Context& context) {
    throw statement_->Execute(closure, context);
}

ClassDefinition::ClassDefinition(ObjectHolder cls) : class_definition_{std::move(cls)} {
}

ObjectHolder ClassDefinition::Execute(Closure& closure, Context& /*context*/) {
    closure.emplace(class_definition_.TryAs<Class>()->GetName(), class_definition_);
    return {};
}

FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
                                 std::unique_ptr<Statement> rv)
    : object_{std::move(object)} , field_name_{std::move(field_name)}, right_value_{std::move(rv)} {

}

ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
    ObjectHolder rv_obj = right_value_->Execute(closure, context);
    ClassInstance* cli = object_.Execute(closure, context).TryAs<ClassInstance>();
    assert(cli);
    cli->Fields()[field_name_] = rv_obj;
    return rv_obj;
}

IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
               std::unique_ptr<Statement> else_body) : condition_{std::move(condition)},
    if_body_{std::move(if_body)}, else_body_{std::move(else_body)} {
}

ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
    if (IsTrue(condition_->Execute(closure, context))) {
        return if_body_->Execute(closure, context);
    }
    if (else_body_) {
        return else_body_->Execute(closure, context);
    }
    return {};
}

ObjectHolder Or::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs = GetLeftArgument()->Execute(closure, context);
    if (bool l = IsTrue(lhs); !l) {
        return ObjectHolder::Own(Bool{l || IsTrue(GetRightArgument()->Execute(closure, context))});
    }
    return ObjectHolder::Own(Bool{true});
}

ObjectHolder And::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs = GetLeftArgument()->Execute(closure, context);
    if (bool l = IsTrue(lhs); l) {
        return ObjectHolder::Own(Bool{l && IsTrue(GetRightArgument()->Execute(closure, context))});
    }
    return ObjectHolder::Own(Bool{false});
}

ObjectHolder Not::Execute(Closure& closure, Context& context) {
    return ObjectHolder::Own(Bool{!IsTrue(GetArgument()->Execute(closure, context))});
}

Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
    : BinaryOperation(std::move(lhs), std::move(rhs)), comparator_{cmp} {
}

ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
    ObjectHolder lhs = GetLeftArgument()->Execute(closure, context);
    ObjectHolder rhs = GetRightArgument()->Execute(closure, context);
    return ObjectHolder::Own(Bool{comparator_(lhs, rhs, context)});
}

NewInstance::NewInstance(const runtime::Class& cls, std::vector<std::unique_ptr<Statement>> args)
    : class_{cls}, args_{std::move(args)} {
}

NewInstance::NewInstance(const runtime::Class& cls) : class_{cls}, args_{} {
}

ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
    ObjectHolder new_object = ObjectHolder::Own(ClassInstance{class_});
    ClassInstance* new_instance = new_object.TryAs<ClassInstance>();
    if (!new_instance->HasMethod(INIT_METHOD, args_.size())) {
        return new_object;
    }
    std::vector<ObjectHolder> actual_args;
    actual_args.reserve(args_.size());
    for (auto& arg : args_) {
        actual_args.emplace_back(arg->Execute(closure, context));
    }
    new_instance->Call(INIT_METHOD, actual_args, context);
    return new_object;
}

MethodBody::MethodBody(std::unique_ptr<Statement>&& body)
    : body_{std::forward<std::unique_ptr<Statement>>(body)} {
}

ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
    try {
        body_->Execute(closure, context);
        return {};
    } catch(ObjectHolder& obj) {
        return obj;
    }
}

}  // namespace ast
