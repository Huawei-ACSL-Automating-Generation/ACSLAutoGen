/**
 * @file expr.h
 * @brief Declares factory-owning symbolic expression and address facades used for ACSL generation.
 *
 * Public values are lightweight facades over immutable nodes interned by the current analysis
 * context's ExprFactory. Concrete nodes and handles are implementation details in `detail/`.
 */
#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_EXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_EXPR_H__

#include <clang/AST/Type.h>
#include <string>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <ranges>
#include <algorithm>
#include <type_traits>
#include <ppl.hh>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RecordLayout.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <unordered_set>

#include "macros.h"
#include "Utils/utils.h"
#include "config.h"

namespace acslg::analyzer {
    class Path;
}

namespace acslg::analyzer::symbolic {
    class ExprFactory;
    class ExprFactoryScope;
    class Addr;
    struct SymbolAddrBaseInfo;

    enum class BinaryOp : unsigned {
#define BIN_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
    };

    enum class UnaryOp : unsigned {
#define UN_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
    };

} // namespace acslg::analyzer::symbolic

namespace acslg::analyzer::symbolic::detail {
    class ExprHandle;
    class AddrHandle;
    class AddressNode;
    class SymbolAddressNode;
    class SymbolicExprNode;
    struct ExprFactoryInternals;
    struct ExprFactoryBackend;
    struct FacadeAccess;
    struct HandleAccess;
} // namespace acslg::analyzer::symbolic::detail

namespace acslg::analyzer::symbolic {

    /**
     * @class SourcePoint
     * @brief Represents a concrete source location together with a label prefix for ACSL emission.
     *
     * SourcePoint keeps a `clang::SourceLocation` plus its owning SourceManager and produces
     * stable labels used to tag synthesized symbols and inserted labels in the rewritten source.
     */
    class SourcePoint {
      public:
        SourcePoint(const SourcePoint &) = default;
        SourcePoint &operator=(const SourcePoint &);
        SourcePoint(SourcePoint &&) = default;
        SourcePoint &operator=(SourcePoint &&);

        /**
         * @brief Build a source point at the start of a function body.
         * @param FD [in] Function declaration owning the body.
         * @param SM [in] Source manager for location resolution.
         * @param LO [in] Language options for lexical queries.
         * @return SourcePoint anchored before the first body token.
         */
        static SourcePoint fromFuncDecl(const clang::FunctionDecl *FD,
                                        const clang::SourceManager &SM,
                                        const clang::LangOptions &LO);

        /**
         * @brief Build a source point immediately before a statement.
         * @param S [in] Statement to reference.
         * @param SM [in] Source manager for location resolution.
         * @param LO [in] Language options for lexical queries.
         * @return SourcePoint at the start token of the statement.
         */
        static SourcePoint fromStmtBefore(const clang::Stmt *S,
                                          const clang::SourceManager &SM,
                                          const clang::LangOptions &LO);

        /**
         * @brief Build a source point immediately after a statement.
         * @param S [in] Statement to reference.
         * @param SM [in] Source manager for location resolution.
         * @param LO [in] Language options for lexical queries.
         * @return SourcePoint at the end of the statement token range.
         */
        static SourcePoint fromStmtAfter(const clang::Stmt *S,
                                         const clang::SourceManager &SM,
                                         const clang::LangOptions &LO);

        /**
         * @brief Strict weak ordering by underlying SourceLocation.
         * @param other [in] SourcePoint to compare.
         * @return True if this point precedes the other.
         */
        bool operator<(const SourcePoint &other) const;

        /**
         * @brief Equality based on SourceLocation identity under the same SourceManager.
         * @param other [in] SourcePoint to compare.
         * @return True if both refer to the same location.
         */
        bool operator==(const SourcePoint &other) const;

        /**
         * @brief Access the underlying `clang::SourceLocation`.
         * @return Location value.
         */
        clang::SourceLocation asSourceLocation() const { return loc_; }

        /**
         * @brief Hash based on the wrapped SourceLocation.
         * @return Hash value.
         */
        size_t hash() const { return utils::hash_val(loc_.getHashValue()); }

        /**
         * @brief Dump a human-readable string representation for diagnostics.
         * @return Formatted description.
         */
        std::string dump() const;

