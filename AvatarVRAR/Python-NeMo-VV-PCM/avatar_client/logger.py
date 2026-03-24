import json
import os
import time
from datetime import datetime
from typing import Optional, Dict, Any


def now_iso() -> str:
    return datetime.now().isoformat(timespec="milliseconds")


def log_event(
    log_path: str,
    trace_id: str,
    step: str,
    t0: float,
    extra: Optional[Dict[str, Any]] = None,
) -> None:
    evt = {
        "ts": now_iso(),
        "trace_id": trace_id,
        "step": step,
        "t_rel_ms": round((time.perf_counter() - t0) * 1000, 2),
    }

    if extra:
        evt.update(extra)

    log_dir = os.path.dirname(log_path)
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)

    with open(log_path, "a", encoding="utf-8") as f:
        f.write(json.dumps(evt, ensure_ascii=False, default=str) + "\n")

    print(f"[{evt['t_rel_ms']:8.2f} ms] {step}" + (f" | {extra}" if extra else ""))