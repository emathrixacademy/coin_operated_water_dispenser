#ifndef HMI_H
#define HMI_H

// Display link -- ESP32 LCD over UART.
//
// The ESP32 runs the UI on its own processor and draws everything itself.
// THE MEGA NEVER RENDERS. It sends short text lines over Serial2 and reads
// touch events back. No graphics work on the Mega -- CLAUDE.md.
//
// PROTOCOL. ASCII, one message per line, terminated by '\n' ('\r' ignored),
// fields separated by one space. Numbers are decimal integers.
//
//   Mega -> ESP32
//     PG <page>                       show page (page_t value)
//     TX <component> <text>           set a text field; text runs to end of line
//     VL <component> <int>            set a numeric field
//     OP <index> <0|1>                volume option enabled / dimmed
//     FT <fault>                      show fault (fault_t value, 0 = none)
//     VL state <n>                    raw state_t, so a screen can tell apart
//                                     states that share a page
//     LV <tank%> <tank_ok> <galA> <galB>
//                                     levels; galA/galB 1 = that gallon is low.
//                                     Sent on change and every
//                                     HMI_STATUS_RESEND_MS. The app shows a
//                                     small "gallon low" warning when either is
//                                     1 -- it is a notice, not a lockout.
//
//     SD <income> <volume> <count>    sales today (centavos, mL, purchases)
//     DT <year> <month> <day>         today's date, 0 0 0 with no clock
//     HS <n> <time> <in> <ml> <chg> <owed>
//                                     one recent purchase, n = 0 newest;
//                                     time is Unix seconds, 0 = no clock;
//                                     owed > 0: change not paid (in is 0)
//     HE                              end of the sales reply
//                                     (SD..HE are sent only when asked, by
//                                     EV 6 = HMI_EVENT_NAV_STATISTICS)
//
//   ESP32 -> Mega
//     EV <event> [payload]            touch event (hmi_event_t value)
//
// The numeric values of page_t, hmi_event_t and fault_t ARE the protocol.
// Append new values at the end; never renumber. The ESP32 sketch must be
// changed in the same commit as any of them.
//
// Fixed buffers only. No String, no dynamic allocation. Amounts are formatted
// from integer centavos into a char[] with snprintf -- there is no float
// anywhere in the display path either, because the value being displayed is the
// value being charged.

#include "types.h"

// Pages. Values are on the wire -- see PROTOCOL above.
enum page_t : uint8_t {
  PAGE_STANDBY = 0,
  PAGE_SELECT_VOLUME,
  PAGE_INSERT_BOTTLE,
  PAGE_DISPENSING,
  PAGE_WAITING,
  PAGE_THANK_YOU,
  PAGE_STATISTICS,
  PAGE_SYSTEM_STATUS,
  PAGE_COIN_INVENTORY,
  PAGE_ADMIN,
  PAGE_FAULT
};

// Touch events read back from the display. Values are on the wire.
enum hmi_event_t : uint8_t {
  HMI_EVENT_NONE = 0,
  HMI_EVENT_SELECT_VOLUME,   // payload = target in mL
  HMI_EVENT_DISPENSE_AGAIN,
  HMI_EVENT_FINISH,          // finish and take the change
  // The back arrow. Its meaning depends on the page it was pressed from, and
  // the state machine decides: from AWAITING_BOTTLE it cancels the selection
  // and returns to SELECTING with credit intact; from SELECTING it means
  // "I'm done, give me my money" and routes to PAYING_CHANGE.
  //
  // It NEVER abandons credit. See decisions.md, "Back arrow".
  HMI_EVENT_BACK,
  HMI_EVENT_NAV_HOME,
  HMI_EVENT_NAV_STATISTICS,
  HMI_EVENT_NAV_STATUS,
  HMI_EVENT_NAV_INVENTORY,
  HMI_EVENT_ADMIN_ENTER,
  HMI_EVENT_ADMIN_SET_P1,    // payload = count
  HMI_EVENT_ADMIN_SET_P5,    // payload = count
  HMI_EVENT_ADMIN_CONFIRM,   // explicit confirm for an inventory edit
  HMI_EVENT_ADMIN_CANCEL,
  HMI_EVENT_ADMIN_EXIT,
  // On-screen stand-in for the physical CONFIRM button (ACCEPTING ->
  // SELECTING). Not queued with the others -- see hmi_confirm_take().
  HMI_EVENT_CONFIRM
};

void hmi_begin();
void hmi_update();

// --- Output -------------------------------------------------------------

void hmi_setPage(page_t page);
page_t hmi_currentPage();

void hmi_setText(const char *component, const char *value);
void hmi_setValue(const char *component, int32_t value);

// Formats centavos as pesos with two decimals -- 2000 becomes "20.00" -- using
// integer division only.
void hmi_setAmount(const char *component, money_t centavos);

void hmi_setVolume(const char *component, volume_t ml);

// Progress bar during a pour, 0-100, computed by integer division.
void hmi_setProgress(const char *component, uint8_t percent);

// Grey out volume options above what the inserted credit can buy.
void hmi_setOptionEnabled(uint8_t option_index, bool enabled);

// Show a fault. The acceptor is disabled by faults before this is called --
// the screen is the last step, not the first.
void hmi_showFault(fault_t fault);

// --- Input --------------------------------------------------------------

// True for one update() cycle when a touch event has been decoded.
bool hmi_event_available();

// Returns the event and clears it. Payload is written to `payload` where the
// event carries one.
hmi_event_t hmi_take_event(int32_t *payload);

// True once per on-screen CONFIRM (or a typed "c" on the test console). Kept
// out of the event slot because ACCEPTING reads the button, not events: a
// CONFIRM parked in the slot would block the next real touch until some later
// state happened to take it.
bool hmi_confirm_take();

#ifdef SERIAL_CONSOLE
// Test console "skip 1": bench bypass of the OUT OF WATER and LOW CHANGE gates.
bool hmi_console_bypass();
#endif

// Push what the screen shows for this state: PG on a page change, and the VL
// fields state, credit, inserted, maxml, target, poured, total -- each only
// when it changed, and only when the TX buffer has room, so it never blocks the
// loop. Everything is re-sent every HMI_STATUS_RESEND_MS for a display that
// rebooted. Call once per loop, after the state machine.
void hmi_sync(state_t s);

// Change the last payout could not give, in centavos; 0 = none. Sent as
// "VL unpaid <n>" so the Thank You screen can say so.
void hmi_set_unpaid(money_t centavos);

#endif  // HMI_H
