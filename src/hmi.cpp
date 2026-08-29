#include <Arduino.h>
#include "hmi.h"

// Milestone 2: contract only. Implementation lands in Milestone 6.
//
// Implementation notes:
//   - Fixed char[HMI_TX_BUFFER]. No String, no dynamic allocation.
//   - Amounts format from integer centavos with snprintf("%ld.%02ld") using
//     integer division and modulo. No float in the display path -- the number
//     shown is the number charged.
//   - RX is parsed incrementally in update(). Never block waiting for a byte.
//   - Refresh during a pour is rate-limited to HMI_DISPENSE_REFRESH_MS so the
//     serial line is not saturated while flow pulses arrive on an interrupt.

static page_t s_page = PAGE_STANDBY;

static void hmi_terminate() {
  for (uint8_t i = 0; i < HMI_TERMINATOR_COUNT; i++) {
    HMI_SERIAL.write((uint8_t)HMI_TERMINATOR_BYTE);
  }
}

void hmi_begin() {
  HMI_SERIAL.begin(HMI_BAUD);
  hmi_terminate();  // flush any partial command left in the display's parser
}

void hmi_update() {
  // TODO(M6): incremental RX parse, rate-limited refresh.
}

void hmi_setPage(page_t page) {
  s_page = page;
  // TODO(M6)
}

page_t hmi_currentPage() {
  return s_page;
}

void hmi_setText(const char *component, const char *value) {
  (void)component;
  (void)value;
  // TODO(M6)
}

void hmi_setValue(const char *component, int32_t value) {
  (void)component;
  (void)value;
  // TODO(M6)
}

void hmi_setAmount(const char *component, money_t centavos) {
  (void)component;
  (void)centavos;
  // TODO(M6): integer format only.
}

void hmi_setVolume(const char *component, volume_t ml) {
  (void)component;
  (void)ml;
  // TODO(M6)
}

void hmi_setProgress(const char *component, uint8_t percent) {
  (void)component;
  (void)percent;
  // TODO(M6)
}

void hmi_setOptionEnabled(uint8_t option_index, bool enabled) {
  (void)option_index;
  (void)enabled;
  // TODO(M6)
}

void hmi_showFault(fault_t fault) {
  (void)fault;
  // TODO(M6): the acceptor is already inhibited by faults before this is called.
}

bool hmi_event_available() {
  return false;
}

hmi_event_t hmi_take_event(int32_t *payload) {
  if (payload) *payload = 0;
  return HMI_EVENT_NONE;
}
