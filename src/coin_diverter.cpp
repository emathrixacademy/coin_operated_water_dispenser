#include <Arduino.h>
#include <Servo.h>
#include "coin_diverter.h"
#include "coin_acceptor.h"
#include "coin_route.h"
#include "persist.h"
#include "coin_level.h"

// Coin diverter -- servo routing and the per-coin lockout window.
//
// The window is the whole point of this module in RECIRCULATE. route() asserts
// the acceptor inhibit BEFORE the servo is commanded, and holds it for
// COIN_LOCKOUT_MS after. A coin arriving in that window is rejected by the
// acceptor and any stray pulses are dropped by coin_acceptor_update() -- never
// queued, never credited late. See scenarios.md case 14.
//
// WHERE a coin goes is decided by coin_route(), which has no Arduino dependency
// and is unit tested. This file only moves the mechanism to match.
//
// Two gate servos in series on one chute (see config.h). Rest is both open,
// straight to the coin box. Gate 1 closed -> P5 tunnel; gate 1 open and gate 2
// closed -> P1 tunnel. One coin = gates set and held (GATE_ROUTE_MS), then back
// to rest (GATE_RETURN_MS) -- COIN_LOCKOUT_MS in all.

// A plain constant rather than #if around the code, so BOTH paths are compiled
// and type-checked in every build. The recirculation path must not rot while
// the machine runs without servos -- we are going back to it.
static const bool COLLECT_ALL = (COIN_ROUTING_MODE == COIN_ROUTING_COLLECT_ALL);

static Servo s_gate1;   // first gate,  P5 tunnel
static Servo s_gate2;   // second gate, P1 tunnel

static enum : uint8_t {
  DIV_IDLE = 0,
  DIV_ROUTING,     // gates set for this coin, coin running down its tunnel
  DIV_RETURNING    // gates travelling back to rest (both open)
} s_state = DIV_IDLE;

static uint32_t s_started_ms = 0;
static coin_t s_coin = COIN_NONE;

// Where the coin in flight was SENT. Decided once, when the gates moved, and
// counted there on settle. Re-asking coin_destination() at settle time would
// count it wherever a hopper-full beam says NOW -- which can differ from where
// the gates actually pointed GATE_ROUTE_MS earlier.
static coin_dest_t s_dest = DEST_BOX_UNKNOWN;

// Gates already aimed for an identified coin that has not been routed yet.
static coin_t s_aim_coin = COIN_NONE;
static coin_dest_t s_aim_dest = DEST_BOX_UNKNOWN;
static uint32_t s_aim_ms = 0;

// Servo signal only while the gates are in use. With no signal an MG996R does
// not move: it holds where it was left by friction. On the bench the servos
// twitched at rest -- their pulse timing was being disturbed by interrupt
// load -- and a servo that is not being driven cannot twitch. Detached
// GATE_DETACH_MS after the last move, once it has certainly arrived.
static uint32_t s_last_move_ms = 0;

static void gate_write(Servo &s, uint8_t pin, int angle) {
  if (!s.attached()) s.attach(pin);
  s.write(angle);
  s_last_move_ms = millis();
}

static void gates_rest() {
  gate_write(s_gate1, PIN_GATE1_SERVO, GATE1_ANGLE_OPEN);
  gate_write(s_gate2, PIN_GATE2_SERVO, GATE2_ANGLE_OPEN);
}

// Every box destination is the same straight run -- the denomination split is
// in the books, not in the mechanism.
static void gates_for(coin_dest_t dest) {
  switch (dest) {
    case DEST_P5_HOPPER:
      gate_write(s_gate1, PIN_GATE1_SERVO, GATE1_ANGLE_CLOSED);
      gate_write(s_gate2, PIN_GATE2_SERVO, GATE2_ANGLE_OPEN);
      break;
    case DEST_P1_HOPPER:
      gate_write(s_gate1, PIN_GATE1_SERVO, GATE1_ANGLE_OPEN);
      gate_write(s_gate2, PIN_GATE2_SERVO, GATE2_ANGLE_CLOSED);
      break;
    default:
      gates_rest();
      break;
  }
}

