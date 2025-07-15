#pragma once

#include <qnumeric.h>
struct TerrainGridCell
{
    double latitude = 0;
    double longitude = 0;
    double altitude = qQNaN();
    double value = 0;
};
