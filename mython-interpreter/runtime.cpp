#include "runtime.h"

#include <cassert>

#include <iostream>

#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
    : data_(std::move(data)) {
}

void ObjectHolder::AssertIsValid() const {
    assert(data_ != nullptr);
}

ObjectHolder ObjectHolder::Share(Object& object) {
    // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
    return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
}

ObjectHolder ObjectHolder::None() {
    return ObjectHolder();
}

Object& ObjectHolder::operator*() const {
    AssertIsValid();
    return *Get();
}

Object* ObjectHolder::operator->() const {
    AssertIsValid();
    return Get();
}

Object* ObjectHolder::Get() const {
    return data_.get();
}

void ObjectHolder::Print(std::ostream& os, Context& context) const {
    if (data_) {
        data_->Print(os, context);
    } else {
        os << "None"s;
    }
}

ObjectHolder::operator bool() const {
    return Get() != nullptr;
}

bool IsTrue(const ObjectHolder& object) {
    if (!object) {
        return false;
    }
    if (const String* str = object.TryAs<String>(); str) {
        return str->GetValue() != std::string{};
    }
    if (const Number* num = object.TryAs<Number>(); num) {
        return num->GetValue() != 0;
    }
    if (const Bool* bl = object.TryAs<Bool>(); bl) {
        return bl->GetValue() != false;
    }
    if (object.TryAs<Class>() || object.TryAs<ClassInstance>()) {
        return false;
    }
    return true;
}

void ClassInstance::Print(std::ostream& os, Context& context) {
    if (HasMethod("__str__"s, 0U)) {
        Call("__str__"s, {}, context)->Print(os, context);
    } else {
        os << this;
    }
}

bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
    const Method* m = class_.GetMethod(method);
    return m != nullptr && m->formal_params.size() == argument_count;
}

Closure& ClassInstance::Fields() {
    return closure_;
}

const Closure& ClassInstance::Fields() const {
    return const_cast<ClassInstance*>(this)->Fields();
}

ClassInstance::ClassInstance(const Class& cls) : class_(cls) {
}

ObjectHolder ClassInstance::Call(const std::string& method_name,
                                 const std::vector<ObjectHolder>& actual_args,
                                 Context& context) {
    const Method* method = class_.GetMethod(method_name);
    if (!method || method->formal_params.size() != actual_args.size()) {
        throw runtime_error("The name of the method or the number of params is invalid.");
    }
    Closure closure; // should compose with the next statement
    closure.emplace("self"s, ObjectHolder::Share(*this));
    for (size_t i = 0; i < method->formal_params.size(); ++i) {
        closure.emplace(method->formal_params[i], actual_args[i]);
    }
    return method->body->Execute(closure, context);
}

Class::Class(std::string name, std::vector<Method> methods, const Class* parent) : name_(name), parent_(parent) {
    for (auto& m : methods) {
        methods_.emplace(m.name, std::move(m));
    }
}

const Method* Class::GetMethod(const std::string& name) const {
    if (auto it = methods_.find(name); it != methods_.end()) {
        return &it->second;
    }
    if (parent_) {
        return parent_->GetMethod(name);
    }
    return nullptr;
}

[[nodiscard]] /*inline*/ const std::string& Class::GetName() const {
    return name_;
}

void Class::Print(ostream& os, Context& /*context*/) {
    os << "Class " << GetName();
}

void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
    os << (GetValue() ? "True"sv : "False"sv);
}

bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    if (!lhs && !rhs) {
        return true;
    }
    if (ClassInstance* l = lhs.TryAs<ClassInstance>(); l && l->HasMethod("__eq__"s, 1U)) {
        auto res = l->Call("__eq__"s, {rhs}, context).TryAs<Bool>();
        if (!res) {
            throw std::runtime_error("Cannot compare class instances for equality"s);
        }
        return res->GetValue();
    }
    if (const String *l = lhs.TryAs<String>(), *r = rhs.TryAs<String>(); l && r) {
        return l->GetValue() == r->GetValue();
    }
    if (const Number *l = lhs.TryAs<Number>(), *r = rhs.TryAs<Number>(); l && r) {
        return l->GetValue() == r->GetValue();
    }
    if (const Bool *l = lhs.TryAs<Bool>(), *r = rhs.TryAs<Bool>(); l && r) {
        return l->GetValue() == r->GetValue();
    }
    throw std::runtime_error("Cannot compare objects for equality"s);
}

bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    if (ClassInstance* l = lhs.TryAs<ClassInstance>(); l && l->HasMethod("__lt__"s, 1U)) {
        auto res = l->Call("__lt__"s, {rhs}, context).TryAs<Bool>();
        if (!res) {
            throw std::runtime_error("Cannot compare class instances for less"s);
        }
        return res->GetValue();
    }
    if (const String *l = lhs.TryAs<String>(), *r = rhs.TryAs<String>(); l && r) {
        return l->GetValue() < r->GetValue();
    }
    if (const Number *l = lhs.TryAs<Number>(), *r = rhs.TryAs<Number>(); l && r) {
        return l->GetValue() < r->GetValue();
    }
    if (const Bool *l = lhs.TryAs<Bool>(), *r = rhs.TryAs<Bool>(); l && r) {
        return l->GetValue() < r->GetValue();
    }
    throw std::runtime_error("Cannot compare objects for less"s);
}

bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Equal(lhs, rhs, context);
}

bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !(Less(lhs, rhs, context) || Equal(lhs, rhs, context));
}

bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Greater(lhs, rhs, context);
}

bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context);
}

}  // namespace runtime