void coin_diverter_begin() {
  s_state = DIV_IDLE;
  s_coin = COIN_NONE;

  // COLLECT_ALL: NO SERVO IS FITTED, so none is attached and none is ever
  // commanded. Both flaps are mechanically pinned, Gate A toward the coin box.
  // The firmware cannot tell an absent servo from a failed one; the mode flag
  // is the only thing that distinguishes them, which is why nothing here waits
  // for travel that will never happen.
  if (COLLECT_ALL) return;

  gates_rest();   // attaches; detached again once idle
}

void coin_diverter_aim(coin_t coin) {
  if (COLLECT_ALL || s_state != DIV_IDLE) return;
  if (coin == COIN_NONE || coin == COIN_INVALID) return;
  if (coin == s_aim_coin) return;   // already aimed

  s_aim_coin = coin;
  s_aim_dest = coin_destination(coin);
  s_aim_ms = millis();
  gates_for(s_aim_dest);
}

// Bench exercise from the test console ("gate <n>"): each cycle closes and
// reopens gate 1, then gate 2, GATE_EXERCISE_STEP_MS per move. The acceptor
// window is held for the whole run -- with the gates moving on their own, a
// coin inserted now could land anywhere.
static uint8_t s_ex_steps = 0;     // moves left; 4 per cycle
static uint32_t s_ex_ms = 0;

void coin_diverter_exercise(uint8_t cycles) {
  if (COLLECT_ALL || s_state != DIV_IDLE || s_ex_steps) return;
  if (cycles == 0) return;
  if (cycles > 20) cycles = 20;
  s_ex_steps = (uint8_t)(cycles * 4);
  s_ex_ms = millis() - GATE_EXERCISE_STEP_MS;   // first move on the next pass
  s_aim_coin = COIN_NONE;
  coin_acceptor_window_inhibit();
}

static void exercise_update() {
  if ((uint32_t)(millis() - s_ex_ms) < GATE_EXERCISE_STEP_MS) return;
  s_ex_ms = millis();
  switch (s_ex_steps % 4) {
    case 0: gate_write(s_gate1, PIN_GATE1_SERVO, GATE1_ANGLE_CLOSED); break;
    case 3: gate_write(s_gate1, PIN_GATE1_SERVO, GATE1_ANGLE_OPEN);   break;
    case 2: gate_write(s_gate2, PIN_GATE2_SERVO, GATE2_ANGLE_CLOSED); break;
    case 1: gate_write(s_gate2, PIN_GATE2_SERVO, GATE2_ANGLE_OPEN);   break;
  }
  s_ex_steps--;
  if (s_ex_steps == 0) {
    gates_rest();
    coin_acceptor_window_release();
  }
}

void coin_diverter_update() {
  if (s_ex_steps) {
    exercise_update();
    return;
  }
  if (s_state == DIV_IDLE) {
    // An aimed coin that was never routed: back to rest, so the next coin is
    // not caught by a hopper gate meant for this one.
    if (s_aim_coin != COIN_NONE &&
        (uint32_t)(millis() - s_aim_ms) >= COIN_AIM_TIMEOUT_MS) {
      s_aim_coin = COIN_NONE;
      gates_rest();
    }
    // Idle and settled: drop the servo signal (see gate_write()).
    if (s_aim_coin == COIN_NONE && s_gate1.attached() &&
        (uint32_t)(millis() - s_last_move_ms) >= GATE_DETACH_MS) {
      s_gate1.detach();
      s_gate2.detach();
    }
    return;
  }

  const uint32_t elapsed = millis() - s_started_ms;

  if (s_state == DIV_ROUTING) {
    if (elapsed < GATE_ROUTE_MS) return;

    // ---------------------------------------------------------------------
    // The coin is physically committed to its chamber only now.
    //
    // The servo has no position feedback, so this timer is the only thing
    // standing between a coin and a jammed chute. DO NOT shorten
    // COIN_LOCKOUT_MS to make the machine feel faster.
    //
    // Inventory increments HERE and nowhere else -- after physical commit, never
    // at the moment the coin was credited. That ordering is what makes the case
    // 13 reconciliation possible: a power cut before this point leaves an
    // in-flight marker and no inventory change.
    // ---------------------------------------------------------------------
    persist_inventory_add(s_dest, +1);
    persist_clear_coin_in_flight();

    gates_rest();
    s_state = DIV_RETURNING;
    s_started_ms = millis();
    return;
  }

  // DIV_RETURNING: the next coin must not meet a gate still swinging open.
  if (elapsed < GATE_RETURN_MS) return;

  s_state = DIV_IDLE;
  s_coin = COIN_NONE;
  coin_acceptor_window_release();
}

