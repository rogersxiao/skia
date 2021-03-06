/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_SWITCHSTATEMENT
#define SKSL_SWITCHSTATEMENT

#include "src/sksl/ir/SkSLStatement.h"
#include "src/sksl/ir/SkSLSwitchCase.h"

namespace SkSL {

class SymbolTable;

/**
 * A 'switch' statement.
 */
struct SwitchStatement : public Statement {
    static constexpr Kind kStatementKind = kSwitch_Kind;

    SwitchStatement(int offset, bool isStatic, std::unique_ptr<Expression> value,
                    std::vector<std::unique_ptr<SwitchCase>> cases,
                    const std::shared_ptr<SymbolTable> symbols)
    : INHERITED(offset, kStatementKind)
    , fIsStatic(isStatic)
    , fValue(std::move(value))
    , fSymbols(std::move(symbols))
    , fCases(std::move(cases)) {}

    int nodeCount() const override {
        int result = 1 + fValue->nodeCount();
        for (const auto& c : fCases) {
            result += c->nodeCount();
        }
        return result;
    }

    std::unique_ptr<Statement> clone() const override {
        std::vector<std::unique_ptr<SwitchCase>> cloned;
        for (const auto& s : fCases) {
            cloned.push_back(std::unique_ptr<SwitchCase>((SwitchCase*) s->clone().release()));
        }
        return std::unique_ptr<Statement>(new SwitchStatement(fOffset, fIsStatic, fValue->clone(),
                                                              std::move(cloned), fSymbols));
    }

    String description() const override {
        String result;
        if (fIsStatic) {
            result += "@";
        }
        result += String::printf("switch (%s) {\n", fValue->description().c_str());
        for (const auto& c : fCases) {
            result += c->description();
        }
        result += "}";
        return result;
    }

    bool fIsStatic;
    std::unique_ptr<Expression> fValue;
    // it's important to keep fCases defined after (and thus destroyed before) fSymbols, because
    // destroying statements can modify reference counts in symbols
    const std::shared_ptr<SymbolTable> fSymbols;
    std::vector<std::unique_ptr<SwitchCase>> fCases;

    typedef Statement INHERITED;
};

}  // namespace SkSL

#endif
