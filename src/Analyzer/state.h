/**
 * @file state.h
 * @brief Declares symbolic execution state, path tracking, and memory modeling helpers.
 */
#ifndef __ACSLG_SRC_ANALYZER_STATE_H__
#define __ACSLG_SRC_ANALYZER_STATE_H__

#include <unordered_map>
#include <unordered_set>
#include <map>
#include <stack>
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Expr.h>
#include <ppl.hh>
#include "Symbolic/expr.h"
#include "function.h"
#include "Context/context.h"

namespace acslg::analyzer {
    using Formulas = std::vector<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>;
    struct SymbolicExprPtrHash {
        using is_transparent = void;

        std::size_t operator()(
            const utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &ptr) const noexcept {
            return ptr->hash();
        }
        std::size_t operator()(const symbolic::SymbolicExpr &expr) const noexcept {
            return expr.hash();
        }
        std::size_t operator()(const symbolic::SymbolicExpr *expr) const noexcept {
            return expr ? expr->hash() : 0;
        }
    };
    struct SymbolicExprPtrEqual {
        using is_transparent = void;

        bool operator()(
            const utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &lhs,
            const utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &rhs) const noexcept {
            return lhs->equal(*rhs);
        }
        bool operator()(const utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &lhs,
                        const symbolic::SymbolicExpr &rhs) const noexcept {
            return lhs->equal(rhs);
        }
        bool operator()(
            const symbolic::SymbolicExpr &lhs,
            const utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &rhs) const noexcept {
            return lhs.equal(*rhs);
        }
        bool operator()(const utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &lhs,
                        const symbolic::SymbolicExpr *rhs) const noexcept {
            return rhs && lhs->equal(*rhs);
        }
        bool operator()(
            const symbolic::SymbolicExpr *lhs,
            const utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &rhs) const noexcept {
            return lhs && lhs->equal(*rhs);
        }
        bool operator()(const symbolic::SymbolicExpr *lhs,
                        const symbolic::SymbolicExpr *rhs) const noexcept {
            return lhs && rhs && lhs->equal(*rhs);
        }
    };
    struct PathConditionHash {
        std::size_t operator()(symbolic::ExprHandle expr) const noexcept { return expr.hash(); }
    };
    struct PathConditionEqual {
        bool operator()(symbolic::ExprHandle lhs, symbolic::ExprHandle rhs) const noexcept {
            return lhs.get().get() == rhs.get().get() || lhs->equal(*rhs);
        }
    };
    using PathConditions =
        std::unordered_set<symbolic::ExprHandle, PathConditionHash, PathConditionEqual>;
    using TransRel = std::tuple<int, int, Parma_Polyhedra_Library::C_Polyhedron *>;
    using InitRel  = std::pair<int, Parma_Polyhedra_Library::C_Polyhedron *>;

    /**
     * @class MemoryModel
     * @brief Represents a symbolic memory model with support for variable addresses,
     *        constant ranges, and symbolic ranges.
     *
     * This class provides read/write access to symbolic expressions stored at symbolic
     * addresses. Memory is represented internally with three maps:
     * - memoryMap_variableAddr_: maps variable addresses to symbolic expressions.
     * - memoryMap_constantRange_: maps base addresses to constant ranges.
     * - memoryMap_symbolicRange_: maps symbolic ranges hashed by base.
     *
     * It also provides a flat_view nested type to iterate over the entire memory
     * content (variables, ranges, and nested structures) in a uniform manner.
     */
    class MemoryModel {
      public:
        using KeySet = std::
            unordered_set<symbolic::AddressBox, symbolic::AddressBoxHash, symbolic::AddressBoxEq>;
        /**
         * @struct flat_view
         * @brief Provides a flat iteration view over the memory contents.
         *
         * flat_view allows iteration over all memory entries, exposing each address
         * and its associated symbolic expression in a flattened sequence.
         */
        struct flat_view;
        /// @brief Default construct an empty memory model.
        MemoryModel();
        explicit MemoryModel(symbolic::ExprFactory &factory);
        /// @brief Copy construct, duplicating all stored symbolic ranges.
        MemoryModel(const MemoryModel &);
        /// @brief Copy-assign, replacing memory content with another model.
        MemoryModel &operator=(const MemoryModel &);
        MemoryModel(MemoryModel &&)            = default;
        MemoryModel &operator=(MemoryModel &&) = default;

        /**
         * @brief Reads a symbolic expression at the given address (const overload).
         * @param addr The symbolic address to read from.
         * @return Optional containing the expression if found, otherwise empty.
         */
        std::optional<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>> read(
            const symbolic::Address &addr) const;

        /**
         * @brief Writes a symbolic expression to the given address.
         * @param address The symbolic address to write to.
         * @param value The symbolic expression to store.
         */
        void write(const symbolic::Address &address,
                   utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> value);

