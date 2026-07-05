#!/usr/bin/env python3
"""Serve editor.html and read/write server_content/ via REST API."""

from __future__ import annotations

import argparse
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any
from urllib.parse import unquote, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(ROOT, "tools")
CONTENT_DIR = os.path.join(ROOT, "server_content")

# Keep in sync with FILE_META / FILE_ORDER in tools/editor.html
KNOWN_FILES = (
    "npcs.json",
    "spawns.json",
    "worlds.json",
    "portals.json",
    "quests.json",
    "skills.json",
    "craft_recipes.json",
    "scenarios.json",
    "shop_items.json",
    "achievements.json",
    "duties.json",
    "abilities.json",
    "traits.json",
    "mmo/mmo_items.json",
    "mmo/mmo_mobs.json",
    "mmo/world_spawns.json",
    "mmo/mini_events.json",
    "mmo/world_boss.json",
    "td/enemies.json",
    "td/effects.json",
    "td/index.json",
)


def resolve_content_path(editor_path: str) -> str:
    editor_path = editor_path.replace("\\", "/").lstrip("/")
    if editor_path not in KNOWN_FILES:
        raise ValueError(f"unknown file: {editor_path}")
    full = os.path.normpath(os.path.join(CONTENT_DIR, editor_path))
    content_root = os.path.abspath(CONTENT_DIR)
    if not os.path.abspath(full).startswith(content_root + os.sep) and os.path.abspath(full) != content_root:
        raise ValueError("path traversal blocked")
    return full


def json_response(handler: BaseHTTPRequestHandler, status: int, payload: Any) -> None:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


class EditorHandler(BaseHTTPRequestHandler):
    server_version = "TDAEditorServer/1.0"

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("[editor] %s - %s\n" % (self.address_string(), fmt % args))

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = unquote(parsed.path)

        if path in ("/", "/editor", "/editor.html"):
            return self._serve_editor()
        if path == "/api/status":
            return json_response(
                self,
                200,
                {
                    "ok": True,
                    "content_dir": CONTENT_DIR,
                    "files": len(KNOWN_FILES),
                },
            )
        if path == "/api/files":
            return self._api_list_files()
        if path.startswith("/api/file/"):
            rel = path[len("/api/file/") :]
            return self._api_read_file(rel)

        self.send_error(404)

    def do_PUT(self) -> None:
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"

        try:
            data = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError as exc:
            return json_response(self, 400, {"ok": False, "error": f"invalid json: {exc}"})

        if path == "/api/save-all":
            return self._api_save_all(data)
        if path.startswith("/api/file/"):
            rel = path[len("/api/file/") :]
            return self._api_write_file(rel, data)

        self.send_error(404)

    def _serve_editor(self) -> None:
        editor_path = os.path.join(TOOLS_DIR, "editor.html")
        if not os.path.isfile(editor_path):
            self.send_error(404, "editor.html not found")
            return
        with open(editor_path, "rb") as fh:
            body = fh.read()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _api_list_files(self) -> None:
        items = []
        for rel in KNOWN_FILES:
            full = resolve_content_path(rel)
            items.append({"path": rel, "exists": os.path.isfile(full)})
        json_response(self, 200, items)

    def _api_read_file(self, rel: str) -> None:
        try:
            full = resolve_content_path(rel)
        except ValueError as exc:
            return json_response(self, 400, {"ok": False, "error": str(exc)})

        if not os.path.isfile(full):
            return json_response(self, 404, {"ok": False, "error": "file not found"})

        try:
            with open(full, encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, json.JSONDecodeError) as exc:
            return json_response(self, 500, {"ok": False, "error": str(exc)})

        json_response(self, 200, {"ok": True, "path": rel, "data": data})

    def _api_write_file(self, rel: str, data: Any) -> None:
        try:
            full = resolve_content_path(rel)
        except ValueError as exc:
            return json_response(self, 400, {"ok": False, "error": str(exc)})

        try:
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w", encoding="utf-8", newline="\n") as fh:
                json.dump(data, fh, ensure_ascii=False, indent=4)
                fh.write("\n")
        except OSError as exc:
            return json_response(self, 500, {"ok": False, "error": str(exc)})

        json_response(self, 200, {"ok": True, "path": rel})

    def _api_save_all(self, batch: Any) -> None:
        if not isinstance(batch, dict):
            return json_response(self, 400, {"ok": False, "error": "expected object map"})

        saved = 0
        failed = 0
        errors: list[str] = []

        for rel, data in batch.items():
            try:
                full = resolve_content_path(str(rel))
                os.makedirs(os.path.dirname(full), exist_ok=True)
                with open(full, "w", encoding="utf-8", newline="\n") as fh:
                    json.dump(data, fh, ensure_ascii=False, indent=4)
                    fh.write("\n")
                saved += 1
            except Exception as exc:  # noqa: BLE001 — report all write failures
                failed += 1
                errors.append(f"{rel}: {exc}")

        json_response(
            self,
            200 if failed == 0 else 207,
            {"ok": failed == 0, "saved": saved, "failed": failed, "errors": errors},
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="TeeDefenceArchive content editor API server")
    parser.add_argument("--host", default="127.0.0.1", help="bind address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="port (default: 8765)")
    args = parser.parse_args()

    if not os.path.isdir(CONTENT_DIR):
        print(f"error: server_content not found: {CONTENT_DIR}", file=sys.stderr)
        return 1

    httpd = HTTPServer((args.host, args.port), EditorHandler)
    url = f"http://{args.host}:{args.port}/"
    print(f"Content editor API running at {url}")
    print(f"Writing to: {CONTENT_DIR}")
    print("Press Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
