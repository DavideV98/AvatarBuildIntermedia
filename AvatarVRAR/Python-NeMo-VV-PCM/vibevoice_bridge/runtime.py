from __future__ import annotations

import copy
import os
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict

import torch
from transformers.utils import logging
from vibevoice.modular.streamer import AudioStreamer
from vibevoice.modular.modeling_vibevoice_streaming_inference import (
    VibeVoiceStreamingForConditionalGenerationInference,
)
from vibevoice.processor.vibevoice_streaming_processor import (
    VibeVoiceStreamingProcessor,
)

from .voice_registry import VoiceRegistry

logging.set_verbosity_info()
logger = logging.get_logger(__name__)


def env_bool(name: str, default: str = "0") -> bool:
    return os.getenv(name, default).strip().lower() in ("1", "true", "yes", "y", "on")


@dataclass
class RuntimeSettings:
    model_path: str
    voices_dir: Path
    device: str
    ddpm_steps: int
    default_cfg_scale: float
    default_chunk_ms: int
    unreal_pcm_url: str
    http_timeout_s: float
    sample_rate: int = 24000
    prewarm_enabled: bool = True
    prewarm_speaker_name: str = "it-spk0_woman"
    prewarm_text: str = "Ciao, questo è un prewarm."

    @staticmethod
    def from_env(base_dir: Path) -> "RuntimeSettings":
        auto_device = (
            "cuda"
            if torch.cuda.is_available()
            else ("mps" if torch.backends.mps.is_available() else "cpu")
        )

        return RuntimeSettings(
            model_path=os.getenv(
                "VIBEVOICE_MODEL_PATH",
                "microsoft/VibeVoice-Realtime-0.5B",
            ),
            voices_dir=Path(
                os.getenv(
                    "VIBEVOICE_VOICES_DIR",
                    str((base_dir / "voices").resolve()),
                )
            ).resolve(),
            device=os.getenv("VIBEVOICE_DEVICE", auto_device).lower(),
            ddpm_steps=int(os.getenv("VIBEVOICE_DDPM_STEPS", "5")),
            default_cfg_scale=float(os.getenv("VIBEVOICE_CFG_SCALE", "1.5")),
            default_chunk_ms=int(os.getenv("VIBEVOICE_CHUNK_MS", "33")),
            unreal_pcm_url=os.getenv(
                "UNREAL_PCM_URL",
                "http://127.0.0.1:7777/ace/audio_samples",
            ),
            http_timeout_s=float(os.getenv("UNREAL_HTTP_TIMEOUT_S", "60")),
            sample_rate=int(os.getenv("VIBEVOICE_SAMPLE_RATE", "24000")),
            prewarm_enabled=env_bool("VIBEVOICE_PREWARM", "1"),
            prewarm_speaker_name=os.getenv("VIBEVOICE_PREWARM_SPEAKER_NAME", "it-spk0_woman"),
            prewarm_text=os.getenv("VIBEVOICE_PREWARM_TEXT", "Ciao, questo è un prewarm."),
        )


