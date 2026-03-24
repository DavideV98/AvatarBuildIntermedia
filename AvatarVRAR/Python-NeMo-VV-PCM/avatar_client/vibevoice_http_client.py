from __future__ import annotations

from typing import Any, Dict

import requests


REQUIRED_RESPONSE_KEYS = (
    "ok",
    "trace_id",
    "utterance_id",
    "speaker_used",
    "sample_rate",
    "num_chunks_sent",
    "first_chunk_latency_ms",
    "total_time_ms",
)


def vibevoice_tts_to_unreal(
    session: requests.Session,
    vibevoice_url: str,
    text: str,
    trace_id: str,
    utterance_id: str,
    speaker_name: str,
    cfg_scale: float,
    chunk_ms: int,
    timeout_s: float = 180.0,
) -> Dict[str, Any]:
    clean_text = (text or "").strip()
    if not clean_text:
        raise ValueError("Testo TTS vuoto")

    url = vibevoice_url.rstrip("/") + "/tts_to_unreal"
    payload = {
        "text": clean_text,
        "trace_id": trace_id,
        "utterance_id": utterance_id,
        "speaker_name": speaker_name,
        "cfg_scale": float(cfg_scale),
        "chunk_ms": int(chunk_ms),
    }

    response = session.post(url, json=payload, timeout=timeout_s)

    if not response.ok:
        raise RuntimeError(
            f"VibeVoice Bridge error {response.status_code}: {response.text}"
        )

    data = response.json()
    if not isinstance(data, dict):
        raise RuntimeError(
            f"Bridge VibeVoice: risposta JSON non valida: {data!r}"
        )

    for key in REQUIRED_RESPONSE_KEYS:
        if key not in data:
            raise RuntimeError(
                f"Bridge VibeVoice: manca '{key}' nella risposta"
            )

    return data