void coin_diverter_route(coin_t coin) {
  // A route already in progress is never interrupted or queued behind. The
  // caller must not have credited a second coin while is_busy() was true.
  if (s_state != DIV_IDLE) return;
  if (coin == COIN_NONE || coin == COIN_INVALID) return;

  if (COLLECT_ALL) {
    // -------------------------------------------------------------------
    // Nothing moves, and three things follow from that. All three are
    // CONSEQUENCES of having no gate to move, not omissions:
    //
    //   1. NO LOCKOUT WINDOW. COIN_LOCKOUT_MS is the time a gate takes to
    //      travel. With the flaps pinned the chute is always in position, so
    //      the acceptor is not inhibited and coins can be fed as fast as the
    //      acceptor reads them.
    //
    //   2. NO ROUTING INTENT. The in-flight marker exists because a power cut
    //      mid-travel leaves a coin whose destination is unknown. A coin
    //      falling through a pinned chute has only one place to go, so there
    //      is nothing to reconcile and the in-flight ring is not written. The
    //      ring itself stays in the EEPROM layout for the return to
    //      RECIRCULATE.
    //
    //   3. THE COIN IS COUNTED AT ONCE, into the box, under its denomination.
    //
    // The caller writes the transaction record immediately after this returns.
    // A power cut between the two writes loses that one coin's credit while
    // the box count already includes it. RECIRCULATE has the same few
    // milliseconds of exposure between its intent write and the transaction
    // write. The hopper counts -- what change is promised against -- are not
    // touched by an inserted coin in this mode at all.
    // -------------------------------------------------------------------
    persist_inventory_add(coin_destination(coin), +1);
    return;
  }

  // Inhibit BEFORE the servo moves, not after. The gap between crediting a coin
  // and asserting the inhibit is exactly the window in which a second coin can
  // land mid-travel.
  coin_acceptor_window_inhibit();

  // GATES FIRST, then EEPROM. The coin is already falling; the in-flight write
  // costs ~30 ms and must not stand between it and the gates. If the gates
  // were aimed for this coin the moment it was identified, keep that decision
  // rather than re-reading the hopper IR a few ms later.
  s_coin = coin;
  s_dest = (coin == s_aim_coin) ? s_aim_dest : coin_destination(coin);
  s_aim_coin = COIN_NONE;
  gates_for(s_dest);
  s_started_ms = millis();
  s_state = DIV_ROUTING;

  // Cleared on settle above. A power cut in the few ms before this lands
  // leaves the coin uncounted -- the understating direction.
  persist_mark_coin_in_flight(coin);

#ifdef DEBUG
  Serial.print(F("[gate] "));
  Serial.print(coin == COIN_P1  ? F("P1")  : coin == COIN_P5  ? F("P5")
             : coin == COIN_P10 ? F("P10") : coin == COIN_P20 ? F("P20")
                                           : F("unknown"));
  Serial.print(F(" -> "));
  Serial.print(s_dest == DEST_P5_HOPPER ? F("P5 hopper (gate1 CLOSED)")
             : s_dest == DEST_P1_HOPPER ? F("P1 hopper (gate2 CLOSED)")
                                        : F("coin box (both OPEN)"));
  Serial.print(F("  P1full="));
  Serial.print(coin_level_hopper_full(HOPPER_P1) ? 1 : 0);
  Serial.print(F(" P5full="));
  Serial.println(coin_level_hopper_full(HOPPER_P5) ? 1 : 0);
#endif
}

bool coin_diverter_is_settled() {
  return s_state == DIV_IDLE;
}

bool coin_diverter_is_busy() {
  return s_state != DIV_IDLE || s_ex_steps != 0;
}

coin_dest_t coin_destination(coin_t coin) {
  return coin_route_full(COIN_ROUTING_MODE, coin,
                         coin_level_hopper_full(HOPPER_P1),
                         coin_level_hopper_full(HOPPER_P5));
}
