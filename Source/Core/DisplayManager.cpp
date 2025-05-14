

#include "Headers/DisplayManager.h"

namespace Divide
{

    U8 DisplayManager::s_activeDisplayCount{ 1u };
    U8 DisplayManager::s_maxMSAASAmples{ 0u };

    NO_DESTROY std::array<DisplayManager::OutputDisplayPropertiesContainer, DisplayManager::g_maxDisplayOutputs> DisplayManager::s_supportedDisplayModes;

} //namespace Divide
