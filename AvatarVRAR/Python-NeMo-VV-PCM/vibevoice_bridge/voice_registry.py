from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List


@dataclass(frozen=True)
class VoiceInfo:
    key: str
    path: Path


class VoiceRegistry:
    def __init__(self, voices_dir: str | Path) -> None:
        self.voices_dir = Path(voices_dir).resolve()
        self._voices: Dict[str, VoiceInfo] = {}
        self.reload()

    def reload(self) -> None:
        self._voices.clear()
        if not self.voices_dir.exists():
            return

        for pt_file in sorted(self.voices_dir.rglob("*.pt")):
            key = pt_file.stem.lower()
            self._voices[key] = VoiceInfo(key=key, path=pt_file.resolve())

    def list_keys(self) -> List[str]:
        return sorted(self._voices.keys())

    def resolve(self, speaker_name: str) -> Path:
        if not self._voices:
            raise RuntimeError(
                f"Nessuna voice .pt trovata in: {self.voices_dir}"
            )

        requested = (speaker_name or "").strip().lower()
        if not requested:
            raise ValueError("speaker_name vuoto")

        exact = self._voices.get(requested)
        if exact is not None:
            return exact.path

        partial_matches = []
        for key, info in self._voices.items():
            if requested in key or key in requested:
                partial_matches.append(info.path)

        if len(partial_matches) == 1:
            return partial_matches[0]

        if len(partial_matches) > 1:
            raise ValueError(
                f"speaker_name ambiguo: {speaker_name!r}. "
                f"Match trovati: {', '.join(self.list_keys())}"
            )

        raise ValueError(
            f"Voce non trovata: {speaker_name!r}. "
            f"Voci disponibili: {', '.join(self.list_keys())}"
        )