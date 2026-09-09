// board_config.h - target board selection.
//
// Uncomment exactly one. The HAL implementation for each board lives in its
// own board_*.cpp, wrapped in the same define, so boards that are not
// selected never reach the binary.

#pragma once

#define BOARD_INKPLATE5V2
// #define BOARD_WAVESHARE75      // 7.5" 800x480 e-paper, not written yet

#if defined(BOARD_INKPLATE5V2) + defined(BOARD_WAVESHARE75) != 1
#error "Select exactly one board in board_config.h"
#endif
