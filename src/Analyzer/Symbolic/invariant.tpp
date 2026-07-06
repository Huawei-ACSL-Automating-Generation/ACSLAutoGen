/**
 * @file invariant.tpp
 * @brief Template implementations for building loop invariants using polyhedral analysis.
 */
#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_INVARIANT_TPP__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_INVARIANT_TPP__

#include <optional>
#include <ppl.hh>
#include <string>

#include "Analyzer/state.h"

namespace acslg::analyzer {
    namespace details {
        namespace ppl = Parma_Polyhedra_Library;
        ppl::C_Polyhedron buildIdentityPoly(const VarManager &vm);
        ppl::C_Polyhedron convertFormulaToPoly(const Formulas &assertions, const VarManager &vm);
        Formulas cloneFormulas(const Formulas &input);
        std::vector<Formulas> negateFormulas(const Formulas &input);
        ppl::C_Polyhedron buildPathPoly(const Path &path, const VarManager &vm, bool init = false);
        ppl::C_Polyhedron primedPolyhedron(const ppl::C_Polyhedron &poly, const VarManager &vm);
        Formulas preprocessConjConds(const Formulas &conjConds);
        Formulas preprocessConjConds(const PathConditions &conjConds);
        void dump(const Parma_Polyhedra_Library::C_Polyhedron &poly, const VarManager &vm);
        void dump(const Parma_Polyhedra_Library::Linear_Expression &expr, const VarManager &vm);

        struct PathsAndExitInvs {
            std::vector<std::vector<ppl::C_Polyhedron>> pathsInvs;
            std::vector<ppl::C_Polyhedron> normalExitInvs;
            std::vector<std::vector<ppl::C_Polyhedron>> interruptExitInvs;
        };
        PathsAndExitInvs computeLinearInv(
            const std::vector<std::string> &locations,
            size_t exitIdx,
            std::vector<std::tuple<size_t, size_t, ppl::C_Polyhedron>> &transitions,
            const std::pair<size_t, ppl::C_Polyhedron> &initial,
            const VarManager &vm);

        std::optional<std::string> buildInvs(
            const std::vector<std::vector<Parma_Polyhedra_Library::C_Polyhedron>> &polyss,
            const VarManager &vm);

        using AddrValueAndCondsPair = std::pair<
            symbolic::AddressBoxMap<symbolic::ExprHandle>,
            std::vector<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>>>;
        AddrValueAndCondsPair buildPostState(const ppl::C_Polyhedron &poly,
                                             const Path &initPath,
                                             const VarManager &vm);
    } // namespace details

