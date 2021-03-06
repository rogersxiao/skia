/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_CONTINUESTATEMENT
#define SKSL_CONTINUESTATEMENT

#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLStatement.h"

namespace SkSL {

/**
 * A 'continue' statement.
 */
struct ContinueStatement : public Statement {
    static constexpr Kind kStatementKind = kContinue_Kind;

    ContinueStatement(int offset)
    : INHERITED(offset, kStatementKind) {}

    int nodeCount() const override {
        return 1;
    }

    std::unique_ptr<Statement> clone() const override {
        return std::unique_ptr<Statement>(new ContinueStatement(fOffset));
    }

    String description() const override {
        return String("continue;");
    }

    typedef Statement INHERITED;
};

}  // namespace SkSL

#endif
