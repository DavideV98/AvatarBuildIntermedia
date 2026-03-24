 # VAD + segment capture
import queue
import time
from dataclasses import dataclass
from typing import Optional, Tuple, List

import numpy as np
import sounddevice as sd
import webrtcvad

@dataclass
class VadConfig:
    samplerate: int
    frame_ms: int
    vad_aggr: int
    pre_roll_ms: int
    silence_ms: int
    min_speech_ms: int
    max_segment_s: float

class MicVadSegmenter:
    def __init__(self, device: Optional[int], channels: int, cfg: VadConfig):
        self.device = device
        self.channels = channels
        self.cfg = cfg

        self.frame_samples = int(cfg.samplerate * cfg.frame_ms / 1000)
        if self.frame_samples <= 0:
            raise ValueError("frame_samples non valido")

        self.pre_roll_frames = max(1, cfg.pre_roll_ms // cfg.frame_ms)
        self.silence_frames = max(1, cfg.silence_ms // cfg.frame_ms)

        self.vad = webrtcvad.Vad(cfg.vad_aggr)
        self.q: "queue.Queue[np.ndarray]" = queue.Queue()

        self.triggered = False
        self.ring_buffer: List[Tuple[bytes, bool]] = []
        self.voiced_frames: List[bytes] = []
        self.segment_start_wall: Optional[float] = None

        self.last_trigger_perf: Optional[float] = None
        self.stream: Optional[sd.InputStream] = None

    def _callback(self, indata, frames, time_info, status):
        data = indata.copy()
        if data.ndim == 2 and data.shape[1] == 2:
            mono = ((data[:, 0].astype(np.int32) + data[:, 1].astype(np.int32)) // 2).astype(np.int16)
        else:
            mono = data.reshape(-1).astype(np.int16)
        self.q.put(mono)

    def start(self):
        self.stream = sd.InputStream(
            samplerate=self.cfg.samplerate,
            channels=self.channels,
            dtype="int16",
            blocksize=self.frame_samples,
            device=self.device,
            callback=self._callback,
        )
        self.stream.start()

    def stop(self):
        if self.stream is not None:
            try:
                self.stream.stop()
            finally:
                self.stream.close()
            self.stream = None

    def reset_state(self):
        self.triggered = False
        self.ring_buffer = []
        self.voiced_frames = []
        self.segment_start_wall = None
        self.last_trigger_perf = None
        while not self.q.empty():
            try:
                self.q.get_nowait()
            except Exception:
                break

    def next_segment(self) -> Tuple[bytes, float]:
        while True:
            mono_pcm16 = self.q.get()
            frame_bytes = mono_pcm16.tobytes()
            is_speech = self.vad.is_speech(frame_bytes, self.cfg.samplerate)

            if not self.triggered:
                self.ring_buffer.append((frame_bytes, is_speech))
                if len(self.ring_buffer) > self.pre_roll_frames:
                    self.ring_buffer.pop(0)

                num_speech = sum(1 for _, s in self.ring_buffer if s)
                if len(self.ring_buffer) >= self.pre_roll_frames and (num_speech / len(self.ring_buffer)) >= 0.6:
                    self.triggered = True
                    self.segment_start_wall = time.perf_counter()
                    self.last_trigger_perf = self.segment_start_wall
                    self.voiced_frames = [b for b, _ in self.ring_buffer]
                    self.ring_buffer = []
                    print("🟢 start segmento")
            else:
                self.voiced_frames.append(frame_bytes)
                self.ring_buffer.append((frame_bytes, is_speech))
                if len(self.ring_buffer) > self.silence_frames:
                    self.ring_buffer.pop(0)

                num_speech = sum(1 for _, s in self.ring_buffer if s)
                mostly_silence = (len(self.ring_buffer) >= self.silence_frames) and (num_speech / len(self.ring_buffer) <= 0.2)
                too_long = (time.perf_counter() - (self.segment_start_wall or time.perf_counter())) >= self.cfg.max_segment_s

                if mostly_silence or too_long:
                    self.triggered = False
                    end_perf = time.perf_counter()
                    rec_time = end_perf - (self.segment_start_wall or end_perf)
                    duration_ms = int(len(self.voiced_frames) * self.cfg.frame_ms)

                    self.ring_buffer = []
                    seg_bytes = b"".join(self.voiced_frames)
                    self.voiced_frames = []
                    self.segment_start_wall = None

                    if duration_ms < self.cfg.min_speech_ms:
                        print(f"🟡 scartato ({duration_ms} ms) troppo corto\n")
                        continue

                    print(f"🔴 fine segmento ({duration_ms} ms)")
                    return seg_bytes, rec_time