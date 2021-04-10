//------------------------------------------------------------------------------
//! @file AssertionExpr.h
//! @brief Assertion expression creation and analysis
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/symbols/ASTSerializer.h"
#include "slang/util/Util.h"

namespace slang {

// clang-format off
#define EXPR(x) \
    x(Invalid) \
    x(Simple) \
    x(SequenceConcat) \
    x(Binary)
ENUM(AssertionExprKind, EXPR);
#undef EXPR

#define OP(x) \
    x(And) \
    x(Or) \
    x(Intersect) \
    x(Throughout) \
    x(Within) \
    x(Iff) \
    x(Until) \
    x(SUntil) \
    x(UntilWith) \
    x(SUntilWith) \
    x(Implies) \
    x(Implication) \
    x(FollowedBy)
ENUM(BinaryAssertionOperator, OP);
#undef OP
// clang-format on

class BindContext;
class Compilation;
class SyntaxNode;
struct PropertyExprSyntax;
struct SequenceExprSyntax;

class AssertionExpr {
public:
    AssertionExprKind kind;

    const SyntaxNode* syntax = nullptr;

    bool bad() const { return kind == AssertionExprKind::Invalid; }

    static const AssertionExpr& bind(const SequenceExprSyntax& syntax, const BindContext& context);
    static const AssertionExpr& bind(const PropertyExprSyntax& syntax, const BindContext& context);

    template<typename T>
    T& as() {
        ASSERT(T::isKind(kind));
        return *static_cast<T*>(this);
    }

    template<typename T>
    const T& as() const {
        ASSERT(T::isKind(kind));
        return *static_cast<const T*>(this);
    }

    template<typename TVisitor, typename... Args>
    decltype(auto) visit(TVisitor& visitor, Args&&... args) const;

protected:
    explicit AssertionExpr(AssertionExprKind kind) : kind(kind) {}

    static AssertionExpr& badExpr(Compilation& compilation, const AssertionExpr* expr);
};

class InvalidAssertionExpr : public AssertionExpr {
public:
    const AssertionExpr* child;

    explicit InvalidAssertionExpr(const AssertionExpr* child) :
        AssertionExpr(AssertionExprKind::Invalid), child(child) {}

    static bool isKind(AssertionExprKind kind) { return kind == AssertionExprKind::Invalid; }

    void serializeTo(ASTSerializer& serializer) const;
};

struct RangeSelectSyntax;
struct SequenceRepetitionSyntax;

/// Represents a range of potential sequence matches.
struct SequenceRange {
    /// The minimum length of the range.
    uint32_t min = 0;

    /// The maximum length of the range. If unset, the maximum is unbounded.
    optional<uint32_t> max;

    static SequenceRange fromSyntax(const RangeSelectSyntax& syntax, const BindContext& context);
};

/// Encodes a repetition of some sub-sequence.
struct SequenceRepetition {
    /// The kind of repetition.
    enum Kind {
        /// A repetition with a match on each consecutive cycle.
        Consecutive,

        /// A nonconsecutive repetition that does not necessarily end
        /// at the last iterative match.
        Nonconsecutive,

        /// A nonconsecutive repetition which ends at the last iterative match.
        GoTo
    } kind = Consecutive;

    /// The range of cycles over which to repeat.
    SequenceRange range;

    SequenceRepetition(const SequenceRepetitionSyntax& syntax, const BindContext& context);

    void serializeTo(ASTSerializer& serializer) const;
};

struct SimpleSequenceExprSyntax;

/// Represents an assertion expression defined as a simple regular expression.
class SimpleAssertionExpr : public AssertionExpr {
public:
    const Expression& expr;
    optional<SequenceRepetition> repetition;

    SimpleAssertionExpr(const Expression& expr, optional<SequenceRepetition> repetition) :
        AssertionExpr(AssertionExprKind::Simple), expr(expr), repetition(repetition) {}

    static AssertionExpr& fromSyntax(const SimpleSequenceExprSyntax& syntax,
                                     const BindContext& context);

    void serializeTo(ASTSerializer& serializer) const;

    static bool isKind(AssertionExprKind kind) { return kind == AssertionExprKind::Simple; }
};

struct DelayedSequenceExprSyntax;

/// Represents an assertion expression defined as a simple regular expression.
class SequenceConcatExpr : public AssertionExpr {
public:
    struct Element {
        SequenceRange delay;
        not_null<const AssertionExpr*> sequence;
    };

    span<const Element> elements;

    explicit SequenceConcatExpr(span<const Element> elements) :
        AssertionExpr(AssertionExprKind::SequenceConcat), elements(elements) {}

    static AssertionExpr& fromSyntax(const DelayedSequenceExprSyntax& syntax,
                                     const BindContext& context);

    void serializeTo(ASTSerializer& serializer) const;

    static bool isKind(AssertionExprKind kind) { return kind == AssertionExprKind::SequenceConcat; }
};

struct BinarySequenceExprSyntax;
struct BinaryPropertyExprSyntax;

/// Represents a binary operator in a sequence or property expression.
class BinaryAssertionExpr : public AssertionExpr {
public:
    BinaryAssertionOperator op;
    const AssertionExpr& left;
    const AssertionExpr& right;

    BinaryAssertionExpr(BinaryAssertionOperator op, const AssertionExpr& left,
                        const AssertionExpr& right) :
        AssertionExpr(AssertionExprKind::Binary),
        op(op), left(left), right(right) {}

    static AssertionExpr& fromSyntax(const BinarySequenceExprSyntax& syntax,
                                     const BindContext& context);

    static AssertionExpr& fromSyntax(const BinaryPropertyExprSyntax& syntax,
                                     const BindContext& context);

    void serializeTo(ASTSerializer& serializer) const;

    static bool isKind(AssertionExprKind kind) { return kind == AssertionExprKind::Binary; }
};

} // namespace slang