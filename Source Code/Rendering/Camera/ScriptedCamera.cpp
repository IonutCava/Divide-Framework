#include "stdafx.h"

#include "Headers/ScriptedCamera.h"

namespace Divide {

ScriptedCamera::ScriptedCamera(const Str256& name, const vec3<F32>& eye)
    : FreeFlyCamera(name, Type(), eye)
{
}

void ScriptedCamera::saveToXML(boost::property_tree::ptree& pt, const string prefix) const {
    FreeFlyCamera::saveToXML(pt, prefix);
}

void ScriptedCamera::loadFromXML(const boost::property_tree::ptree& pt, const string prefix) {
    FreeFlyCamera::loadFromXML(pt, prefix);
}

};