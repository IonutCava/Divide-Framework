#include "stdafx.h"

#include "Headers/Vehicle.h"

namespace Divide {

Vehicle::Vehicle()
    : Unit(UnitType::UNIT_TYPE_VEHICLE),
      _vehicleTypeMask(0)
{
    _playerControlled = false;
}


void Vehicle::setVehicleTypeMask(const U32 mask) {
    assert((mask &
            ~(to_base(VehicleType::COUNT) - 1)) == 0);
    _vehicleTypeMask = mask;
}

bool Vehicle::checkVehicleMask(const VehicleType type) const {
    return (_vehicleTypeMask & to_U32(type)) != to_U32(type);
}
}