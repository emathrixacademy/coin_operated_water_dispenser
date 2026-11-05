"""Copy the ESP32 phone page (esp32_bridge/src/page.h) into the simulation, unchanged.

The page is the one the machine serves at http://192.168.4.1 from its WiFi hotspot. In the
simulation a stand-in socket (phone/link.js) feeds it the simulated machine's lines instead.

  git show origin/fix/review-bugs-layout5:esp32_bridge/src/page.h > page.h
  python deploy/site/tools/extract_phone_page.py page.h deploy/site/public/simulation/phone/index.html
"""
import re
import sys

src, out = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()
m = re.search(r'R"HTML\((.*)\)HTML"', text, re.S)
if not m:
    sys.exit("PAGE_HTML raw string not found")
html = m.group(1)
# The stand-in socket must load before the page's own script opens its WebSocket.
html = html.replace("<script>", '<script src="link.js"></script>\n<script>', 1)
html = html.replace("<!doctype html>", "<!doctype html>\n<!-- Generated from esp32_bridge/src/page.h by deploy/site/tools/extract_phone_page.py. Do not edit. -->", 1)
open(out, "w", encoding="utf-8", newline="\n").write(html)
print(f"wrote {out} ({len(html)} chars)")