class VibeVoiceRuntime:
    def __init__(self, settings: RuntimeSettings) -> None:
        self.settings = settings
        self.voice_registry = VoiceRegistry(settings.voices_dir)

        self.processor: VibeVoiceStreamingProcessor | None = None
        self.model: VibeVoiceStreamingForConditionalGenerationInference | None = None

        self._voice_prompt_cache: Dict[str, Dict[str, Any]] = {}
        self._prewarm_done = False

    def _resolve_device_and_dtype(self) -> tuple[str, torch.dtype, str]:
        device = self.settings.device

        if device == "mpx":
            device = "mps"

        if device == "mps":
            if not torch.backends.mps.is_available():
                device = "cpu"
                return device, torch.float32, "sdpa"
            return device, torch.float32, "sdpa"

        if device == "cuda":
            if not torch.cuda.is_available():
                device = "cpu"
                return device, torch.float32, "sdpa"
            return device, torch.bfloat16, "flash_attention_2"

        return "cpu", torch.float32, "sdpa"

    def load(self) -> None:
        if self.processor is not None and self.model is not None:
            return

        device, load_dtype, attn_impl = self._resolve_device_and_dtype()
        self.settings.device = device

        logger.info(
            "Loading VibeVoice processor/model | device=%s dtype=%s attn=%s model=%s",
            device,
            load_dtype,
            attn_impl,
            self.settings.model_path,
        )

        self.processor = VibeVoiceStreamingProcessor.from_pretrained(
            self.settings.model_path
        )

        try:
            if device == "mps":
                model = VibeVoiceStreamingForConditionalGenerationInference.from_pretrained(
                    self.settings.model_path,
                    torch_dtype=load_dtype,
                    attn_implementation=attn_impl,
                    device_map=None,
                )
                model.to("mps")
            elif device == "cuda":
                model = VibeVoiceStreamingForConditionalGenerationInference.from_pretrained(
                    self.settings.model_path,
                    torch_dtype=load_dtype,
                    device_map="cuda",
                    attn_implementation=attn_impl,
                )
            else:
                model = VibeVoiceStreamingForConditionalGenerationInference.from_pretrained(
                    self.settings.model_path,
                    torch_dtype=load_dtype,
                    device_map="cpu",
                    attn_implementation=attn_impl,
                )
        except Exception:
            if attn_impl != "flash_attention_2":
                raise

            logger.warning(
                "flash_attention_2 load failed; retry with sdpa",
                exc_info=True,
            )
            if device == "mps":
                model = VibeVoiceStreamingForConditionalGenerationInference.from_pretrained(
                    self.settings.model_path,
                    torch_dtype=load_dtype,
                    attn_implementation="sdpa",
                    device_map=None,
                )
                model.to("mps")
            else:
                model = VibeVoiceStreamingForConditionalGenerationInference.from_pretrained(
                    self.settings.model_path,
                    torch_dtype=load_dtype,
                    attn_implementation="sdpa",
                    device_map=device,
                )

        model.eval()
        model.set_ddpm_inference_steps(self.settings.ddpm_steps)

        self.model = model
        logger.info("VibeVoice ready")

    def get_voice_prompt(self, speaker_name: str) -> tuple[str, Dict[str, Any]]:
        self.load()

        voice_path = self.voice_registry.resolve(speaker_name)
        voice_key = voice_path.stem.lower()

        if voice_key not in self._voice_prompt_cache:
            logger.info("Loading voice preset: %s", voice_path)
            prompt = torch.load(
                str(voice_path),
                map_location=self.settings.device,
                weights_only=False,
            )
            self._voice_prompt_cache[voice_key] = prompt

        return voice_key, copy.deepcopy(self._voice_prompt_cache[voice_key])

    def build_inputs(
        self,
        *,
        text: str,
        cached_prompt: Dict[str, Any],
    ) -> Dict[str, Any]:
        assert self.processor is not None

        inputs = self.processor.process_input_with_cached_prompt(
            text=text,
            cached_prompt=cached_prompt,
            padding=True,
            return_tensors="pt",
            return_attention_mask=True,
        )

        target_device = self.settings.device
        for k, v in inputs.items():
            if torch.is_tensor(v):
                inputs[k] = v.to(target_device)

        return inputs

    def prewarm(self) -> None:
        if self._prewarm_done:
            return

        if not self.settings.prewarm_enabled:
            logger.info("VibeVoice prewarm disabled")
            return

        self.load()
        assert self.model is not None
        assert self.processor is not None

        text = (self.settings.prewarm_text or "").strip() or "Ciao."
        speaker_name = (self.settings.prewarm_speaker_name or "").strip() or "it-spk0_woman"

        logger.info(
            "Starting VibeVoice prewarm | speaker=%s text=%s",
            speaker_name,
            text,
        )

        _, cached_prompt = self.get_voice_prompt(speaker_name)
        inputs = self.build_inputs(text=text, cached_prompt=cached_prompt)

        audio_streamer = AudioStreamer(batch_size=1, stop_signal="__STOP__")
        generation_exc: list[BaseException] = []

        def _run_generate() -> None:
            try:
                with torch.inference_mode():
                    self.model.generate(
                        **inputs,
                        max_new_tokens=None,
                        cfg_scale=self.settings.default_cfg_scale,
                        tokenizer=self.processor.tokenizer,
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
            name="vibevoice-prewarm-worker",
            daemon=True,
        )
        worker.start()

        got_audio = False
        total_samples = 0

        for chunk in audio_streamer.get_stream(0):
            if chunk is None:
                continue
            if torch.is_tensor(chunk):
                numel = int(chunk.numel())
                total_samples += numel
                if numel > 0:
                    got_audio = True

        worker.join()

        if generation_exc:
            raise generation_exc[0]

        self._prewarm_done = True
        logger.info(
            "VibeVoice prewarm done | speaker=%s got_audio=%s samples=%d",
            speaker_name,
            got_audio,
            total_samples,
        )