        /**
         * @brief Checks whether a given address is contained in the memory model.
         * @param addr The address to check.
         * @return true if address exists, false otherwise.
         */
        bool contains(const symbolic::Address &addr) const;

        /// Clears all memory maps.
        void clear() {
            memoryMap_variableAddr_.clear();
            memoryMap_constantRange_.clear();
            memoryMap_symbolicRange_.clear();
        }

        /**
         * @brief Just call `mergeConstantRanges` and `mergeSymbolicRanges` to merge ranges.
         */
        void mergeRanges() {
            mergeConstantRanges();
            mergeSymbolicRanges();
        }

        /**
         * @brief Merge adjacent constant ranges with identical values per BaseInfo.
         *
         * For each BaseInfo bucket inside `memoryMap_constantRange_`, this function:
         * 1) Moves all entries into a vector and sorts them by offset (ascending).
         * 2) Performs a single left-to-right pass to merge adjacent ranges
         *    [pOff, pOff+pLen) and [cOff, cOff+cLen) iff pOff+pLen == cOff AND values are equal.
         * 3) Writes the merged result back into the original map.
         *
         * Complexity: O(n log n) due to sorting, where n is the number of ranges per BaseInfo.
         */
        void mergeConstantRanges();

        /**
         * @brief Merge adjacent symbolic ranges with identical values per BaseInfo.
         *
         * This version does NOT impose any total order on symbolic expressions.
         * Instead, for each BaseInfo bucket inside `memoryMap_symbolicRange_` it:
         * 1) Precomputes, for each range, the left endpoint hash (Lh := hash(offset.simplified)),
         *    the right endpoint hash (Rh := hash((offset + length).simplified) for range,
         *    or Rh := hash((offset + 1).simplified) for single-address semantics),
         *    and the value hash (for coarse grouping by equal "stored value").
         * 2) Groups ranges by value-hash to only consider merges among equal-value candidates.
         * 3) Within each value group, builds adjacency by hashes: Lh → outgoing edges, Rh →
         * incoming edges, then emits chains by greedily following unique successors where Rh ==
         * next.Lh.
         *    - If multiple merge candidates exist at a boundary, it stops conservatively (no
         * ambiguous merge). 4) Emits the merged key (offset preserved from the first segment;
         * length is accumulated) and moves the group's value into the result.
         *
         * Complexity: Near O(n) per BaseInfo bucket (linear passes + hash maps).
         */
        void mergeSymbolicRanges();

        /**
         * @brief Return number of memoryModel's entries without structures' fields.
         * @return Number of memoryModel's entries without structures' fields.
         */
        size_t sizeWithoutFields() const {
            size_t sum{0};
            sum += memoryMap_variableAddr_.size();
            for (auto &[_, map] : memoryMap_constantRange_)
                sum += map.size();
            for (auto &[_, map] : memoryMap_symbolicRange_)
                sum += map.size();
            return sum;
        }

        /**
         * @brief Returns a flat view of the memory contents (const).
         * @return A flat_view instance for iteration.
         */
        const flat_view flat() const;

        /**
         * @brief Remove all memory entries associated with a set of local variables
         *        that have gone out of scope.
         *
         * This function inspects the memory model and erases any Address→Value
         * pairs whose Address was derived from one of the provided local
         * variables. It should be called at the end of a scope to ensure that
         * memory state does not retain references to variables which are no
         * longer visible.
         *
         * @param localVars The set of VarDecl pointers representing local
         *                  variables that have expired (gone out of scope).
         */
        void eraseExpiredLocals(const std::unordered_set<const clang::VarDecl *> &localVars);

      private:
        friend struct flat_view;

        /// Constant range [offset, offset+length)
        using ConstRange = std::pair<uint64_t, uint64_t>;
        using StoredValue = symbolic::ExprHandle;

        /// SymbolValue address to symbolic expression mapping
        std::unordered_map<symbolic::VariableAddress, StoredValue> memoryMap_variableAddr_;

        /**
         * @brief Constant range mapping.
         * Maps a base address to non-overlapping constant ranges (non-zero length).
         */
        std::unordered_map<
            symbolic::SymbolAddrBaseInfo,
            std::map<ConstRange, StoredValue>>
            memoryMap_constantRange_; ///< ConstRanges must be non-overlapping and non-zero-length.

        /// Symbolic range mapping
        std::unordered_map<
            symbolic::SymbolAddrBaseInfo,
            std::unordered_map<symbolic::SymbolAddress, StoredValue>>
            memoryMap_symbolicRange_;

        symbolic::ExprFactory &factory() const { return *factory_; }
        StoredValue importValue(const symbolic::SymbolicExpr &value);
        StoredValue copyStoredValueFrom(const MemoryModel &other, StoredValue value);

        std::unique_ptr<symbolic::ExprFactory> ownedFactory_;
        symbolic::ExprFactory *factory_;
    };

