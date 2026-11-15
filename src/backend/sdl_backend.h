#pragma once

#if STATUS_SDL
#include "backend/backend.h"

namespace status {

// Desktop window for developing screens without the device.
class SdlBackend : public Backend {
public:
    bool init(lv_display_rotation_t rotation) override;
};

}  // namespace status
#endif
