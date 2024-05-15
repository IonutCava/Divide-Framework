
#include "Headers/Material.h"

namespace Divide {

void Material::Properties::doubleSided(const bool state) noexcept {
    if (_doubleSided != state) {
        _doubleSided = state;
        _cullUpdated = true;
    }
}

void Material::Properties::isRefractive(const bool state) noexcept {
    if (_isRefractive != state) {
        _isRefractive = state;
        _needsNewShader = true;
    }
}

void Material::Properties::receivesShadows(const bool state) noexcept {
    if (_receivesShadows != state) {
        _receivesShadows = state;
        _needsNewShader = true;
    }
}

void Material::Properties::isStatic(const bool state) noexcept {
    if (_isStatic != state) {
        _isStatic = state;
        _needsNewShader = true;
    }
}

void Material::Properties::isInstanced(const bool state) noexcept {
    if (_isInstanced != state) {
        _isInstanced = state;
        _needsNewShader = true;
    }
}

void Material::Properties::ignoreTexDiffuseAlpha(const bool state) noexcept {
    if (overrides()._ignoreTexDiffuseAlpha != state) {
        overrides()._ignoreTexDiffuseAlpha = state;
        _needsNewShader = true;
    }
}

void Material::Properties::hardwareSkinning(const bool state) noexcept {
    if (_hardwareSkinning != state) {
        _hardwareSkinning = state;
        _needsNewShader = true;
    }
}

void Material::Properties::texturesInFragmentStageOnly(const bool state) noexcept {
    if (_texturesInFragmentStageOnly != state) {
        _texturesInFragmentStageOnly = state;
        _needsNewShader = true;
    }
}

void Material::Properties::toggleTransparency(const bool state) noexcept {
    if (overrides()._transparencyEnabled != state) {
        overrides()._transparencyEnabled = state;
        _transparencyUpdated = true;
    }
}

void Material::Properties::useAlphaDiscard(const bool state) noexcept {
    if (overrides()._useAlphaDiscard != state) {
        overrides()._useAlphaDiscard = state;
        _needsNewShader = true;
    }
}

void Material::Properties::baseColour(const FColour4& colour) noexcept {
    _baseColour = colour;
    _transparencyUpdated = true;
}

void Material::Properties::bumpMethod(const BumpMethod newBumpMethod) noexcept {
    if (_bumpMethod != newBumpMethod) {
        _bumpMethod = newBumpMethod;
        _needsNewShader = true;
    }
}

void Material::Properties::shadingMode(const ShadingMode mode) noexcept {
    if (_shadingMode != mode) {
        _shadingMode = mode;
        _needsNewShader = true;
    }
}

void Material::Properties::saveToXML(const std::string& entryName, boost::property_tree::ptree& pt) const {
    pt.put(entryName + ".shadingMode", TypeUtil::ShadingModeToString(shadingMode()));

    pt.put(entryName + ".colour.<xmlattr>.r", baseColour().r);
    pt.put(entryName + ".colour.<xmlattr>.g", baseColour().g);
    pt.put(entryName + ".colour.<xmlattr>.b", baseColour().b);
    pt.put(entryName + ".colour.<xmlattr>.a", baseColour().a);

    pt.put(entryName + ".emissive.<xmlattr>.r", emissive().r);
    pt.put(entryName + ".emissive.<xmlattr>.g", emissive().g);
    pt.put(entryName + ".emissive.<xmlattr>.b", emissive().b);

    pt.put(entryName + ".ambient.<xmlattr>.r", ambient().r);
    pt.put(entryName + ".ambient.<xmlattr>.g", ambient().g);
    pt.put(entryName + ".ambient.<xmlattr>.b", ambient().b);

    pt.put(entryName + ".specular.<xmlattr>.r", specular().r);
    pt.put(entryName + ".specular.<xmlattr>.g", specular().g);
    pt.put(entryName + ".specular.<xmlattr>.b", specular().b);
    pt.put(entryName + ".specular.<xmlattr>.a", shininess());

    pt.put(entryName + ".specular_factor", specGloss().x);
    pt.put(entryName + ".glossiness_factor", specGloss().y);

    pt.put(entryName + ".metallic", metallic());

    pt.put(entryName + ".roughness", roughness());

    pt.put(entryName + ".doubleSided", doubleSided());

    pt.put(entryName + ".receivesShadows", receivesShadows());

    pt.put(entryName + ".ignoreTexDiffuseAlpha", overrides().ignoreTexDiffuseAlpha());

    pt.put(entryName + ".bumpMethod", TypeUtil::BumpMethodToString(bumpMethod()));

    pt.put(entryName + ".parallaxFactor", parallaxFactor());

    pt.put(entryName + ".transparencyEnabled", overrides().transparencyEnabled());

    pt.put(entryName + ".useAlphaDiscard", overrides().useAlphaDiscard());

    pt.put(entryName + ".isRefractive", isRefractive());
}

void Material::Properties::loadFromXML(const std::string& entryName, const boost::property_tree::ptree& pt)
{
    const ShadingMode shadingModeCrt = shadingMode();
    ShadingMode shadingModeFile = TypeUtil::StringToShadingMode(pt.get<std::string>(entryName + ".shadingMode", TypeUtil::ShadingModeToString(shadingModeCrt)));
    if (shadingModeFile == ShadingMode::COUNT)
    {
        shadingModeFile = shadingModeCrt;
    }

    shadingMode(shadingModeFile);

    baseColour(FColour4(pt.get<F32>(entryName + ".colour.<xmlattr>.r", baseColour().r),
                        pt.get<F32>(entryName + ".colour.<xmlattr>.g", baseColour().g),
                        pt.get<F32>(entryName + ".colour.<xmlattr>.b", baseColour().b),
                        pt.get<F32>(entryName + ".colour.<xmlattr>.a", baseColour().a)));

    emissive(FColour3(pt.get<F32>(entryName + ".emissive.<xmlattr>.r", emissive().r),
                      pt.get<F32>(entryName + ".emissive.<xmlattr>.g", emissive().g),
                      pt.get<F32>(entryName + ".emissive.<xmlattr>.b", emissive().b)));
    
    ambient(FColour3(pt.get<F32>(entryName + ".ambient.<xmlattr>.r", ambient().r),
                     pt.get<F32>(entryName + ".ambient.<xmlattr>.g", ambient().g),
                     pt.get<F32>(entryName + ".ambient.<xmlattr>.b", ambient().b)));

    specular(FColour3(pt.get<F32>(entryName + ".specular.<xmlattr>.r", specular().r),
                      pt.get<F32>(entryName + ".specular.<xmlattr>.g", specular().g),
                      pt.get<F32>(entryName + ".specular.<xmlattr>.b", specular().b)));

    shininess(pt.get<F32>(entryName + ".specular.<xmlattr>.a", shininess()));

    specGloss(SpecularGlossiness(pt.get<F32>(entryName + ".specular_factor", specGloss().x),
                                 pt.get<F32>(entryName + ".glossiness_factor", specGloss().y)));

    metallic(pt.get<F32>(entryName + ".metallic", metallic()));

    roughness(pt.get<F32>(entryName + ".roughness", roughness()));

    parallaxFactor(pt.get<F32>(entryName + ".parallaxFactor", parallaxFactor()));

    receivesShadows(pt.get<bool>(entryName + ".receivesShadows", receivesShadows()));

    ignoreTexDiffuseAlpha(pt.get<bool>(entryName + ".ignoreTexDiffuseAlpha", overrides().ignoreTexDiffuseAlpha()));

    bumpMethod(TypeUtil::StringToBumpMethod(pt.get<std::string>(entryName + ".bumpMethod", TypeUtil::BumpMethodToString(bumpMethod()))));

    toggleTransparency(pt.get<bool>(entryName + ".transparencyEnabled", overrides().transparencyEnabled()));
    
    useAlphaDiscard(pt.get<bool>(entryName + ".useAlphaDiscard", overrides().useAlphaDiscard()));

    isRefractive(pt.get<bool>(entryName + ".isRefractive", isRefractive()));

    doubleSided(pt.get<bool>(entryName + ".doubleSided", doubleSided()));
    {
        //Clear this flag when loading from XML as it will conflict with our custom RenderStateBlock when loading it from XML!!
        //doubleSided calls set this flag to true thus invalidating our recently loaded render state.
        cullUpdated(false);
    }
}

} //namespace Divide
