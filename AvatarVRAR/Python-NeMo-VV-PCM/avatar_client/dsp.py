# preprocess audio
import wave
import numpy as np

def write_wav_pcm16_mono(path: str, pcm16_bytes: bytes, sample_rate: int) -> None:
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm16_bytes)

def highpass_1pole(x: np.ndarray, sr: int, cutoff_hz: float = 80.0) -> np.ndarray:
    if cutoff_hz <= 0:
        return x
    rc = 1.0 / (2.0 * np.pi * cutoff_hz)
    dt = 1.0 / sr
    alpha = rc / (rc + dt)

    y = np.zeros_like(x, dtype=np.float32)
    prev_y = 0.0
    prev_x = 0.0
    for i in range(len(x)):
        xi = float(x[i])
        yi = alpha * (prev_y + xi - prev_x)
        y[i] = yi
        prev_y, prev_x = yi, xi
    return y

def noise_gate(x: np.ndarray, threshold: float = 0.01) -> np.ndarray:
    if threshold <= 0:
        return x
    y = x.copy()
    y[np.abs(y) < threshold] = 0.0
    return y

def normalize_soft(x: np.ndarray, target_peak: float = 0.9) -> np.ndarray:
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    if peak <= 1e-9:
        return x
    gain = min(target_peak / peak, 3.0)
    return (x * gain).astype(np.float32)

def preprocess_mic_pcm16(pcm16: np.ndarray, sr: int, hp_cutoff: float, gate_thr: float) -> np.ndarray:
    x = pcm16.astype(np.float32) / 32768.0
    x = highpass_1pole(x, sr, cutoff_hz=hp_cutoff)
    x = noise_gate(x, threshold=gate_thr)
    x = normalize_soft(x, target_peak=0.9)
    x = np.clip(x, -1.0, 1.0)
    return (x * 32767.0).astype(np.int16)