    /**
     * @struct MemoryModel::flat_view
     * @brief Provides iteration over flattened memory model entries.
     *
     * flat_view exposes all variable addresses, constant ranges, symbolic ranges,
     * and fields of Structure objects as a single iterable sequence of address/value pairs.
     */
    struct MemoryModel::flat_view {
      private:
        /**
         * @brief Compose a SymbolAddress from base info, offset, and length.
         * @param base Base info for the symbol address.
         * @param off Offset within the base.
         * @param len Length of the range.
         * @return A unique_ptr to the composed Address.
         * @note Length must be non-zero.
         */
        template <class Owner>
        static std::unique_ptr<symbolic::Address> compose_address(Owner &owner,
                                                                  symbolic::SymbolAddrBaseInfo base,
                                                                  uint64_t off,
                                                                  uint64_t len) {
            if (len == 0)
                ERROR("Length should not be 0, something goes wrong.");

	            auto &factory = owner.factory();
	            std::optional<symbolic::Addr> from;
	            if (base.fromAddr_)
	                from.emplace(factory, factory.importAddress(*base.fromAddr_.value()));

	            auto offset = symbolic::Expr{factory, factory.literal(off)};
	            auto addr =
	                [&]() {
	                    if (len == 1) {
	                        if (from)
	                            return symbolic::Addr::symbol(base.pointeeType_, *from,
	                                                          base.fromPoint_, offset);
	                        return symbolic::Addr::symbol(base.pointeeType_, base.fromPoint_,
	                                                      offset);
	                    }

	                    auto length = symbolic::Expr{factory, factory.literal(len)};
	                    if (from)
	                        return symbolic::Addr::symbol(base.pointeeType_, *from,
	                                                      base.fromPoint_, offset, length);
	                    return symbolic::Addr::symbol(base.pointeeType_, base.fromPoint_,
	                                                  offset, length);
	                }();
	            return addr->addressClone().into_underlying();
	        }

        /// Helper to access variable address map from owner
        template <class Owner> static auto &var_addr_map(Owner &mm) {
            return mm.memoryMap_variableAddr_;
        }
        /// Helper to access constant range map from owner
        template <class Owner> static auto &const_range_map(Owner &mm) {
            return mm.memoryMap_constantRange_;
        }
        /// Helper to access symbolic range map from owner
        template <class Owner> static auto &symb_range_map(Owner &mm) {
            return mm.memoryMap_symbolicRange_;
        }

