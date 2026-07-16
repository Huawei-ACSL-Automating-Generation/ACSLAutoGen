#pragma once

#include "../expr.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal node representing a literal constant value.
    class LiteralExprNode : public SymbolicExpr {
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
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        LiteralExprNode(bool value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Bool, 1}, explicitType),
              type_(LiteralType::Boolean) {
            data_.boolValue = value;
        }

        LiteralExprNode(int value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 32}, explicitType),
              type_(LiteralType::Int) {
            data_.intValue = value;
        }

        LiteralExprNode(unsigned int value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 32}, explicitType),
              type_(LiteralType::UnsignedInt) {
            data_.uintValue = value;
        }

        LiteralExprNode(short value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 16}, explicitType),
              type_(LiteralType::Short) {
            data_.shortValue = value;
        }

        LiteralExprNode(unsigned short value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 16}, explicitType),
              type_(LiteralType::UnsignedShort) {
            data_.ushortValue = value;
        }

        LiteralExprNode(int64_t value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 64}, explicitType),
              type_(LiteralType::Int64) {
            data_.int64Value = value;
        }

        LiteralExprNode(uint64_t value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 64}, explicitType),
              type_(LiteralType::UInt64) {
            data_.uint64Value = value;
        }

      public:
        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_LiteralExpr;
        }

        LiteralType getLiteralType() const { return type_; }
        ExprHandle importInto(ExprFactory &factory) const;
        std::unique_ptr<LiteralExprNode> rebuildNode(
            std::optional<Type> explicitType = std::nullopt) const;

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const;
        bool equal(const SymbolicExpr &expr) const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 0; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;
        int64_t getLiteralValue() const;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
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
    class BinaryOpExprNode : public SymbolicExpr {
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

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_BinaryOpExpr;
        }

        ExprHandle getLeft() const { return left_.handle(); }
        ExprHandle getRight() const { return right_.handle(); }
        Operator getOperator() const { return op_; }

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const;
        bool equal(const SymbolicExpr &expr) const override;
        bool isUnknown() const override { return left_->isUnknown() || right_->isUnknown(); }
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        BinaryOpExprNode(ExprHandle left,
                         Operator op,
                         ExprHandle right,
                         std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_BinaryOpExpr, left->getValType(), explicitType), left_(left),
              op_(op), right_(right) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        ExprChild left_;
        Operator op_;
        ExprChild right_;
    };

    /// Internal node representing a unary operation expression.
    class UnaryOpExprNode : public SymbolicExpr {
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

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_UnaryOpExpr;
        }

        ExprHandle getSub() const { return expr_.handle(); }
        Operator getOperator() const { return op_; }

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const;
        bool equal(const SymbolicExpr &expr) const override;
        bool isUnknown() const override { return expr_->isUnknown(); }
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        UnaryOpExprNode(Operator op,
                        ExprHandle expr,
                        std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_UnaryOpExpr, expr->getValType(), explicitType), op_(op),
              expr_(expr) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        Operator op_;
        ExprChild expr_;
    };

    /// Internal node representing a symbolic value that cannot be modeled.
    class UnknownExprNode : public SymbolicExpr {
      public:
        ~UnknownExprNode() = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_UnknownExpr;
        }

        std::string dump() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExpr &expr) const override;
        bool isUnknown() const override { return true; }
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        explicit UnknownExprNode(std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_UnknownExpr, {ScalarKind::Void, 0}, explicitType) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
    };

    /// Internal node containing the immutable field handles of a structure value.
    class StructureNode : public SymbolicExpr, public Symbol {
      public:
        StructureNode(const StructureNode &) = delete;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_Structure;
        }
        static bool classof(const Symbol *symbol) {
            return symbol->getKind() == Symbol::Kind::K_Structure;
        }

        size_t getNumFields() const { return info_.getNumFields(); }
        utils::not_null<const SymbolicExpr *> getFieldValue(size_t index) const {
            if (index >= fields_.size())
                ERROR("Out-of-bounds access");
            return fields_[index].get();
        }
        auto fieldsValues() const {
            return fields_ | std::views::transform(
                                 [](const ExprChild &field) -> utils::not_null<const SymbolicExpr *> {
                                     return field.get();
                                 });
        }
        const StructureInfo &getInfo() const { return info_; }

        std::string dump() const override;
        std::optional<SourcePoint> getFromPoint() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExpr &expr) const override;
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
                      std::optional<Type> explicitType = std::nullopt);

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        StructureInfo info_;
        std::vector<ExprChild> fields_;
    };

    /// Internal symbolic value with an immutable address origin.
    class SymbolValueNode : public SymbolicExpr, public Symbol {
      public:
        SymbolValueNode(const SymbolValueNode &) = delete;
        SymbolValueNode(SymbolValueNode &&)      = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_SymbolValue;
        }
        static bool classof(const Symbol *symbol) {
            return symbol->getKind() == Symbol::Kind::K_SymbolValue;
        }

        std::string dump() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExpr &expr) const override;
        AddrHandle getFromAddrHandle() const { return AddrHandle{fromAddr_.get().get()}; }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        friend struct ExprFactoryInternals;

        SymbolValueNode(Type varType, AddrHandle from, SourcePoint fromPoint)
            : SymbolicExpr(ExprKind::K_SymbolValue, varType), Symbol(Kind::K_SymbolValue),
              fromAddr_(from), fromPoint_(std::move(fromPoint)) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        AddressChild fromAddr_;
        SourcePoint fromPoint_;
    };

    /// Internal symbolic address with immutable origin, offset, and optional length.
    class SymbolAddressNode : public Address, public Symbol {
      public:
        SymbolAddressNode(const SymbolAddressNode &) = delete;
        SymbolAddressNode(SymbolAddressNode &&)      = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_SymbolAddress;
        }
        static bool classof(const Symbol *symbol) {
            return symbol->getKind() == Symbol::Kind::K_SymbolAddress;
        }

        bool operator==(const SymbolAddressNode &other) const { return equal(other); }
        ExprHandle getOffset() const { return offset_.handle(); }
        const std::optional<ExprChild> &getLength() const { return length_; }
        std::optional<ExprHandle> getRightBound() const;
        SymbolAddrBaseInfo getBaseInfo() const;

        std::string dump() const override;
        bool equal(const SymbolicExpr &expr) const override;
        std::size_t hash() const override;
        UsedMap collectUsedSymbols() const override;
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
            const std::unordered_map<size_t, size_t> &) const override;
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        std::optional<AddrHandle> getFromAddrHandle() const {
            if (!fromAddr_)
                return std::nullopt;
            return AddrHandle{fromAddr_->get().get()};
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }

      private:
        friend struct ExprFactoryInternals;

        SymbolAddressNode(clang::QualType pointeeType,
                          std::optional<AddrHandle> from,
                          SourcePoint fromPoint,
                          ExprHandle offset,
                          std::optional<ExprHandle> length,
                          std::optional<Type> explicitType = std::nullopt);

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, GetACSLError> doGetACSLOfValue(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        ExprChild offset_;
        std::optional<AddressChild> fromAddr_;
        SourcePoint fromPoint_;
        std::optional<ExprChild> length_;
    };

    /// Internal address of a C variable.
    class VariableAddressNode : public Address {
      public:
        VariableAddressNode(const VariableAddressNode &)            = delete;
        VariableAddressNode &operator=(const VariableAddressNode &) = delete;
        VariableAddressNode(VariableAddressNode &&)                 = default;
        VariableAddressNode &operator=(VariableAddressNode &&)      = delete;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_VariableAddress;
        }

        bool operator==(const VariableAddressNode &other) const { return equal(other); }
        std::string dump() const override;
        ExprHandle simplifiedExpr() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        bool equal(const SymbolicExpr &expr) const override;
        std::size_t hash() const override;
        utils::not_null<const clang::VarDecl *> getFrom() const { return from_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        UsedMap collectUsedSymbols() const override {
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
            const std::unordered_map<size_t, size_t> &) const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }

      private:
        friend struct ExprFactoryInternals;

        explicit VariableAddressNode(utils::not_null<const clang::VarDecl *> from,
                                     std::optional<Type> explicitType = std::nullopt)
            : Address(ExprKind::K_VariableAddress,
                      Type{ScalarKind::UInt, 64},
                      from->getType(),
                      explicitType),
              from_(from) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, GetACSLError> doGetACSLOfValue(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        utils::not_null<const clang::VarDecl *> from_;
    };

    /// Internal address of a C record field.
    class FieldAddressNode : public Address {
      public:
        FieldAddressNode(const FieldAddressNode &)            = delete;
        FieldAddressNode &operator=(const FieldAddressNode &) = delete;
        FieldAddressNode(FieldAddressNode &&)                 = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_FieldAddress;
        }

        std::string dump() const override;
        ExprHandle simplifiedExpr() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        bool equal(const SymbolicExpr &expr) const override;
        std::size_t hash() const override;
        utils::not_null<const clang::RecordDecl *> getDefinition() const { return definition_; }
        AddrHandle getBaseAddr() const { return baseAddr_.handle(); }
        size_t getFieldIndex() const { return fieldIndex_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        UsedMap collectUsedSymbols() const override {
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
            const std::unordered_map<size_t, size_t> &) const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }

      private:
        friend struct ExprFactoryInternals;

        FieldAddressNode(clang::QualType pointeeType,
                         const clang::RecordDecl *record,
                         AddrHandle baseAddr,
                         size_t fieldIndex,
                         std::optional<Type> explicitType = std::nullopt)
            : Address(ExprKind::K_FieldAddress,
                      Type{ScalarKind::UInt, 64},
                      pointeeType,
                      explicitType),
              definition_(record), baseAddr_(baseAddr), fieldIndex_(fieldIndex) {
            if (!record->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            definition_ = record->getDefinition();
        }

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, GetACSLError> doGetACSLOfValue(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        utils::not_null<const clang::RecordDecl *> definition_;
        AddressChild baseAddr_;
        size_t fieldIndex_;
    };

} // namespace acslg::analyzer::symbolic::detail
