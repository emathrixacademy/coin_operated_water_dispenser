#ifndef FLOW_H
#define FLOW_H

// Flow sensor -- pulse capture and volume accumulation.
//
// THE FLOW SENSOR IS A CUTOFF, NOT A CASHIER.
//
// Nothing in this module returns money, and nothing anywhere may compute an
// amount owed from what this module reports. The target was fixed from the
// user's coins before the valve opened; this module only counts toward it.
// See CLAUDE.md -- billing from a measured volume is the bug the whole design
// exists to prevent.
//
// The ISR on PIN_FLOW_PULSE increments a volatile counter and returns.
// Conversion to millilitres happens in update().
//
// Millilitres are computed as pulses * ML_PER_PULSE_NUM / ML_PER_PULSE_DEN with
// the division remainder carried between calls, so truncation error stays under
// one millilitre across a whole pour rather than accumulating once per pulse.
// No floating point -- CLAUDE.md.

#include "types.h"

void flow_begin();
void flow_update();

// Start accumulating from zero. Called as the valve opens.
void flow_reset();

// Millilitres delivered since the last flow_reset().
volume_t flow_ml();

// Raw pulse count since the last flow_reset(). Used by the calibration routine
// in DEBUG builds -- see docs/calibration.md.
uint32_t flow_pulses();

// True if the valve has been open with no pulses for FLOW_STALL_TIMEOUT_MS.
//
// Three physical causes -- blocked line, dead flow sensor, closed upstream tap --
// and all three need a person on site. The caller closes the valve, settles up
// on the rounded-down volume actually delivered, refunds the balance, and locks.
// It does not retry. See scenarios.md case 19.
//
// The same timeout covers the never-started case: valve open and nothing
// arriving at all trips at FLOW_STALL_TIMEOUT_MS like any other stall.
bool flow_is_stalled();

// Tell the module whether the valve is currently open. Stall detection only
// runs while it is -- an idle machine with no pulses is not a stall.
void flow_set_valve_open(bool open);

#endif  // FLOW_H