      public:
        /**
         * @class flat_iterator
         * @brief Iterator for flat_view.
         *
         * Iterates over all memory entries (variable addresses, constant ranges,
         * symbolic ranges, and fields of Structure objects). Produces pairs of
         * (Address, SymbolicExpr).
         *
         * @tparam IsConst true for const_iterator, false for iterator
         */
        template <bool IsConst> class flat_iterator {
            using Owner  = std::conditional_t<IsConst, const MemoryModel, MemoryModel>;
            using VOuter = decltype(var_addr_map(std::declval<Owner &>()).begin());
            using COuter = decltype(const_range_map(std::declval<Owner &>()).begin());
            using CInner =
                decltype(const_range_map(std::declval<Owner &>()).begin()->second.begin());
            using SOuter = decltype(symb_range_map(std::declval<Owner &>()).begin());
            using SInner =
                decltype(symb_range_map(std::declval<Owner &>()).begin()->second.begin());

            using R =
                std::pair<symbolic::AddressBox, utils::not_null<const symbolic::SymbolicExpr *>>;

          public:
            /// Default constructor
            flat_iterator() = default;

            /**
             * @brief Construct iterator for a MemoryModel owner.
             * @param o Reference to owner MemoryModel.
             * @param to_begin If true, positions at beginning; else at end.
             */
            flat_iterator(Owner &o, bool to_begin) : owner_(o) {
                var_outer_     = var_addr_map(owner_).begin();
                var_outer_end_ = var_addr_map(owner_).end();

                c_outer_     = const_range_map(owner_).begin();
                c_outer_end_ = const_range_map(owner_).end();

                s_outer_     = symb_range_map(owner_).begin();
                s_outer_end_ = symb_range_map(owner_).end();

                if (to_begin)
                    advance_to_first(); ///< initialize to first valid element
                else
                    phase_ = Phase::End;
            }

            /**
             * @brief Dereference operator
             * @return A pair of (AddressBox, SymbolicExpr reference)
             *
             * Depending on phase, extracts variable's address, constant range, symbolic range,
             * or Structure field as address and associated value.
             */
            R operator*() const {
                switch (phase_) {
                    case Phase::VarAddr: {
                        const symbolic::Address &addr = var_outer_->first;
                        return R{addr, var_outer_->second.get()};
                    }
                    case Phase::Const: {
                        const symbolic::SymbolAddrBaseInfo &base = c_outer_->first;
                        const auto [off, offPlusLen]             = c_inner_->first;
                        auto addr = compose_address(owner_, base, off, offPlusLen - off);
                        return R{std::move(addr), c_inner_->second.get()};
                    }
                    case Phase::Symb: {
                        const symbolic::Address &addr = s_inner_->first;
                        return R{addr, s_inner_->second.get()};
                    }
                    case Phase::Field: {
                        // Handle Structure fields
                        assert(!state_saver_.empty());
                        auto &current_state = state_saver_.top();
                        assert(current_state.st_ != nullptr);
                        auto &baseAddr = current_state.base_addr_;
                        auto &st       = *current_state.st_;
                        auto &index    = current_state.index_;
                        auto fieldType =
                            std::ranges::next(st.getInfo().definition_->field_begin(), index)
                                ->getType();
                        auto &factory = owner_.factory();
                        auto addr = factory
                                        .fieldAddress(fieldType, st.getInfo().definition_,
                                                      factory.importAddress(baseAddr.get()), index)
                                        ->addressClone()
                                        .into_underlying();
                        return R{std::move(addr), st.getFieldValue(index).get()};
                    }
                    default: break;
                }
                UNREACHABLE();
            }

            /// Pre-increment
            flat_iterator &operator++() {
                advance();
                return *this;
            }

            /// Post-increment
            flat_iterator operator++(int) {
                auto tmp = *this;
                ++*this;
                return tmp;
            }

            /// Equality comparison
            friend bool operator==(const flat_iterator &a, const flat_iterator &b) {
                if (&a.owner_ != &b.owner_)
                    return false;
                if (a.phase_ == Phase::End && b.phase_ == Phase::End)
                    return true;
                if (a.phase_ != b.phase_)
                    return false;

                // Compare according to current phase
                switch (a.phase_) {
                    case Phase::VarAddr: return a.var_outer_ == b.var_outer_;
                    case Phase::Const:
                        return a.c_outer_ == b.c_outer_ &&
                               (a.c_outer_ == a.c_outer_end_ || a.c_inner_ == b.c_inner_);
                    case Phase::Symb:
                        return a.s_outer_ == b.s_outer_ &&
                               (a.s_outer_ == a.s_outer_end_ || a.s_inner_ == b.s_inner_);
                    case Phase::Field: {
                        assert(!a.state_saver_.empty());
                        assert(!b.state_saver_.empty());
                        return a.state_saver_.top().st_ == b.state_saver_.top().st_ &&
                               a.state_saver_.top().index_ == b.state_saver_.top().index_;
                    }
                    default: UNREACHABLE();
                }
            }

          private:
            /// Iteration phases
            enum class Phase {
                VarAddr, ///< Iterating over variable addresses
                Const,   ///< Iterating over constant ranges
                Symb,    ///< Iterating over symbolic ranges
                Field,   ///< Iterating over fields of a Structure
                End      ///< End sentinel
            };
            Owner &owner_;             ///< Reference to MemoryModel owner
            Phase phase_ = Phase::End; ///< Current iteration phase

            // Iterators for variable addresses
            VOuter var_outer_{}, var_outer_end_{};
            // Iterators for constant ranges
            COuter c_outer_{}, c_outer_end_{};
            CInner c_inner_{};
            // Iterators for symbolic ranges
            SOuter s_outer_{}, s_outer_end_{};
            SInner s_inner_{};

            /// State for traversing fields inside a Structure
            struct FieldState {
                const symbolic::AddressBox base_addr_; ///< Base address of the structure
                const symbolic::Structure *st_;        ///< Pointer to Structure
                size_t index_;                         ///< Current field index
                Phase pre_phase_;                      ///< Previous phase before entering fields
            };
            std::stack<FieldState> state_saver_; ///< Stack of Structure traversal states

            /// Advance iterator to first available element
            void advance_to_first() {
                if (var_outer_ != var_outer_end_) {
                    phase_ = Phase::VarAddr;
                    return;
                }
                for (; c_outer_ != c_outer_end_; ++c_outer_) {
                    c_inner_ = c_outer_->second.begin();
                    if (c_inner_ != c_outer_->second.end()) {
                        phase_ = Phase::Const;
                        return;
                    }
                }
                for (; s_outer_ != s_outer_end_; ++s_outer_) {
                    s_inner_ = s_outer_->second.begin();
                    if (s_inner_ != s_outer_->second.end()) {
                        phase_ = Phase::Symb;
                        return;
                    }
                }
                phase_ = Phase::End;
            }

            /// Advance within current phase without diving into Structure fields
            void advance_without_check() {
                switch (phase_) {
                    case Phase::VarAddr: {
                        ++var_outer_;
                        if (var_outer_ != var_outer_end_)
                            return;

                        // Move to constant ranges
                        for (; c_outer_ != c_outer_end_; ++c_outer_) {
                            c_inner_ = c_outer_->second.begin();
                            if (c_inner_ != c_outer_->second.end()) {
                                phase_ = Phase::Const;
                                return;
                            }
                        }
                        // Move to symbolic ranges
                        for (; s_outer_ != s_outer_end_; ++s_outer_) {
                            s_inner_ = s_outer_->second.begin();
                            if (s_inner_ != s_outer_->second.end()) {
                                phase_ = Phase::Symb;
                                return;
                            }
                        }
                        phase_ = Phase::End;
                        break;
                    }
                    case Phase::Const: {
                        ++c_inner_;
                        while (c_outer_ != c_outer_end_ && c_inner_ == c_outer_->second.end()) {
                            ++c_outer_;
                            if (c_outer_ != c_outer_end_)
                                c_inner_ = c_outer_->second.begin();
                        }
                        if (c_outer_ != c_outer_end_)
                            return;
                        // Move to symbolic ranges
                        for (; s_outer_ != s_outer_end_; ++s_outer_) {
                            s_inner_ = s_outer_->second.begin();
                            if (s_inner_ != s_outer_->second.end()) {
                                phase_ = Phase::Symb;
                                return;
                            }
                        }
                        phase_ = Phase::End;
                        break;
                    }
                    case Phase::Symb: {
                        ++s_inner_;
                        while (s_outer_ != s_outer_end_ && s_inner_ == s_outer_->second.end()) {
                            ++s_outer_;
                            if (s_outer_ != s_outer_end_)
                                s_inner_ = s_outer_->second.begin();
                        }
                        if (s_outer_ == s_outer_end_)
                            phase_ = Phase::End;
                        break;
                    }
                    case Phase::Field: {
                        // Advance within structure fields
                        assert(!state_saver_.empty());
                        auto &current_state = state_saver_.top();
                        ++current_state.index_;
                        assert(current_state.st_ != nullptr);
                        if (current_state.index_ < current_state.st_->getNumFields())
                            return;
                        // End of fields -> return to previous phase
                        phase_ = current_state.pre_phase_;
                        state_saver_.pop();
                        advance_without_check();
                        break;
                    }
                    default: UNREACHABLE();
                }
            }

            /// Advance iterator, diving into Structure fields if needed
            void advance() {
                auto &&[addr, value] = (*this).operator*();
                auto st              = symbolic::dyn_cast<const symbolic::Structure>(value.get());
                if (st == nullptr) {
                    advance_without_check();
                    return;
                }

                // Dive into Structure's fields
                auto state = FieldState{std::move(addr), st, 0, phase_};
                state_saver_.push(std::move(state));
                phase_ = Phase::Field;
            }
        }; // flat_iterator

