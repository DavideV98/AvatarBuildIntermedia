from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Callable, Optional


class GreetingRequestServer:
    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 8011,
        route: str = "/greeting/start",
        on_request: Optional[Callable[[dict], tuple[int, dict]]] = None,
    ) -> None:
        self.host = host
        self.port = port
        self.route = route if route.startswith("/") else ("/" + route)
        self.on_request = on_request

        self._httpd: HTTPServer | None = None
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        server_ref = self

        class Handler(BaseHTTPRequestHandler):
            def _send(self, code: int, payload: dict) -> None:
                body = json.dumps(payload).encode("utf-8")
                self.send_response(code)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self) -> None:
                if self.path not in (server_ref.route, server_ref.route + "/"):
                    self._send(404, {"ok": False, "error": "not_found"})
                    return

                try:
                    length = int(self.headers.get("Content-Length", "0"))
                except ValueError:
                    self._send(400, {"ok": False, "error": "invalid_content_length"})
                    return

                raw = self.rfile.read(length).decode("utf-8", errors="replace")

                try:
                    data = json.loads(raw) if raw else {}
                except Exception:
                    self._send(400, {"ok": False, "error": "invalid_json"})
                    return

                if not isinstance(data, dict):
                    self._send(400, {"ok": False, "error": "json_object_required"})
                    return

                if server_ref.on_request is None:
                    self._send(500, {"ok": False, "error": "server_not_configured"})
                    return

                try:
                    code, payload = server_ref.on_request(data)
                except Exception as exc:
                    self._send(
                        500,
                        {
                            "ok": False,
                            "error": "internal_error",
                            "detail": str(exc),
                        },
                    )
                    return

                self._send(code, payload)

            def do_GET(self) -> None:
                if self.path in ("/health", "/healthz"):
                    self._send(
                        200,
                        {
                            "ok": True,
                            "service": "greeting_http_server",
                            "route": server_ref.route,
                        },
                    )
                    return

                self._send(404, {"ok": False, "error": "not_found"})

            def log_message(self, format, *args):
                return

        self._httpd = HTTPServer((self.host, self.port), Handler)
        self._thread = threading.Thread(
            target=self._httpd.serve_forever,
            name="GreetingRequestServer",
            daemon=True,
        )
        self._thread.start()

        print(f"👋 Greeting server: http://{self.host}:{self.port}{self.route}")

    def stop(self) -> None:
        if self._httpd is not None:
            try:
                self._httpd.shutdown()
                self._httpd.server_close()
            finally:
                self._httpd = None

        if self._thread is not None:
            try:
                self._thread.join(timeout=1.0)
            finally:
                self._thread = None