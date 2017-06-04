#include <algorithm>
#include <set>
#include <list>
#include <cmath>
#include <cassert>

#include "ao/tree/cache.hpp"
#include "ao/tree/template.hpp"

namespace Kernel {

Tree::Tree()
    : ptr(nullptr)
{
    // Nothing to do here
}

Tree::Tree(float v)
    : ptr(Cache::instance()->constant(v))
{
    // Nothing to do here
}

Tree::Tree(Opcode::Opcode op, Tree a, Tree b)
    : ptr(Cache::instance()->operation(op, a.ptr, b.ptr))
{
    // Aggressive sanity-checking
    assert((Opcode::args(op) == 0 && a.ptr.get() == 0 && b.ptr.get() == 0) ||
           (Opcode::args(op) == 1 && a.ptr.get() != 0 && b.ptr.get() == 0) ||
           (Opcode::args(op) == 2 && a.ptr.get() != 0 && b.ptr.get() != 0));

    // POW only accepts integral values as its second argument
    if (op == Opcode::POW)
    {
        assert(b->op == Opcode::CONST &&
               b->value == std::round(b->value));
    }
    else if (op == Opcode::NTH_ROOT)
    {
        assert(b->op == Opcode::CONST &&
               b->value == std::round(b->value) &&
               b->value > 0);
    }
}

Tree Tree::var()
{
    return Tree(Cache::instance()->var());
}

Tree::Tree_::~Tree_()
{
    if (op == Opcode::CONST)
    {
        Cache::instance()->del(value);
    }
    else if (op != Opcode::VAR)
    {
        Cache::instance()->del(op, lhs, rhs);
    }
}

std::list<Tree> Tree::ordered() const
{
    std::set<Id> found = {nullptr};
    std::list<std::shared_ptr<Tree_>> todo = { ptr };
    std::map<unsigned, std::list<std::shared_ptr<Tree_>>> ranks;

    while (todo.size())
    {
        auto t = todo.front();
        todo.pop_front();

        if (found.find(t.get()) == found.end())
        {
            todo.push_back(t->lhs);
            todo.push_back(t->rhs);
            found.insert(t.get());
            ranks[t->rank].push_back(t);
        }
    }

    std::list<Tree> out;
    for (auto& r : ranks)
    {
        for (auto& t : r.second)
        {
            out.push_back(Tree(t));
        }
    }
    return out;
}

std::vector<uint8_t> Tree::serialize() const
{
    return serialize(*this);
}

std::vector<uint8_t> Tree::serialize(const Template& t)
{
    static_assert(Opcode::LAST_OP <= 255, "Too many opcodes");

    std::vector<uint8_t> out;
    out.push_back('T');
    serializeString(t.name, out);
    serializeString(t.doc, out);

    std::map<Tree::Id, uint32_t> ids;

    for (auto& n : t.tree.ordered())
    {
        out.push_back(n->op);
        ids.insert({n.id(), ids.size()});

        // Write constants as raw bytes
        if (n->op == Opcode::CONST)
        {
            serializeBytes(n->value, out);
        }
        else if (n->op == Opcode::VAR)
        {
            auto a = t.var_names.find(n.id());
            auto d = t.var_docs.find(n.id());
            serializeString(a == t.var_names.end() ? "" : a->second, out);
            serializeString(d == t.var_docs.end()  ? "" : d->second, out);
        }
        switch (Opcode::args(n->op))
        {
            case 2:  serializeBytes(ids.at(n->rhs.get()), out);    // FALLTHROUGH
            case 1:  serializeBytes(ids.at(n->lhs.get()), out);    // FALLTHROUGH
            default: break;
        }
    }
    return out;
}

void Tree::serializeString(const std::string& s, std::vector<uint8_t>& out)
{
    out.push_back('"');
    for (auto& c : s)
    {
        if (c == '"' || c == '\\')
        {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
}

Tree Tree::remap(Tree X_, Tree Y_, Tree Z_) const
{
    std::map<Tree::Id, std::shared_ptr<Tree_>> m = {
        {X().id(), X_.ptr}, {Y().id(), Y_.ptr}, {Z().id(), Z_.ptr}};

    for (const auto& t : ordered())
    {
        if (Opcode::args(t->op) >= 1)
        {
            auto lhs = m.find(t->lhs.get());
            auto rhs = m.find(t->rhs.get());
            m.insert({t.id(), Cache::instance()->operation(t->op,
                        lhs == m.end() ? t->lhs : lhs->second,
                        rhs == m.end() ? t->rhs : rhs->second)});
        }
    }

    // If this Tree was remapped, then return it; otherwise return itself
    auto r = m.find(id());
    return r == m.end() ? *this : Tree(r->second);
}

}   // namespace Kernel

////////////////////////////////////////////////////////////////////////////////

// Mass-produce definitions for overloaded operations
#define OP_UNARY(name, opcode) \
Kernel::Tree name(const Kernel::Tree& a) { return Kernel::Tree(opcode, a); }
OP_UNARY(square,    Kernel::Opcode::SQUARE);
OP_UNARY(sqrt,      Kernel::Opcode::SQRT);
Kernel::Tree Kernel::Tree::operator-() const
    { return Kernel::Tree(Kernel::Opcode::NEG, *this); }
Kernel::Tree abs(const Kernel::Tree& a) { return max(a, -a); }
OP_UNARY(sin,       Kernel::Opcode::SIN);
OP_UNARY(cos,       Kernel::Opcode::COS);
OP_UNARY(tan,       Kernel::Opcode::TAN);
OP_UNARY(asin,      Kernel::Opcode::ASIN);
OP_UNARY(acos,      Kernel::Opcode::ACOS);
OP_UNARY(atan,      Kernel::Opcode::ATAN);
OP_UNARY(exp,       Kernel::Opcode::EXP);
#undef OP_UNARY

#define OP_BINARY(name, opcode) \
Kernel::Tree name(const Kernel::Tree& a, const Kernel::Tree& b) \
    { return Kernel::Tree(opcode, a, b); }
OP_BINARY(operator+,    Kernel::Opcode::ADD);
OP_BINARY(operator*,    Kernel::Opcode::MUL);
OP_BINARY(min,          Kernel::Opcode::MIN);
OP_BINARY(max,          Kernel::Opcode::MAX);
OP_BINARY(operator-,    Kernel::Opcode::SUB);
OP_BINARY(operator/,    Kernel::Opcode::DIV);
OP_BINARY(atan2,        Kernel::Opcode::ATAN2);
OP_BINARY(pow,          Kernel::Opcode::POW);
OP_BINARY(nth_root,     Kernel::Opcode::NTH_ROOT);
OP_BINARY(mod,          Kernel::Opcode::MOD);
OP_BINARY(nanfill,      Kernel::Opcode::NANFILL);
#undef OP_BINARY