        /// Mutable iterator
        using iterator = flat_iterator<false>;
        /// Const iterator
        using const_iterator = flat_iterator<true>;

        /// Construct flat_view with given MemoryModel reference
        explicit flat_view(MemoryModel &p) : owner_(p) {}

        /// Begin iterator (mutable)
        iterator begin() { return iterator{owner_, true}; }
        /// End iterator (mutable)
        iterator end() { return iterator{owner_, false}; }
        /// Begin iterator (const)
        const_iterator begin() const { return const_iterator{owner_, true}; }
        /// End iterator (const)
        const_iterator end() const { return const_iterator{owner_, false}; }

      private:
        MemoryModel &owner_; ///< Reference to owning MemoryModel
    };

    /**
     * @class Path
     * @brief Represents a single symbolic execution path and its evolving memory state.
     *
     * A Path owns variable allocations, symbolic memory, accumulated path conditions, and the
     * current control state (e.g., continuing, breaking, returning). It is cloned and merged while
     * exploring control-flow constructs.
     */
    class Path {
      public:
        using EvalResult = std::pair<std::vector<utils::not_null<std::unique_ptr<Path>>>, Formulas>;

        /**
         * @brief Construct a path with an initial source point.
         * @param context [in] Shared analysis context for AST and rewrite operations.
         * @param startPoint [in] Source location representing where symbolic execution begins.
         */
        Path(context::ACSLGContext &context, symbolic::SourcePoint startPoint)
            : memoryState_(context.getExprFactory()), context_(context), startPoint_(startPoint) {};
        ~Path() = default;
        /**
         * @brief Copy constructor supporting shallow or deep semantics depending on usage.
         * @param other [in] Path to copy from.
         * @param shallowCopy [in] When true, reuses symbolic expressions instead of duplicating.
         */
        Path(const Path &other, bool shallowCopy);
        /**
         * @brief Swap all internal resources with another path.
         * @param o [in,out] Other path instance to exchange state with.
         */
        void swap(Path &o) noexcept;

        enum class PathState {
            Step,
            Continue,
            Break,
            Return
        };

        /**
         * @brief Recreate symbolic values using a new starting source point.
         * @param newStartPoint [in] Source point that tags newly generated symbols.
         */
        void resymbolize(symbolic::SourcePoint newStartPoint);