        /**
         * @brief Produce a stable label for ACSL annotations, optionally suffixed with a hash.
         * @return Label string.
         */
        std::string getLabel() const {
            auto &config = GlobalConfig::instance();
            auto suffix =
                config.acslLabelSuffixLength
                    ? "_" + utils::hash_prefix_hex_chars(hash(), config.acslLabelSuffixLength)
                    : "";
            return labelPrefix_ + suffix;
        }

      private:
        /**
         * @brief Private constructor to initialize a SourcePoint from a SourceManager.
         *
         * Only accessible to the static factory functions.
         *
         * @param SM The SourceManager to associate with this SourcePoint.
         */
        SourcePoint(const clang::SourceManager &SM, std::string labelPrefix)
            : SM_(SM), labelPrefix_(labelPrefix){};

        clang::SourceLocation loc_;      ///< Clang source location.
        const clang::SourceManager &SM_; ///< Reference to the source manager for resolution.

        // helper member
        std::string labelPrefix_;
    };
} // namespace acslg::analyzer::symbolic

namespace std {
    template <> struct hash<acslg::analyzer::symbolic::SourcePoint> {
        size_t operator()(const acslg::analyzer::symbolic::SourcePoint &sp) const noexcept {
            return sp.hash();
        }
    };
} // namespace std

namespace acslg::analyzer::symbolic {
    enum class ExprScalarKind {
        Int,
        UInt,
        Bool,
        Void,
        Structure
    };

    struct ExprType {
        ExprScalarKind kind;
        unsigned bitWidth;

        friend bool operator==(ExprType lhs, ExprType rhs) {
            return lhs.kind == rhs.kind && lhs.bitWidth == rhs.bitWidth;
        }
        friend bool operator!=(ExprType lhs, ExprType rhs) { return !(lhs == rhs); }
    };

    struct ACSLConfig {
        bool noStateLabelFunctionAt{false};
        std::unordered_map<SourcePoint, std::string> predefinedLabels{};
        bool useDerefWithZeroOffset{true};
        bool UnknownExprAsError{true};

        struct SourcePointOutputFilter {
            std::optional<std::unordered_set<SourcePoint>> whitelist{std::nullopt};
        };
        SourcePointOutputFilter sourcePointOutputFilter{};
    };

    enum class ACSLError {
        HeapAddress,
        PartiallyModifiedStruct,
        UnknownExpr,
    };

} // namespace acslg::analyzer::symbolic

namespace acslg::analyzer::symbolic {

    /// Record metadata shared by factory-built structure nodes and read-only views.

    struct StructureInfo {
        utils::not_null<const clang::RecordDecl *> definition_;
        const clang::ASTRecordLayout &layout_;

        StructureInfo(const clang::RecordDecl *record, const clang::ASTRecordLayout &layout)
            : definition_(record), layout_(layout) {
            if (!record->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            definition_ = record->getDefinition();
        }

        StructureInfo(const StructureInfo &other)
            : definition_(other.definition_), layout_(other.layout_) {}
        StructureInfo(StructureInfo &&) = default;

        bool equal(const StructureInfo &other) const;
        bool operator==(const StructureInfo &other) const;
        std::string dump() const;
        size_t getNumFields() const { return layout_.getFieldCount(); }
    };

} // namespace acslg::analyzer::symbolic

namespace acslg::analyzer::symbolic {

    class AddressBox {
      public:
        explicit AddressBox(const Addr &address) noexcept;
        AddressBox(const AddressBox &)                = default;
        AddressBox &operator=(const AddressBox &)     = default;
        AddressBox(AddressBox &&) noexcept            = default;
        AddressBox &operator=(AddressBox &&) noexcept = default;

        Addr importedInto(ExprFactory &target) const;

        std::string dump() const;
        bool isSymbolAddress() const;
        bool isVariableAddress() const;
        bool isFieldAddress() const;
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;
        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSLOfValue(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const;

        friend bool operator==(const AddressBox &a, const AddressBox &b);

        friend bool operator!=(const AddressBox &a, const AddressBox &b) { return !(a == b); }

        std::size_t hash() const noexcept;

      private:
        friend struct detail::FacadeAccess;

        explicit AddressBox(detail::AddrHandle handle) noexcept;
        detail::AddrHandle handle() const;

        const detail::AddressNode *ptr_;
    };

    struct AddressBoxHash {
        std::size_t operator()(const AddressBox &k) const noexcept { return k.hash(); }
    };

    struct AddressBoxEq {
        bool operator()(const AddressBox &a, const AddressBox &b) const;
    };

