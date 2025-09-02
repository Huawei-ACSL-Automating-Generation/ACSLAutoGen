#ifndef STATE_H
#define STATE_H

#include <unordered_map>
#include <map>
#include <variant>
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Expr.h>
#include <ppl.hh>
#include "Symbolic/expr.h"
#include "function.h"

using namespace Symbolic;
using namespace std;
using LValueTarget = std::variant<const clang::VarDecl *, std::unique_ptr<Address>>;
using Formulas     = std::vector<std::unique_ptr<SymbolicExpr>>;
using TransRel     = std::tuple<int, int, Parma_Polyhedra_Library::C_Polyhedron *>;
using InitRel      = std::pair<int, Parma_Polyhedra_Library::C_Polyhedron *>;

class MemoryModel {
  public:
    struct flat_view;
    MemoryModel() = default;
    MemoryModel(const MemoryModel &);
    MemoryModel &operator=(const MemoryModel &);
    MemoryModel(MemoryModel &&)            = default;
    MemoryModel &operator=(MemoryModel &&) = default;
    unique_ptr<SymbolicExpr> read(const Address &addr) const;
    void write(const Address &address, unique_ptr<const SymbolicExpr> value);
    size_t size() const;
    bool contains(const Address &addr) const;

    void clear() {
        memoryMap_noOffset_.clear();
        memoryMap_constantRange_.clear();
        memoryMap_symbolicRange_.clear();
    }

    // flat_view flat();
    const flat_view flat() const;

  private:
    friend struct flat_view;

    unordered_map<Address, unique_ptr<const SymbolicExpr>, AddressHash> memoryMap_noOffset_;

    unordered_map<Address,
                  map<pair<uint64_t, uint64_t>, unique_ptr<const SymbolicExpr>>,
                  AddressHash>
        memoryMap_constantRange_; ///< Ranges(pair<uint64_t, uint64_t>) must be non-overlapping
                                  ///< and non-zero-length.
    unordered_map<Address,
                  unordered_map<Address, unique_ptr<const SymbolicExpr>, AddressHash>,
                  AddressHash>
        memoryMap_symbolicRange_;
};

struct MemoryModel::flat_view {
  private:
    static Address compose_address(const Address &base, uint64_t off, uint64_t len) {
        auto result = base;
        result.setOffset(make_unique<LiteralExpr>(off));
        if (len == 0)
            ERROR("Length should not be 0, something goes wrong.");
        if (len > 1)
            result.setLength(make_unique<LiteralExpr>(len));

        return result;
    }
    template <class Owner> static auto &no_offset_map(Owner &mm) { return mm.memoryMap_noOffset_; }
    template <class Owner> static auto &const_range_map(Owner &mm) {
        return mm.memoryMap_constantRange_;
    }
    template <class Owner> static auto &symb_range_map(Owner &mm) {
        return mm.memoryMap_symbolicRange_;
    }

