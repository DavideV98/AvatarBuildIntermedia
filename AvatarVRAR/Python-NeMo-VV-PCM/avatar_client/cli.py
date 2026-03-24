from __future__ import annotations

import argparse
import os

from dotenv import load_dotenv


def env_bool(name: str, default: str = "0") -> bool:
    return os.getenv(name, default).strip().lower() in (
        "1",
        "true",
        "yes",
        "y",
        "on",
    )


def build_parser() -> argparse.ArgumentParser:
    load_dotenv()

    ap = argparse.ArgumentParser(
        description="Avatar client: Mic -> NeMo STT -> LLM/RAG -> VibeVoice bridge -> Unreal ACK"
    )

    # Device / audio capture
    ap.add_argument("--list-devices", action="store_true")
    ap.add_argument(
        "--device",
        type=int,
        default=None if os.getenv("DEVICE") is None else int(os.getenv("DEVICE")),
    )
    ap.add_argument(
        "--channels",
        type=int,
        default=int(os.getenv("CHANNELS", "2")),
        choices=[1, 2],
    )
    ap.add_argument(
        "--samplerate",
        type=int,
        default=int(os.getenv("SAMPLERATE", "48000")),
    )

    # NeMo STT
    ap.add_argument(
        "--nemo_url",
        default=os.getenv("NEMO_URL", "http://127.0.0.1:8000"),
    )
    ap.add_argument(
        "--stt_timeout_s",
        type=float,
        default=float(os.getenv("STT_TIMEOUT_S", "60.0")),
    )

    # Output / logging
    ap.add_argument("--out_dir", default=os.getenv("OUT_DIR", "recordings"))
    ap.add_argument("--save_txt", action="store_true")
    ap.add_argument("--log_file", default=os.getenv("LOG_FILE", "latency_log.jsonl"))
    ap.add_argument("--trace_print", action="store_true")

    # VAD
    ap.add_argument(
        "--frame_ms",
        type=int,
        default=int(os.getenv("FRAME_MS", "30")),
        choices=[10, 20, 30],
    )
    ap.add_argument(
        "--vad_aggr",
        type=int,
        default=int(os.getenv("VAD_AGGR", "2")),
        choices=[0, 1, 2, 3],
    )
    ap.add_argument(
        "--pre_roll_ms",
        type=int,
        default=int(os.getenv("PRE_ROLL_MS", "300")),
    )
    ap.add_argument(
        "--silence_ms",
        type=int,
        default=int(os.getenv("SILENCE_MS", "700")),
    )
    ap.add_argument(
        "--min_speech_ms",
        type=int,
        default=int(os.getenv("MIN_SPEECH_MS", "500")),
    )
    ap.add_argument(
        "--max_segment_s",
        type=float,
        default=float(os.getenv("MAX_SEGMENT_S", "12.0")),
    )

    # DSP
    ap.add_argument(
        "--hp_cutoff",
        type=float,
        default=float(os.getenv("HP_CUTOFF", "80.0")),
    )
    ap.add_argument(
        "--gate",
        type=float,
        default=float(os.getenv("GATE", "0.008")),
    )

    # OpenAI / orchestrator
    ap.add_argument("--openai_key", default=os.getenv("OPENAI_API_KEY"))
    ap.add_argument(
        "--router_model",
        default=os.getenv("ANGY_ROUTER_MODEL", "gpt-4o-mini"),
    )
    ap.add_argument(
        "--agent_model",
        default=os.getenv("ANGY_AGENT_MODEL", "gpt-4o-mini"),
    )
    ap.add_argument(
        "--embed_model",
        default=os.getenv("ANGY_EMBED_MODEL", "text-embedding-3-small"),
    )

    # RAG
    ap.add_argument("--rag_dir", default=os.getenv("RAG_DIR", "rag"))
    ap.add_argument(
        "--rag_index",
        default=os.getenv("RAG_INDEX", "contenuti_faiss_index.faiss"),
    )
    ap.add_argument(
        "--rag_metadata",
        default=os.getenv("RAG_METADATA", "contenuti_metadata.pkl"),
    )
    ap.add_argument(
        "--rag_k",
        type=int,
        default=int(os.getenv("RAG_TOP_K", "5")),
    )

    # VibeVoice bridge
    ap.add_argument(
        "--vibevoice_url",
        default=os.getenv("VIBEVOICE_URL", "http://127.0.0.1:8001"),
    )
    ap.add_argument(
        "--vibevoice_timeout_s",
        type=float,
        default=float(os.getenv("VIBEVOICE_TIMEOUT_S", "180.0")),
    )
    ap.add_argument(
        "--vv_speaker_name",
        default=os.getenv("VV_SPEAKER_NAME", "it-spk0_woman"),
        help="Speaker logico da passare al bridge VibeVoice",
    )
    ap.add_argument(
        "--vv_cfg_scale",
        type=float,
        default=float(os.getenv("VIBEVOICE_CFG_SCALE", "1.5")),
    )
    ap.add_argument(
        "--vv_chunk_ms",
        type=int,
        default=int(os.getenv("VIBEVOICE_CHUNK_MS", "33")),
    )

    # Unreal ACK
    ap.add_argument("--ack_host", default=os.getenv("ACK_HOST", "127.0.0.1"))
    ap.add_argument(
        "--ack_port",
        type=int,
        default=int(os.getenv("ACK_PORT", "9998")),
    )
    ap.add_argument(
        "--ack_route",
        default=os.getenv("ACK_ROUTE", "/playback_done"),
    )
    ap.add_argument(
        "--ack_timeout_s",
        type=float,
        default=float(os.getenv("ACK_TIMEOUT_S", "120.0")),
    )
    ap.add_argument(
        "--mic_resume_pad_ms",
        type=int,
        default=int(os.getenv("MIC_RESUME_PAD_MS", "150")),
    )

    # Mic gating
    ap.add_argument(
        "--disable_mic_gating",
        dest="disable_mic_gating",
        action="store_true",
    )
    ap.add_argument(
        "--enable_mic_gating",
        dest="disable_mic_gating",
        action="store_false",
    )
    ap.set_defaults(disable_mic_gating=env_bool("DISABLE_MIC_GATING", "0"))

    return ap