    template <class T>
    using AddressBoxMap = std::unordered_map<AddressBox, T, AddressBoxHash, AddressBoxEq>;

} // namespace acslg::analyzer::symbolic

namespace acslg::analyzer::symbolic {

    inline bool AddressBoxEq::operator()(const AddressBox &a, const AddressBox &b) const {
        return a == b;
    }

    class ExprFactory {
      public:
        ExprFactory();
        ~ExprFactory();

      private:
        friend struct detail::ExprFactoryInternals;

        std::unique_ptr<detail::ExprFactoryBackend> backend_;
    };

    class ExprFactoryScope {
      public:
        explicit ExprFactoryScope(ExprFactory &factory);
        ~ExprFactoryScope();

        ExprFactoryScope(const ExprFactoryScope &)            = delete;
        ExprFactoryScope(ExprFactoryScope &&)                 = delete;
        ExprFactoryScope &operator=(const ExprFactoryScope &) = delete;
        ExprFactoryScope &operator=(ExprFactoryScope &&)      = delete;

        static ExprFactory &current();
        static bool hasCurrent();

      private:
        ExprFactory *previous_;
        static thread_local ExprFactory *current_;
    };

    class Expr;
    class ExprSubstitutions;
    struct ExprIdentityHash {
        std::size_t operator()(const Expr &expression) const;
    };
    using ExprSet      = std::unordered_set<Expr, ExprIdentityHash>;
    using ExprIndexMap = std::unordered_map<Expr, std::size_t, ExprIdentityHash>;

    class Addr {
      public:
        static Addr variable(utils::not_null<const clang::VarDecl *> from);
        static Addr variable(ExprFactory &factory, utils::not_null<const clang::VarDecl *> from);
        static Addr symbol(ExprFactory &factory,
                           clang::QualType pointeeType,
                           SourcePoint fromPoint);
        static Addr symbol(clang::QualType pointeeType, SourcePoint fromPoint);
        static Addr symbol(clang::QualType pointeeType, SourcePoint fromPoint, const Expr &offset);
        static Addr symbol(clang::QualType pointeeType,
                           SourcePoint fromPoint,
                           const Expr &offset,
                           const Expr &length);
        static Addr symbol(clang::QualType pointeeType, const Addr &from, SourcePoint fromPoint);
        static Addr symbol(clang::QualType pointeeType,
                           const Addr &from,
                           SourcePoint fromPoint,
                           const Expr &offset);
        static Addr symbol(clang::QualType pointeeType,
                           const Addr &from,
                           SourcePoint fromPoint,
                           const Expr &offset,
                           const Expr &length);

        ExprFactory &factory() const { return *factory_; }
        Addr importedInto(ExprFactory &target) const;

