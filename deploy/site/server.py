"""Project page for the water vending machine (Railway).

Serves public/ as static files, the client documents from docs/ (Markdown rendered as a
readable page, everything else as-is), and /health for Railway's health check.
Only the files in DOCS are served from docs/: internal documents never leave the repo.
"""

from __future__ import annotations

import html
import mimetypes
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parent
PUBLIC = ROOT / "public"
DOCS_DIR = Path(os.environ.get("DOCS_DIR", ROOT.parent.parent / "docs"))

# Client-facing documents. Anything not listed here is not served.
DOCS = {
    "SPECIFICATION.md": "Specification",
    "scenarios.md": "Test scenarios",
    "calibration.md": "Calibration manual",
    "wiring.md": "Wiring notes",
    "wiring-asbuilt.html": None,
    "Water-Vendo-Wiring.pdf": None,
    "Emathrix_FabDrawing_CoinRouting_RevB.pdf": None,
    "WATER-VENDO.pdf": None,
}

PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>{title}</title>
<style>
:root{{--ink:#14212b;--muted:#5d6b76;--line:#d3e2e6;--brand:#0f5f73;--bg:#fff;--code:#f2f6f8}}
@media (prefers-color-scheme:dark){{:root{{--ink:#e6edf2;--muted:#9fb0bb;--line:#2b3b44;--brand:#4fb3cc;--bg:#0f171c;--code:#17262e}}}}
body{{margin:0;background:var(--bg);color:var(--ink);font:16px/1.6 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}}
main{{max-width:860px;margin:0 auto;padding:24px 16px 56px}}
a{{color:var(--brand)}} h1,h2,h3{{line-height:1.25}} h1{{color:var(--brand)}}
table{{border-collapse:collapse;display:block;overflow-x:auto;margin:12px 0}}
th,td{{border:1px solid var(--line);padding:6px 10px;text-align:left;vertical-align:top}}
code,pre{{background:var(--code);border-radius:6px;font-size:.9em}} code{{padding:1px 5px}} pre{{padding:12px;overflow-x:auto}}
pre code{{padding:0;background:none}} img{{max-width:100%}}
</style></head><body><main><p><a href="../">← Back to the project page</a></p>{body}</main></body></html>"""


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(PUBLIC), **kwargs)

    def end_headers(self) -> None:
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        if path == "/health":
            self._send(200, b"ok", "text/plain; charset=utf-8")
            return
        if path.startswith("/docs/"):
            self._doc(path[len("/docs/"):])
            return
        super().do_GET()

    def _doc(self, name: str) -> None:
        if name not in DOCS or not (DOCS_DIR / name).is_file():
            self._send(404, b"Not found", "text/plain; charset=utf-8")
            return
        f = DOCS_DIR / name
        if name.endswith(".md"):
            body = markdown.markdown(f.read_text(encoding="utf-8"), extensions=["tables", "fenced_code", "sane_lists"])
            page = PAGE.format(title=html.escape(DOCS[name] or name), body=body)
            self._send(200, page.encode("utf-8"), "text/html; charset=utf-8")
            return
        ctype = mimetypes.guess_type(name)[0] or "application/octet-stream"
        if ctype.startswith("text/"):
            ctype += "; charset=utf-8"
        self._send(200, f.read_bytes(), ctype)

    def _send(self, code: int, data: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main() -> None:
    port = int(os.environ.get("PORT", "8080"))
    print(f"Serving on port {port}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
