#include <cassert>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <unordered_set>


struct Foo { int x; Foo(int x) : x(x) {} };
struct Bar {};

struct AST: Arena<Foo, Bar> {
    Foo &foo1(Handle<Foo> foo) {
        return (*this)[foo];
    }
    Foo &foo2(Handle<Foo> foo) {
        return foo(*this);
    }
    const Foo &foo1(Handle<Foo> foo) const {
        return (*this)[foo];
    }
    const Foo &foo2(Handle<Foo> foo) const {
        return foo(*this);
    }

    Handle<Foo> aha() {
        return alloc<Foo>(123);
    }
};


int main() {
    std::unordered_set<Handle<Foo>> buc;
}