  public:
    using UPtr = std::unique_ptr<const SymbolicExpr>;
    template <bool IsConst> class flat_iterator {
        using Owner   = std::conditional_t<IsConst, const MemoryModel, MemoryModel>;
        using NoOuter = decltype(no_offset_map(std::declval<Owner &>()).begin());
        using COuter  = decltype(const_range_map(std::declval<Owner &>()).begin());
        using CInner  = decltype(const_range_map(std::declval<Owner &>()).begin()->second.begin());

        using SOuter = decltype(symb_range_map(std::declval<Owner &>()).begin());
        using SInner = decltype(symb_range_map(std::declval<Owner &>()).begin()->second.begin());

        using UPtrRef = std::conditional_t<IsConst, const UPtr &, UPtr &>;

      public:
        using difference_type   = std::ptrdiff_t;
        using value_type        = std::pair<Address, UPtr>;
        using reference         = std::pair<Address, UPtrRef>;
        using rvalue_reference  = value_type;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept  = std::input_iterator_tag;

        flat_iterator() = default;
        flat_iterator(Owner &o, bool to_begin) : owner_(o) {
            no_outer_     = no_offset_map(owner_).begin();
            no_outer_end_ = no_offset_map(owner_).end();

            c_outer_     = const_range_map(owner_).begin();
            c_outer_end_ = const_range_map(owner_).end();

            s_outer_     = symb_range_map(owner_).begin();
            s_outer_end_ = symb_range_map(owner_).end();

            if (to_begin)
                advance_to_first();
            else
                phase_ = Phase::End;
        }

        reference operator*() const {
            switch (phase_) {
                case Phase::NoOff: {
                    const Address &key = no_outer_->first;
                    return reference{key, no_outer_->second};
                }
                case Phase::Const: {
                    const Address &base          = c_outer_->first;
                    const auto [off, offPlusLen] = c_inner_->first;
                    Address composed             = compose_address(base, off, offPlusLen - off);
                    return reference{std::move(composed), c_inner_->second};
                }
                case Phase::Symb: {
                    const Address &key = s_inner_->first;
                    return reference{key, s_inner_->second};
                }
                default: break;
            }
            UNREACHABLE();
        }

        flat_iterator &operator++() {
            advance();
            return *this;
        }
        flat_iterator operator++(int) {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        friend bool operator==(const flat_iterator &a, const flat_iterator &b) {
            if (&a.owner_ != &b.owner_)
                return false;
            if (a.phase_ == Phase::End && b.phase_ == Phase::End)
                return true;
            if (a.phase_ != b.phase_)
                return false;

            switch (a.phase_) {
                case Phase::NoOff: return a.no_outer_ == b.no_outer_;
                case Phase::Const:
                    return a.c_outer_ == b.c_outer_ &&
                           (a.c_outer_ == a.c_outer_end_ || a.c_inner_ == b.c_inner_);
                case Phase::Symb:
                    return a.s_outer_ == b.s_outer_ &&
                           (a.s_outer_ == a.s_outer_end_ || a.s_inner_ == b.s_inner_);
                default: return true;
            }
        }

      private:
        enum class Phase {
            NoOff,
            Const,
            Symb,
            End
        };
        Owner &owner_;
        Phase phase_ = Phase::End;

        NoOuter no_outer_{}, no_outer_end_{};
        COuter c_outer_{}, c_outer_end_{};
        CInner c_inner_{};
        SOuter s_outer_{}, s_outer_end_{};
        SInner s_inner_{};

        void advance_to_first() {
            if (no_outer_ != no_outer_end_) {
                phase_ = Phase::NoOff;
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

        void advance() {
            if (phase_ == Phase::NoOff) {
                ++no_outer_;
                if (no_outer_ != no_outer_end_)
                    return;

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
            } else if (phase_ == Phase::Const) {
                ++c_inner_;
                while (c_outer_ != c_outer_end_ && c_inner_ == c_outer_->second.end()) {
                    ++c_outer_;
                    if (c_outer_ != c_outer_end_)
                        c_inner_ = c_outer_->second.begin();
                }
                if (c_outer_ != c_outer_end_)
                    return;
                for (; s_outer_ != s_outer_end_; ++s_outer_) {
                    s_inner_ = s_outer_->second.begin();
                    if (s_inner_ != s_outer_->second.end()) {
                        phase_ = Phase::Symb;
                        return;
                    }
                }
                phase_ = Phase::End;
            } else if (phase_ == Phase::Symb) {
                ++s_inner_;
                while (s_outer_ != s_outer_end_ && s_inner_ == s_outer_->second.end()) {
                    ++s_outer_;
                    if (s_outer_ != s_outer_end_)
                        s_inner_ = s_outer_->second.begin();
                }
                if (s_outer_ == s_outer_end_)
                    phase_ = Phase::End;
            }
        }
    }; // flat_iterator

    using iterator       = flat_iterator<false>;
    using const_iterator = flat_iterator<true>;

    explicit flat_view(MemoryModel &p) : owner_(p) {}

    iterator begin() { return iterator{owner_, true}; }
    iterator end() { return iterator{owner_, false}; }
    const_iterator begin() const { return const_iterator{owner_, true}; }
    const_iterator end() const { return const_iterator{owner_, false}; }

  private:
    MemoryModel &owner_;
};

class Path {
  public:
    using EvalResult = std::pair<std::vector<std::unique_ptr<Path>>, Formulas>;

    Path()  = default;
    ~Path() = default;
    Path(const Path &other, bool shallowCopy);

    enum class PathState {
        Step,
        Continue,
        Break,
        Return
    };

    void resymbolize();

    LValueTarget extractLValue(const clang::Expr *lhs);
    std::unique_ptr<Address> extractAddress(const clang::Expr *lhs);

    std::unique_ptr<SymbolicExpr> getVarState(const clang::VarDecl *var) const;
    const Formulas &getPathConditions() const;

    Address *allocMemory(const clang::VarDecl *);
    std::unique_ptr<Address> allocMemory(const Address &from);

    void updateMemory(Address *addr, std::unique_ptr<SymbolicExpr> expr);
    void updateVarState(const clang::VarDecl *var, std::unique_ptr<SymbolicExpr> expr);
    void insertPathCondition(std::unique_ptr<SymbolicExpr> cond);

    void setReturnExpr(std::unique_ptr<SymbolicExpr> expr) {
        if (expr == nullptr)
            returnExpr_ = std::nullopt;
        else
            returnExpr_.emplace(std::move(expr));
    };
    void setPathState(PathState state) { currentState_ = state; }

    bool isActive() const { return currentState_ == PathState::Step; }
    bool isUnchanged(const Address &addr) const;
    std::unique_ptr<Path> clone() const;

    const clang::Stmt *StmtCtx = nullptr;

    std::string dump() const;
    EvalResult evalExpr(const clang::Expr *expr);
    friend class ProgramState;

    auto getVarAddr() const -> const auto & { return varAddr_; };
    auto getMemoryState() const -> const auto & { return memoryState_; }
    int getNextSymVarId() { return symbolVarAndAddrCounter_++; }
    auto getReturnExpr() const -> const auto & { return returnExpr_; }
    auto getPathState() const -> const auto & { return currentState_; }
    auto getAddrCounter() const -> const auto & { return symbolVarAndAddrCounter_; }
    auto getNextStructureId() { return structureCounter_++; }

  private:
    // Map: variable record definition ID -> corresponding symbolic address.
    std::unordered_map<const clang::VarDecl *, std::unique_ptr<Address>> varAddr_;

    MemoryModel memoryState_;

    // SET: List of symbolic expressions representing the path condition.
    Formulas pathConditions_;

    // Holds the current path state. Default is set to Step
    PathState currentState_ = PathState::Step;

    optional<not_null<std::unique_ptr<const SymbolicExpr>>> returnExpr_ = std::nullopt;

    unsigned int symbolVarAndAddrCounter_ = 0;

    // The Structure's counter is decoupled from the Variable and Address's counter to prevent
    // interference with the ppl library's computations.
    unsigned int structureCounter_ = 0;
};

class ProgramState {
  public:
    ProgramState(std::unique_ptr<Path> initialPath, std::unique_ptr<ACSLFunction> context);
    ProgramState(std::unique_ptr<ACSLFunction> context);
    ~ProgramState() = default;

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
    std::unique_ptr<ProgramState> clone() const;
    std::unique_ptr<ProgramState> cloneWithPaths(std::vector<std::unique_ptr<Path>> &newPaths) const;
    bool isInactive() const;

    const clang::Stmt *StmtCtx = nullptr;

    std::string dump() const;
    void resetState();
    void resymbolize();

    auto getPaths() const -> const auto & { return paths_; }
    auto getContext() const -> const auto & { return context_; }

    void deriveLinearPostState(std::vector<Formulas> invs);

  private:
    // Only be used in step when processing SwitchStmt, just for a cleaner code.
    std::vector<std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<SymbolicExpr>>> splitStateBySwitchCond(
        const clang::Expr *switchCond);
    void stepSimpleSwitch(const clang::SwitchStmt *switchstmt);

    void stepBranch(const std::vector<const clang::Expr *> &branchConds,
                    const std::vector<const clang::Stmt *> &branchStmts);

    void stepLoop(const clang::Stmt *loopStmt);

    std::vector<std::unique_ptr<Path>> paths_{};

    std::unique_ptr<ACSLFunction> context_;
};

struct VarManager {
    size_t numVars = 0;
    std::unordered_map<std::string, size_t> varIndexMap;
    std::vector<std::string> orderedVars;
    std::vector<const clang::VarDecl *> varDecls;

    static VarManager fromPaths(const std::vector<std::unique_ptr<Path>> &paths) {
        VarManager vm;
        std::vector<pair<std::string, const clang::VarDecl *>> rawVars;
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

    [[deprecated("seems to contain an error")]] int getIndex(const Symbolic::Variable &var) const {
        auto it = varIndexMap.find(var.getName());
        if (it == varIndexMap.end()) {
            ERROR("VarManager: Variable name '" + var.getName() + "' not found in index map.");
        }
        return it->second;
    }
};

struct InvsAndPostStates {
    std::optional<std::string> invs_;
    pair<std::unordered_map<Address, unique_ptr<SymbolicExpr>, AddressHash>,
         vector<unique_ptr<SymbolicExpr>>>
        postStates_;
};

std::vector<InvsAndPostStates> buildLoopInvariant(unique_ptr<SymbolicExpr> loopCond,
                                                  const std::vector<std::unique_ptr<Path>> &paths,
                                                  const ProgramState &initState);

vector<InvsAndPostStates> buildLoopInvariant(unique_ptr<SymbolicExpr> loopCond,
                                             const ProgramState &loopEntry,
                                             const ProgramState &loopCurrent);
#endif
