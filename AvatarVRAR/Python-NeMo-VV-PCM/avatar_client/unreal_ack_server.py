from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import threading
from typing import Dict, Optional


class UnrealAckServer:
    """
    Unreal POST su:
      http://<host>:<port><route>

    Eventi accettati:
      {"event":"playback_done","trace_id":"...","utterance_id":"..."}
      {"event":"utterance_done","trace_id":"...","utterance_id":"..."}  # compat legacy
    """

    VALID_EVENTS = {"playback_done", "utterance_done"}

    def __init__(self, host: str = "127.0.0.1", port: int = 9998, route: str = "/playback_done"):
        self.host = host
        self.port = port
        self.route = route if route.startswith("/") else ("/" + route)

        self._httpd: Optional[HTTPServer] = None
        self._thread: Optional[threading.Thread] = None

        self._lock = threading.Lock()
        self._events: Dict[str, threading.Event] = {}
        self._payloads: Dict[str, dict] = {}

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
                    return self._send(404, {"ok": False, "error": "not_found"})

                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length).decode("utf-8", errors="replace")

                try:
                    data = json.loads(raw) if raw else {}
                except Exception:
                    return self._send(400, {"ok": False, "error": "invalid_json"})

                event_name = data.get("event")
                if event_name not in server_ref.VALID_EVENTS:
                    return self._send(400, {"ok": False, "error": "bad_event"})

                utterance_id = (
                    data.get("utterance_id")
                    or data.get("utt")
                    or ""
                )
                if not utterance_id:
                    return self._send(400, {"ok": False, "error": "missing_utterance_id"})

                with server_ref._lock:
                    server_ref._payloads[utterance_id] = data
                    ev = server_ref._events.get(utterance_id)
                    if ev:
                        ev.set()

                return self._send(200, {"ok": True})

            def log_message(self, format, *args):
                return

        self._httpd = HTTPServer((self.host, self.port), Handler)
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()

        print(f"🔔 Unreal ACK server: http://{self.host}:{self.port}{self.route}")

    def stop(self) -> None:
        if self._httpd is not None:
            self._httpd.shutdown()
            self._httpd.server_close()
            self._httpd = None

        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None

    def register(self, utterance_id: str) -> None:
        with self._lock:
            self._events[utterance_id] = threading.Event()
            self._payloads.pop(utterance_id, None)

    def wait(self, utterance_id: str, timeout_s: float) -> bool:
        with self._lock:
            ev = self._events.get(utterance_id)

        if ev is None:
            return False

        return ev.wait(timeout=timeout_s)

    def payload(self, utterance_id: str) -> Optional[dict]:
        with self._lock:
            return self._payloads.get(utterance_id)

    def clear(self, utterance_id: str) -> None:
        with self._lock:
            self._events.pop(utterance_id, None)
            self._payloads.pop(utterance_id, None)