        /**
         * @brief Derive the symbolic l-value address from a left-hand side expression.
         * @param lhs [in] Expression used as an assignment target.
         * @return Address pointing to the storage referenced by the expression.
         */
        utils::not_null<std::unique_ptr<symbolic::Address>> extractLValue(const clang::Expr *lhs);

        /**
         * @brief Retrieve the symbolic value of a variable within the path.
         * @param var [in] Variable declaration to query.
         * @return A clone of the stored symbolic expression.
         */
        utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> getVarState(
            const clang::VarDecl *var) const;
        /**
         * @brief Access accumulated path conditions.
         * @return Const reference to path condition set.
         */
        const PathConditions &getPathConditions() const;

        /**
         * @brief Allocate symbolic memory for a variable if not already allocated.
         * @param var [in] Variable declaration to allocate.
         * @param initSymbolic [in] When true, initialize with a symbolic value for the variable type.
         * @return Pointer to the symbolic variable address.
         */
        utils::not_null<symbolic::VariableAddress *> allocMemory(const clang::VarDecl *,
                                                                 bool initSymbolic = false);

        /**
         * @brief Write a symbolic value to the specified address in memory.
         * @param addr [in] Target address.
         * @param expr [in] Symbolic expression to store.
         */
        void updateMemory(const symbolic::Address &addr,
                          utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr);
        /**
         * @brief Update the symbolic state of a variable.
         * @param var [in] Variable declaration being updated.
         * @param expr [in] New symbolic value.
         */
        void updateVarState(utils::not_null<const clang::VarDecl *> var,
                            utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr);
        /**
         * @brief Add a new constraint to the path condition set.
         * @param cond [in] Constraint expression to insert.
         */
        void insertPathCondition(utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> cond);

        void setReturnExpr(
            std::optional<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>> expr) {
            if (expr == std::nullopt) {
                returnExpr_ = std::nullopt;
                return;
            }
            returnExpr_.emplace(context_.getExprFactory().importExpr(*expr.value()));
        };
        /// @brief Update the control-flow marker for this path.
        void setPathState(PathState state) { currentState_ = state; }

        /// @brief Check whether the path is still active (not terminated or branched away).
        bool isActive() const { return currentState_ == PathState::Step; }
        /**
         * @brief Determine if a memory address has been modified since another path snapshot.
         * @param addr [in] Address to compare.
         * @param since [in] Reference path providing the baseline state.
         * @return True if the address holds the same symbolic value.
         */
        bool isUnchanged(const symbolic::Address &addr, const Path &since) const;
        /**
         * @brief Test if the address refers to a structure object.
         * @param addr [in] Address to inspect.
         * @return True if the address resolves to a structure.
         */
        bool is_point_to_structure(const symbolic::Address &addr) const;
        /**
         * @brief Merge another compatible path into this one, reconciling memory and conditions.
         * @param other [in] Path to merge.
         */
        void mergeWith(const Path &other);
        /**
         * @brief Create a deep copy of the path.
         * @return Newly allocated clone with duplicated symbolic state.
         */
        std::unique_ptr<Path> clone() const;

        /**
         * @brief Produce a textual dump of the path for debugging.
         * @return String representation of state and constraints.
         */
        std::string dump() const;
        /**
         * @brief Evaluate a clang expression symbolically.
         * @param expr [in] Expression to evaluate.
         * @return Pair of forked paths (if branching occurs) and resulting symbolic values.
         */
        EvalResult evalExpr(const clang::Expr *expr);
        friend class ProgramState;

        auto getVarAddr() const -> const auto & { return varAddr_; };
        auto getMemoryState() const -> const auto & { return memoryState_; }
        auto getMutMemoryState() -> auto & { return memoryState_; }
        auto getReturnExpr() const -> const auto & { return returnExpr_; }
        auto getPathState() const -> const auto & { return currentState_; }
        auto getContext() const -> const auto & { return context_; }
        auto getStartPoint() const -> const auto & { return startPoint_; }

      private:
        // Map: variable record definition ID -> corresponding symbolic address.
        std::unordered_map<const clang::VarDecl *,
                           utils::not_null<std::unique_ptr<symbolic::VariableAddress>>>
            varAddr_;

        MemoryModel memoryState_;

        // SET: List of symbolic expressions representing the path
        // condition.
        PathConditions pathConditions_;

        // Holds the current path state. Default is set to Step
        PathState currentState_ = PathState::Step;

        std::optional<symbolic::ExprHandle> returnExpr_ = std::nullopt;

        context::ACSLGContext &context_;

        symbolic::SourcePoint startPoint_;

        const clang::Stmt *stmtCtx_ = nullptr;
    };

