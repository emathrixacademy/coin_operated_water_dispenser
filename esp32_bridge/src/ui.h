// The on-board touchscreen. Built only with -DHAS_LCD (env:cyd35); on the plain
// test bridge these are empty and the web page is the only screen.

#pragma once

void ui_begin();
void ui_changed();   // the model changed; redraw what depends on it
void ui_loop();      // touch polling and timed redraws; call every loop
