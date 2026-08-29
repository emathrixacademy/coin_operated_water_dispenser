#include <Arduino.h>

#include "config.h"
#include "types.h"

#include "coin_acceptor.h"
#include "coin_diverter.h"
#include "coin_hopper.h"
#include "flow.h"
#include "bottle.h"
#include "water_level.h"
#include "dispense.h"
#include "billing.h"
#include "persist.h"
#include "hmi.h"
#include "faults.h"

// Single non-blocking state machine.
// Project EMX-2026-WATERVENDO-01, eMathrix Technologies.
//
// Milestone 2: wiring of begin()/update() only. Transitions land in Milestone 5.
//
// RULES THIS FILE ENFORCES:
//
//   - No delay() outside setup(). A blocked loop is a missed coin pulse.
//   - No module owns the loop. Every module gets its update() call every pass,
//     unconditionally, whatever state the machine is in. A module that stops
//     being polled stops debouncing, stops timing out, and stops noticing that
//     its hopper jammed.
//   - Transitions live here and nowhere else. A module reports what happened;
//     it does not decide what the machine does next.
//   - No dynamic allocation anywhere. Fixed buffers and char[] only.

static state_t s_state = STATE_BOOT;

void setup() {
#ifdef DEBUG
  Serial.begin(DEBUG_BAUD);
  Serial.println(F("EMX-2026-WATERVENDO-01 boot [DEBUG BUILD - NOT FOR SERVICE]"));
#endif

  // persist first: the inventory and any open transaction are inputs to every
  // decision the other modules make on boot.
  persist_begin();

  faults_begin();
  coin_acceptor_begin();
  coin_diverter_begin();
  coin_hopper_begin();
  flow_begin();
  bottle_begin();
  water_level_begin();
  dispense_begin();
  billing_begin();
  hmi_begin();

  // TODO(M5): boot reconciliation.
  //   - An in-flight coin recorded but never confirmed routed goes through
  //     persist_reconcile_unrouted_coin(). Credit the user, assume the profit
  //     chamber, tag the event.
  //   - An open transaction is restored and resumed with its balance shown,
  //     rather than dropping to Standby and swallowing the user's money.
  //   - A persistent fault is re-raised before the machine can take a coin.

  s_state = STATE_STANDBY;
}

void loop() {
  // Every module, every pass, unconditionally.
  persist_update();
  coin_acceptor_update();
  coin_diverter_update();
  coin_hopper_update();
  flow_update();
  bottle_update();
  water_level_update();
  dispense_update();
  hmi_update();
  faults_update();

  // TODO(M5): the transition table.
  switch (s_state) {
    case STATE_BOOT:
    case STATE_STANDBY:
    case STATE_ACCEPTING:
    case STATE_SELECT_VOLUME:
    case STATE_WAIT_BOTTLE:
    case STATE_DISPENSING:
    case STATE_BOTTLE_REMOVED:
    case STATE_POUR_COMPLETE:
    case STATE_PAYING_CHANGE:
    case STATE_THANK_YOU:
    case STATE_LOCKED:
    case STATE_ADMIN:
    default:
      break;
  }
}
