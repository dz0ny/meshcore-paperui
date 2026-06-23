#pragma once
// Sim shim — the showcase screens only use the ADV_TYPE_* constants, not the
// real parser/builder (which pulls MeshCore's Mesh.h).
#include <cstdint>
#define ADV_TYPE_NONE      0
#define ADV_TYPE_CHAT      1
#define ADV_TYPE_REPEATER  2
#define ADV_TYPE_ROOM      3
#define ADV_TYPE_SENSOR    4
#define ADV_LATLON_MASK    0x10
#define ADV_NAME_MASK      0x80
