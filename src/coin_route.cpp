// Coin routing truth table. No Arduino dependency by design -- see coin_route.h.

#include "coin_route.h"

// The box destination for a denomination. Shared by COLLECT_ALL, where every
// coin goes there, and by the power-loss reconciliation, which assumes it.
static coin_dest_t box_for(coin_t coin) {
  switch (coin) {
    case COIN_P1:  return DEST_BOX_P1;
    case COIN_P5:  return DEST_BOX_P5;
    case COIN_P10: return DEST_BOX_P10;
    case COIN_P20: return DEST_BOX_P20;
    // Anything unrecognised is in the box with no stated value. Defaulting to
    // the box rather than to a hopper keeps an odd coin out of the change
    // float: fail toward understating hopper stock.
    default:       return DEST_BOX_UNKNOWN;
  }
}

coin_dest_t coin_route(uint8_t mode, coin_t coin) {
  if (mode == COIN_ROUTING_COLLECT_ALL) {
    return box_for(coin);
  }

  // RECIRCULATE, and any mode value this firmware does not know. Falling
  // through to the full table is the safe reading of an unknown mode: it is
  // the configuration the cabinet is ultimately built for.
  switch (coin) {
    case COIN_P1:  return DEST_P1_HOPPER;
    case COIN_P5:  return DEST_P5_HOPPER;
    default:       return box_for(coin);
  }
}

coin_dest_t coin_route_unrouted(coin_t coin) {
  return box_for(coin);
}

bool coin_dest_is_hopper(coin_dest_t dest) {
  return dest == DEST_P1_HOPPER || dest == DEST_P5_HOPPER;
}