        std::size_t hash() const;
        std::string dump() const;
        ExprType getValType() const;
        bool isSymbolAddress() const;
        bool isVariableAddress() const;
        bool isFieldAddress() const;
        bool structurallyEqual(const Addr &other) const;
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;
        int getDimension() const;
        const clang::QualType &pointeeType() const;
        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSL(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const;
        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSLOfValue(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const;
        Expr asExpr() const;
        Addr withOffset(const Expr &offset) const;
        Addr withAddedOffset(const Expr &extra) const;
        Addr withSubtractedOffset(const Expr &extra) const;
        Addr withLength(const Expr &length) const;
        Addr withAddedLength(const Expr &extra) const;
        Addr withoutLength() const;
        Addr field(clang::QualType pointeeType,
                   const clang::RecordDecl *record,
                   size_t fieldIndex) const;

        friend bool operator==(const Addr &lhs, const Addr &rhs) {
            return lhs.factory_ == rhs.factory_ && lhs.node_ == rhs.node_;
        }

      private:
        friend class Expr;
        friend struct detail::FacadeAccess;

        Addr(ExprFactory &factory, detail::AddrHandle handle);
        explicit Addr(detail::AddrHandle handle);
        detail::AddrHandle handle() const;
        void ensureSameFactory(const Expr &expr) const;

        ExprFactory *factory_;
        const detail::AddressNode *node_;
    };

    class Expr {
      public:
        static Expr unknown();
        static Expr unknown(ExprFactory &factory);
        static Expr rangeIndex(std::string_view name);
        static Expr rangeIndex(ExprFactory &factory, std::string_view name);
        static Expr symbolValue(ExprType varType, const Addr &from, SourcePoint fromPoint);
        static Expr symbol(clang::QualType type, const Addr &from, SourcePoint fromPoint);
        static Expr symbol(clang::QualType type, SourcePoint fromPoint);
        static Expr structure(const clang::RecordDecl *record,
                              const Addr &base,
                              SourcePoint fromPoint);

        ExprFactory &factory() const { return *factory_; }
        Expr importedInto(ExprFactory &target) const;

        std::size_t hash() const;
        std::string dump() const;
        ExprType getValType() const;
        bool isUnknown() const;
        bool isRangeIndex() const;
        bool isSymbolValue() const;
        bool isStructure() const;
        bool isOverRange() const;
        int getMaxDegree() const;
        ExprSet collectUsedSymbols() const;
        std::optional<int64_t> tryEvalAsConstant() const;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &varIndexMap) const;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprIndexMap &exprIndexMap) const;
        bool structurallyEqual(const Expr &other) const;
        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSL(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const;
        Expr withType(ExprType newType) const;
        Expr withField(size_t index, const Expr &value) const;
        Expr simplified() const;
        std::optional<Addr> tryAsAddress() const;
        std::optional<Addr> evaluatedAddress() const;
        std::optional<Addr> sourceAddress() const;
        bool isFrom(const Addr &address, const SourcePoint &point) const;
        Expr substituteValues(const ExprSubstitutions &substitutions) const;
        Expr substitutePath(const Path &path, const SourcePoint &point) const;
        Expr substituteRangeIndex(const SymbolAddrBaseInfo &rangeBase, const Expr &index) const;

        Expr binary(BinaryOp op, const Expr &rhs) const;

        Expr unary(UnaryOp op) const;

        Expr equalTo(const Expr &rhs) const { return binary(BinaryOp::Equal, rhs); }
        Expr notEqualTo(const Expr &rhs) const { return binary(BinaryOp::NotEqual, rhs); }
        Expr lessThan(const Expr &rhs) const { return binary(BinaryOp::LessThan, rhs); }
        Expr lessEqual(const Expr &rhs) const { return binary(BinaryOp::LessEqual, rhs); }
        Expr greaterThan(const Expr &rhs) const { return binary(BinaryOp::GreaterThan, rhs); }
        Expr greaterEqual(const Expr &rhs) const { return binary(BinaryOp::GreaterEqual, rhs); }
        Expr logicalAnd(const Expr &rhs) const { return binary(BinaryOp::LogicalAnd, rhs); }
        Expr logicalOr(const Expr &rhs) const { return binary(BinaryOp::LogicalOr, rhs); }
        Expr logicalNot() const { return unary(UnaryOp::LogicalNot); }

        friend bool operator==(const Expr &lhs, const Expr &rhs) {
            return lhs.factory_ == rhs.factory_ && lhs.node_ == rhs.node_;
        }

        friend Expr operator+(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Add, rhs);
        }
        friend Expr operator-(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Subtract, rhs);
        }
        friend Expr operator*(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Multiply, rhs);
        }
        friend Expr operator/(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Divide, rhs);
        }
        friend Expr operator-(const Expr &expr) { return expr.unary(UnaryOp::Minus); }
        friend Expr operator!(const Expr &expr) { return expr.logicalNot(); }

      private:
        friend class Addr;
        friend class LiteralExpr;
        friend class SumOverRangeExpr;
        friend class QuantifierOverRangeExpr;
        friend class MaxMinOverRangeExpr;
        friend struct detail::FacadeAccess;

        Expr(ExprFactory &factory, detail::ExprHandle handle);
        explicit Expr(detail::ExprHandle handle);
        detail::ExprHandle handle() const;
        void ensureSameFactory(const Expr &rhs) const {
            if (factory_ != rhs.factory_)
                ERROR("Cannot combine expressions from different factories.");
        }

