import numpy as np


def to_mono_16k(audio: np.ndarray, sr: int, target_sr: int = 16000) -> np.ndarray:
    if audio.ndim == 1:
        audio = audio[:, None]

    audio = audio.mean(axis=1)

    if sr != target_sr:
        n_src = audio.shape[0]
        n_tgt = int(round(n_src * (target_sr / sr)))
        if n_tgt <= 0:
            return np.zeros((0,), dtype=np.float32)

        x_old = np.linspace(0.0, 1.0, num=n_src, endpoint=False)
        x_new = np.linspace(0.0, 1.0, num=n_tgt, endpoint=False)
        audio = np.interp(x_new, x_old, audio).astype(np.float32)
    else:
        audio = audio.astype(np.float32)

    return np.nan_to_num(audio, nan=0.0, posinf=0.0, neginf=0.0).astype(np.float32)