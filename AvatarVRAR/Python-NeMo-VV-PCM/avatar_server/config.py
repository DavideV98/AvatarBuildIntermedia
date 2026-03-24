import os


def env_bool(name: str, default: str = "0") -> bool:
    return os.getenv(name, default).strip().lower() in ("1", "true", "yes", "y", "on")


NEMO_STT_MODEL = os.getenv(
    "NEMO_STT_MODEL",
    "nvidia/stt_it_fastconformer_hybrid_large_pc",
)

WAV_DIR = os.getenv("WAV_DIR", "/work/recordings")
ALLOW_TF32 = env_bool("ALLOW_TF32", "1")

# TranscribeConfig standardizzato ma leggero
STT_BATCH_SIZE = int(os.getenv("STT_BATCH_SIZE", "1"))
STT_NUM_WORKERS = int(os.getenv("STT_NUM_WORKERS", "0"))
STT_RETURN_HYPOTHESES = env_bool("STT_RETURN_HYPOTHESES", "0")
STT_TIMESTAMPS = env_bool("STT_TIMESTAMPS", "0")
STT_USE_LHOTSE = env_bool("STT_USE_LHOTSE", "0")
STT_VERBOSE = env_bool("STT_VERBOSE", "0")

# None = disabilitato. Se un giorno vuoi demandare a NeMo il multicanale:
# es. "average" oppure "0"
_raw_channel_selector = os.getenv("STT_CHANNEL_SELECTOR", "").strip()
STT_CHANNEL_SELECTOR = _raw_channel_selector if _raw_channel_selector else None

STT_WARMUP = env_bool("STT_WARMUP", "1")
STT_WARMUP_DURATION_S = float(os.getenv("STT_WARMUP_DURATION_S", "1.0"))