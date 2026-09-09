// log.h - serial output.
//
// Serial belongs to the Arduino core rather than to any particular board, so
// this file does not break the hardware isolation rule. Bringing the port up
// is done by board_*.cpp in boardEarlyInit(), since baud rate and pins are a
// board matter.
//
// The device goes to sleep after a few seconds, so the output is short. Open
// the monitor before pressing RESET if you want to see the whole run.

#pragma once
#include <Arduino.h>

#ifndef SKY_LOG
#define SKY_LOG 1
#endif

#if SKY_LOG
  #define LOGF(fmt, ...)  Serial.printf("[%7lu] " fmt "\n", millis(), ##__VA_ARGS__)
  #define LOGS(msg)       Serial.printf("[%7lu] %s\n", millis(), msg)
  #define LOGSEP()        Serial.println(F("--------------------------------"))
#else
  #define LOGF(fmt, ...)  do {} while (0)
  #define LOGS(msg)       do {} while (0)
  #define LOGSEP()        do {} while (0)
#endif