    /**
     * @brief Build invariants and post-states for a loop using polyhedral abstraction.
     * @param loopCond [in] Symbolic loop condition.
     * @param entryPath [in] Path at loop entry to seed variable mapping.
     * @param loopCurrent [in] Program state representing one loop iteration.
     * @param inactivePaths [in] Collection of inactive/interrupt paths.
     * @param generateBranches [in] Whether to enumerate branch-specific post-states.
     * @return Invariants and synthesized post-states for normal and interrupt exits.
     */
    InvsAndPostStates buildLoopInvariant(std::unique_ptr<symbolic::SymbolicExpr> loopCond,
                                         const Path &entryPath,
                                         const ProgramState &loopCurrent,
                                         std::ranges::range auto &inactivePaths,
                                         bool generateBranches) {
        InvsAndPostStates invsAndPostStates;
        auto &paths         = loopCurrent.getPaths();
        auto normalPathsNum = paths.size();
        VarManager vm       = VarManager::fromPath(entryPath);

        auto initPathPoly = details::buildPathPoly(entryPath, vm, true);

        std::vector<Parma_Polyhedra_Library::C_Polyhedron> transPolys;
        for (const auto &path : paths)
            transPolys.push_back(details::buildPathPoly(*path, vm));

        for (const auto &path : inactivePaths)
            transPolys.push_back(details::buildPathPoly(*path, vm));

        std::vector<Parma_Polyhedra_Library::C_Polyhedron> locAsStartPolys;
        std::vector<Parma_Polyhedra_Library::C_Polyhedron> locAsEndPolys;
        for (const auto &path : paths) {
            locAsStartPolys.push_back(details::convertFormulaToPoly(
                details::preprocessConjConds(path->getPathConditions()), vm));
            locAsEndPolys.push_back(details::primedPolyhedron(locAsStartPolys.back(), vm));
        }
        for (const auto &path : inactivePaths) {
            locAsStartPolys.push_back(details::convertFormulaToPoly(
                details::preprocessConjConds(path->getPathConditions()), vm));
            locAsEndPolys.push_back(details::primedPolyhedron(locAsStartPolys.back(), vm));
        }

        std::vector<std::string> locations;

        locations.push_back("init");
        for (size_t i = 0; i < paths.size(); ++i)
            locations.push_back("normal_path_" + std::to_string(i));
        for (size_t i = 0; i < inactivePaths.size(); ++i)
            locations.push_back("interrupt_path_" + std::to_string(i));
        locations.push_back("normal_exit");
        for (size_t i = 0; i < inactivePaths.size(); ++i)
            locations.push_back("interrupt_exit_" + std::to_string(i));

        auto pathNum   = paths.size() + inactivePaths.size();
        size_t initIdx = 0;
        size_t exitIdx = pathNum + 1;
        assert(pathNum + 2 + inactivePaths.size() == locations.size());

        auto identityPoly = details::buildIdentityPoly(vm); // shared across all paths

        Formulas assertions;
        assertions.push_back(std::move(loopCond)); // Yes, there is only one.
        assertions               = details::preprocessConjConds(std::move(assertions));
        auto baseConditionPoly   = details::convertFormulaToPoly(assertions, vm);
        auto primedConditionPoly = details::primedPolyhedron(baseConditionPoly, vm);

        auto negatedConds = details::negateFormulas(assertions);

        // === Precompute negated polyhedra
        std::vector<Parma_Polyhedra_Library::C_Polyhedron> negatedCondPolys;
        auto dimension = vm.numVars * 2;
        for (auto &cond : negatedConds) {
            auto negatedCondPoly = details::convertFormulaToPoly(cond, vm);
            if (negatedCondPoly.is_empty())
                continue;
            negatedCondPolys.push_back(std::move(negatedCondPoly));
        }
        if (negatedCondPolys.empty()) {
            // if loop condition is 'true', this loop may has unknown condition or just be an
            // infinite loop. So we set the normal_exit condition as true to get a (maybe fake) post
            // state.
            negatedCondPolys.emplace_back(dimension, Parma_Polyhedra_Library::UNIVERSE);
        }

        // INFO("baseConditionPoly: ");
        // details::dump(baseConditionPoly, vm);
        // INFO("primedConditionPoly: ");
        // details::dump(primedConditionPoly, vm);
        // INFO("negatedCondPolys: ");
        // for (size_t i = 0; i < negatedCondPolys.size(); ++i) {
        //     INFO(i << ": ");
        //     details::dump(negatedCondPolys.at(i), vm);
        // }
        // INFO("transPolys: ");
        // for (size_t i = 0; i < transPolys.size(); ++i) {
        //     INFO(i << ": ");
        //     details::dump(transPolys.at(i), vm);
        // }
        // INFO("locAsStartPolys: ");
        // for (size_t i = 0; i < locAsStartPolys.size(); ++i) {
        //     INFO(i << ": ");
        //     details::dump(locAsStartPolys.at(i), vm);
        // }
        // INFO("locAsEndPolys: ");
        // for (size_t i = 0; i < locAsEndPolys.size(); ++i) {
        //     INFO(i << ": ");
        //     details::dump(locAsEndPolys.at(i), vm);
        // }

        std::vector<std::tuple<size_t, size_t, Parma_Polyhedra_Library::C_Polyhedron>> transitions;

        // === init -> path_k ===
        for (size_t k = 0; k < pathNum; ++k) {
            auto poly = identityPoly;
            poly.intersection_assign(locAsEndPolys[k]);
            transitions.push_back(std::make_tuple(initIdx, k + 1, poly));
        }

        // === path_j -> path_k ===
        // Interrupted path has no transformation.
        for (size_t j = 0; j < normalPathsNum; ++j) {
            for (size_t k = 0; k < pathNum; ++k) {
                auto joined = transPolys[j];
                joined.intersection_assign(locAsStartPolys[j]);
                joined.intersection_assign(locAsEndPolys[k]);
                joined.intersection_assign(baseConditionPoly);
                joined.intersection_assign(primedConditionPoly);

                if (!joined.is_empty())
                    transitions.push_back(std::make_tuple(j + 1, k + 1, joined));
            }
        }

        // === path_j -> normal_exit using precomputed negatedPolys
        for (size_t j = 0; j < normalPathsNum; ++j) {
            for (auto &negatedCondPoly : negatedCondPolys) {
                auto exitPoly = details::primedPolyhedron(negatedCondPoly, vm);
                exitPoly.intersection_assign(transPolys[j]);
                exitPoly.intersection_assign(locAsStartPolys[j]);
                exitPoly.intersection_assign(baseConditionPoly);
                if (!exitPoly.is_empty())
                    transitions.push_back(std::make_tuple(j + 1, exitIdx, exitPoly));
            }
        }

        for (size_t j = normalPathsNum, k = exitIdx + 1; j < pathNum; ++j, ++k) {
            assert(k < locations.size());
            auto joined = transPolys.at(j);
            joined.intersection_assign(locAsStartPolys.at(j));
            joined.intersection_assign(baseConditionPoly);

            if (!joined.is_empty())
                transitions.push_back(std::make_tuple(j + 1, k, joined));
        }

        // === this initPoly as InitRel ===
        auto initRel = std::make_pair(initIdx, initPathPoly);
        auto invs    = details::computeLinearInv(locations, exitIdx, transitions, initRel, vm);

        invsAndPostStates.invs = details::buildInvs(invs.pathsInvs, vm);

        auto mergeClosures = [](const std::vector<Parma_Polyhedra_Library::C_Polyhedron> &polys)
            -> std::optional<Parma_Polyhedra_Library::C_Polyhedron> {
            if (polys.empty())
                return std::nullopt;
            Parma_Polyhedra_Library::C_Polyhedron merged = polys.front();
            for (size_t i = 1; i < polys.size(); ++i)
                merged.poly_hull_assign(polys.at(i));
            return merged;
        };

        if (generateBranches) {
            for (auto &exitInv : invs.normalExitInvs)
                invsAndPostStates.normalPostStates.push_back(
                    details::buildPostState(exitInv, entryPath, vm));

            for (auto &exitInvsPerPath : invs.interruptExitInvs) {
                std::vector<InvsAndPostStates::MemoryMapAndPathConds> postState;
                for (auto &exitInv : exitInvsPerPath)
                    postState.push_back(details::buildPostState(exitInv, entryPath, vm));
                invsAndPostStates.interruptPostStates.push_back(std::move(postState));
            }
        } else {
            if (auto mergedExit = mergeClosures(invs.normalExitInvs)) {
                invsAndPostStates.normalPostStates.push_back(
                    details::buildPostState(*mergedExit, entryPath, vm));
            }

            for (auto &exitInvsPerPath : invs.interruptExitInvs) {
                std::vector<InvsAndPostStates::MemoryMapAndPathConds> postState;
                if (auto mergedExit = mergeClosures(exitInvsPerPath))
                    postState.push_back(details::buildPostState(*mergedExit, entryPath, vm));
                invsAndPostStates.interruptPostStates.push_back(std::move(postState));
            }
        }

        return invsAndPostStates;
    }