    /**
     * @class ProgramState
     * @brief Owns the collection of active paths for a function and coordinates stepping logic.
     *
     * A ProgramState orchestrates symbolic execution by advancing statements, splitting on
     * branches, and merging paths. It also exposes helpers to clone states and introspect
     * execution progress.
     */
    class ProgramState {
      public:
        /**
         * @brief Construct with an initial path and function wrapper.
         * @param initialPath [in] Pre-built symbolic path to seed execution.
         * @param func [in] Wrapper around the function being analyzed.
         * @param context [in] Shared analysis context.
         */
        ProgramState(std::unique_ptr<Path> initialPath,
                     std::unique_ptr<ACSLFunction> func,
                     context::ACSLGContext &context);
        /**
         * @brief Construct an empty state with a function wrapper; paths are created on init().
         * @param func [in] Target function wrapper.
         * @param context [in] Shared analysis context.
         */
        ProgramState(std::unique_ptr<ACSLFunction> func, context::ACSLGContext &context);
        ~ProgramState() = default;
        ProgramState(const ProgramState &);
        ProgramState(ProgramState &&) = default;
        ProgramState &operator=(ProgramState &&);

        /// @brief Initialize the program state before stepping through statements.
        void init();

        /**
         * @brief Step through a statement for all active paths.
         * @param stmt [in] Statement to execute symbolically.
         */
        void step(const clang::Stmt *stmt);
        /**
         * @brief Symbolically evaluate an expression across paths.
         * @param expr [in] Expression to handle.
         */
        void stepExpr(const clang::Expr *expr);

        /**
         * @brief Allocate memory for newly declared variables in the given declaration statement.
         * @param declStmt [in] Declaration statement containing variables.
         */
        void addNewDecls(const clang::DeclStmt *declStmt);

        /**
         * @brief Update path states according to control-flow constructs.
         * @param state [in] Path state to assign.
         * @param stmt [in] Statement that triggered the transition.
         */
        void setStates(Path::PathState state, const clang::Stmt *stmt);

        /**
         * @brief Set return expression for all active paths.
         * @param expr [in] Expression representing return value.
         */
        void setReturnExpr(const clang::Expr *expr);
        /**
         * @brief Update variable state for a binary assignment operator.
         * @param binOp [in] Binary operator describing the assignment.
         */
        void updateVarState(const clang::BinaryOperator *binOp);

        /**
         * @brief Split the current state into active and inactive subsets.
         * @return Pair of unique_ptrs for active and inactive states.
         */
        std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<ProgramState>> splitActiveInactive();
        /**
         * @brief Merge multiple program states into a single combined state.
         * @param states [in] Collection of state pointers to merge.
         * @return Combined ProgramState owning merged paths.
         */
        static std::unique_ptr<ProgramState> merge(const std::vector<const ProgramState *> &states);
        static std::unique_ptr<ProgramState> merge(
            const std::vector<std::unique_ptr<ProgramState>> &states);
        /**
         * @brief Deep-copy the program state, optionally with or without paths.
         * @param withPath [in] Whether to clone path data.
         * @return Cloned ProgramState.
         */
        std::unique_ptr<ProgramState> clone(bool withPath = true) const;
        /**
         * @brief Clone the state while replacing its paths with supplied ones.
         * @param newPaths [in,out] Paths to attach to the cloned state.
         * @return Newly allocated ProgramState with moved-in paths.
         */
        std::unique_ptr<ProgramState> cloneWithPaths(
            std::vector<std::unique_ptr<Path>> &newPaths) const;
        /// @brief True if all paths are inactive (terminated or returned).
        bool isInactive() const;

        /// @brief Dump a human-readable rendering of the program state.
        std::string dump() const;
        /// @brief Reset any break markers after finishing a loop.
        void resetBreakState();
        /**
         * @brief Recreate symbols in contained paths using a new start point.
         * @param newStartPoint [in] Source point for new symbols.
         */
        void resymbolize(symbolic::SourcePoint newStartPoint);

        /// @brief Append a new path to the managed collection.
        void insertPath(utils::not_null<std::unique_ptr<Path>> path) {
            paths_.push_back(std::move(path));
        }
        /// @brief Access all paths (const).
        auto getPaths() const -> const auto & { return paths_; }
        /// @brief Access all paths (mutable).
        auto getPaths() -> auto & { return paths_; }
        /// @brief Access the wrapped function.
        auto getFunction() const -> const auto & { return func_; }
        /// @brief Access the shared context.
        auto getContext() const -> const auto & { return context_; }
        /// @brief Access the starting source point.
        auto getStartPoint() const -> const auto & { return startPoint_; }

        /**
         * @brief Extract and remove a specific path by index.
         * @param i [in] Index of the path to remove.
         * @return Optional path if index valid.
         */
        std::optional<utils::not_null<std::unique_ptr<Path>>> takePath(size_t i);
        /**
         * @brief Remove and return all tracked paths.
         * @return Vector of moved-out paths.
         */
        std::vector<utils::not_null<std::unique_ptr<Path>>> takeAllPaths();

