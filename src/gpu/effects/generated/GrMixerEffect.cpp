/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrMixerEffect.fp; do not modify.
 **************************************************************************************************/
#include "GrMixerEffect.h"

#include "src/gpu/GrTexture.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"
#include "src/gpu/glsl/GrGLSLFragmentShaderBuilder.h"
#include "src/gpu/glsl/GrGLSLProgramBuilder.h"
#include "src/sksl/SkSLCPP.h"
#include "src/sksl/SkSLUtil.h"
class GrGLSLMixerEffect : public GrGLSLFragmentProcessor {
public:
    GrGLSLMixerEffect() {}
    void emitCode(EmitArgs& args) override {
        GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
        const GrMixerEffect& _outer = args.fFp.cast<GrMixerEffect>();
        (void)_outer;
        auto weight = _outer.weight;
        (void)weight;
        weightVar = args.fUniformHandler->addUniform(&_outer, kFragment_GrShaderFlag,
                                                     kHalf_GrSLType, "weight");
        SkString _input1278 = SkStringPrintf("%s", args.fInputColor);
        SkString _sample1278;
        _sample1278 = this->invokeChild(_outer.fp0_index, _input1278.c_str(), args);
        fragBuilder->codeAppendf("half4 in0 = %s;", _sample1278.c_str());
        SkString _input1335 = SkStringPrintf("%s", args.fInputColor);
        SkString _sample1335;
        if (_outer.fp1_index >= 0) {
            _sample1335 = this->invokeChild(_outer.fp1_index, _input1335.c_str(), args);
        } else {
            _sample1335 = _input1335;
        }
        fragBuilder->codeAppendf("\nhalf4 in1 = %s ? %s : %s;\n%s = mix(in0, in1, %s);\n",
                                 _outer.fp1_index >= 0 ? "true" : "false", _sample1335.c_str(),
                                 args.fInputColor, args.fOutputColor,
                                 args.fUniformHandler->getUniformCStr(weightVar));
    }

private:
    void onSetData(const GrGLSLProgramDataManager& pdman,
                   const GrFragmentProcessor& _proc) override {
        const GrMixerEffect& _outer = _proc.cast<GrMixerEffect>();
        { pdman.set1f(weightVar, (_outer.weight)); }
    }
    UniformHandle weightVar;
};
GrGLSLFragmentProcessor* GrMixerEffect::onCreateGLSLInstance() const {
    return new GrGLSLMixerEffect();
}
void GrMixerEffect::onGetGLSLProcessorKey(const GrShaderCaps& caps,
                                          GrProcessorKeyBuilder* b) const {}
bool GrMixerEffect::onIsEqual(const GrFragmentProcessor& other) const {
    const GrMixerEffect& that = other.cast<GrMixerEffect>();
    (void)that;
    if (weight != that.weight) return false;
    return true;
}
GrMixerEffect::GrMixerEffect(const GrMixerEffect& src)
        : INHERITED(kGrMixerEffect_ClassID, src.optimizationFlags()), weight(src.weight) {
    {
        auto fp0_clone = src.childProcessor(src.fp0_index).clone();
        if (src.childProcessor(src.fp0_index).isSampledWithExplicitCoords()) {
            fp0_clone->setSampledWithExplicitCoords();
        }
        fp0_index = this->registerChildProcessor(std::move(fp0_clone));
    }
    if (src.fp1_index >= 0) {
        auto fp1_clone = src.childProcessor(src.fp1_index).clone();
        if (src.childProcessor(src.fp1_index).isSampledWithExplicitCoords()) {
            fp1_clone->setSampledWithExplicitCoords();
        }
        fp1_index = this->registerChildProcessor(std::move(fp1_clone));
    }
}
std::unique_ptr<GrFragmentProcessor> GrMixerEffect::clone() const {
    return std::unique_ptr<GrFragmentProcessor>(new GrMixerEffect(*this));
}