    // std::vector<InvsAndPostStates> buildLoopInvariantWithComplexLoop(
    //     Formulas loopCond,
    //     const std::vector<unique_ptr<Path>> &paths,
    //     const ProgramState &initState) {
    //     // TODO: process case that paths contain return.
    //     std::vector<InvsAndPostStates> invsAndPostStates;
    //     VarManager vm = VarManager::fromPaths(paths);

    //     std::vector<Formulas> processedCond = preprocessLoopCond(std::move(loopCond));

    //     const auto &initPaths = initState.getPaths();
    //     std::vector<Parma_Polyhedra_Library::C_Polyhedron *> initPathPolys;
    //     for (size_t i = 0; i < initPaths.size(); ++i) {
    //         if (initPaths[i])
    //             initPathPolys.push_back(buildPathPoly(*initPaths[i], vm, true));
    //         else
    //             UNREACHABLE();
    //     }

    //     std::vector<Parma_Polyhedra_Library::C_Polyhedron *> transPolys;
    //     for (const auto &path : paths) {
    //         if (path)
    //             transPolys.push_back(buildPathPoly(*path, vm));
    //         else
    //             UNREACHABLE();
    //     }

    //     std::vector<std::string> locations;

    //     locations.push_back("init");
    //     for (size_t i = 0; i < paths.size(); ++i) {
    //         locations.push_back("path_" + to_string(i));
    //     }
    //     locations.push_back("exit");

    //     int initIdx = 0;
    //     int exitIdx = paths.size() + 1;

    //     // TODO: fully disjunctive.
    //     auto identityPoly = buildIdentityPoly(vm); // shared across all paths
    //     for (size_t i = 0; i < processedCond.size(); ++i) {
    //         const Formulas &assertions = processedCond[i];
    //         auto *baseConditionPoly    = convertFormulaToPoly(assertions, vm);
    //         auto *primedConditionPoly  = primedPolyhedron(*baseConditionPoly, vm);

