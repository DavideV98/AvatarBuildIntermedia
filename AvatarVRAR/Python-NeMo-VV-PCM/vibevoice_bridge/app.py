from __future__ import annotations

import logging
from pathlib import Path

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

from .runtime import RuntimeSettings, VibeVoiceRuntime
from .service import VibeVoiceBridgeService
from .unreal_sender import UnrealPCMSender

load_dotenv()

logger = logging.getLogger(__name__)

BASE_DIR = Path(__file__).resolve().parent
SETTINGS = RuntimeSettings.from_env(BASE_DIR)
RUNTIME = VibeVoiceRuntime(SETTINGS)
SENDER = UnrealPCMSender(
    unreal_pcm_url=SETTINGS.unreal_pcm_url,
    timeout_s=SETTINGS.http_timeout_s,
)
SERVICE = VibeVoiceBridgeService(RUNTIME, SENDER)

app = FastAPI(title="VibeVoice Bridge", version="0.1.0")


class TTSBridgeRequest(BaseModel):
    text: str = Field(..., min_length=1)
    trace_id: str = Field(..., min_length=1)
    utterance_id: str = Field(..., min_length=1)
    speaker_name: str = Field(default="it-spk0_woman")
    cfg_scale: float | None = Field(default=None, ge=0.0, le=10.0)
    chunk_ms: int | None = Field(default=None, ge=20, le=2000)


class TTSBridgeResponse(BaseModel):
    ok: bool
    trace_id: str
    utterance_id: str
    speaker_used: str
    sample_rate: int
    num_chunks_sent: int
    first_chunk_latency_ms: float | None
    total_time_ms: float


@app.on_event("startup")
def _startup() -> None:
    if not SETTINGS.prewarm_enabled:
        logger.info("VibeVoice startup: prewarm disabled")
        return

    try:
        RUNTIME.prewarm()
    except Exception:
        logger.exception("VibeVoice prewarm failed")


@app.on_event("shutdown")
def _shutdown() -> None:
    SENDER.close()


@app.get("/health")
def health() -> dict:
    return {
        "ok": True,
        "model_path": SETTINGS.model_path,
        "device": SETTINGS.device,
        "voices_dir": str(SETTINGS.voices_dir),
        "voices": RUNTIME.voice_registry.list_keys(),
        "unreal_pcm_url": SETTINGS.unreal_pcm_url,
        "sample_rate": SETTINGS.sample_rate,
        "ddpm_steps": SETTINGS.ddpm_steps,
        "default_cfg_scale": SETTINGS.default_cfg_scale,
        "default_chunk_ms": SETTINGS.default_chunk_ms,
        "prewarm_enabled": SETTINGS.prewarm_enabled,
        "prewarm_speaker_name": SETTINGS.prewarm_speaker_name,
        "prewarm_done": getattr(RUNTIME, "_prewarm_done", False),
    }


@app.get("/voices")
def voices() -> dict:
    return {
        "ok": True,
        "voices": RUNTIME.voice_registry.list_keys(),
    }


@app.post("/tts_to_unreal", response_model=TTSBridgeResponse)
def tts_to_unreal(req: TTSBridgeRequest) -> TTSBridgeResponse:
    try:
        result = SERVICE.synthesize_to_unreal(
            text=req.text,
            trace_id=req.trace_id,
            utterance_id=req.utterance_id,
            speaker_name=req.speaker_name,
            cfg_scale=(
                SETTINGS.default_cfg_scale
                if req.cfg_scale is None
                else req.cfg_scale
            ),
            chunk_ms=(
                SETTINGS.default_chunk_ms
                if req.chunk_ms is None
                else req.chunk_ms
            ),
        )
        return TTSBridgeResponse(**result.__dict__)
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc