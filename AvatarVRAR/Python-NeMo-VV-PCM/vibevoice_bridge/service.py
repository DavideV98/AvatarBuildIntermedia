from __future__ import annotations

import logging
import threading
import time
from collections import deque
from dataclasses import dataclass

import numpy as np
import torch

from vibevoice.modular.streamer import AudioStreamer

from .runtime import VibeVoiceRuntime
from .unreal_sender import UnrealPCMSender

logger = logging.getLogger(__name__)


@dataclass
class SynthesisResult:
    ok: bool
    trace_id: str
    utterance_id: str
    speaker_used: str
    sample_rate: int
    num_chunks_sent: int
    first_chunk_latency_ms: float | None
    total_time_ms: float


class VibeVoiceBridgeService:
    def __init__(
        self,
        runtime: VibeVoiceRuntime,
        sender: UnrealPCMSender,
    ) -> None:
        self.runtime = runtime
        self.sender = sender
        self._generation_lock = threading.Lock()

    @staticmethod
    def _tensor_to_float32_mono(chunk: torch.Tensor) -> np.ndarray:
        arr = chunk.detach().float().cpu().numpy()
        arr = np.asarray(arr, dtype=np.float32)
        arr = np.squeeze(arr)

        if arr.ndim == 0:
            arr = arr.reshape(1)

        if arr.ndim > 1:
            arr = arr.reshape(-1)

        return arr.astype(np.float32, copy=False)

    def _post_chunk(
        self,
        *,
        trace_id: str,
        utterance_id: str,
        chunk_index: int,
        chunk_total: int,
        is_last: bool,
        sample_rate: int,
        samples: np.ndarray,
    ) -> None:
        resp = self.sender.post_chunk(
            trace_id=trace_id,
            utterance_id=utterance_id,
            chunk_index=chunk_index,
            chunk_total=chunk_total,
            is_last=is_last,
            sample_rate=sample_rate,
            num_channels=1,
            samples=samples,
        )
        if resp.status_code >= 400:
            raise RuntimeError(
                f"Unreal PCM POST failed: code={resp.status_code} body={resp.body}"
            )

    def synthesize_to_unreal(
        self,
        *,
        text: str,
        trace_id: str,
        utterance_id: str,
        speaker_name: str,
        cfg_scale: float,
        chunk_ms: int,
    ) -> SynthesisResult:
        started_at = time.perf_counter()

        with self._generation_lock:
            self.runtime.load()
            assert self.runtime.model is not None
            assert self.runtime.processor is not None

            speaker_used, cached_prompt = self.runtime.get_voice_prompt(speaker_name)
            inputs = self.runtime.build_inputs(
                text=text,
                cached_prompt=cached_prompt,
            )

            audio_streamer = AudioStreamer(batch_size=1, stop_signal="__STOP__")
            generation_exc: list[BaseException] = []

            def _run_generate() -> None:
                try:
                    with torch.inference_mode():
                        self.runtime.model.generate(
                            **inputs,
                            max_new_tokens=None,
                            cfg_scale=cfg_scale,
                            tokenizer=self.runtime.processor.tokenizer,
                            generation_config={"do_sample": False},
                            verbose=False,
                            return_speech=False,
                            audio_streamer=audio_streamer,
                            all_prefilled_outputs=cached_prompt,
                        )
                except BaseException as exc:
                    generation_exc.append(exc)
                    try:
                        audio_streamer.end()
                    except Exception:
                        pass

            worker = threading.Thread(
                target=_run_generate,
                name="vibevoice-generate-worker",
                daemon=True,
            )
            worker.start()

            sample_rate = int(self.runtime.settings.sample_rate)
            samples_per_chunk = max(1, int(sample_rate * (chunk_ms / 1000.0)))

            pending_samples = np.zeros((0,), dtype=np.float32)
            ready_chunks: deque[np.ndarray] = deque()
            chunk_index = 0
            first_chunk_latency_ms: float | None = None

            for chunk in audio_streamer.get_stream(0):
                np_chunk = self._tensor_to_float32_mono(chunk)
                if np_chunk.size == 0:
                    continue

                pending_samples = np.concatenate([pending_samples, np_chunk])

                while pending_samples.size >= samples_per_chunk:
                    ready = pending_samples[:samples_per_chunk].copy()
                    pending_samples = pending_samples[samples_per_chunk:]
                    ready_chunks.append(ready)

                # Manteniamo sempre un chunk in look-ahead per sapere
                # con certezza quale sarà l'ultimo.
                while len(ready_chunks) > 1:
                    to_send = ready_chunks.popleft()

                    self._post_chunk(
                        trace_id=trace_id,
                        utterance_id=utterance_id,
                        chunk_index=chunk_index,
                        chunk_total=0,
                        is_last=False,
                        sample_rate=sample_rate,
                        samples=to_send,
                    )

                    if first_chunk_latency_ms is None:
                        first_chunk_latency_ms = (
                            time.perf_counter() - started_at
                        ) * 1000.0

                    chunk_index += 1

            worker.join()

            if generation_exc:
                raise generation_exc[0]

            if pending_samples.size > 0:
                ready_chunks.append(pending_samples.copy())

            if not ready_chunks:
                raise RuntimeError("VibeVoice non ha prodotto audio")

            total_remaining = len(ready_chunks)
            while len(ready_chunks) > 1:
                to_send = ready_chunks.popleft()

                self._post_chunk(
                    trace_id=trace_id,
                    utterance_id=utterance_id,
                    chunk_index=chunk_index,
                    chunk_total=chunk_index + total_remaining,
                    is_last=False,
                    sample_rate=sample_rate,
                    samples=to_send,
                )

                if first_chunk_latency_ms is None:
                    first_chunk_latency_ms = (
                        time.perf_counter() - started_at
                    ) * 1000.0

                chunk_index += 1
                total_remaining = len(ready_chunks)

            last_chunk = ready_chunks.popleft()
            final_total = chunk_index + 1

            self._post_chunk(
                trace_id=trace_id,
                utterance_id=utterance_id,
                chunk_index=chunk_index,
                chunk_total=final_total,
                is_last=True,
                sample_rate=sample_rate,
                samples=last_chunk,
            )

            if first_chunk_latency_ms is None:
                first_chunk_latency_ms = (time.perf_counter() - started_at) * 1000.0

            chunk_index += 1
            total_time_ms = (time.perf_counter() - started_at) * 1000.0

            logger.info(
                "VV_DONE trace=%s utt=%s speaker=%s chunks=%d total_ms=%.2f",
                trace_id,
                utterance_id,
                speaker_used,
                chunk_index,
                total_time_ms,
            )

            return SynthesisResult(
                ok=True,
                trace_id=trace_id,
                utterance_id=utterance_id,
                speaker_used=speaker_used,
                sample_rate=sample_rate,
                num_chunks_sent=chunk_index,
                first_chunk_latency_ms=first_chunk_latency_ms,
                total_time_ms=total_time_ms,
            )