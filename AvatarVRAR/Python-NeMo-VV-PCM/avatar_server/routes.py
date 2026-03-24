import os
import time
import tempfile

import numpy as np
import soundfile as sf
import torch
from fastapi import APIRouter, File, Form, Request, UploadFile
from fastapi.responses import JSONResponse
from nemo.collections.asr.parts.mixins import TranscribeConfig

from avatar_server.audio_utils import to_mono_16k
from avatar_server.config import (
    NEMO_STT_MODEL,
    WAV_DIR,
    STT_BATCH_SIZE,
    STT_NUM_WORKERS,
    STT_RETURN_HYPOTHESES,
    STT_TIMESTAMPS,
    STT_USE_LHOTSE,
    STT_VERBOSE,
    STT_CHANNEL_SELECTOR,
)
from avatar_server.models import ASR_MODEL, DEVICE, get_cuda_name

router = APIRouter()


def build_transcribe_config() -> TranscribeConfig:
    cfg = TranscribeConfig()
    cfg.batch_size = STT_BATCH_SIZE
    cfg.num_workers = STT_NUM_WORKERS
    cfg.return_hypotheses = STT_RETURN_HYPOTHESES
    cfg.timestamps = STT_TIMESTAMPS
    cfg.use_lhotse = STT_USE_LHOTSE
    cfg.verbose = STT_VERBOSE
    cfg.channel_selector = STT_CHANNEL_SELECTOR
    return cfg


@router.get("/health")
def health():
    return {
        "status": "ok",
        "device": DEVICE,
        "cuda_available": bool(torch.cuda.is_available()),
        "cuda_name": get_cuda_name(),
        "wav_dir": WAV_DIR,
        "stt_model": NEMO_STT_MODEL,
        "transcribe_cfg": {
            "batch_size": STT_BATCH_SIZE,
            "num_workers": STT_NUM_WORKERS,
            "return_hypotheses": STT_RETURN_HYPOTHESES,
            "timestamps": STT_TIMESTAMPS,
            "use_lhotse": STT_USE_LHOTSE,
            "verbose": STT_VERBOSE,
            "channel_selector": STT_CHANNEL_SELECTOR,
        },
    }


@router.post("/stt")
async def stt(
    request: Request,
    file: UploadFile = File(...),
    trace_id: str = Form(""),
):
    effective_trace_id = trace_id or request.headers.get("X-Trace-Id", "")

    filename = (file.filename or "").lower()
    if not filename.endswith((".wav", ".wave")):
        return JSONResponse(
            status_code=400,
            content={"error": "Formato non supportato.", "trace_id": effective_trace_id},
        )

    t0 = time.perf_counter()

    try:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as tmp:
            tmp.write(await file.read())
            tmp.flush()

            t_read0 = time.perf_counter()
            audio, sr = sf.read(tmp.name, dtype="float32", always_2d=True)
            t_read1 = time.perf_counter()

            t_res0 = time.perf_counter()
            audio_16k_mono = to_mono_16k(audio, sr, target_sr=16000)
            audio_16k_mono = np.ascontiguousarray(audio_16k_mono, dtype=np.float32)
            t_res1 = time.perf_counter()

            transcribe_cfg = build_transcribe_config()

            t_inf0 = time.perf_counter()
            outputs = ASR_MODEL.transcribe(
                audio=[audio_16k_mono],
                override_config=transcribe_cfg,
            )
            if DEVICE == "cuda":
                torch.cuda.synchronize()
            t_inf1 = time.perf_counter()

        t1 = time.perf_counter()

        # Con return_hypotheses=False ci aspettiamo stringhe.
        # Manteniamo comunque un fallback prudente.
        text = ""
        if outputs:
            first = outputs[0]
            if isinstance(first, str):
                text = first
            elif hasattr(first, "text"):
                text = first.text or ""
            else:
                text = str(first)

        return {
            "text": text,
            "trace_id": effective_trace_id,
            "timings": {
                "read_wav_ms": round((t_read1 - t_read0) * 1000, 2),
                "resample_ms": round((t_res1 - t_res0) * 1000, 2),
                "stt_infer_ms": round((t_inf1 - t_inf0) * 1000, 2),
                "total_ms": round((t1 - t0) * 1000, 2),
            },
        }

    except Exception as e:
        msg = str(e)
        if "illegal memory access" in msg.lower():
            os._exit(1)

        return JSONResponse(
            status_code=500,
            content={"error": msg, "trace_id": effective_trace_id},
        )