        ExprFactory *factory_;
        const detail::SymbolicExprNode *node_;
    };

    class ExprSubstitutions {
      public:
        void insertOrAssign(const Expr &source, const Expr &replacement) {
            substitutions_.insert_or_assign(source, replacement);
        }

        bool empty() const { return substitutions_.empty(); }
        std::size_t size() const { return substitutions_.size(); }

      private:
        friend class Expr;

        struct ExprIdentityHash {
            std::size_t operator()(const Expr &expression) const { return expression.hash(); }
        };

        std::unordered_map<Expr, Expr, ExprIdentityHash> substitutions_;
    };

    template <typename... Exprs> auto collectUsedSymbols(const Expr &first, const Exprs &...rest) {
        ExprSet merged;

        auto mergeIntoOne = [&](const ExprSet &symbols) {
            merged.insert(symbols.begin(), symbols.end());
        };

        mergeIntoOne(first.collectUsedSymbols());
        (mergeIntoOne(rest.collectUsedSymbols()), ...);

        std::size_t index = 0;
        ExprIndexMap expressionIndexMap;
        for (const auto &expression : merged)
            expressionIndexMap.emplace(expression, index++);
        return std::pair{std::move(merged), std::move(expressionIndexMap)};
    }

    inline void Addr::ensureSameFactory(const Expr &expr) const {
        if (factory_ != &expr.factory())
            ERROR("Cannot rebuild address with expression from a different factory.");
    }

    class VariableAddress : public Addr {
      public:
        explicit VariableAddress(const Addr &address);

        static std::optional<VariableAddress> tryFrom(const Addr &address);

        utils::not_null<const clang::VarDecl *> declaration() const;
    };

    class FieldAddress : public Addr {
      public:
        explicit FieldAddress(const Addr &address);

        static std::optional<FieldAddress> tryFrom(const Addr &address);

        utils::not_null<const clang::RecordDecl *> definition() const;
        Addr base() const;
        size_t fieldIndex() const;
    };

    /// Public factory-owning facade for a symbolic address node.
    class SymbolAddress : public Addr {
      public:
        inline static constexpr signed long ZERO_OFFSET = 0;

        explicit SymbolAddress(const Addr &address);

        static std::optional<SymbolAddress> tryFrom(const Addr &address);

        clang::QualType pointeeType() const;
        std::optional<Addr> from() const;
        std::optional<SourcePoint> fromPoint() const;
        Expr offset() const;
        std::optional<Expr> length() const;
        std::optional<Expr> rightBound() const;
        SymbolAddrBaseInfo baseInfo() const;
    };

    class LiteralExpr : public Expr {
      public:
        explicit LiteralExpr(const Expr &expression);

        static std::optional<LiteralExpr> tryFrom(const Expr &expression);

        explicit LiteralExpr(bool value);
        explicit LiteralExpr(int value);
        explicit LiteralExpr(unsigned int value);
        explicit LiteralExpr(short value);
        explicit LiteralExpr(unsigned short value);
        explicit LiteralExpr(int64_t value);
        explicit LiteralExpr(uint64_t value);
        LiteralExpr(ExprFactory &factory, bool value);
        LiteralExpr(ExprFactory &factory, int value);
        LiteralExpr(ExprFactory &factory, unsigned int value);
        LiteralExpr(ExprFactory &factory, short value);
        LiteralExpr(ExprFactory &factory, unsigned short value);
        LiteralExpr(ExprFactory &factory, int64_t value);
        LiteralExpr(ExprFactory &factory, uint64_t value);

        int64_t value() const;
    };

    class UnaryExpr : public Expr {
      public:
        explicit UnaryExpr(const Expr &expression);

        static std::optional<UnaryExpr> tryFrom(const Expr &expression);

        UnaryOp operation() const;
        Expr operand() const;
    };

    class BinaryExpr : public Expr {
      public:
        explicit BinaryExpr(const Expr &expression);

        static std::optional<BinaryExpr> tryFrom(const Expr &expression);

        BinaryOp operation() const;
        Expr left() const;
        Expr right() const;
    };

    class StructureExpr : public Expr {
      public:
        explicit StructureExpr(const Expr &expression);

        static std::optional<StructureExpr> tryFrom(const Expr &expression);

        size_t size() const;
        Expr field(size_t index) const;
        const StructureInfo &info() const;
        std::optional<SourcePoint> fromPoint() const;
    };

    struct SymbolAddrBaseInfo {
        SymbolAddrBaseInfo(SourcePoint fromPoint, clang::QualType pointeeType)
            : fromPoint_(std::move(fromPoint)), pointeeType_(pointeeType) {}
        SymbolAddrBaseInfo(const SymbolAddrBaseInfo &)            = default;
        SymbolAddrBaseInfo &operator=(const SymbolAddrBaseInfo &) = default;
        SymbolAddrBaseInfo(SymbolAddrBaseInfo &&)                 = default;
        SymbolAddrBaseInfo &operator=(SymbolAddrBaseInfo &&)      = default;

        std::optional<Addr> fromAddress(ExprFactory &factory) const;
        const SourcePoint &fromPoint() const { return fromPoint_; }
        clang::QualType pointeeType() const { return pointeeType_; }
        size_t hash() const;
        bool operator==(const SymbolAddrBaseInfo &other) const {
            if (fromPoint_ != other.fromPoint_)
                return false;
            if (fromAddr_ && other.fromAddr_ && *fromAddr_ != *other.fromAddr_)
                return false;
            if ((fromAddr_ == std::nullopt) ^ (other.fromAddr_ == std::nullopt))
                return false;
            return true;
        }

        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;

      private:
        friend class detail::SymbolAddressNode;

        SymbolAddrBaseInfo(std::optional<AddressBox> fromAddr,
                           SourcePoint fromPoint,
                           clang::QualType pointeeType)
            : fromAddr_(fromAddr), fromPoint_(std::move(fromPoint)), pointeeType_(pointeeType) {}

        std::optional<AddressBox> fromAddr_;
        SourcePoint fromPoint_;
        clang::QualType pointeeType_;
    };

    class SymbolValueExpr : public Expr {
      public:
        explicit SymbolValueExpr(const Expr &expression);

        static std::optional<SymbolValueExpr> tryFrom(const Expr &expression);

        Addr from() const;
        std::optional<SourcePoint> fromPoint() const;
        std::optional<utils::not_null<const clang::VarDecl *>> fromRoot() const;
    };

    BinaryOp getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp);
    BinaryOp getBinaryOp(clang::BinaryOperatorKind op);
    ExprType deriveType(clang::QualType type);

    enum class Operator : unsigned {
#define ALL_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
    };

    inline unsigned getPrecedence(Operator op) {
        switch (op) {
// clang-format off
#define ALL_OP(name, tok, prec, isRightAssoc)                                                      \
    case Operator::name: return prec;
#include "operators.def"
                // clang-format on
            default: ERROR("Unknown Operator");
        }
    }

    inline bool isRightAssociative(Operator op) {
        switch (op) {
// clang-format off
#define ALL_OP(name, tok, prec, isRightAssoc)                                                      \
    case Operator::name: return isRightAssoc;
#include "operators.def"
                // clang-format on
            default: ERROR("Unknown operator");
        }
    }
} // namespace acslg::analyzer::symbolic