    //         auto negatedConds = negateFormulas(cloneFormulas(assertions));

    //         // === Precompute negated polyhedra
    //         std::vector<C_Polyhedron *> negatedPolys;
    //         for (const auto &neg : negatedConds) {
    //             auto *poly = convertFormulaToPoly(neg, vm);
    //             negatedPolys.push_back(poly);
    //         }
    //         for (size_t path_i = 0; path_i < initPathPolys.size(); ++path_i) {
    //             auto *initPoly = initPathPolys[path_i];
    //             std::vector<TransRel> transitions;

    //             // === init -> path_k ===
    //             for (size_t k = 0; k < paths.size(); ++k) {
    //                 auto *poly = new C_Polyhedron(identityPoly);
    //                 transitions.push_back(make_tuple(initIdx, static_cast<int>(k + 1), poly));
    //             }

    //             // === path_j -> path_k ===
    //             for (size_t j = 0; j < paths.size(); ++j) {
    //                 for (size_t k = 0; k < paths.size(); ++k) {
    //                     auto *joined = new C_Polyhedron(*transPolys[j]);
    //                     joined->intersection_assign(*baseConditionPoly);
    //                     joined->intersection_assign(*primedConditionPoly);

    //                     if (!joined->is_empty()) {
    //                         transitions.push_back(make_tuple(static_cast<int>(j + 1),
    //                                                          static_cast<int>(k + 1), joined));
    //                     } else {
    //                         delete joined;
    //                     }
    //                 }
    //             }

    //             // === path_j -> exit using precomputed negatedPolys
    //             for (size_t j = 0; j < paths.size(); ++j) {
    //                 for (auto *negPoly : negatedPolys) {
    //                     auto *exitPoly = primedPolyhedron(*negPoly, vm);
    //                     exitPoly->intersection_assign(*transPolys[j]);
    //                     exitPoly->intersection_assign(*baseConditionPoly);
    //                     if (!exitPoly->is_empty()) {
    //                         transitions.push_back(
    //                             make_tuple(static_cast<int>(j + 1), exitIdx, exitPoly));
    //                     } else {
    //                         delete exitPoly;
    //                     }
    //                 }
    //             }

    //             // === this initPoly as InitRel ===
    //             InitRel initRel = make_pair(initIdx, new C_Polyhedron(*initPoly));
    //             auto invs       = computeLinearInv(locations, transitions, initRel, vm);

    //             if (!invs.exitInvs_.empty() &&
    //                 (invs.pathsInvs_.size() <= path_i ||
    //                  invs.pathsInvs_[path_i].size() != invs.exitInvs_.size())) {
    //                 if (invs.pathsInvs_.size() > path_i) {
    //                     for (auto &inv : invs.pathsInvs_[path_i]) {
    //                         INFO("invariant");
    //                         dump(inv, vm);
    //                         INFO("\n");
    //                     }
    //                 }
    //                 for (auto &inv : invs.exitInvs_) {
    //                     INFO("post state");
    //                     dump(inv, vm);
    //                     INFO("\n");
    //                     INFO(buildPostPath(inv, *initPaths[path_i], vm)->dump());
    //                 }
    //                 ERROR("Wrong? or check computeLinearInv.\n"
    //                       "invariants's size: " +
    //                       to_string(invs.pathsInvs_.size()) +
    //                       (invs.pathsInvs_.size() <= path_i
    //                            ? " less or equal to path_i!\n"
    //                            : "\ninvariants[path_i]'s size: " +
    //                                  to_string(invs.pathsInvs_[path_i].size()) + "\n") +
    //                       "post state's size: " + to_string(invs.exitInvs_.size()) + "\n");
    //             }

    //             for (size_t k = 0; k < invs.exitInvs_.size(); k++) {
    //                 dump(invs.pathsInvs_[path_i][k], vm);
    //                 dump(invs.exitInvs_[k], vm);

    //                 auto loopInvs   = buildInvs(invs.pathsInvs_[path_i][k], vm);
    //                 auto postStates = buildPostPath(invs.exitInvs_[k], *initPaths[path_i], vm);

    //                 invsAndPostStates.emplace_back(std::move(loopInvs), std::move(postStates));
    //             }
    //             delete initRel.second;
    //         }

    //         delete baseConditionPoly;
    //         delete primedConditionPoly;
    //         for (auto *p : negatedPolys) {
    //             delete p;
    //         }
    //     }

    //     return invsAndPostStates;
    // }
} // namespace acslg::analyzer
#endif
