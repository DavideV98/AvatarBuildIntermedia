import torch
import numpy as np
from nemo.collections.asr.models import EncDecHybridRNNTCTCBPEModel

from avatar_server.config import (
    ALLOW_TF32,
    NEMO_STT_MODEL,
    STT_WARMUP,
    STT_WARMUP_DURATION_S,
)


def get_device() -> str:
    return "cuda" if torch.cuda.is_available() else "cpu"


def get_cuda_name() -> str:
    try:
        if torch.cuda.is_available():
            return torch.cuda.get_device_name(0)
    except Exception:
        pass
    return ""


def setup_perf(device: str) -> None:
    if device == "cuda" and ALLOW_TF32:
        try:
            torch.backends.cuda.matmul.allow_tf32 = True
            torch.backends.cudnn.allow_tf32 = True
            torch.set_float32_matmul_precision("high")
        except Exception:
            pass


def load_asr(device: str):
    asr = EncDecHybridRNNTCTCBPEModel.from_pretrained(
        model_name=NEMO_STT_MODEL
    ).to(device)
    asr.eval()

    try:
        decoding_cfg = asr.cfg.aux_ctc.decoding
        decoding_cfg.strategy = "greedy_batch"
        asr.change_decoding_strategy(
            decoder_type="ctc",
            decoding_cfg=decoding_cfg,
        )
    except TypeError:
        asr.change_decoding_strategy(decoder_type="ctc")

    return asr


def warmup_asr(asr, device: str, duration_s: float = 1.0, sample_rate: int = 16000) -> None:
    try:
        n = int(duration_s * sample_rate)
        dummy_audio = np.zeros((n,), dtype=np.float32)

        with torch.inference_mode():
            _ = asr.transcribe(audio=[dummy_audio], batch_size=1)
            if device == "cuda":
                torch.cuda.synchronize()

        print(f"✅ STT warmup done. ({duration_s:.1f}s dummy audio @ {sample_rate} Hz)")
    except Exception as e:
        print(f"⚠️ STT warmup failed: {e}")


DEVICE = get_device()
setup_perf(DEVICE)

print(f"🚀 Device: {DEVICE}" + (f" ({get_cuda_name()})" if DEVICE == "cuda" else ""))
print(f"⏳ Loading STT model: {NEMO_STT_MODEL}")

ASR_MODEL = load_asr(DEVICE)

print("✅ STT ready!")

if STT_WARMUP:
    print("🔥 STT warmup...")
    warmup_asr(ASR_MODEL, DEVICE, duration_s=STT_WARMUP_DURATION_S)