// version.h - the one place where the version is bumped.
//
// Semver. Bump MINOR for a new feature, PATCH for a fix.
// When the Config struct changes, bump CFG_MAGIC in config.h as well,
// otherwise an old configuration is misread instead of discarded.

#pragma once

#define FW_NAME     "SkyEink"
#define FW_VERSION  "0.1.0"
#define FW_BOARD    "Inkplate 5 Gen2"

// Used for the access point SSID and for file names on the card.
#define FW_SLUG     "skyeink"