namespace std {
    template <> struct hash<acslg::analyzer::symbolic::SymbolAddrBaseInfo> {
        size_t operator()(const acslg::analyzer::symbolic::SymbolAddrBaseInfo &bi) const noexcept {
            return bi.hash();
        }
    };
} // namespace std

namespace acslg::analyzer::symbolic {
    // @WindOctober: TODO Split define and declaration.
    // @WindOctober: TODO process more complicate expr case.
    // Strip one exact factor `sizeofBytes` if it appears as a literal factor.
    //
    // Transforms:
    //   - sizeofBytes * X  -> X
    //   - X * sizeofBytes  -> X
    //   - sizeofBytes      -> 1
    // Otherwise returns the input unchanged.
    Expr strip_sizeof_factor(const Expr &in, std::uint64_t sizeofBytes);

} // namespace acslg::analyzer::symbolic

// The project's file structure makes it difficult to distinguish between internal and external
// header files, so they are directly placed here. It would be better to place external header files
// in a unified `/include` directory, which would also help maintain consistent #include path formats.
namespace acslg::analyzer::symbolic::details {
    inline std::pair<std::string, std::string> getPrefixSuffixAndUpdateMap(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        const SourcePoint &myPoint) {
        if (!config.noStateLabelFunctionAt && myPoint != currentPoint) {
            if (config.sourcePointOutputFilter.whitelist) {
                const auto &wl = *config.sourcePointOutputFilter.whitelist;
                if (!wl.contains(myPoint))
                    return {};
            }

            if (auto it = config.predefinedLabels.find(myPoint);
                it != config.predefinedLabels.end()) {
                return std::pair{"\\at(", ", " + it->second + ")"};
            }

            auto label = myPoint.getLabel();
            usedPoints.insert(myPoint);
            return std::pair{"\\at(", ", " + label + ")"};
        }
        return {};
    }
    inline bool isNeedParens(Operator myOp, unsigned parentPrec, bool isRightChild) {
        auto myPrec = getPrecedence(myOp);
        return (myPrec < parentPrec) ||
               (myPrec == parentPrec && isRightChild && !isRightAssociative(myOp));
    }
} // namespace acslg::analyzer::symbolic::details

#endif
