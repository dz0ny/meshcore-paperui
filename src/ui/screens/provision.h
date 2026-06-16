#pragma once
#include "../ui_screen_mgr.h"
namespace ui::screen::provision {
extern screen_lifecycle_t lifecycle;       // SCREEN_PROVISION: Share / Receive menu
extern screen_lifecycle_t run_lifecycle;   // SCREEN_PROVISION_RUN: transfer status
extern screen_lifecycle_t pick_lifecycle;  // SCREEN_PROVISION_PICK: choose a sharer to receive from
}