      private:
        void setStmtCtx(const clang::Stmt *stmtCtx);
        // TODO: remove from private member. [a local helper function.]
        // Only be used in step when processing SwitchStmt, just for a cleaner code.
        std::vector<std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<symbolic::SymbolicExpr>>> splitStateBySwitchCond(
            const clang::Expr *switchCond);
        void stepSimpleSwitch(const clang::SwitchStmt *switchstmt);

        void stepBranch(const std::vector<const clang::Expr *> &branchConds,
                        const std::vector<const clang::Stmt *> &branchStmts);

        void stepLoop(const clang::Stmt *loopStmt);

        std::vector<utils::not_null<std::unique_ptr<Path>>> paths_{};

        std::unique_ptr<ACSLFunction> func_;

        context::ACSLGContext &context_;

        symbolic::SourcePoint startPoint_;

        const clang::Stmt *stmtCtx_ = nullptr;
    };

    /**
     * @struct VarManager
     * @brief Tracks variables encountered across paths for invariant generation.
     */
    struct VarManager {
        size_t numVars = 0;
        std::unordered_map<std::string, size_t> varIndexMap;
        std::vector<std::string> orderedVars;
        std::vector<const clang::VarDecl *> varDecls;

        /**
         * @brief Build a variable manager from a collection of paths.
         * @param paths [in] Paths to scan for variable addresses.
         * @return Populated VarManager instance.
         */
        static VarManager fromPaths(
            const std::vector<utils::not_null<std::unique_ptr<Path>>> &paths) {
            VarManager vm;
            std::vector<std::pair<std::string, const clang::VarDecl *>> rawVars;
            size_t varCounter = 0;

            auto collectFromPath = [&](const Path &path) {
                const auto &varAddrMap = path.getVarAddr();
                for (const auto &[varDecl, addrPtr] : varAddrMap) {
                    if (!varDecl)
                        continue;
                    if (varDecl->getType()->isStructureType()) {
                        TODO();
                    }

                    std::string name = varDecl->getNameAsString();
                    if (auto [_, ok] = vm.varIndexMap.insert({name, varCounter}); ok) {
                        rawVars.push_back({name, varDecl->getCanonicalDecl()});
                        ++varCounter;
                    }
                }
            };

            for (const auto &path : paths)
                collectFromPath(*path);

            for (const auto &[name, varDecl] : rawVars) {
                vm.orderedVars.push_back(name);
                vm.varDecls.push_back(varDecl);
            }
            for (const auto &[name, _] : rawVars)
                vm.orderedVars.push_back(name + "_init");

            vm.numVars = rawVars.size() * 2;
            return vm;
        }

        /**
         * @brief Helper to build a manager from a single path.
         * @param path [in] Path to introspect.
         * @return Populated VarManager.
         */
        static VarManager fromPath(const Path &path) {
            std::vector<utils::not_null<std::unique_ptr<Path>>> single;
            single.emplace_back(path.clone());
            return fromPaths(single);
        }
    };

    /**
     * @struct InvsAndPostStates
     * @brief Holds generated invariants and corresponding post-states for loops.
     */
    struct InvsAndPostStates {
        std::optional<std::string> invs;
        using MemoryMapAndPathConds = std::pair<
            symbolic::AddressBoxMap<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>,
            std::vector<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>>;
        std::vector<MemoryMapAndPathConds> normalPostStates;
        std::vector<std::vector<MemoryMapAndPathConds>> interruptPostStates;
    };

    /**
     * @brief Build loop invariants from the condition and explored paths.
     * @param loopCond [in] Symbolic loop condition.
     * @param paths [in] Active execution paths to analyze.
     * @param initState [in] Initial program state before the loop.
     * @param generateBranches [in] Whether to enumerate branch-specific invariants.
     * @return Invariants and post-states captured for the loop.
     */
    InvsAndPostStates buildLoopInvariant(std::unique_ptr<symbolic::SymbolicExpr> loopCond,
                                         const std::vector<std::unique_ptr<Path>> &paths,
                                         const ProgramState &initState,
                                         bool generateBranches = true);

    /**
     * @brief Build loop invariants from a single entry path and potential inactive paths.
     * @param loopCond [in] Symbolic loop condition.
     * @param entryPath [in] Path at loop entry.
     * @param loopCurrent [in] Program state representing the loop body evaluation.
     * @param inactivePaths [in] Range of inactive paths that may represent interrupts.
     * @param generateBranches [in] Whether to enumerate branch-specific invariants.
     * @return Invariants and post-states captured for the loop.
     */
    InvsAndPostStates buildLoopInvariant(std::unique_ptr<symbolic::SymbolicExpr> loopCond,
                                         const Path &entryPath,
                                         const ProgramState &loopCurrent,
                                         std::ranges::range auto &inactivePaths,
                                         bool generateBranches = true);
} // namespace acslg::analyzer

#include "Symbolic/invariant.tpp" // IWYU pragma: keep

#endif
