/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/SkSLCPPCodeGenerator.h"

#include "include/private/SkSLSampleUsage.h"
#include "src/sksl/SkSLAnalysis.h"
#include "src/sksl/SkSLCPPUniformCTypes.h"
#include "src/sksl/SkSLCompiler.h"
#include "src/sksl/SkSLHCodeGenerator.h"

#include <algorithm>

namespace SkSL {

static bool needs_uniform_var(const Variable& var) {
    return (var.fModifiers.fFlags & Modifiers::kUniform_Flag) &&
           var.fType.kind() != Type::kSampler_Kind;
}

CPPCodeGenerator::CPPCodeGenerator(const Context* context, const Program* program,
                                   ErrorReporter* errors, String name, OutputStream* out)
    : INHERITED(context, program, errors, out)
    , fName(std::move(name))
    , fFullName(String::printf("Gr%s", fName.c_str()))
    , fSectionAndParameterHelper(program, *errors) {
    fLineEnding = "\n";
    fTextureFunctionOverride = "sample";
}

void CPPCodeGenerator::writef(const char* s, va_list va) {
    static constexpr int BUFFER_SIZE = 1024;
    va_list copy;
    va_copy(copy, va);
    char buffer[BUFFER_SIZE];
    int length = std::vsnprintf(buffer, BUFFER_SIZE, s, va);
    if (length < BUFFER_SIZE) {
        fOut->write(buffer, length);
    } else {
        std::unique_ptr<char[]> heap(new char[length + 1]);
        vsprintf(heap.get(), s, copy);
        fOut->write(heap.get(), length);
    }
    va_end(copy);
}

void CPPCodeGenerator::writef(const char* s, ...) {
    va_list va;
    va_start(va, s);
    this->writef(s, va);
    va_end(va);
}

void CPPCodeGenerator::writeHeader() {
}

bool CPPCodeGenerator::usesPrecisionModifiers() const {
    return false;
}

String CPPCodeGenerator::getTypeName(const Type& type) {
    return type.name();
}

void CPPCodeGenerator::writeBinaryExpression(const BinaryExpression& b,
                                             Precedence parentPrecedence) {
    if (b.fOperator == Token::Kind::TK_PERCENT) {
        // need to use "%%" instead of "%" b/c the code will be inside of a printf
        Precedence precedence = GetBinaryPrecedence(b.fOperator);
        if (precedence >= parentPrecedence) {
            this->write("(");
        }
        this->writeExpression(*b.fLeft, precedence);
        this->write(" %% ");
        this->writeExpression(*b.fRight, precedence);
        if (precedence >= parentPrecedence) {
            this->write(")");
        }
    } else if (b.fLeft->fKind == Expression::kNullLiteral_Kind ||
               b.fRight->fKind == Expression::kNullLiteral_Kind) {
        const Variable* var;
        if (b.fLeft->fKind != Expression::kNullLiteral_Kind) {
            var = &b.fLeft->as<VariableReference>().fVariable;
        } else {
            var = &b.fRight->as<VariableReference>().fVariable;
        }
        SkASSERT(var->fType.kind() == Type::kNullable_Kind &&
                 var->fType.componentType() == *fContext.fFragmentProcessor_Type);
        this->write("%s");
        const char* op = "";
        switch (b.fOperator) {
            case Token::Kind::TK_EQEQ:
                op = "!";
                break;
            case Token::Kind::TK_NEQ:
                op = "";
                break;
            default:
                SkASSERT(false);
        }
        int childIndex = this->getChildFPIndex(*var);
        fFormatArgs.push_back(String(op) + "_outer.childProcessor(" + to_string(childIndex) +
                              ") ? \"true\" : \"false\"");
    } else {
        INHERITED::writeBinaryExpression(b, parentPrecedence);
    }
}

void CPPCodeGenerator::writeIndexExpression(const IndexExpression& i) {
    const Expression& base = *i.fBase;
    if (base.fKind == Expression::kVariableReference_Kind) {
        int builtin = ((VariableReference&) base).fVariable.fModifiers.fLayout.fBuiltin;
        if (SK_TEXTURESAMPLERS_BUILTIN == builtin) {
            this->write("%s");
            if (i.fIndex->fKind != Expression::kIntLiteral_Kind) {
                fErrors.error(i.fIndex->fOffset,
                              "index into sk_TextureSamplers must be an integer literal");
                return;
            }
            int64_t index = i.fIndex->as<IntLiteral>().fValue;
            fFormatArgs.push_back("        fragBuilder->getProgramBuilder()->samplerVariable("
                                            "args.fTexSamplers[" + to_string(index) + "])");
            return;
        }
    }
    INHERITED::writeIndexExpression(i);
}

static String default_value(const Type& type) {
    if (type.fName == "bool") {
        return "false";
    }
    switch (type.kind()) {
        case Type::kScalar_Kind: return "0";
        case Type::kVector_Kind: return type.name() + "(0)";
        case Type::kMatrix_Kind: return type.name() + "(1)";
        default: ABORT("unsupported default_value type\n");
    }
}

static String default_value(const Variable& var) {
    if (var.fModifiers.fLayout.fCType == SkSL::Layout::CType::kSkPMColor4f) {
        return "{SK_FloatNaN, SK_FloatNaN, SK_FloatNaN, SK_FloatNaN}";
    }
    return default_value(var.fType);
}

static bool is_private(const Variable& var) {
    return !(var.fModifiers.fFlags & Modifiers::kUniform_Flag) &&
           !(var.fModifiers.fFlags & Modifiers::kIn_Flag) &&
           var.fStorage == Variable::kGlobal_Storage &&
           var.fModifiers.fLayout.fBuiltin == -1;
}

static bool is_uniform_in(const Variable& var) {
    return (var.fModifiers.fFlags & Modifiers::kUniform_Flag) &&
           (var.fModifiers.fFlags & Modifiers::kIn_Flag) &&
           var.fType.kind() != Type::kSampler_Kind;
}

String CPPCodeGenerator::formatRuntimeValue(const Type& type,
                                            const Layout& layout,
                                            const String& cppCode,
                                            std::vector<String>* formatArgs) {
    if (type.isFloat()) {
        formatArgs->push_back(cppCode);
        return "%f";
    }
    if (type == *fContext.fInt_Type) {
        formatArgs->push_back(cppCode);
        return "%d";
    }
    if (type == *fContext.fBool_Type) {
        formatArgs->push_back("(" + cppCode + " ? \"true\" : \"false\")");
        return "%s";
    }
    if (type == *fContext.fFloat2_Type || type == *fContext.fHalf2_Type) {
        formatArgs->push_back(cppCode + ".fX");
        formatArgs->push_back(cppCode + ".fY");
        return type.name() + "(%f, %f)";
    }
    if (type == *fContext.fFloat3_Type || type == *fContext.fHalf3_Type) {
        formatArgs->push_back(cppCode + ".fX");
        formatArgs->push_back(cppCode + ".fY");
        formatArgs->push_back(cppCode + ".fZ");
        return type.name() + "(%f, %f, %f)";
    }
    if (type == *fContext.fFloat4_Type || type == *fContext.fHalf4_Type) {
        switch (layout.fCType) {
            case Layout::CType::kSkPMColor:
                formatArgs->push_back("SkGetPackedR32(" + cppCode + ") / 255.0");
                formatArgs->push_back("SkGetPackedG32(" + cppCode + ") / 255.0");
                formatArgs->push_back("SkGetPackedB32(" + cppCode + ") / 255.0");
                formatArgs->push_back("SkGetPackedA32(" + cppCode + ") / 255.0");
                break;
            case Layout::CType::kSkPMColor4f:
                formatArgs->push_back(cppCode + ".fR");
                formatArgs->push_back(cppCode + ".fG");
                formatArgs->push_back(cppCode + ".fB");
                formatArgs->push_back(cppCode + ".fA");
                break;
            case Layout::CType::kSkV4:
                formatArgs->push_back(cppCode + ".x");
                formatArgs->push_back(cppCode + ".y");
                formatArgs->push_back(cppCode + ".z");
                formatArgs->push_back(cppCode + ".w");
                break;
            case Layout::CType::kSkRect:
            case Layout::CType::kDefault:
                formatArgs->push_back(cppCode + ".left()");
                formatArgs->push_back(cppCode + ".top()");
                formatArgs->push_back(cppCode + ".right()");
                formatArgs->push_back(cppCode + ".bottom()");
                break;
            default:
                SkASSERT(false);
        }
        return type.name() + "(%f, %f, %f, %f)";
    }
    if (type.kind() == Type::kMatrix_Kind) {
        SkASSERT(type.componentType() == *fContext.fFloat_Type ||
                 type.componentType() == *fContext.fHalf_Type);

        String format = type.name() + "(";
        for (int c = 0; c < type.columns(); ++c) {
            for (int r = 0; r < type.rows(); ++r) {
                formatArgs->push_back(String::printf("%s.rc(%d, %d)", cppCode.c_str(), r, c));
                format += "%f, ";
            }
        }

        // Replace trailing ", " with ")".
        format.pop_back();
        format.back() = ')';
        return format;
    }
    if (type.kind() == Type::kEnum_Kind) {
        formatArgs->push_back("(int) " + cppCode);
        return "%d";
    }
    if (type == *fContext.fInt4_Type ||
        type == *fContext.fShort4_Type ||
        type == *fContext.fByte4_Type) {
        formatArgs->push_back(cppCode + ".left()");
        formatArgs->push_back(cppCode + ".top()");
        formatArgs->push_back(cppCode + ".right()");
        formatArgs->push_back(cppCode + ".bottom()");
        return type.name() + "(%d, %d, %d, %d)";
    }

    SkDEBUGFAILF("unsupported runtime value type '%s'\n", String(type.fName).c_str());
    return "";
}

void CPPCodeGenerator::writeRuntimeValue(const Type& type, const Layout& layout,
                                         const String& cppCode) {
    this->write(this->formatRuntimeValue(type, layout, cppCode, &fFormatArgs));
}

void CPPCodeGenerator::writeVarInitializer(const Variable& var, const Expression& value) {
    if (is_private(var)) {
        this->writeRuntimeValue(var.fType, var.fModifiers.fLayout, var.fName);
    } else {
        this->writeExpression(value, kTopLevel_Precedence);
    }
}

String CPPCodeGenerator::getSamplerHandle(const Variable& var) {
    int samplerCount = 0;
    for (const auto param : fSectionAndParameterHelper.getParameters()) {
        if (&var == param) {
            return "args.fTexSamplers[" + to_string(samplerCount) + "]";
        }
        if (param->fType.kind() == Type::kSampler_Kind) {
            ++samplerCount;
        }
    }
    ABORT("should have found sampler in parameters\n");
}

void CPPCodeGenerator::writeIntLiteral(const IntLiteral& i) {
    this->write(to_string((int32_t) i.fValue));
}

void CPPCodeGenerator::writeSwizzle(const Swizzle& swizzle) {
    if (fCPPMode) {
        SkASSERT(swizzle.fComponents.size() == 1); // no support for multiple swizzle components yet
        this->writeExpression(*swizzle.fBase, kPostfix_Precedence);
        switch (swizzle.fComponents[0]) {
            case 0: this->write(".left()");   break;
            case 1: this->write(".top()");    break;
            case 2: this->write(".right()");  break;
            case 3: this->write(".bottom()"); break;
        }
    } else {
        INHERITED::writeSwizzle(swizzle);
    }
}

void CPPCodeGenerator::writeVariableReference(const VariableReference& ref) {
    if (fCPPMode) {
        this->write(ref.fVariable.fName);
        return;
    }
    switch (ref.fVariable.fModifiers.fLayout.fBuiltin) {
        case SK_OUTCOLOR_BUILTIN:
            this->write("%s");
            fFormatArgs.push_back(String("args.fOutputColor"));
            break;
        case SK_MAIN_COORDS_BUILTIN:
            this->write("%s");
            fFormatArgs.push_back(String("args.fSampleCoord"));
            fAccessSampleCoordsDirectly = true;
            break;
        case SK_WIDTH_BUILTIN:
            this->write("sk_Width");
            break;
        case SK_HEIGHT_BUILTIN:
            this->write("sk_Height");
            break;
        default:
            if (ref.fVariable.fType.kind() == Type::kSampler_Kind) {
                this->write("%s");
                fFormatArgs.push_back("fragBuilder->getProgramBuilder()->samplerVariable(" +
                                      this->getSamplerHandle(ref.fVariable) + ")");
                return;
            }
            if (ref.fVariable.fModifiers.fFlags & Modifiers::kUniform_Flag) {
                this->write("%s");
                String name = ref.fVariable.fName;
                String var = String::printf("args.fUniformHandler->getUniformCStr(%sVar)",
                                            HCodeGenerator::FieldName(name.c_str()).c_str());
                String code;
                if (ref.fVariable.fModifiers.fLayout.fWhen.fLength) {
                    code = String::printf("%sVar.isValid() ? %s : \"%s\"",
                                          HCodeGenerator::FieldName(name.c_str()).c_str(),
                                          var.c_str(),
                                          default_value(ref.fVariable.fType).c_str());
                } else {
                    code = var;
                }
                fFormatArgs.push_back(code);
            } else if (SectionAndParameterHelper::IsParameter(ref.fVariable)) {
                String name(ref.fVariable.fName);
                this->writeRuntimeValue(ref.fVariable.fType, ref.fVariable.fModifiers.fLayout,
                                        String::printf("_outer.%s", name.c_str()).c_str());
            } else {
                this->write(ref.fVariable.fName);
            }
    }
}

void CPPCodeGenerator::writeIfStatement(const IfStatement& s) {
    if (s.fIsStatic) {
        this->write("@");
    }
    INHERITED::writeIfStatement(s);
}

void CPPCodeGenerator::writeReturnStatement(const ReturnStatement& s) {
    if (fInMain) {
        fErrors.error(s.fOffset, "fragmentProcessor main() may not contain return statements");
    }
    INHERITED::writeReturnStatement(s);
}

void CPPCodeGenerator::writeSwitchStatement(const SwitchStatement& s) {
    if (s.fIsStatic) {
        this->write("@");
    }
    INHERITED::writeSwitchStatement(s);
}

void CPPCodeGenerator::writeFieldAccess(const FieldAccess& access) {
    if (access.fBase->fType.name() == "fragmentProcessor") {
        // Special field access on fragment processors are converted into function calls on
        // GrFragmentProcessor's getters.
        if (access.fBase->fKind != Expression::kVariableReference_Kind) {
            fErrors.error(access.fBase->fOffset, "fragmentProcessor must be a reference\n");
            return;
        }

        const Type::Field& field = fContext.fFragmentProcessor_Type->fields()[access.fFieldIndex];
        const Variable& var = ((const VariableReference&) *access.fBase).fVariable;
        String cppAccess = String::printf("_outer.childProcessor(%d)->%s()",
                                          this->getChildFPIndex(var),
                                          String(field.fName).c_str());

        if (fCPPMode) {
            this->write(cppAccess.c_str());
        } else {
            writeRuntimeValue(*field.fType, Layout(), cppAccess);
        }
        return;
    }
    INHERITED::writeFieldAccess(access);
}

int CPPCodeGenerator::getChildFPIndex(const Variable& var) const {
    int index = 0;
    bool found = false;
    for (const auto& p : fProgram) {
        if (ProgramElement::kVar_Kind == p.fKind) {
            const VarDeclarations& decls = (const VarDeclarations&) p;
            for (const auto& raw : decls.fVars) {
                const VarDeclaration& decl = (VarDeclaration&) *raw;
                if (decl.fVar == &var) {
                    found = true;
                } else if (decl.fVar->fType.nonnullable() == *fContext.fFragmentProcessor_Type) {
                    ++index;
                }
            }
        }
        if (found) {
            break;
        }
    }
    SkASSERT(found);
    return index;
}

void CPPCodeGenerator::writeFunctionCall(const FunctionCall& c) {
    if (c.fFunction.fBuiltin && c.fFunction.fName == "sample" &&
        c.fArguments[0]->fType.kind() != Type::Kind::kSampler_Kind) {
        // Validity checks that are detected by function definition in sksl_fp.inc
        SkASSERT(c.fArguments.size() >= 1 && c.fArguments.size() <= 3);
        SkASSERT("fragmentProcessor"  == c.fArguments[0]->fType.name() ||
                 "fragmentProcessor?" == c.fArguments[0]->fType.name());

        // Actually fail during compilation if arguments with valid types are
        // provided that are not variable references, since sample() is a
        // special function that impacts code emission.
        if (c.fArguments[0]->fKind != Expression::kVariableReference_Kind) {
            fErrors.error(c.fArguments[0]->fOffset,
                    "sample()'s fragmentProcessor argument must be a variable reference\n");
            return;
        }
        const Variable& child = ((const VariableReference&) *c.fArguments[0]).fVariable;

        // Start a new extra emit code section so that the emitted child processor can depend on
        // sksl variables defined in earlier sksl code.
        this->newExtraEmitCodeBlock();

        String inputColor;
        if (c.fArguments.size() > 1 && c.fArguments[1]->fType.name() == "half4") {
            // Use the invokeChild() variant that accepts an input color, so convert the 2nd
            // argument's expression into C++ code that produces sksl stored in an SkString.
            String inputColorName = "_input" + to_string(c.fOffset);
            addExtraEmitCodeLine(convertSKSLExpressionToCPP(*c.fArguments[1], inputColorName));

            // invokeChild() needs a char* and a pre-pended comma
            inputColor = ", " + inputColorName + ".c_str()";
        }

        String inputCoord;
        String invokeFunction = "invokeChild";
        if (c.fArguments.back()->fType.name() == "float2") {
            // Invoking child with explicit coordinates at this call site
            inputCoord = "_coords" + to_string(c.fOffset);
            addExtraEmitCodeLine(convertSKSLExpressionToCPP(*c.fArguments.back(), inputCoord));
            inputCoord.append(".c_str()");
        } else if (c.fArguments.back()->fType.name() == "float3x3") {
            // Invoking child with a matrix, sampling relative to the input coords.
            invokeFunction = "invokeChildWithMatrix";
            SampleUsage usage = Analysis::GetSampleUsage(fProgram, child);

            if (!usage.hasUniformMatrix()) {
                inputCoord = "_matrix" + to_string(c.fOffset);
                addExtraEmitCodeLine(convertSKSLExpressionToCPP(*c.fArguments.back(), inputCoord));
                inputCoord.append(".c_str()");
            }
            // else pass in the empty string to rely on invokeChildWithMatrix's automatic uniform
            // resolution
        }
        if (!inputCoord.empty()) {
            inputCoord = ", " + inputCoord;
        }

        // Write the output handling after the possible input handling
        String childName = "_sample" + to_string(c.fOffset);
        String childIndexStr = to_string(this->getChildFPIndex(child));
        addExtraEmitCodeLine("SkString " + childName + " = this->" + invokeFunction + "(" +
                             childIndexStr + inputColor + ", args" + inputCoord + ");");

        this->write("%s");
        fFormatArgs.push_back(childName + ".c_str()");
        return;
    }
    if (c.fFunction.fBuiltin) {
        INHERITED::writeFunctionCall(c);
    } else {
        this->write("%s");
        fFormatArgs.push_back((String(c.fFunction.fName) + "_name.c_str()").c_str());
        this->write("(");
        const char* separator = "";
        for (const auto& arg : c.fArguments) {
            this->write(separator);
            separator = ", ";
            this->writeExpression(*arg, kSequence_Precedence);
        }
        this->write(")");
    }
    if (c.fFunction.fBuiltin && c.fFunction.fName == "sample") {
        this->write(".%s");
        SkASSERT(c.fArguments.size() >= 1);
        SkASSERT(c.fArguments[0]->fKind == Expression::kVariableReference_Kind);
        String sampler = this->getSamplerHandle(((VariableReference&) *c.fArguments[0]).fVariable);
        fFormatArgs.push_back("fragBuilder->getProgramBuilder()->samplerSwizzle(" + sampler +
                              ").asString().c_str()");
    }
}

static const char* glsltype_string(const Context& context, const Type& type) {
    if (type == *context.fFloat_Type) {
        return "kFloat_GrSLType";
    } else if (type == *context.fHalf_Type) {
        return "kHalf_GrSLType";
    } else if (type == *context.fInt_Type) {
        return "kInt_GrSLType";
    } else if (type == *context.fFloat2_Type) {
        return "kFloat2_GrSLType";
    } else if (type == *context.fHalf2_Type) {
        return "kHalf2_GrSLType";
    } else if (type == *context.fInt2_Type) {
        return "kInt2_GrSLType";
    } else if (type == *context.fFloat3_Type) {
        return "kFloat3_GrSLType";
    } else if (type == *context.fHalf3_Type) {
        return "kHalf3_GrSLType";
    } else if (type == *context.fInt3_Type) {
        return "kInt3_GrSLType";
    } else if (type == *context.fFloat4_Type) {
        return "kFloat4_GrSLType";
    } else if (type == *context.fHalf4_Type) {
        return "kHalf4_GrSLType";
    } else if (type == *context.fInt4_Type) {
        return "kInt4_GrSLType";
    } else if (type == *context.fFloat2x2_Type) {
        return "kFloat2x2_GrSLType";
    } else if (type == *context.fHalf2x2_Type) {
        return "kHalf2x2_GrSLType";
    } else if (type == *context.fFloat2x3_Type) {
        return "kFloat2x3_GrSLType";
    } else if (type == *context.fHalf2x3_Type) {
        return "kHalf2x3_GrSLType";
    } else if (type == *context.fFloat2x4_Type) {
        return "kFloat2x4_GrSLType";
    } else if (type == *context.fHalf2x4_Type) {
        return "kHalf2x4_GrSLType";
    } else if (type == *context.fFloat3x2_Type) {
        return "kFloat3x2_GrSLType";
    } else if (type == *context.fHalf3x2_Type) {
        return "kHalf3x2_GrSLType";
    } else if (type == *context.fFloat3x3_Type) {
        return "kFloat3x3_GrSLType";
    } else if (type == *context.fHalf3x3_Type) {
        return "kHalf3x3_GrSLType";
    } else if (type == *context.fFloat3x4_Type) {
        return "kFloat3x4_GrSLType";
    } else if (type == *context.fHalf3x4_Type) {
        return "kHalf3x4_GrSLType";
    } else if (type == *context.fFloat4x2_Type) {
        return "kFloat4x2_GrSLType";
    } else if (type == *context.fHalf4x2_Type) {
        return "kHalf4x2_GrSLType";
    } else if (type == *context.fFloat4x3_Type) {
        return "kFloat4x3_GrSLType";
    } else if (type == *context.fHalf4x3_Type) {
        return "kHalf4x3_GrSLType";
    } else if (type == *context.fFloat4x4_Type) {
        return "kFloat4x4_GrSLType";
    } else if (type == *context.fHalf4x4_Type) {
        return "kHalf4x4_GrSLType";
    } else if (type == *context.fVoid_Type) {
        return "kVoid_GrSLType";
    } else if (type.kind() == Type::kEnum_Kind) {
        return "int";
    }
    SkASSERT(false);
    return nullptr;
}

void CPPCodeGenerator::writeFunction(const FunctionDefinition& f) {
    const FunctionDeclaration& decl = f.fDeclaration;
    if (decl.fBuiltin) {
        return;
    }
    fFunctionHeader = "";
    OutputStream* oldOut = fOut;
    StringStream buffer;
    fOut = &buffer;
    if (decl.fName == "main") {
        fInMain = true;
        for (const auto& s : ((Block&) *f.fBody).fStatements) {
            this->writeStatement(*s);
            this->writeLine();
        }
        fInMain = false;

        fOut = oldOut;
        this->write(fFunctionHeader);
        this->write(buffer.str());
    } else {
        this->addExtraEmitCodeLine("SkString " + decl.fName + "_name;");
        String args = "const GrShaderVar " + decl.fName + "_args[] = { ";
        const char* separator = "";
        for (const auto& param : decl.fParameters) {
            args += String(separator) + "GrShaderVar(\"" + param->fName + "\", " +
                    glsltype_string(fContext, param->fType) + ")";
            separator = ", ";
        }
        args += "};";
        this->addExtraEmitCodeLine(args.c_str());
        for (const auto& s : ((Block&) *f.fBody).fStatements) {
            this->writeStatement(*s);
            this->writeLine();
        }

        fOut = oldOut;
        String emit = "fragBuilder->emitFunction(";
        emit += glsltype_string(fContext, decl.fReturnType);
        emit += ", \"" + decl.fName + "\"";
        emit += ", " + to_string((int64_t) decl.fParameters.size());
        emit += ", " + decl.fName + "_args";
        emit += ",\nR\"SkSL(" + buffer.str() + ")SkSL\"";
        emit += ", &" + decl.fName + "_name);";
        this->addExtraEmitCodeLine(emit.c_str());
    }
}

void CPPCodeGenerator::writeSetting(const Setting& s) {
    this->write(s.fName.c_str());
}

bool CPPCodeGenerator::writeSection(const char* name, const char* prefix) {
    const Section* s = fSectionAndParameterHelper.getSection(name);
    if (s) {
        this->writef("%s%s", prefix, s->fText.c_str());
        return true;
    }
    return false;
}

void CPPCodeGenerator::writeProgramElement(const ProgramElement& p) {
    if (p.fKind == ProgramElement::kSection_Kind) {
        return;
    }
    if (p.fKind == ProgramElement::kVar_Kind) {
        const VarDeclarations& decls = (const VarDeclarations&) p;
        if (!decls.fVars.size()) {
            return;
        }
        const Variable& var = *((VarDeclaration&) *decls.fVars[0]).fVar;
        if (var.fModifiers.fFlags & (Modifiers::kIn_Flag | Modifiers::kUniform_Flag) ||
            -1 != var.fModifiers.fLayout.fBuiltin) {
            return;
        }
    }
    INHERITED::writeProgramElement(p);
}

void CPPCodeGenerator::addUniform(const Variable& var) {
    if (!needs_uniform_var(var)) {
        return;
    }
    if (var.fModifiers.fLayout.fWhen.fLength) {
        this->writef("        if (%s) {\n    ", String(var.fModifiers.fLayout.fWhen).c_str());
    }
    const char* type = glsltype_string(fContext, var.fType);
    String name(var.fName);
    this->writef("        %sVar = args.fUniformHandler->addUniform(&_outer, kFragment_GrShaderFlag,"
                 " %s, \"%s\");\n", HCodeGenerator::FieldName(name.c_str()).c_str(), type,
                 name.c_str());
    if (var.fModifiers.fLayout.fWhen.fLength) {
        this->write("        }\n");
    }
}

void CPPCodeGenerator::writeInputVars() {
}

void CPPCodeGenerator::writePrivateVars() {
    for (const auto& p : fProgram) {
        if (ProgramElement::kVar_Kind == p.fKind) {
            const VarDeclarations& decls = (const VarDeclarations&) p;
            for (const auto& raw : decls.fVars) {
                VarDeclaration& decl = (VarDeclaration&) *raw;
                if (is_private(*decl.fVar)) {
                    if (decl.fVar->fType == *fContext.fFragmentProcessor_Type) {
                        fErrors.error(decl.fOffset,
                                      "fragmentProcessor variables must be declared 'in'");
                        return;
                    }
                    this->writef("%s %s = %s;\n",
                                 HCodeGenerator::FieldType(fContext, decl.fVar->fType,
                                                           decl.fVar->fModifiers.fLayout).c_str(),
                                 String(decl.fVar->fName).c_str(),
                                 default_value(*decl.fVar).c_str());
                } else if (decl.fVar->fModifiers.fLayout.fFlags & Layout::kTracked_Flag) {
                    // An auto-tracked uniform in variable, so add a field to hold onto the prior
                    // state. Note that tracked variables must be uniform in's and that is validated
                    // before writePrivateVars() is called.
                    const UniformCTypeMapper* mapper = UniformCTypeMapper::Get(fContext, *decl.fVar);
                    SkASSERT(mapper && mapper->supportsTracking());

                    String name = HCodeGenerator::FieldName(String(decl.fVar->fName).c_str());
                    // The member statement is different if the mapper reports a default value
                    if (mapper->defaultValue().size() > 0) {
                        this->writef("%s %sPrev = %s;\n",
                                     Layout::CTypeToStr(mapper->ctype()), name.c_str(),
                                     mapper->defaultValue().c_str());
                    } else {
                        this->writef("%s %sPrev;\n",
                                     Layout::CTypeToStr(mapper->ctype()), name.c_str());
                    }
                }
            }
        }
    }
}

void CPPCodeGenerator::writePrivateVarValues() {
    for (const auto& p : fProgram) {
        if (ProgramElement::kVar_Kind == p.fKind) {
            const VarDeclarations& decls = (const VarDeclarations&) p;
            for (const auto& raw : decls.fVars) {
                VarDeclaration& decl = (VarDeclaration&) *raw;
                if (is_private(*decl.fVar) && decl.fValue) {
                    this->writef("%s = ", String(decl.fVar->fName).c_str());
                    fCPPMode = true;
                    this->writeExpression(*decl.fValue, kAssignment_Precedence);
                    fCPPMode = false;
                    this->write(";\n");
                }
            }
        }
    }
}

static bool is_accessible(const Variable& var) {
    const Type& type = var.fType.nonnullable();
    return Type::kSampler_Kind != type.kind() &&
           Type::kOther_Kind != type.kind();
}

void CPPCodeGenerator::newExtraEmitCodeBlock() {
    // This should only be called when emitting SKSL for emitCode(), which can be detected if the
    // cpp buffer is not null, and the cpp buffer is not the current output.
    SkASSERT(fCPPBuffer && fCPPBuffer != fOut);

    // Start a new block as an empty string
    fExtraEmitCodeBlocks.push_back("");
    // Mark its location in the output buffer, uses ${\d} for the token since ${} will not occur in
    // valid sksl and makes detection trivial.
    this->writef("${%zu}", fExtraEmitCodeBlocks.size() - 1);
}

void CPPCodeGenerator::addExtraEmitCodeLine(const String& toAppend) {
    SkASSERT(fExtraEmitCodeBlocks.size() > 0);
    String& currentBlock = fExtraEmitCodeBlocks[fExtraEmitCodeBlocks.size() - 1];
    // Automatically add indentation and newline
    currentBlock += "        " + toAppend + "\n";
}

void CPPCodeGenerator::flushEmittedCode() {
    if (fCPPBuffer == nullptr) {
        // Not actually within writeEmitCode() so nothing to flush
        return;
    }

    StringStream* skslBuffer = static_cast<StringStream*>(fOut);

    String sksl = skslBuffer->str();
    // Empty the accumulation buffer since its current contents are consumed.
    skslBuffer->reset();

    // Switch to the cpp buffer
    fOut = fCPPBuffer;

    // Iterate through the sksl, keeping track of where the last statement ended (e.g. the latest
    // encountered ';', '{', or '}'). If an extra emit code block token is encountered then the
    // code from 0 to last statement end is sent to writeCodeAppend, the extra code block is
    // appended to the cpp buffer, and then the sksl string is trimmed to start where the last
    // statement left off (minus the encountered token).
    size_t i = 0;
    int flushPoint = -1;
    int tokenStart = -1;
    while (i < sksl.size()) {
        if (tokenStart >= 0) {
            // Looking for the end of the token
            if (sksl[i] == '}') {
                // Must append the sksl from 0 to flushPoint (inclusive) then the extra code
                // accumulated in the block with index parsed from chars [tokenStart+2, i-1]
                String toFlush = String(sksl.c_str(), flushPoint + 1);
                // writeCodeAppend automatically removes the format args that it consumed, so
                // fFormatArgs will be in a valid state for any future sksl
                this->writeCodeAppend(toFlush);

                int codeBlock = stoi(String(sksl.c_str() + tokenStart + 2, i - tokenStart - 2));
                SkASSERT(codeBlock < (int) fExtraEmitCodeBlocks.size());
                if (fExtraEmitCodeBlocks[codeBlock].size() > 0) {
                    this->write(fExtraEmitCodeBlocks[codeBlock].c_str());
                }

                // Now reset the sksl buffer to start after the flush point, but remove the token.
                String compacted = String(sksl.c_str() + flushPoint + 1,
                                          tokenStart - flushPoint - 1);
                if (i < sksl.size() - 1) {
                    compacted += String(sksl.c_str() + i + 1, sksl.size() - i - 1);
                }
                sksl = compacted;

                // And reset iteration
                i = -1;
                flushPoint = -1;
                tokenStart = -1;
            }
        } else {
            // Looking for the start of extra emit block tokens, and tracking when statements end
            if (sksl[i] == ';' || sksl[i] == '{' || sksl[i] == '}') {
                flushPoint = i;
            } else if (i < sksl.size() - 1 && sksl[i] == '$' && sksl[i + 1] == '{') {
                // found an extra emit code block token
                tokenStart = i++;
            }
        }
        i++;
    }

    // Once we've gone through the sksl string to this point, there are no remaining extra emit
    // code blocks to interleave, so append the remainder as usual.
    this->writeCodeAppend(sksl);

    // After appending, switch back to the emptied sksl buffer and reset the extra code blocks
    fOut = skslBuffer;
    fExtraEmitCodeBlocks.clear();
}

void CPPCodeGenerator::writeCodeAppend(const String& code) {
    if (!code.empty()) {
        // Count % format specifiers.
        size_t argCount = 0;
        for (size_t index = 0; index < code.size(); ++index) {
            if ('%' == code[index]) {
                if (index == code.size() - 1) {
                    break;
                }
                if (code[index + 1] != '%') {
                    ++argCount;
                }
            }
        }

        // Emit the code string.
        this->writef("        fragBuilder->codeAppendf(\n"
                     "R\"SkSL(%s)SkSL\"\n", code.c_str());
        for (size_t i = 0; i < argCount; ++i) {
            this->writef(", %s", fFormatArgs[i].c_str());
        }
        this->write(");\n");

        // argCount is equal to the number of fFormatArgs that were consumed, so they should be
        // removed from the list.
        if (argCount > 0) {
            fFormatArgs.erase(fFormatArgs.begin(), fFormatArgs.begin() + argCount);
        }
    }
}

String CPPCodeGenerator::convertSKSLExpressionToCPP(const Expression& e,
                                                    const String& cppVar) {
    // To do this conversion, we temporarily switch the sksl output stream
    // to an empty stringstream and reset the format args to empty.
    OutputStream* oldSKSL = fOut;
    StringStream exprBuffer;
    fOut = &exprBuffer;

    std::vector<String> oldArgs(fFormatArgs);
    fFormatArgs.clear();

    // Convert the argument expression into a format string and args
    this->writeExpression(e, Precedence::kTopLevel_Precedence);
    std::vector<String> newArgs(fFormatArgs);
    String expr = exprBuffer.str();

    // After generating, restore the original output stream and format args
    fFormatArgs = oldArgs;
    fOut = oldSKSL;

    // The sksl written to exprBuffer is not processed by flushEmittedCode(), so any extra emit code
    // block tokens won't get handled. So we need to strip them from the expression and stick them
    // to the end of the original sksl stream.
    String exprFormat = "";
    int tokenStart = -1;
    for (size_t i = 0; i < expr.size(); i++) {
        if (tokenStart >= 0) {
            if (expr[i] == '}') {
                // End of the token, so append the token to fOut
                fOut->write(expr.c_str() + tokenStart, i - tokenStart + 1);
                tokenStart = -1;
            }
        } else {
            if (i < expr.size() - 1 && expr[i] == '$' && expr[i + 1] == '{') {
                tokenStart = i++;
            } else {
                exprFormat += expr[i];
            }
        }
    }

    // Now build the final C++ code snippet from the format string and args
    String cppExpr;
    if (newArgs.empty()) {
        // This was a static expression, so we can simplify the input
        // color declaration in the emitted code to just a static string
        cppExpr = "SkString " + cppVar + "(\"" + exprFormat + "\");";
    } else if (newArgs.size() == 1 && exprFormat == "%s") {
        // If the format expression is simply "%s", we can avoid an expensive call to printf.
        // This happens fairly often in codegen so it is worth simplifying.
        cppExpr = "SkString " + cppVar + "(" + newArgs[0] + ");";
    } else {
        // String formatting must occur dynamically, so have the C++ declaration
        // use SkStringPrintf with the format args that were accumulated
        // when the expression was written.
        cppExpr = "SkString " + cppVar + " = SkStringPrintf(\"" + exprFormat + "\"";
        for (size_t i = 0; i < newArgs.size(); i++) {
            cppExpr += ", " + newArgs[i];
        }
        cppExpr += ");";
    }
    return cppExpr;
}

bool CPPCodeGenerator::writeEmitCode(std::vector<const Variable*>& uniforms) {
    this->write("    void emitCode(EmitArgs& args) override {\n"
                "        GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;\n");
    this->writef("        const %s& _outer = args.fFp.cast<%s>();\n"
                 "        (void) _outer;\n",
                 fFullName.c_str(), fFullName.c_str());
    for (const auto& p : fProgram) {
        if (ProgramElement::kVar_Kind == p.fKind) {
            const VarDeclarations& decls = (const VarDeclarations&) p;
            for (const auto& raw : decls.fVars) {
                VarDeclaration& decl = (VarDeclaration&) *raw;
                String nameString(decl.fVar->fName);
                const char* name = nameString.c_str();
                if (SectionAndParameterHelper::IsParameter(*decl.fVar) &&
                    is_accessible(*decl.fVar)) {
                    this->writef("        auto %s = _outer.%s;\n"
                                 "        (void) %s;\n",
                                 name, name, name);
                }
            }
        }
    }
    this->writePrivateVarValues();
    for (const auto u : uniforms) {
        this->addUniform(*u);
    }
    this->writeSection(kEmitCodeSection);

    // Save original buffer as the CPP buffer for flushEmittedCode()
    fCPPBuffer = fOut;
    StringStream skslBuffer;
    fOut = &skslBuffer;

    this->newExtraEmitCodeBlock();
    bool result = INHERITED::generateCode();
    this->flushEmittedCode();

    // Then restore the original CPP buffer and close the function
    fOut = fCPPBuffer;
    fCPPBuffer = nullptr;
    this->write("    }\n");
    return result;
}

void CPPCodeGenerator::writeSetData(std::vector<const Variable*>& uniforms) {
    const char* fullName = fFullName.c_str();
    const Section* section = fSectionAndParameterHelper.getSection(kSetDataSection);
    const char* pdman = section ? section->fArgument.c_str() : "pdman";
    this->writef("    void onSetData(const GrGLSLProgramDataManager& %s, "
                                    "const GrFragmentProcessor& _proc) override {\n",
                 pdman);
    bool wroteProcessor = false;
    for (const Variable* u : uniforms) {
        if (is_uniform_in(*u)) {
            if (!wroteProcessor) {
                this->writef("        const %s& _outer = _proc.cast<%s>();\n", fullName, fullName);
                wroteProcessor = true;
                this->writef("        {\n");
            }

            const UniformCTypeMapper* mapper = UniformCTypeMapper::Get(fContext, *u);
            SkASSERT(mapper);

            String nameString(u->fName);
            const char* name = nameString.c_str();

            // Switches for setData behavior in the generated code
            bool conditionalUniform = u->fModifiers.fLayout.fWhen != "";
            bool isTracked = u->fModifiers.fLayout.fFlags & Layout::kTracked_Flag;
            bool needsValueDeclaration = isTracked || !mapper->canInlineUniformValue();

            String uniformName = HCodeGenerator::FieldName(name) + "Var";

            String indent = "        "; // 8 by default, 12 when nested for conditional uniforms
            if (conditionalUniform) {
                // Add a pre-check to make sure the uniform was emitted
                // before trying to send any data to the GPU
                this->writef("        if (%s.isValid()) {\n", uniformName.c_str());
                indent += "    ";
            }

            String valueVar = "";
            if (needsValueDeclaration) {
                valueVar.appendf("%sValue", name);
                // Use AccessType since that will match the return type of _outer's public API.
                String valueType = HCodeGenerator::AccessType(fContext, u->fType,
                                                              u->fModifiers.fLayout);
                this->writef("%s%s %s = _outer.%s;\n",
                             indent.c_str(), valueType.c_str(), valueVar.c_str(), name);
            } else {
                // Not tracked and the mapper only needs to use the value once
                // so send it a safe expression instead of the variable name
                valueVar.appendf("(_outer.%s)", name);
            }

            if (isTracked) {
                SkASSERT(mapper->supportsTracking());

                String prevVar = HCodeGenerator::FieldName(name) + "Prev";
                this->writef("%sif (%s) {\n"
                             "%s    %s;\n"
                             "%s    %s;\n"
                             "%s}\n", indent.c_str(),
                        mapper->dirtyExpression(valueVar, prevVar).c_str(), indent.c_str(),
                        mapper->saveState(valueVar, prevVar).c_str(), indent.c_str(),
                        mapper->setUniform(pdman, uniformName, valueVar).c_str(), indent.c_str());
            } else {
                this->writef("%s%s;\n", indent.c_str(),
                        mapper->setUniform(pdman, uniformName, valueVar).c_str());
            }

            if (conditionalUniform) {
                // Close the earlier precheck block
                this->writef("        }\n");
            }
        }
    }
    if (wroteProcessor) {
        this->writef("        }\n");
    }
    if (section) {
        int samplerIndex = 0;
        for (const auto& p : fProgram) {
            if (ProgramElement::kVar_Kind == p.fKind) {
                const VarDeclarations& decls = (const VarDeclarations&) p;
                for (const std::unique_ptr<Statement>& raw : decls.fVars) {
                    const VarDeclaration& decl = static_cast<VarDeclaration&>(*raw);
                    const Variable& variable = *decl.fVar;
                    String nameString(variable.fName);
                    const char* name = nameString.c_str();
                    if (variable.fType.kind() == Type::kSampler_Kind) {
                        this->writef("        const GrSurfaceProxyView& %sView = "
                                     "_outer.textureSampler(%d).view();\n",
                                     name, samplerIndex);
                        this->writef("        GrTexture& %s = *%sView.proxy()->peekTexture();\n",
                                     name, name);
                        this->writef("        (void) %s;\n", name);
                        ++samplerIndex;
                    } else if (needs_uniform_var(variable)) {
                        this->writef("        UniformHandle& %s = %sVar;\n"
                                     "        (void) %s;\n",
                                     name, HCodeGenerator::FieldName(name).c_str(), name);
                    } else if (SectionAndParameterHelper::IsParameter(variable) &&
                               variable.fType != *fContext.fFragmentProcessor_Type) {
                        if (!wroteProcessor) {
                            this->writef("        const %s& _outer = _proc.cast<%s>();\n", fullName,
                                         fullName);
                            wroteProcessor = true;
                        }

                        if (variable.fType.nonnullable() != *fContext.fFragmentProcessor_Type) {
                            this->writef("        auto %s = _outer.%s;\n"
                                         "        (void) %s;\n",
                                         name, name, name);
                        }
                    }
                }
            }
        }
        this->writeSection(kSetDataSection);
    }
    this->write("    }\n");
}

void CPPCodeGenerator::writeOnTextureSampler() {
    bool foundSampler = false;
    for (const auto& param : fSectionAndParameterHelper.getParameters()) {
        if (param->fType.kind() == Type::kSampler_Kind) {
            if (!foundSampler) {
                this->writef(
                        "const GrFragmentProcessor::TextureSampler& %s::onTextureSampler(int "
                        "index) const {\n",
                        fFullName.c_str());
                this->writef("    return IthTextureSampler(index, %s",
                             HCodeGenerator::FieldName(String(param->fName).c_str()).c_str());
                foundSampler = true;
            } else {
                this->writef(", %s",
                             HCodeGenerator::FieldName(String(param->fName).c_str()).c_str());
            }
        }
    }
    if (foundSampler) {
        this->write(");\n}\n");
    }
}

void CPPCodeGenerator::writeClone() {
    if (!this->writeSection(kCloneSection)) {
        if (fSectionAndParameterHelper.getSection(kFieldsSection)) {
            fErrors.error(/*offset=*/0, "fragment processors with custom @fields must also have a "
                                        "custom @clone");
        }
        this->writef("%s::%s(const %s& src)\n"
                     ": INHERITED(k%s_ClassID, src.optimizationFlags())", fFullName.c_str(),
                     fFullName.c_str(), fFullName.c_str(), fFullName.c_str());
        for (const Variable* param : fSectionAndParameterHelper.getParameters()) {
            String fieldName = HCodeGenerator::FieldName(String(param->fName).c_str());
            if (param->fType.nonnullable() != *fContext.fFragmentProcessor_Type) {
                this->writef("\n, %s(src.%s)",
                             fieldName.c_str(),
                             fieldName.c_str());
            }
        }
        this->writef(" {\n");
        this->writef("        this->cloneAndRegisterAllChildProcessors(src);\n");
        int samplerCount = 0;
        for (const auto& param : fSectionAndParameterHelper.getParameters()) {
            if (param->fType.kind() == Type::kSampler_Kind) {
                ++samplerCount;
            }
        }
        if (samplerCount) {
            this->writef("     this->setTextureSamplerCnt(%d);", samplerCount);
        }
        if (fAccessSampleCoordsDirectly) {
            this->writef("    this->setUsesSampleCoordsDirectly();\n");
        }
        this->write("}\n");
        this->writef("std::unique_ptr<GrFragmentProcessor> %s::clone() const {\n",
                     fFullName.c_str());
        this->writef("    return std::make_unique<%s>(*this);\n",
                     fFullName.c_str());
        this->write("}\n");
    }
}

void CPPCodeGenerator::writeDumpInfo() {
    this->writef("#if GR_TEST_UTILS\n"
                 "SkString %s::onDumpInfo() const {\n", fFullName.c_str());

    if (!this->writeSection(kDumpInfoSection)) {
        if (fSectionAndParameterHelper.getSection(kFieldsSection)) {
            fErrors.error(/*offset=*/0, "fragment processors with custom @fields must also have a "
                                        "custom @dumpInfo");
        }

        String formatString;
        std::vector<String> argumentList;

        for (const Variable* param : fSectionAndParameterHelper.getParameters()) {
            // dumpInfo() doesn't need to log child FPs.
            if (param->fType.nonnullable() == *fContext.fFragmentProcessor_Type) {
                continue;
            }

            // Add this field onto the format string and argument list.
            String fieldName = HCodeGenerator::FieldName(String(param->fName).c_str());
            String runtimeValue = this->formatRuntimeValue(param->fType, param->fModifiers.fLayout,
                                                           param->fName, &argumentList);
            formatString.appendf("%s%s=%s",
                                 formatString.empty() ? "" : ", ",
                                 fieldName.c_str(),
                                 runtimeValue.c_str());
        }

        if (!formatString.empty()) {
            // Emit the finished format string and associated arguments.
            this->writef("    return SkStringPrintf(\"(%s)\"", formatString.c_str());

            for (const String& argument : argumentList) {
                this->writef(", %s", argument.c_str());
            }

            this->write(");");
        } else {
            // No fields to dump at all; just return an empty string.
            this->write("    return SkString();");
        }
    }

    this->write("\n"
                "}\n"
                "#endif\n");
}

void CPPCodeGenerator::writeTest() {
    const Section* test = fSectionAndParameterHelper.getSection(kTestCodeSection);
    if (test) {
        this->writef(
                "GR_DEFINE_FRAGMENT_PROCESSOR_TEST(%s);\n"
                "#if GR_TEST_UTILS\n"
                "std::unique_ptr<GrFragmentProcessor> %s::TestCreate(GrProcessorTestData* %s) {\n",
                fFullName.c_str(),
                fFullName.c_str(),
                test->fArgument.c_str());
        this->writeSection(kTestCodeSection);
        this->write("}\n"
                    "#endif\n");
    }
}

void CPPCodeGenerator::writeGetKey() {
    this->writef("void %s::onGetGLSLProcessorKey(const GrShaderCaps& caps, "
                                                "GrProcessorKeyBuilder* b) const {\n",
                 fFullName.c_str());
    for (const auto& p : fProgram) {
        if (ProgramElement::kVar_Kind == p.fKind) {
            const VarDeclarations& decls = (const VarDeclarations&) p;
            for (const auto& raw : decls.fVars) {
                const VarDeclaration& decl = (VarDeclaration&) *raw;
                const Variable& var = *decl.fVar;
                String nameString(var.fName);
                const char* name = nameString.c_str();
                if (var.fModifiers.fLayout.fKey != Layout::kNo_Key &&
                    (var.fModifiers.fFlags & Modifiers::kUniform_Flag)) {
                    fErrors.error(var.fOffset,
                                  "layout(key) may not be specified on uniforms");
                }
                switch (var.fModifiers.fLayout.fKey) {
                    case Layout::kKey_Key:
                        if (is_private(var)) {
                            this->writef("%s %s =",
                                         HCodeGenerator::FieldType(fContext, var.fType,
                                                                   var.fModifiers.fLayout).c_str(),
                                         String(var.fName).c_str());
                            if (decl.fValue) {
                                fCPPMode = true;
                                this->writeExpression(*decl.fValue, kAssignment_Precedence);
                                fCPPMode = false;
                            } else {
                                this->writef("%s", default_value(var).c_str());
                            }
                            this->write(";\n");
                        }
                        if (var.fModifiers.fLayout.fWhen.fLength) {
                            this->writef("if (%s) {", String(var.fModifiers.fLayout.fWhen).c_str());
                        }
                        if (var.fType == *fContext.fHalf4_Type) {
                            this->writef("    uint16_t red = SkFloatToHalf(%s.fR);\n",
                                         HCodeGenerator::FieldName(name).c_str());
                            this->writef("    uint16_t green = SkFloatToHalf(%s.fG);\n",
                                         HCodeGenerator::FieldName(name).c_str());
                            this->writef("    uint16_t blue = SkFloatToHalf(%s.fB);\n",
                                         HCodeGenerator::FieldName(name).c_str());
                            this->writef("    uint16_t alpha = SkFloatToHalf(%s.fA);\n",
                                         HCodeGenerator::FieldName(name).c_str());
                            this->write("    b->add32(((uint32_t)red << 16) | green);\n");
                            this->write("    b->add32(((uint32_t)blue << 16) | alpha);\n");
                        } else if (var.fType == *fContext.fHalf_Type ||
                                   var.fType == *fContext.fFloat_Type) {
                            this->writef("    b->add32(sk_bit_cast<uint32_t>(%s));\n",
                                         HCodeGenerator::FieldName(name).c_str());
                        } else if (var.fType.isInteger() || var.fType == *fContext.fBool_Type ||
                                   var.fType.kind() == Type::kEnum_Kind) {
                            this->writef("    b->add32((uint32_t) %s);\n",
                                         HCodeGenerator::FieldName(name).c_str());
                        } else {
                            ABORT("NOT YET IMPLEMENTED: automatic key handling for %s\n",
                                  var.fType.displayName().c_str());
                        }
                        if (var.fModifiers.fLayout.fWhen.fLength) {
                            this->write("}");
                        }
                        break;
                    case Layout::kIdentity_Key:
                        if (var.fType.kind() != Type::kMatrix_Kind) {
                            fErrors.error(var.fOffset,
                                          "layout(key=identity) requires matrix type");
                        }
                        this->writef("    b->add32(%s.isIdentity() ? 1 : 0);\n",
                                     HCodeGenerator::FieldName(name).c_str());
                        break;
                    case Layout::kNo_Key:
                        break;
                }
            }
        }
    }
    this->write("}\n");
}

bool CPPCodeGenerator::generateCode() {
    std::vector<const Variable*> uniforms;
    for (const auto& p : fProgram) {
        if (ProgramElement::kVar_Kind == p.fKind) {
            const VarDeclarations& decls = (const VarDeclarations&) p;
            for (const auto& raw : decls.fVars) {
                VarDeclaration& decl = (VarDeclaration&) *raw;
                if ((decl.fVar->fModifiers.fFlags & Modifiers::kUniform_Flag) &&
                           decl.fVar->fType.kind() != Type::kSampler_Kind) {
                    uniforms.push_back(decl.fVar);
                }

                if (is_uniform_in(*decl.fVar)) {
                    // Validate the "uniform in" declarations to make sure they are fully supported,
                    // instead of generating surprising C++
                    const UniformCTypeMapper* mapper =
                            UniformCTypeMapper::Get(fContext, *decl.fVar);
                    if (mapper == nullptr) {
                        fErrors.error(decl.fOffset, String(decl.fVar->fName)
                                + "'s type is not supported for use as a 'uniform in'");
                        return false;
                    }
                    if (decl.fVar->fModifiers.fLayout.fFlags & Layout::kTracked_Flag) {
                        if (!mapper->supportsTracking()) {
                            fErrors.error(decl.fOffset, String(decl.fVar->fName)
                                    + "'s type does not support state tracking");
                            return false;
                        }
                    }

                } else {
                    // If it's not a uniform_in, it's an error to be tracked
                    if (decl.fVar->fModifiers.fLayout.fFlags & Layout::kTracked_Flag) {
                        fErrors.error(decl.fOffset, "Non-'in uniforms' cannot be tracked");
                        return false;
                    }
                }
            }
        }
    }
    const char* baseName = fName.c_str();
    const char* fullName = fFullName.c_str();
    this->writef("%s\n", HCodeGenerator::GetHeader(fProgram, fErrors).c_str());
    this->writef(kFragmentProcessorHeader, fullName);
    this->writef("#include \"%s.h\"\n\n", fullName);
    this->writeSection(kCppSection);
    this->writef("#include \"src/core/SkUtils.h\"\n"
                 "#include \"src/gpu/GrTexture.h\"\n"
                 "#include \"src/gpu/glsl/GrGLSLFragmentProcessor.h\"\n"
                 "#include \"src/gpu/glsl/GrGLSLFragmentShaderBuilder.h\"\n"
                 "#include \"src/gpu/glsl/GrGLSLProgramBuilder.h\"\n"
                 "#include \"src/sksl/SkSLCPP.h\"\n"
                 "#include \"src/sksl/SkSLUtil.h\"\n"
                 "class GrGLSL%s : public GrGLSLFragmentProcessor {\n"
                 "public:\n"
                 "    GrGLSL%s() {}\n",
                 baseName, baseName);
    bool result = this->writeEmitCode(uniforms);
    this->write("private:\n");
    this->writeSetData(uniforms);
    this->writePrivateVars();
    for (const auto& u : uniforms) {
        if (needs_uniform_var(*u) && !(u->fModifiers.fFlags & Modifiers::kIn_Flag)) {
            this->writef("    UniformHandle %sVar;\n",
                         HCodeGenerator::FieldName(String(u->fName).c_str()).c_str());
        }
    }
    for (const auto& param : fSectionAndParameterHelper.getParameters()) {
        if (needs_uniform_var(*param)) {
            this->writef("    UniformHandle %sVar;\n",
                         HCodeGenerator::FieldName(String(param->fName).c_str()).c_str());
        }
    }
    this->writef("};\n"
                 "GrGLSLFragmentProcessor* %s::onCreateGLSLInstance() const {\n"
                 "    return new GrGLSL%s();\n"
                 "}\n",
                 fullName, baseName);
    this->writeGetKey();
    this->writef("bool %s::onIsEqual(const GrFragmentProcessor& other) const {\n"
                 "    const %s& that = other.cast<%s>();\n"
                 "    (void) that;\n",
                 fullName, fullName, fullName);
    for (const auto& param : fSectionAndParameterHelper.getParameters()) {
        if (param->fType.nonnullable() == *fContext.fFragmentProcessor_Type) {
            continue;
        }
        String nameString(param->fName);
        const char* name = nameString.c_str();
        this->writef("    if (%s != that.%s) return false;\n",
                     HCodeGenerator::FieldName(name).c_str(),
                     HCodeGenerator::FieldName(name).c_str());
    }
    this->write("    return true;\n"
                "}\n");
    this->writeClone();
    this->writeDumpInfo();
    this->writeOnTextureSampler();
    this->writeTest();
    this->writeSection(kCppEndSection);

    result &= 0 == fErrors.errorCount();
    return result;
}

}  // namespace SkSL
