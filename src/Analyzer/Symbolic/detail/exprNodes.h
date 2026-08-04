#pragma once

#include "../expr.h"
#include "addressNode.h"
#include "symbolNode.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal node representing a literal constant value.
    class LiteralExprNode : public SymbolicExprNode {
      public:
        enum class LiteralType {
            Boolean,
            Int,
            UnsignedInt,
            Short,
            UnsignedShort,
            Int64,
            UInt64
        };

        LiteralExprNode(const LiteralExprNode &) = delete;

      private:
        friend struct ExprFactoryInternals;

        LiteralExprNode(bool value, std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_LiteralExpr, {ExprScalarKind::Bool, 1}, explicitType),
              type_(LiteralType::Boolean) {
            data_.boolValue = value;
        }

        LiteralExprNode(int value, std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_LiteralExpr, {ExprScalarKind::Int, 32}, explicitType),
              type_(LiteralType::Int) {
            data_.intValue = value;
        }

        LiteralExprNode(unsigned int value, std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_LiteralExpr, {ExprScalarKind::UInt, 32}, explicitType),
              type_(LiteralType::UnsignedInt) {
            data_.uintValue = value;
        }

        LiteralExprNode(short value, std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_LiteralExpr, {ExprScalarKind::Int, 16}, explicitType),
              type_(LiteralType::Short) {
            data_.shortValue = value;
        }

        LiteralExprNode(unsigned short value, std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_LiteralExpr, {ExprScalarKind::UInt, 16}, explicitType),
              type_(LiteralType::UnsignedShort) {
            data_.ushortValue = value;
        }

        LiteralExprNode(int64_t value, std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_LiteralExpr, {ExprScalarKind::Int, 64}, explicitType),
              type_(LiteralType::Int64) {
            data_.int64Value = value;
        }

        LiteralExprNode(uint64_t value, std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_LiteralExpr, {ExprScalarKind::UInt, 64}, explicitType),
              type_(LiteralType::UInt64) {
            data_.uint64Value = value;
        }

      public:
        LiteralType getLiteralType() const { return type_; }
        ExprHandle importInto(ExprFactory &factory) const;
        std::unique_ptr<LiteralExprNode> rebuildNode(
            std::optional<ExprType> explicitType = std::nullopt) const;

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const;
        bool equal(const SymbolicExprNode &expr) const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 0; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override;
        int64_t getLiteralValue() const;
        std::variant<std::int64_t, std::uint64_t> getIntegerValue() const;

      private:
        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        LiteralType type_;
        union Data {
            bool boolValue;
            int intValue;
            unsigned int uintValue;
            short shortValue;
            unsigned short ushortValue;
            int64_t int64Value;
            uint64_t uint64Value;

            Data() {}
            ~Data() {}
        } data_;
    };

    /// Internal node representing a binary operation expression.
    class BinaryOpExprNode : public SymbolicExprNode {
      public:
        using Operator = BinaryOp;

        inline static unsigned getPrecedence(Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, isRightAssoc) case Operator::name: return prec;
#include "../operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, isRightAssoc) case Operator::name: return isRightAssoc;
#include "../operators.def"
                default: ERROR("Unknown operator");
            }
        }

        ExprHandle getLeft() const { return left_; }
        ExprHandle getRight() const { return right_; }
        Operator getOperator() const { return op_; }

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const;
        bool equal(const SymbolicExprNode &expr) const override;
        bool isUnknown() const override { return left_.isUnknown() || right_.isUnknown(); }
        UsedSet collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override;

      private:
        friend struct ExprFactoryInternals;

        BinaryOpExprNode(ExprHandle left,
                         Operator op,
                         ExprHandle right,
                         std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_BinaryOpExpr, left.getValType(), explicitType), left_(left),
              op_(op), right_(right) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        ExprHandle left_;
        Operator op_;
        ExprHandle right_;
    };

    /// Internal node representing a unary operation expression.
    class UnaryOpExprNode : public SymbolicExprNode {
      public:
        using Operator = UnaryOp;

        inline static unsigned getPrecedence(Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, isRightAssoc) case Operator::name: return prec;
#include "../operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, isRightAssoc) case Operator::name: return isRightAssoc;
#include "../operators.def"
                default: ERROR("Unknown operator");
            }
        }

        ExprHandle getSub() const { return expr_; }
        Operator getOperator() const { return op_; }

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const;
        bool equal(const SymbolicExprNode &expr) const override;
        bool isUnknown() const override { return expr_.isUnknown(); }
        UsedSet collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override;

      private:
        friend struct ExprFactoryInternals;

        UnaryOpExprNode(Operator op,
                        ExprHandle expr,
                        std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_UnaryOpExpr, expr.getValType(), explicitType), op_(op),
              expr_(expr) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        Operator op_;
        ExprHandle expr_;
    };

    /// Internal node representing an explicit scalar value conversion.
    class CastExprNode : public SymbolicExprNode {
      public:
        ExprHandle getOperand() const { return operand_; }

        std::string dump() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExprNode &expr) const override;
        bool isUnknown() const override { return operand_.isUnknown(); }
        UsedSet collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override;

      private:
        friend struct ExprFactoryInternals;

        CastExprNode(ExprHandle operand, ExprType targetType)
            : SymbolicExprNode(ExprKind::K_CastExpr, targetType), operand_(operand) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        ExprHandle operand_;
    };

    /// Internal node representing a symbolic value that cannot be modeled.
    class UnknownExprNode : public SymbolicExprNode {
      public:
        ~UnknownExprNode() = default;

        std::string dump() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExprNode &expr) const override;
        bool isUnknown() const override { return true; }
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }

      private:
        friend struct ExprFactoryInternals;

        explicit UnknownExprNode(std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(ExprKind::K_UnknownExpr, {ExprScalarKind::Void, 0}, explicitType) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
    };

    /// Internal node containing the immutable field handles of a structure value.
    class StructureNode : public SymbolicExprNode, public Symbol {
      public:
        StructureNode(const StructureNode &) = delete;

        size_t getNumFields() const { return info_.getNumFields(); }
        ExprHandle getFieldValue(size_t index) const {
            if (index >= fields_.size())
                ERROR("Out-of-bounds access");
            return fields_[index];
        }
        auto fieldsValues() const {
            return std::views::all(fields_);
        }
        const StructureInfo &getInfo() const { return info_; }

        std::string dump() const override;
        std::optional<SourcePoint> getFromPoint() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExprNode &expr) const override;
        bool isLinear() const override {
            WARN("Met Structure in isLinear.");
            return false;
        }
        int getMaxDegree() const override {
            WARN("Met Structure in getMaxDegree.");
            return -1;
        }

      private:
        friend struct ExprFactoryInternals;

        StructureNode(StructureInfo info,
                      std::vector<ExprHandle> fields,
                      std::optional<ExprType> explicitType = std::nullopt);

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        StructureInfo info_;
        std::vector<ExprHandle> fields_;
    };

    /// Internal symbolic value with an immutable address origin.
    class SymbolValueNode : public SymbolicExprNode, public Symbol {
      public:
        SymbolValueNode(const SymbolValueNode &) = delete;
        SymbolValueNode(SymbolValueNode &&)      = default;

        std::string dump() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExprNode &expr) const override;
        AddrHandle getFromAddrHandle() const { return fromAddr_; }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;
        UsedSet collectUsedSymbols() const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override;

      private:
        friend struct ExprFactoryInternals;

        SymbolValueNode(ExprType varType, AddrHandle from, SourcePoint fromPoint)
            : SymbolicExprNode(ExprKind::K_SymbolValue, varType), Symbol(Kind::K_SymbolValue),
              fromAddr_(from), fromPoint_(std::move(fromPoint)) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        AddrHandle fromAddr_;
        SourcePoint fromPoint_;
    };

    /// Internal symbolic address with immutable origin, offset, and optional length.
    class SymbolAddressNode : public AddressNode, public Symbol {
      public:
        SymbolAddressNode(const SymbolAddressNode &) = delete;
        SymbolAddressNode(SymbolAddressNode &&)      = default;

        bool operator==(const SymbolAddressNode &other) const { return equal(other); }
        ExprHandle getOffset() const { return offset_; }
        const std::optional<ExprHandle> &getLength() const { return length_; }
        std::optional<ExprHandle> getRightBound() const;
        SymbolAddrBaseInfo getBaseInfo() const;

        std::string dump() const override;
        bool equal(const SymbolicExprNode &expr) const override;
        std::size_t hash() const override;
        UsedSet collectUsedSymbols() const override;
        bool isLinear() const override {
            if (length_)
                ERROR("Address range is solely for address representation and should not be "
                      "used as an expression.");
            return true;
        }
        int getMaxDegree() const override {
            if (length_)
                ERROR("Address range is solely for address representation and should not be "
                      "used as an expression.");
            return 1;
        }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override;
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        std::optional<AddrHandle> getFromAddrHandle() const {
            if (!fromAddr_)
                return std::nullopt;
            return *fromAddr_;
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }

      private:
        friend struct ExprFactoryInternals;

        SymbolAddressNode(clang::QualType pointeeType,
                          std::optional<AddrHandle> from,
                          SourcePoint fromPoint,
                          ExprHandle offset,
                          std::optional<ExprHandle> length,
                          std::optional<ExprType> explicitType = std::nullopt);

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, ACSLError> doGetACSLOfValue(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        ExprHandle offset_;
        std::optional<AddrHandle> fromAddr_;
        SourcePoint fromPoint_;
        std::optional<ExprHandle> length_;
    };

    /// Internal address of a C variable.
    class VariableAddressNode : public AddressNode {
      public:
        VariableAddressNode(const VariableAddressNode &)            = delete;
        VariableAddressNode &operator=(const VariableAddressNode &) = delete;
        VariableAddressNode(VariableAddressNode &&)                 = default;
        VariableAddressNode &operator=(VariableAddressNode &&)      = delete;

        bool operator==(const VariableAddressNode &other) const { return equal(other); }
        std::string dump() const override;
        ExprHandle simplifiedExpr() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        bool equal(const SymbolicExprNode &expr) const override;
        std::size_t hash() const override;
        utils::not_null<const clang::VarDecl *> getFrom() const { return from_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        UsedSet collectUsedSymbols() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        bool isLinear() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        int getMaxDegree() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }

      private:
        friend struct ExprFactoryInternals;

        explicit VariableAddressNode(utils::not_null<const clang::VarDecl *> from,
                                     std::optional<ExprType> explicitType = std::nullopt)
            : AddressNode(ExprKind::K_VariableAddress,
                      ExprType{ExprScalarKind::UInt, 64},
                      from->getType(),
                      explicitType),
              from_(from) {}

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, ACSLError> doGetACSLOfValue(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        utils::not_null<const clang::VarDecl *> from_;
    };

    /// Internal address of a C record field.
    class FieldAddressNode : public AddressNode {
      public:
        FieldAddressNode(const FieldAddressNode &)            = delete;
        FieldAddressNode &operator=(const FieldAddressNode &) = delete;
        FieldAddressNode(FieldAddressNode &&)                 = default;

        std::string dump() const override;
        ExprHandle simplifiedExpr() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        bool equal(const SymbolicExprNode &expr) const override;
        std::size_t hash() const override;
        utils::not_null<const clang::RecordDecl *> getDefinition() const { return definition_; }
        AddrHandle getBaseAddr() const { return baseAddr_; }
        size_t getFieldIndex() const { return fieldIndex_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        UsedSet collectUsedSymbols() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        bool isLinear() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        int getMaxDegree() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }

      private:
        friend struct ExprFactoryInternals;

        FieldAddressNode(clang::QualType pointeeType,
                         const clang::RecordDecl *record,
                         AddrHandle baseAddr,
                         size_t fieldIndex,
                         std::optional<ExprType> explicitType = std::nullopt)
            : AddressNode(ExprKind::K_FieldAddress,
                      ExprType{ExprScalarKind::UInt, 64},
                      pointeeType,
                      explicitType),
              definition_(record), baseAddr_(baseAddr), fieldIndex_(fieldIndex) {
            if (!record->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            definition_ = record->getDefinition();
        }

        utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, ACSLError> doGetACSLOfValue(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        utils::not_null<const clang::RecordDecl *> definition_;
        AddrHandle baseAddr_;
        size_t fieldIndex_;
    };

} // namespace acslg::analyzer::symbolic::detail
