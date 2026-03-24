from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import time
from datetime import datetime

LOG_FILE = "latency_log.jsonl"


def now_iso():
    return datetime.now().isoformat(timespec="milliseconds")


class Handler(BaseHTTPRequestHandler):
    def _send(self, code: int, payload: dict):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path not in ("/event", "/event/"):
            return self._send(404, {"ok": False, "error": "not_found"})

        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length).decode("utf-8", errors="replace")

        try:
            data = json.loads(raw) if raw else {}
        except Exception:
            return self._send(400, {"ok": False, "error": "invalid_json"})

        evt = {
            "ts": now_iso(),
            "t_abs_ms": int(time.time() * 1000),
            "trace_id": data.get("trace_id", ""),
            "step": data.get("step", "unreal.event"),
        }

        for k, v in data.items():
            if k not in ("ts", "t_abs_ms", "trace_id", "step"):
                evt[k] = v

        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(json.dumps(evt, ensure_ascii=False, default=str) + "\n")

        return self._send(200, {"ok": True})

    def log_message(self, format, *args):
        return


def main():
    host = "0.0.0.0"
    port = 9988
    httpd = HTTPServer((host, port), Handler)
    print(f"Latency sink in ascolto su http://{host}:{port}/event")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()