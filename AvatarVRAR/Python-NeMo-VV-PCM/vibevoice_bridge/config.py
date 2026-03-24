from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(slots=True)
class Settings:
    model_path: str = os.getenv("VIBEVVOICE_MODEL_PATH", "microsoft/VibeVoice-Realtime-0.5B")
    device: str = os.getenv("VIBEVVOICE_DEVICE", "cuda")
    torch_dtype: str = os.getenv("VIBEVVOICE_TORCH_DTYPE", "bfloat16")
    attn_implementation: str = os.getenv("VIBEVVOICE_ATTN_IMPL", "flash_attention_2")
    ddpm_steps: int = int(os.getenv("VIBEVVOICE_DDPM_STEPS", "10"))
    cfg_scale: float = float(os.getenv("VIBEVVOICE_CFG_SCALE", "1.5"))
    voice_dir: Path = Path(os.getenv("VIBEVVOICE_VOICE_DIR", Path(__file__).resolve().parent / "voices"))
    sample_rate: int = int(os.getenv("VIBEVVOICE_SAMPLE_RATE", "24000"))
    unreal_timeout_s: float = float(os.getenv("VIBEVVOICE_UNREAL_TIMEOUT_S", "10.0"))


settings = Settings()
