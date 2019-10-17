#include <torch/csrc/jit/scope.h>
#include <torch/csrc/jit/function.h>

namespace torch {
namespace jit {

ScopePtr Scope::intrusive_from_this() {
  c10::raw::intrusive_ptr::incref(this); // we are creating a new pointer
                                         // from a raw `this` pointer
                                         // so we need to bump the refcount
                                         // to account for this ownership
  return c10::intrusive_ptr<Scope>::reclaim(this);
}

Scope::Scope() {
  name_ = Symbol::scope("");
}

Scope::Scope(ScopePtr parent, Symbol name) {
  name_ = name;
  parent_ = std::move(parent);
}

ScopePtr Scope::push(Symbol name) {
  return c10::make_intrusive<Scope>(intrusive_from_this(), name);
}

ScopePtr Scope::parent() {
  if (!parent_) {
    throw std::runtime_error("Cannot get parent from Scope with no parent");
  }
  return parent_;
}

bool Scope::isRoot() const {
  return !parent_;
}

bool Scope::isBlank() const {
  static const Symbol blank = Symbol::scope("");
  return isRoot() && name() == blank;
}

ScopePtr Scope::getRoot() {
  ScopePtr current = intrusive_from_this();
  while (current->parent_) {
    current = current->parent_;
  }
  return current;
}

size_t Scope::getDepth() {
  size_t d = 1;
  ScopePtr current = intrusive_from_this();
  while (current->parent_) {
    current = current->parent_;
    d += 1;
  }
  return d;
}

Symbol Scope::name() const {
  return name_;
}

std::string Scope::namesFromRoot(const std::string& separator) const {
  // TODO: I think the answer is we shouldn't have used Symbol here
  std::string out = this->name_.toUnqualString();
  if (this->isRoot()) {
    return out;
  }
  ScopePtr parent = this->parent_;
  while (!parent->isRoot()) {
    // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
    out = std::string(parent->name_.toUnqualString()) + separator + out;
    parent = parent->parent_;
  }
  return out;
}

InlinedCallStackPtr InlinedCallStack::intrusive_from_this() {
  c10::raw::intrusive_ptr::incref(this); // we are creating a new pointer
                                         // from a raw `this` pointer
                                         // so we need to bump the refcount
                                         // to account for this ownership
  return c10::intrusive_ptr<InlinedCallStack>::reclaim(this);
}

InlinedCallStack::InlinedCallStack(Function* fn, SourceRange source_range)
    : fn_(fn), source_range_(source_range) {}

InlinedCallStack::InlinedCallStack(
    InlinedCallStackPtr caller,
    Function* fn,
    SourceRange source_range)
    : fn_(fn), source_range_(source_range) {
  caller_ = std::move(caller);
}

InlinedCallStackPtr InlinedCallStack::insertCallStackEntry(
    Function* fn,
    SourceRange source_range) {
  auto ent = std::make_pair(fn, source_range);
  if (callees_.count(ent)) {
    return callees_.at(ent);
  }
  auto subscope = c10::make_intrusive<InlinedCallStack>(
      intrusive_from_this(), fn, source_range);
  callees_[ent] = subscope;
  return subscope;
}

c10::optional<InlinedCallStackPtr> InlinedCallStack::caller() const {
  return caller_;
}

std::vector<InlinedCallStackEntry> InlinedCallStack::vec() {
  std::vector<InlinedCallStackEntry> r;
  c10::optional<InlinedCallStackPtr> current = intrusive_from_this();
  while (current) {
    r.push_back(std::make_pair((*current)->fn_, (*current)->source_range_));
    current = (*current)->caller_;
  }
  return r;
}
} // namespace jit
} // namespace torch
