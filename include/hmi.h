#ifndef HMI_H
#define HMI_H

// Nextion HMI -- serial commands and screen state.
//
// The Nextion runs the UI on its own processor and draws everything itself.
// THE MEGA NEVER RENDERS. It sends short commands over Serial2 and reads touch
// events. No graphics work on the Mega -- CLAUDE.md.
//
// Every command is terminated with three 0xFF bytes.
//
// Fixed buffers only. No String, no dynamic allocation. Amounts are formatted
// from integer centavos into a char[] with snprintf -- there is no float
// anywhere in the display path either, because the value being displayed is the
// value being charged.
//
// Firmware and HMI project versions must match. Adding a screen means changing
// hmi/watervendo.HMI as well as this module, in the same commit.

#include "types.h"

// Pages. Order and names must match the Nextion project.
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

// Touch events read back from the display.
enum hmi_event_t : uint8_t {
  HMI_EVENT_NONE = 0,
  HMI_EVENT_SELECT_VOLUME,   // payload = target in mL
  HMI_EVENT_DISPENSE_AGAIN,
  HMI_EVENT_FINISH,          // finish and take the change
  HMI_EVENT_NAV_HOME,
  HMI_EVENT_NAV_STATISTICS,
  HMI_EVENT_NAV_STATUS,
  HMI_EVENT_NAV_INVENTORY,
  HMI_EVENT_ADMIN_ENTER,
  HMI_EVENT_ADMIN_SET_P1,    // payload = count
  HMI_EVENT_ADMIN_SET_P5,    // payload = count
  HMI_EVENT_ADMIN_CONFIRM,   // explicit confirm for an inventory edit
  HMI_EVENT_ADMIN_CANCEL,
  HMI_EVENT_ADMIN_EXIT
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

#endif  // HMI_H
