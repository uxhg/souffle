/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file BinRelationStatement.h
 *
 ***********************************************************************/

#pragma once

#include "ram/Node.h"
#include "ram/Relation.h"
#include "ram/Statement.h"
#include "ram/utility/NodeMapper.h"
#include "souffle/utility/ContainerUtil.h"
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace souffle::ram {

/**
 * @class BinRelationStatement
 * @brief Abstract class for a binary relation
 *
 * Comprises two Relations
 */
class BinRelationStatement : public Statement {
public:
    BinRelationStatement(const std::string &f, const std::string &s)
            : first(f), second(s) {
    }

    /** @brief Get first relation */
    const std::string& getFirstRelation() const {
        return first;
    }

    /** @brief Get second relation */
    const std::string& getSecondRelation() const {
        return second;
    }

protected:
    bool equal(const Node& node) const override {
        const auto& other = static_cast<const BinRelationStatement&>(node);
        return first == other.first && second == other.second;
    }

protected:
    /** first relation */ 
    std::string first;

    /** second relation */ 
    std::string second;
};

}  // namespace souffle::ram
