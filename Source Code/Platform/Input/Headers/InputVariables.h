/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _INPUT_VARIABLES_H_
#define _INPUT_VARIABLES_H_

#include "Core/TemplateLibraries/Headers/STLString.h"
#include "Platform/Headers/PlatformDefines.h"

namespace Divide {
namespace Input {
//////////// Variable classes
///////////////////////////////////////////////////////////

class Variable {
   protected:
    D64 _dInitValue;
    D64 _dValue{};

   public:
    Variable(const D64 dInitValue)
       : _dInitValue(dInitValue),
         _dValue(dInitValue)
    {
    }

    virtual ~Variable() = default;

    [[nodiscard]] D64 getValue() const { return _dValue; }

    void reset() { _dValue = _dInitValue; }

    virtual void setValue(const D64 dValue) { _dValue = dValue; }

    [[nodiscard]] virtual string toString() const {
        return Util::to_string(_dValue);
    }

    virtual void update(){};
};

class Constant final : public Variable {
   public:
    Constant(const D64 dInitValue) : Variable(dInitValue) {}

    void setValue([[maybe_unused]] const D64 dValue) override {
    }
};

class LimitedVariable : public Variable {
   protected:
    D64 _dMinValue;
    D64 _dMaxValue;

   public:
    LimitedVariable(const D64 dInitValue, const D64 dMinValue, const D64 dMaxValue)
        : Variable(dInitValue),
          _dMinValue(dMinValue),
          _dMaxValue(dMaxValue)
   {

   }

    void setValue(const D64 dValue) override {
        _dValue = dValue;
        if (_dValue > _dMaxValue)
            _dValue = _dMaxValue;
        else if (_dValue < _dMinValue)
            _dValue = _dMinValue;
    }
};

class TriangleVariable final : public LimitedVariable {
   protected:
    D64 _dDeltaValue;

   public:
    TriangleVariable(const D64 dInitValue, const D64 dDeltaValue, const D64 dMinValue, const D64 dMaxValue)
        : LimitedVariable(dInitValue, dMinValue, dMaxValue),
          _dDeltaValue(dDeltaValue){};

    void update() override {
        D64 dValue = getValue() + _dDeltaValue;
        if (dValue > _dMaxValue) {
            dValue = _dMaxValue;
            _dDeltaValue = -_dDeltaValue;
            // cout << "Decreasing variable towards " << _dMinValue << endl;
        } else if (dValue < _dMinValue) {
            dValue = _dMinValue;
            _dDeltaValue = -_dDeltaValue;
            // cout << "Increasing variable towards " << _dMaxValue << endl;
        }
        setValue(dValue);
        // cout << "TriangleVariable::update : delta=" << _dDeltaValue << ",
        // value=" << dValue << endl;
    }
};

//////////// Variable effect class
/////////////////////////////////////////////////////////////

using MapVariables = hashMap<U64, Variable*>;
typedef void (*EffectVariablesApplier)(MapVariables& mapVars, OIS::Effect* pEffect);

class VariableEffect {
   protected:
    // Effect description
    const char* _pszDesc;

    // The associate OIS effect
    OIS::Effect* _pEffect{};

    // The effect variables.
    MapVariables _mapVariables;

    // The effect variables applier function.
    EffectVariablesApplier _pfApplyVariables;

    // True if the effect is currently being played.
    bool _bActive = false;

   public:
    VariableEffect(const char* pszDesc, OIS::Effect* pEffect,
                   const MapVariables& mapVars,
                   const EffectVariablesApplier pfApplyVars)
        : _pszDesc(pszDesc),
          _pEffect(pEffect),
          _mapVariables(mapVars),
          _pfApplyVariables(pfApplyVars)
    {
    }

    ~VariableEffect()
    {
        MemoryManager::DELETE(_pEffect);
        for (MapVariables::iterator iterVars = std::begin(_mapVariables);
             iterVars != std::end(_mapVariables); ++iterVars) {
            MemoryManager::DELETE(iterVars->second);
        }
    }

    void setActive(const bool bActive = true) {
        reset();
        _bActive = bActive;
    }

    [[nodiscard]] bool isActive() const noexcept { return _bActive; }
    OIS::Effect* getFFEffect() { return _pEffect; }

    [[nodiscard]] const char* getDescription() const { return _pszDesc; }

    void update() {
        if (isActive()) {
            for (MapVariables::iterator iterVars = std::begin(_mapVariables);
                 iterVars != std::end(_mapVariables); ++iterVars) {
                iterVars->second->update();
            }

            // Apply the updated variable values to the effect.
            _pfApplyVariables(_mapVariables, _pEffect);
        }
    }

    void reset() {
        for (MapVariables::iterator iterVars = std::begin(_mapVariables);
             iterVars != std::end(_mapVariables); ++iterVars) {
            iterVars->second->reset();
        }
        _pfApplyVariables(_mapVariables, _pEffect);
    }

    [[nodiscard]] string toString() const {
        string str;
        for (MapVariables::const_iterator iterVars = std::begin(_mapVariables);
             iterVars != std::end(_mapVariables); ++iterVars) {
           str += iterVars->first + ":" + iterVars->second->toString() + " ";
        }
        return str;
    }
};

};  // namespace Input
};  // namespace Divide
#endif
