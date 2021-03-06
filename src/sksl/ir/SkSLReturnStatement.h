/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_RETURNSTATEMENT
#define SKSL_RETURNSTATEMENT

#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLStatement.h"

namespace SkSL {

/**
 * A 'return' statement.
 */
struct ReturnStatement : public Statement {
    static constexpr Kind kStatementKind = kReturn_Kind;

    ReturnStatement(int offset)
    : INHERITED(offset, kStatementKind) {}

    ReturnStatement(std::unique_ptr<Expression> expression)
    : INHERITED(expression->fOffset, kStatementKind)
    , fExpression(std::move(expression)) {}

    int nodeCount() const override {
        return 1 + fExpression->nodeCount();
    }

    std::unique_ptr<Statement> clone() const override {
        if (fExpression) {
            return std::unique_ptr<Statement>(new ReturnStatement(fExpression->clone()));
        }
        return std::unique_ptr<Statement>(new ReturnStatement(fOffset));
    }

    String description() const override {
        if (fExpression) {
            return "return " + fExpression->description() + ";";
        } else {
            return String("return;");
        }
    }

    std::unique_ptr<Expression> fExpression;

    typedef Statement INHERITED;
};

}  // namespace SkSL

#endif
