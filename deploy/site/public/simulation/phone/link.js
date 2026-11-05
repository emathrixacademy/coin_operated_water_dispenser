"use strict";
// Stand-in for the ESP32's WebSocket (port 81). The phone page next to this file is the
// machine's own page, unchanged; this hands it the simulated machine's lines and passes its
// touches back, through the simulation page that holds this one in a frame.
(() => {
  const link = window.parent && window.parent.vendoPhoneLink;
  class SimSocket {
    constructor() {
      this.readyState = 0;
      this.onmessage = null; this.onclose = null;
      setTimeout(() => { this.readyState = 1; if (link) link.attach(this); }, 50);
    }
    send(text) { if (link) link.receive(this, String(text)); }
    close() { this.readyState = 3; if (link) link.detach(this); }
    deliver(line) { if (this.onmessage) this.onmessage({ data: line }); }
  }
  window.WebSocket = SimSocket;
})();
