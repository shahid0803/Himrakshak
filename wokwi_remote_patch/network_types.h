#pragma once

/*
 * HimRakshak V2 - network/queue types shared by sketch.ino
 *
 * Keep these declarations in a header rather than directly in the .ino file.
 * The Arduino build preprocessor may generate function prototypes before the
 * .ino body; types declared only in the .ino can therefore be unknown when
 * those prototypes are emitted.
 */

#include <Arduino.h>

struct PendingPacket {
  String json;
  unsigned long sequence;
  unsigned long retryAt;
  unsigned long retryDelay;
  bool occupied;
};

struct BackendStationState {
  bool retrieved;
  bool receiverConnected;
  bool hasLatest;
  unsigned long latestSequence;
  String latestReading;
};
