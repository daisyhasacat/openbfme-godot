#!/usr/bin/env python3
"""Serve the repository's Lua reference: pip install -r tools/requirements-lua-docs.txt."""
from __future__ import annotations

import argparse
import html
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit
import webbrowser

try:
    import markdown
except ImportError:
    raise SystemExit("Install docs dependencies: pip install -r tools/requirements-lua-docs.txt")

ROOT = Path(__file__).resolve().parent.parent
STYLE = """
:root{color-scheme:light;--ink:#202f39;--muted:#5b6d76;--line:#dce3e6;--accent:#276653}
*{box-sizing:border-box}body{margin:0;background:#fafbf9;color:var(--ink);font:16px/1.7 system-ui,sans-serif}
a{color:var(--accent);text-underline-offset:3px}aside{position:fixed;inset:0 auto 0 0;width:290px;background:#eef2ed;border-right:1px solid var(--line);padding:28px 24px;overflow:auto}
.brand{font-weight:750;letter-spacing:.1em;font-size:13px}.subtitle{color:var(--muted);font-size:13px;margin:4px 0 22px}input{width:100%;padding:11px;border:1px solid #b9c8c0;border-radius:6px;font:inherit;background:white}
nav{font-size:13px;margin-top:20px}nav ul{list-style:none;padding-left:12px}nav>div>ul{padding:0}nav a{display:block;padding:4px 0;text-decoration:none}nav a:hover{text-decoration:underline}
main{margin-left:290px;max-width:1190px;padding:38px 60px 100px}.eyebrow{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}h1{font-size:38px;line-height:1.2;margin:16px 0 28px}h2{margin-top:52px;border-top:1px solid var(--line);padding-top:24px;font-size:25px}h3{margin-top:32px}h1,h2,h3{scroll-margin-top:24px}p{margin:16px 0}code{font: .88em/1.6 ui-monospace,SFMono-Regular,Consolas,monospace;background:#eaf0eb;padding:2px 5px;border-radius:3px}pre{position:relative;background:#172a31;color:#e6eee7;padding:22px;border-radius:8px;overflow:auto}pre code{background:none;padding:0;color:inherit}pre button{position:absolute;right:8px;top:8px;border:1px solid #728780;border-radius:4px;padding:4px 9px;background:#283f44;color:white;cursor:pointer}table{border-collapse:collapse;display:block;overflow:auto;font-size:14px;margin:22px 0}td,th{border:1px solid var(--line);padding:10px 13px;text-align:left;vertical-align:top}th{background:#eef2ed}blockquote{border-left:3px solid #86a595;margin-left:0;padding-left:20px;color:var(--muted)}.empty{display:none;color:var(--muted)}
@media(max-width:850px){aside{position:static;width:auto;max-height:330px;border-bottom:1px solid var(--line)}main{margin:0;padding:26px 22px 70px}h1{font-size:30px}}
"""
SCRIPT = """
const search=document.querySelector('input');
search.addEventListener('input',()=>{
 const term=search.value.toLowerCase().trim(); let visible=0;
 document.querySelectorAll('main section').forEach(s=>{s.hidden=!!term&&!s.textContent.toLowerCase().includes(term);if(!s.hidden)visible++});
 document.querySelector('.empty').style.display=visible?'none':'block';
});
document.querySelectorAll('pre').forEach(pre=>{const b=document.createElement('button');b.textContent='Copy';b.onclick=async()=>{try{await navigator.clipboard.writeText(pre.querySelector('code').textContent);b.textContent='Copied';setTimeout(()=>b.textContent='Copy',1500)}catch{b.textContent='Select to copy'}};pre.append(b)});
document.querySelectorAll('nav a').forEach(a=>a.addEventListener('click',()=>{search.value='';search.dispatchEvent(new Event('input'))}));
"""


def render(path: Path) -> bytes:
    md = markdown.Markdown(extensions=["fenced_code", "tables", "toc", "sane_lists"], extension_configs={"toc": {"toc_depth": "2-3"}})
    content = md.convert(path.read_text(encoding="utf-8"))
    # Group level-two sections so search retains each heading and its examples.
    content = '<section>' + content.replace('<h2 ', '</section><section><h2 ') + '</section>'
    title = html.escape(path.stem.replace('_', ' '))
    return f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{title} · OpenBFME Lua</title><style>{STYLE}</style></head><body><aside><div class="brand">OPENBFME / LUA</div><div class="subtitle">Scripting reference</div><label for="search">Search this page</label><input id="search" type="search" placeholder="Players, units, camera…"><nav aria-label="On this page">{md.toc}</nav></aside><main><div class="eyebrow">Local documentation · rendered from Markdown</div>{content}<p class="empty" role="status">No matching sections.</p></main><script>{SCRIPT}</script></body></html>'''.encode()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        route = unquote(urlsplit(self.path).path)
        if route == "/":
            self.send_response(302)
            self.send_header("Location", "/docs/BFME_LUA.md")
            self.end_headers()
            return
        path = (ROOT / route.lstrip("/")).resolve()
        allowed = any(path.is_relative_to(ROOT / folder) for folder in ("docs", "godot/dev"))
        if not allowed or path.suffix.lower() != ".md" or not path.is_file():
            self.send_error(404)
            return
        body = render(path)
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--open", action="store_true", help="Open the page in your default browser")
    args = parser.parse_args()
    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        url = f"http://127.0.0.1:{server.server_port}/docs/BFME_LUA.md"
        print(f"Lua documentation: {url}", flush=True)
        if args.open:
            webbrowser.open(url)
        server.serve_forever()


if __name__ == "__main__":
    main()
