/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamTransforms.h
 *
 * Defines RAM transformation passes.
 *
 ***********************************************************************/

#pragma once

#include "RamConditionLevel.h"
#include "RamConstValue.h"
#include "RamExpressionLevel.h"
#include "RamTransformer.h"
#include "RamTranslationUnit.h"
#include <memory>
#include <string>

namespace souffle {

class RamProgram;

/**
 * @class HoistConditionsTransformer
 * @brief Hosts conditions in a loop-nest to the most-outer/semantically-correct loop
 *
 * Hoists the conditions to the earliest point in the loop nest where their
 * evaluation is still semantically correct.
 *
 * The transformations assumes that filter operations are stored verbose,
 * i.e. a conjunction is expressed by two consecutive filter operations.
 * For example ..
 *
 *  QUERY
 *   ...
 *    IF C1 /\ C2 then
 *     ...
 *
 * should be rewritten / or produced by the translator as
 *
 *  QUERY
 *   ...
 *    IF C1
 *     IF C2
 *      ...
 *
 * otherwise the levelling becomes imprecise, i.e., for both conditions
 * the most outer-level is sought rather than considered separately.
 *
 * If there are transformers prior to hoistConditions() that introduce
 * conjunction, another transformer is required that splits the
 * filter operations. However, at the moment this is not necessary
 * because the translator delivers already the right RAM format.
 *
 * TODO: break-up conditions while transforming so that this requirement
 * is removed.
 */
class HoistConditionsTransformer : public RamTransformer {
public:
    std::string getName() const override {
        return "HoistConditionsTransformer";
    }

    /**
     * @brief Hoist filter operations.
     * @param Program that is transformed
     * @return Flag showing whether the program has been changed by the transformation
     *
     * There are two types of conditions in
     * filter operations. The first type depends on tuples of
     * RamSearch operations. The second type are independent of
     * tuple access. Both types of conditions will be hoisted to
     * the most out-scope such that the program is still valid.
     */
    bool hoistConditions(RamProgram& program);

protected:
    RamConditionLevelAnalysis* rcla{nullptr};

    bool transform(RamTranslationUnit& translationUnit) override {
        rcla = translationUnit.getAnalysis<RamConditionLevelAnalysis>();
        return hoistConditions(*translationUnit.getProgram());
    }
};

/**
 * @class MakeIndexTransformer
 * @brief Make indexable operations to indexed operations.
 *
 * The transformer assumes that the RAM has been levelled before.
 * The conditions that could be used for an index must be located
 * immediately after the scan or aggregrate operation.
 *
 *  QUERY
 *   ...
 *   FOR t1 in A
 *    IF t1.x = 10 /\ t1.y = 20 /\ C
 *     ...
 *
 * will be rewritten to
 *
 *  QUERY
 *   ...
 *    SEARCH t1 in A INDEX t1.x=10 and t1.y = 20
 *     IF C
 *      ...
 */

class MakeIndexTransformer : public RamTransformer {
public:
    std::string getName() const override {
        return "MakeIndexTransformer";
    }

    /**
     * @brief Get expression of RAM element access
     *
     * @param Equivalence constraints of the format t1.x = <expression> or <expression> = t1.x
     * @param Element that was accessed, e.g., for t1.x this would be the index of attribute x.
     * @param Tuple identifier
     *
     * The method retrieves expression the expression of an equivalence constraint of the
     * format t1.x = <expr> or <expr> = t1.x
     */
    std::unique_ptr<RamExpression> getExpression(RamCondition* c, size_t& element, int level);

    /**
     * @brief Construct query patterns for an indexable operation
     * @param Query pattern that is to be constructed
     * @param Flag to indicate whether operation is indexable
     * @param A list of conditions that will be transformed to query patterns
     * @param Tuple identifier of the indexable operation
     * @result Remaining conditions that could not be transformed to an index
     */
    std::unique_ptr<RamCondition> constructPattern(std::vector<std::unique_ptr<RamExpression>>& queryPattern,
            bool& indexable, std::vector<std::unique_ptr<RamCondition>> conditionList, int identifier);

    /**
     * @brief Rewrite a scan operation to an indexed scan operation
     * @param Scan operation that is potentially rewritten to an IndexScan
     * @result The result is null if the scan could not be rewritten to an IndexScan;
     *         otherwise the new IndexScan operation is returned.
     */
    std::unique_ptr<RamOperation> rewriteScan(const RamScan* scan);

    /**
     * @brief Rewrite an aggregate operation to an indexed aggregate operation
     * @param Aggregate operation that is potentially rewritten to an indexed version
     * @result The result is null if the aggregate could not be rewritten to an indexed version;
     *         otherwise the new indexed version of the aggregate is returned.
     */
    std::unique_ptr<RamOperation> rewriteAggregate(const RamAggregate* agg);

    /**
     * @brief Make indexable RAM operation indexed
     * @param RAM program that is transformed
     * @result Flag that indicates whether the input program has changed
     */
    bool makeIndex(RamProgram& program);

protected:
    RamExpressionLevelAnalysis* rvla{nullptr};
    RamConstValueAnalysis* rcva{nullptr};
    bool transform(RamTranslationUnit& translationUnit) override {
        rvla = translationUnit.getAnalysis<RamExpressionLevelAnalysis>();
        rcva = translationUnit.getAnalysis<RamConstValueAnalysis>();
        return makeIndex(*translationUnit.getProgram());
    }
};

/**
 * @class IfConversionTransformer
 * @brief Convert IndexScan operations to Filter/Existence Checks

 * If there exists IndexScan operations in the RAM, and their tuples
 * are not further used in subsequent operations, the IndexScan operations
 * will be rewritten to Filter/Existence Checks.
 *
 * For example,
 *
 *  QUERY
 *   ...
 *    SEARCH t1 IN A INDEX t1.x=10 AND t1.y = 20
 *      ... // no occurrence of t1
 *
 * will be rewritten to
 *
 *  QUERY
 *   ...
 *    IF (10,20) NOT IN A
 *      ...
 *
 */
class IfConversionTransformer : public RamTransformer {
public:
    std::string getName() const override {
        return "IfConversionTransformer";
    }

    /**
     * @brief Rewrite IndexScan operations
     * @param An index operation
     * @result The old operation if the if-conversion fails; otherwise the filter/existence check
     *
     * Rewrites IndexScan operations to a filter/existence check if the IndexScan's tuple
     * is not used in a consecutive RAM operation
     */
    std::unique_ptr<RamOperation> rewriteIndexScan(const RamIndexScan* indexScan);

    /**
     * @brief Apply if-conversion to the whole program
     * @param RAM program
     * @result A flag indicating whether the RAM program has been changed.
     *
     * Search for queries and rewrite their IndexScan operations if possible.
     */
    bool convertIndexScans(RamProgram& program);

protected:
    bool transform(RamTranslationUnit& translationUnit) override {
        return convertIndexScans(*translationUnit.getProgram());
    }
};

class ChoiceConversionTransformer : public RamTransformer {
public:
    std::string getName() const override {
        return "ChoiceConversionTransformer";
    }

    std::unique_ptr<RamOperation> rewriteScan(const RamScan* scan);
    std::unique_ptr<RamOperation> rewriteIndexScan(const RamIndexScan* indexScan);

    bool convertScans(RamProgram& program);

protected:
    RamConditionLevelAnalysis* rcla{nullptr};
    bool transform(RamTranslationUnit& translationUnit) override {
        rcla = translationUnit.getAnalysis<RamConditionLevelAnalysis>();
        return convertScans(*translationUnit.getProgram());
    }
};

}  // end of namespace souffle
