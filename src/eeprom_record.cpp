#include "eeprom_record.h"

// No Arduino dependency by design -- see eeprom_record.h.

uint8_t record_crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    uint8_t in = *data++;
    for (uint8_t i = 8; i; i--) {
      const uint8_t mix = (uint8_t)((crc ^ in) & 0x01);
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      in >>= 1;
    }
  }
  return crc;
}

void record_pack(uint8_t *buf, const uint8_t *payload, uint8_t len) {
  buf[0] = (uint8_t)(EEPROM_MAGIC & 0xFF);
  buf[1] = (uint8_t)((EEPROM_MAGIC >> 8) & 0xFF);
  buf[2] = (uint8_t)EEPROM_LAYOUT_VERSION;
  buf[3] = record_crc8(payload, len);
  for (uint8_t i = 0; i < len; i++) buf[RECORD_OVERHEAD + i] = payload[i];
}

bool record_unpack(const uint8_t *buf, uint8_t *payload, uint8_t len) {
  // Magic first. A virgin cell reads 0xFF and an erased one 0x00; both are
  // rejected here before a single payload byte is trusted.
  const uint16_t magic = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  if (magic != EEPROM_MAGIC) return false;

  // A record written by a firmware with a different layout is not readable,
  // even though its magic matches. Reading it would misinterpret every field.
  if (buf[2] != (uint8_t)EEPROM_LAYOUT_VERSION) return false;

  // CRC last: catches a cell that has degraded since a genuinely valid write.
  if (record_crc8(&buf[RECORD_OVERHEAD], len) != buf[3]) return false;

  for (uint8_t i = 0; i < len; i++) payload[i] = buf[RECORD_OVERHEAD + i];
  return true;
}
