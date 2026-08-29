#ifndef DISPENSE_H
#define DISPENSE_H

// Dispense -- valve control, cutoff, partial-stop handling.
//
// The target in millilitres is fixed by billing before the valve opens and is
// passed in here. This module opens the valve, watches flow toward that target,
// and closes it. It never computes a price and never adjusts a target from what
// the flow sensor reports.
//
// Partial stops -- bottle removed, flow stall -- report the volume delivered so
// billing can round it DOWN to the nearest REFUND_ROUND_ML and charge that.
// Rounding lives in billing, not here. This module reports millilitres and
// nothing else.

#include "types.h"

enum dispense_result_t : uint8_t {
  DISPENSE_IDLE = 0,
  DISPENSE_RUNNING,
  DISPENSE_PAUSED,     // bottle removed, valve shut, grace running in main
  DISPENSE_COMPLETE,   // target reached
  DISPENSE_STALLED     // no flow for FLOW_STALL_TIMEOUT_MS; machine must lock
};

void dispense_begin();
void dispense_update();

// Open the valve and pour toward target_ml. Non-blocking.
void dispense_start(volume_t target_ml);

// Bottle removed mid-pour. Closes the valve immediately and holds the volume
// delivered so far. The grace countdown is main's business, not this module's.
void dispense_pause();

// Bottle replaced within the grace window. Reopens the valve and continues
// toward the ORIGINAL target from the volume already delivered -- not from
// zero. A resume that restarts the count gives away free water.
void dispense_resume();

// End the pour early and for good -- grace expired, or a fault. Valve closed,
// delivered volume held for billing to settle against.
void dispense_abort();

dispense_result_t dispense_status();

// Millilitres delivered toward the current target. Valid during and after a
// pour, until the next dispense_start().
volume_t dispense_delivered();

volume_t dispense_target();

bool dispense_valve_open();

// Clears COMPLETE, STALLED or an aborted state back to IDLE, once the caller
// has settled the transaction against dispense_delivered().
void dispense_clear();

#endif  // DISPENSE_H
