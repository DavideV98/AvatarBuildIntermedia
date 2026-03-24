from __future__ import annotations

import base64
from typing import Iterable

import numpy as np
import torch


def tensor_chunk_to_numpy_mono_float32(chunk: torch.Tensor | np.ndarray | Iterable[float]) -> np.ndarray:
    if isinstance(chunk, torch.Tensor):
        arr = chunk.detach().cpu().float().numpy()
    elif isinstance(chunk, np.ndarray):
        arr = chunk.astype(np.float32, copy=False)
    else:
        arr = np.asarray(list(chunk), dtype=np.float32)

    arr = np.squeeze(arr)
    if arr.ndim == 0:
        arr = arr.reshape(1)
    if arr.ndim > 1:
        arr = arr.reshape(-1)
    return np.ascontiguousarray(arr, dtype=np.float32)


def encode_float32_pcm_b64(samples: np.ndarray) -> str:
    raw = np.ascontiguousarray(samples, dtype=np.float32).tobytes()
    return base64.b64encode(raw).decode("ascii")
