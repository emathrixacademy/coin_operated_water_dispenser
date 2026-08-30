#ifndef EEPROM_RECORD_H
#define EEPROM_RECORD_H

// Validated EEPROM record framing.
//
// Every persisted record is wrapped as:
//
//   [magic lo][magic hi][layout version][crc8]  payload...
//
// The pack/unpack pair below operates on plain byte buffers and has NO Arduino
// dependency, so the validation path -- including the first-boot virgin-cell
// case -- is exercised by the host-side unit tests rather than only being
// observable on hardware with a soldering iron.
//
// WHY THIS EXISTS:
//
// A virgin AVR EEPROM cell reads 0xFF. Without framing, an inventory record
// read from a fresh chip is 65535 coins in each hopper, and the machine
// confidently believes it can make change it does not have. The magic word
// rejects that case before any byte of payload is trusted, and the CRC catches
// a cell that has degraded after a valid write.
//
// Order of checks matters: magic, then version, then CRC. Magic first means an
// all-0xFF or all-0x00 region is rejected immediately without the CRC ever
// being consulted.

#include <stdint.h>
#include <stddef.h>
#include "config.h"

// Bytes of framing added in front of every payload.
#define RECORD_OVERHEAD 4

// Dallas/Maxim CRC-8, polynomial 0x8C reflected. Small and adequate for
// catching a degraded cell; this is integrity checking, not cryptography.
uint8_t record_crc8(const uint8_t *data, uint8_t len);

// Write framing + payload into `buf`, which must hold len + RECORD_OVERHEAD.
void record_pack(uint8_t *buf, const uint8_t *payload, uint8_t len);

// Validate framing in `buf` and copy the payload out.
//
// Returns false -- and leaves `payload` untouched -- if the magic word does not
// match (virgin or foreign cells), the layout version differs (a firmware
// upgrade that changed the record shape), or the CRC fails (a degraded cell).
//
// A false return means INITIALISE, never "use it anyway".
bool record_unpack(const uint8_t *buf, uint8_t *payload, uint8_t len);

#endif  // EEPROM_RECORD_H
