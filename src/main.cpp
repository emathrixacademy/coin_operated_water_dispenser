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
#include "rtc.h"

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

  // ---------------------------------------------------------------------
  // Init order below is load-bearing. Do not alphabetise it.
  //
  //   1. persist   -- the inventory, any open transaction and the stored fault
  //                   flags are inputs to every decision the others make.
  //   2. acceptor  -- must own PIN_COIN_INHIBIT (pinMode OUTPUT, asserted)
  //                   BEFORE anything can ask it to inhibit. It boots
  //                   inhibited: taking a coin before the inventory has been
  //                   validated is money the machine may not be able to honour.
  //   3. faults    -- restores persistent faults from EEPROM and inhibits the
  //                   acceptor if any are set, so it needs both of the above.
  // ---------------------------------------------------------------------
  persist_begin();
  coin_acceptor_begin();
  faults_begin();

  rtc_begin();
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
#ifdef DEBUG
  // Watchdog on the no-blocking-calls rule.
  //
  // Every module below is polled, and two of them -- the hopper outlet counter
  // and the bottle sensor -- depend on being polled faster than their debounce
  // window to work at all. A slow pass is not a performance nit here: it drops
  // a counted coin, and a dropped count is a false jam under a paying user.
  //
  // DEBUG only. This costs a micros() pair and a comparison, and nothing that
  // handles money runs with serial output enabled.
  const uint32_t loop_start_us = micros();
#endif

  // Every module, every pass, unconditionally.
  persist_update();
  rtc_update();
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
    case STATE_SELECTING:
    case STATE_AWAITING_BOTTLE:
    case STATE_DISPENSING:
    case STATE_PAUSED:
    case STATE_SETTLING:
    case STATE_COMPLETE:
    case STATE_PAYING_CHANGE:
    case STATE_THANK_YOU:
    case STATE_FAULT:
    case STATE_ADMIN:
    default:
      break;
  }

#ifdef DEBUG
  {
    const uint32_t elapsed_us = micros() - loop_start_us;
    if (elapsed_us > LOOP_WARN_US) {
      // Rate-limited: a slow pass is usually slow for many passes in a row, and
      // printing on every one of them would itself become the blocking call.
      static uint32_t last_warn_ms = 0;
      const uint32_t now = millis();
      if ((uint32_t)(now - last_warn_ms) >= 1000) {
        last_warn_ms = now;
        Serial.print(F("[SLOW LOOP] "));
        Serial.print(elapsed_us);
        Serial.println(F(" us"));
      }
    }
  }
#endif
}
