from __future__ import annotations

import base64
from dataclasses import dataclass

import httpx
import numpy as np


@dataclass
class UnrealPostResult:
    status_code: int
    body: str


class UnrealPCMSender:
    def __init__(self, unreal_pcm_url: str, timeout_s: float = 60.0) -> None:
        self.unreal_pcm_url = unreal_pcm_url
        self.timeout_s = timeout_s
        self._client = httpx.Client(timeout=self.timeout_s)

    def close(self) -> None:
        self._client.close()

    @staticmethod
    def _float32_to_b64(samples: np.ndarray) -> str:
        if samples.dtype != np.float32:
            samples = samples.astype(np.float32, copy=False)
        raw = samples.tobytes(order="C")
        return base64.b64encode(raw).decode("ascii")

    def post_chunk(
        self,
        *,
        trace_id: str,
        utterance_id: str,
        chunk_index: int,
        chunk_total: int,
        is_last: bool,
        sample_rate: int,
        num_channels: int,
        samples: np.ndarray,
    ) -> UnrealPostResult:
        if samples.dtype != np.float32:
            samples = samples.astype(np.float32, copy=False)

        payload = {
            "trace_id": trace_id,
            "utterance_id": utterance_id,
            "chunk_index": int(chunk_index),
            "chunk_total": int(chunk_total),
            "is_last": bool(is_last),
            "sample_rate": int(sample_rate),
            "num_channels": int(num_channels),
            "samples_b64": self._float32_to_b64(samples),
        }

        resp = self._client.post(self.unreal_pcm_url, json=payload)

        status_code = resp.status_code
        body = resp.text

        # Anche se Unreal risponde HTTP 200, consideriamo fallimento un JSON {"ok": false}
        if status_code < 400:
            try:
                data = resp.json()
            except Exception:
                return UnrealPostResult(
                    status_code=500,
                    body=f"Unreal response is not valid JSON: {resp.text[:500]}",
                )

            if not isinstance(data, dict):
                return UnrealPostResult(
                    status_code=500,
                    body=f"Unreal response JSON is not an object: {data!r}",
                )

            if not data.get("ok", False):
                return UnrealPostResult(
                    status_code=500,
                    body=resp.text,
                )

        return UnrealPostResult(status_code=status_code, body=body)