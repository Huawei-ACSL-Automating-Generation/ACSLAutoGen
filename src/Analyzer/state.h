#ifndef STATE_H
#define STATE_H

#include <unordered_map>
#include <map>
#include <stack>
#include <variant>
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Expr.h>
#include <ppl.hh>
#include "Symbolic/expr.h"
#include "function.h"
#include "Context/context.h"

namespace acslg::analyzer {
    using Formulas = std::vector<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>;
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
        MemoryModel() = default;
        MemoryModel(const MemoryModel &);
        MemoryModel &operator=(const MemoryModel &);
        MemoryModel(MemoryModel &&)            = default;
        MemoryModel &operator=(MemoryModel &&) = default;

        /**
         * @brief Reads a symbolic expression at the given address (const overload).
         * @param addr The symbolic address to read from.
         * @return Optional containing the expression if found, otherwise empty.
         */
        std::optional<utils::not_null<const symbolic::SymbolicExpr *>> read(
            const symbolic::Address &addr) const;

        /**
         * @brief Reads a symbolic expression at the given address (mutable overload).
         * @param addr The symbolic address to read from.
         * @return Optional containing the expression if found, otherwise empty.
         */
        std::optional<utils::not_null<symbolic::SymbolicExpr *>> read(const symbolic::Address &addr);

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

        /// Variable address to symbolic expression mapping
        std::unordered_map<symbolic::VariableAddress,
                           utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>
            memoryMap_variableAddr_;

        /**
         * @brief Constant range mapping.
         * Maps a base address to non-overlapping constant ranges (non-zero length).
         */
        std::unordered_map<
            symbolic::SymbolAddress::BaseInfo,
            std::map<ConstRange, utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>>
            memoryMap_constantRange_; ///< ConstRanges must be non-overlapping and non-zero-length.

