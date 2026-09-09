// portal.h - settings over WiFi.
//
// No OTA. The access point is secured; the password is derived from the MAC
// address, so it differs per device and stays the same across restarts.

#pragma once
#include <stdint.h>

namespace portal {

// Starts the access point and serves it until the settings are saved or the
// timeout expires. Returns true when the configuration changed.
bool run(uint16_t timeoutSec);

}  // namespace portal
