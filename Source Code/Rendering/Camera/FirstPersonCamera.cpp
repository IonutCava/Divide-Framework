#include "stdafx.h"

#include "Headers/FirstPersonCamera.h"

namespace Divide {

FirstPersonCamera::FirstPersonCamera(const Str256& name, const vec3<F32>& eye)
    : FreeFlyCamera(name, Type(), eye)
{
}

void FirstPersonCamera::saveToXML(boost::property_tree::ptree& pt, const string prefix) const {
    FreeFlyCamera::saveToXML(pt, prefix);
}

void FirstPersonCamera::loadFromXML(const boost::property_tree::ptree& pt, const string prefix) {
    FreeFlyCamera::loadFromXML(pt, prefix);
}

};