        /// Symbolic range mapping
        std::unordered_map<
            symbolic::SymbolAddress::BaseInfo,
            std::unordered_map<symbolic::SymbolAddress,
                               utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>>
            memoryMap_symbolicRange_;
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
        static std::unique_ptr<symbolic::Address> compose_address(
            symbolic::SymbolAddress::BaseInfo base,
            uint64_t off,
            uint64_t len) {
            if (len == 0)
                ERROR("Length should not be 0, something goes wrong.");
            else if (len == 1)
                return std::make_unique<symbolic::SymbolAddress>(
                    std::move(base.from_), base.fromPoint_,
                    std::make_unique<symbolic::LiteralExpr>(off));
            else
                return make_unique<symbolic::SymbolAddress>(
                    std::move(base.from_), base.fromPoint_,
                    std::make_unique<symbolic::LiteralExpr>(off),
                    std::make_unique<symbolic::LiteralExpr>(len));
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

            using UPtrRef =
                std::conditional_t<IsConst,
                                   utils::not_null<const symbolic::SymbolicExpr *>,
                                   utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> &>;
            using R = std::pair<symbolic::AddressBox, UPtrRef>;

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
             * Depending on phase, extracts variable, constant range, symbolic range,
             * or Structure field as address and associated value.
             */
            R operator*() const {
                switch (phase_) {
                    case Phase::VarAddr: {
                        const symbolic::Address &addr = var_outer_->first;
                        if constexpr (IsConst)
                            return R{addr, var_outer_->second.get().get()};
                        else
                            return R{addr, var_outer_->second};
                    }
                    case Phase::Const: {
                        const symbolic::SymbolAddress::BaseInfo &base = c_outer_->first;
                        const auto [off, offPlusLen]                  = c_inner_->first;
                        auto addr = compose_address(base, off, offPlusLen - off);
                        if constexpr (IsConst)
                            return R{std::move(addr), c_inner_->second.get().get()};
                        else
                            return R{std::move(addr), c_inner_->second};
                    }
                    case Phase::Symb: {
                        const symbolic::Address &addr = s_inner_->first;
                        if constexpr (IsConst)
                            return R{addr, s_inner_->second.get().get()};
                        else
                            return R{addr, s_inner_->second};
                    }
                    case Phase::Field: {
                        // Handle Structure fields
                        assert(!state_saver_.empty());
                        auto &current_state = state_saver_.top();
                        assert(current_state.st_ != nullptr);
                        auto &baseAddr = current_state.base_addr_;
                        auto &st       = *current_state.st_;
                        auto &index    = current_state.index_;
                        auto addr      = make_unique<symbolic::FieldAddress>(
                            st.getInfo().definition_,
                            std::pair<utils::not_null<std::unique_ptr<const symbolic::Address>>,
                                           const size_t>{baseAddr.get().addressClone().into_underlying(),
                                                         index});
                        auto &fieldValue = st.getFieldValue(index);
                        if constexpr (IsConst)
                            return R{std::move(addr), fieldValue.get().get()};
                        else
                            return R{std::move(addr), fieldValue};
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
                symbolic::Structure *st_;              ///< Pointer to Structure
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
                if (value->getType() != symbolic::SymbolicExpr::ExprType::Structure) {
                    advance_without_check();
                    return;
                }
                // Dive into Structure's fields
                auto &st = dynamic_cast<const symbolic::Structure &>(*value);
                auto state =
                    FieldState{std::move(addr), const_cast<symbolic::Structure *>(&st), 0, phase_};
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

    class Path {
      public:
        using EvalResult = std::pair<std::vector<utils::not_null<std::unique_ptr<Path>>>, Formulas>;

        Path(context::ACSLContext &context, symbolic::SourcePoint startPoint)
            : context_(context), startPoint_(startPoint) {};
        ~Path() = default;
        Path(const Path &other, bool shallowCopy);
        void swap(Path &o) noexcept;

        enum class PathState {
            Step,
            Continue,
            Break,
            Return
        };

        void resymbolize(symbolic::SourcePoint newStartPoint);

        utils::not_null<std::unique_ptr<symbolic::Address>> extractLValue(const clang::Expr *lhs);

        utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> getVarState(
            const clang::VarDecl *var) const;
        const Formulas &getPathConditions() const;

        utils::not_null<symbolic::VariableAddress *> allocMemory(const clang::VarDecl *);

        void updateMemory(const symbolic::Address &addr,
                          utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr);
        void updateVarState(utils::not_null<const clang::VarDecl *> var,
                            utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr);
        void insertPathCondition(utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> cond);

        void setReturnExpr(
            std::optional<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>> expr) {
            if (expr == std::nullopt) {
                returnExpr_ = std::nullopt;
                return;
            }
            returnExpr_.emplace(std::move(expr).value().into_underlying());
        };
        void setPathState(PathState state) { currentState_ = state; }

        bool isActive() const { return currentState_ == PathState::Step; }
        bool isUnchanged(const symbolic::Address &addr,
                         std::optional<symbolic::SourcePoint> since = std::nullopt) const;
        bool is_point_to_structure(const symbolic::Address &addr) const;
        std::unique_ptr<Path> clone() const;

        const clang::Stmt *StmtCtx = nullptr;

        std::string dump() const;
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

        // SET: List of symbolic expressions representing the path condition.
        Formulas pathConditions_;

        // Holds the current path state. Default is set to Step
        PathState currentState_ = PathState::Step;

        std::optional<utils::not_null<std::unique_ptr<const symbolic::SymbolicExpr>>> returnExpr_ =
            std::nullopt;

        context::ACSLContext &context_;

        symbolic::SourcePoint startPoint_;
    };

    class ProgramState {
      public:
        ProgramState(std::unique_ptr<Path> initialPath,
                     std::unique_ptr<ACSLFunction> func,
                     context::ACSLContext &context);
        ProgramState(std::unique_ptr<ACSLFunction> func, context::ACSLContext &context);
        ~ProgramState() = default;
        ProgramState(const ProgramState &);
        ProgramState(ProgramState &&) = default;
        ProgramState &operator=(ProgramState &&);

        void init();

        void step(const clang::Stmt *stmt);
        Formulas stepExpr(const clang::Expr *expr);

        void addNewDecls(const std::vector<const clang::VarDecl *> &varDecls);

        void setStates(Path::PathState state, const clang::Stmt *stmt);

        void setReturnExpr(const clang::Expr *expr);
        void updateVarState(const clang::BinaryOperator *binOp);

        std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<ProgramState>> splitActiveInactive();
        static std::unique_ptr<ProgramState> merge(const std::vector<const ProgramState *> &states);
        static std::unique_ptr<ProgramState> merge(
            const std::vector<std::unique_ptr<ProgramState>> &states);
        std::unique_ptr<ProgramState> clone(bool withPath = true) const;
        std::unique_ptr<ProgramState> cloneWithPaths(
            std::vector<std::unique_ptr<Path>> &newPaths) const;
        bool isInactive() const;

        const clang::Stmt *StmtCtx = nullptr;

        std::string dump() const;
        void resetState();
        void resymbolize(symbolic::SourcePoint newStartPoint);

        auto getPaths() const -> const auto & { return paths_; }
        auto getPaths() -> auto & { return paths_; }
        auto getFunction() const -> const auto & { return func_; }
        auto getContext() const -> const auto & { return context_; }
        auto getStartPoint() const -> const auto & { return startPoint_; }

        std::optional<utils::not_null<std::unique_ptr<Path>>> takePath(size_t i);
        std::vector<utils::not_null<std::unique_ptr<Path>>> takeAllPaths();

      private:
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

        context::ACSLContext &context_;

        symbolic::SourcePoint startPoint_;
    };

    struct VarManager {
        size_t numVars = 0;
        std::unordered_map<std::string, size_t> varIndexMap;
        std::vector<std::string> orderedVars;
        std::vector<const clang::VarDecl *> varDecls;

        static VarManager fromPaths(
            const std::vector<utils::not_null<std::unique_ptr<Path>>> &paths) {
            VarManager vm;
            std::vector<std::pair<std::string, const clang::VarDecl *>> rawVars;
            size_t varCounter = 0;

            for (const auto &path : paths) {
                const auto &varAddrMap = path->getVarAddr();
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
            }

            for (const auto &[name, varDecl] : rawVars) {
                vm.orderedVars.push_back(name);
                vm.varDecls.push_back(varDecl);
            }
            for (const auto &[name, _] : rawVars)
                vm.orderedVars.push_back(name + "_init");

            vm.numVars = rawVars.size() * 2;
            return vm;
        }
    };

    struct InvsAndPostStates {
        std::optional<std::string> invs_;
        std::pair<symbolic::AddressBoxMap<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>,
                  std::vector<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>>
            postStates_;
    };

    std::vector<InvsAndPostStates> buildLoopInvariant(
        std::unique_ptr<symbolic::SymbolicExpr> loopCond,
        const std::vector<std::unique_ptr<Path>> &paths,
        const ProgramState &initState);

    std::vector<InvsAndPostStates> buildLoopInvariant(
        std::unique_ptr<symbolic::SymbolicExpr> loopCond,
        const ProgramState &loopEntry,
        const ProgramState &loopCurrent);
} // namespace acslg::analyzer
#endif
