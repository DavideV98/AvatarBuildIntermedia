from __future__ import annotations

import argparse
import json
import os
import time
import uuid
from pathlib import Path
from typing import Any, Dict, List

import requests
from dotenv import load_dotenv

from avatar_client.angy_multiagent import AngyMultiAgent, RagConfig, LlmConfig
from avatar_client.chunker import split_text_for_tts


def build_parser() -> argparse.ArgumentParser:
    load_dotenv()

    ap = argparse.ArgumentParser(
        description="Test end-to-end: sample.wav -> NeMo STT -> LLM -> VibeVoice Bridge (/tts_to_unreal)"
    )
    ap.add_argument("--sample_wav", default=os.getenv("SAMPLE_WAV", "sample.wav"))
    ap.add_argument("--out_dir", default=os.getenv("TEST_OUT_DIR", "test_outputs"))

    ap.add_argument("--nemo_url", default=os.getenv("NEMO_URL", "http://127.0.0.1:8000"))
    ap.add_argument("--vibevoice_url", default=os.getenv("VIBEVOICE_URL", "http://127.0.0.1:8001"))

    ap.add_argument("--language", default=os.getenv("LANGUAGE", "it"))
    ap.add_argument("--vv_speaker_name", default=os.getenv("VV_SPEAKER_NAME", "it-spk0_woman"))
    ap.add_argument("--vv_cfg_scale", type=float, default=float(os.getenv("VIBEVOICE_CFG_SCALE", "1.5")))
    ap.add_argument("--vv_chunk_ms", type=int, default=int(os.getenv("VIBEVOICE_CHUNK_MS", "33")))

    ap.add_argument("--tts_chunking", action="store_true")
    ap.add_argument("--tts_chunk_chars", type=int, default=int(os.getenv("TTS_CHUNK_CHARS", "90")))
    ap.add_argument("--tts_first_chunk_chars", type=int, default=int(os.getenv("TTS_FIRST_CHUNK_CHARS", "45")))

    ap.add_argument("--openai_key", default=os.getenv("OPENAI_API_KEY"))
    ap.add_argument("--router_model", default=os.getenv("ANGY_ROUTER_MODEL", "gpt-4o-mini"))
    ap.add_argument("--agent_model", default=os.getenv("ANGY_AGENT_MODEL", "gpt-4o-mini"))
    ap.add_argument("--embed_model", default=os.getenv("ANGY_EMBED_MODEL", "text-embedding-3-small"))

    ap.add_argument("--rag_dir", default=os.getenv("RAG_DIR", "rag"))
    ap.add_argument("--rag_index", default=os.getenv("RAG_INDEX", "contenuti_faiss_index.faiss"))
    ap.add_argument("--rag_metadata", default=os.getenv("RAG_METADATA", "contenuti_metadata.pkl"))
    ap.add_argument("--rag_k", type=int, default=int(os.getenv("RAG_TOP_K", "5")))

    ap.add_argument("--trace_print", action="store_true")
    return ap


def save_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def save_json(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def nemo_stt_file(
    session: requests.Session,
    nemo_url: str,
    wav_path: Path,
    trace_id: str,
    timeout_s: float = 120.0,
) -> Dict[str, Any]:
    url = nemo_url.rstrip("/") + "/stt"
    headers = {"X-Trace-Id": trace_id}

    with wav_path.open("rb") as f:
        files = {"file": (wav_path.name, f, "audio/wav")}
        data = {"trace_id": trace_id}
        r = session.post(url, files=files, data=data, headers=headers, timeout=timeout_s)

    r.raise_for_status()
    return r.json()


def build_tts_chunks(text: str, do_chunking: bool, max_chars: int, first_chunk_chars: int) -> List[str]:
    clean = (text or "").strip()
    if not clean:
        return []

    if not do_chunking:
        return [clean]

    try:
        chunks = split_text_for_tts(
            clean,
            max_chars=max_chars,
            first_chunk_chars=first_chunk_chars,
        )
    except TypeError:
        chunks = split_text_for_tts(clean, max_chars=max_chars)

    chunks = [c.strip() for c in (chunks or []) if c and c.strip()]
    return chunks or [clean]


def vibevoice_tts_to_unreal_chunk(
    session: requests.Session,
    vibevoice_url: str,
    text: str,
    trace_id: str,
    utterance_id: str,
    speaker_name: str,
    cfg_scale: float,
    chunk_ms: int,
    timeout_s: float = 300.0,
) -> Dict[str, Any]:
    url = vibevoice_url.rstrip("/") + "/tts_to_unreal"

    payload = {
        "text": text,
        "trace_id": trace_id,
        "utterance_id": utterance_id,
        "speaker_name": speaker_name,
        "cfg_scale": float(cfg_scale),
        "chunk_ms": int(chunk_ms),
    }

    r = session.post(url, json=payload, timeout=timeout_s)
    if not r.ok:
        raise RuntimeError(f"VibeVoice Bridge error {r.status_code}: {r.text}")

    data = r.json()
    if not isinstance(data, dict):
        raise RuntimeError(f"Bridge VibeVoice: risposta JSON non valida: {data!r}")

    for key in (
        "ok",
        "trace_id",
        "utterance_id",
        "speaker_used",
        "sample_rate",
        "num_chunks_sent",
        "first_chunk_latency_ms",
        "total_time_ms",
    ):
        if key not in data:
            raise RuntimeError(f"Bridge VibeVoice: manca '{key}' nella risposta")

    return data


def main() -> None:
    load_dotenv()
    args = build_parser().parse_args()

    sample_wav = Path(args.sample_wav)
    if not sample_wav.exists():
        raise FileNotFoundError(f"sample WAV non trovato: {sample_wav}")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    trace_id = uuid.uuid4().hex[:12]
    utterance_id = uuid.uuid4().hex[:10]

    if args.trace_print:
        print(f"TRACE_ID={trace_id} UTTERANCE_ID={utterance_id}")

    session = requests.Session()

    angy = AngyMultiAgent(
        openai_api_key=args.openai_key,
        rag=RagConfig(
            rag_dir=args.rag_dir,
            index_file=args.rag_index,
            metadata_file=args.rag_metadata,
            top_k=args.rag_k,
        ),
        llm=LlmConfig(
            router_model=args.router_model,
            agent_model=args.agent_model,
            embedding_model=args.embed_model,
            max_history_messages=20,
        ),
    )

    result: Dict[str, Any] = {
        "trace_id": trace_id,
        "utterance_id": utterance_id,
        "input_wav": str(sample_wav.resolve()),
        "nemo_url": args.nemo_url,
        "vibevoice_url": args.vibevoice_url,
        "timings_ms": {},
        "config": {
            "language": args.language,
            "vv_speaker_name": args.vv_speaker_name,
            "vv_cfg_scale": args.vv_cfg_scale,
            "vv_chunk_ms": args.vv_chunk_ms,
            "tts_chunking": args.tts_chunking,
            "tts_chunk_chars": args.tts_chunk_chars,
            "tts_first_chunk_chars": args.tts_first_chunk_chars,
        },
        "stt": {},
        "llm": {},
        "tts": {
            "chunks": []
        },
    }

    t0 = time.perf_counter()
    stt_json = nemo_stt_file(
        session=session,
        nemo_url=args.nemo_url,
        wav_path=sample_wav,
        trace_id=trace_id,
    )
    result["timings_ms"]["stt_total_client_ms"] = round((time.perf_counter() - t0) * 1000, 2)

    transcript = (stt_json.get("text", "") or "").strip()
    result["stt"] = stt_json
    save_text(out_dir / "01_transcript.txt", transcript)

    t1 = time.perf_counter()
    assistant_text, dbg = angy.reply(transcript)
    result["timings_ms"]["llm_total_ms"] = round((time.perf_counter() - t1) * 1000, 2)
    result["llm"] = {
        "debug": dbg,
        "reply": assistant_text,
    }
    save_text(out_dir / "02_assistant_reply.txt", assistant_text)

    chunks = build_tts_chunks(
        assistant_text,
        do_chunking=args.tts_chunking,
        max_chars=args.tts_chunk_chars,
        first_chunk_chars=args.tts_first_chunk_chars,
    )
    result["tts"]["prepared_chunks"] = chunks
    save_text(out_dir / "03_chunks.txt", "\n\n---\n\n".join(chunks))

    if not chunks:
        save_json(out_dir / "manifest.json", result)
        print("Nessun chunk TTS prodotto.")
        return

    total_client_tts_ms = 0.0
    total_server_tts_ms = 0.0
    first_chunk_latencies: List[float] = []
    total_chunks_sent = 0

    for i, chunk_text in enumerate(chunks):
        chunk_trace_id = f"{trace_id}-tts-{i:02d}"
        chunk_utterance_id = f"{utterance_id}-{i:02d}"

        t2 = time.perf_counter()
        vv = vibevoice_tts_to_unreal_chunk(
            session=session,
            vibevoice_url=args.vibevoice_url,
            text=chunk_text,
            trace_id=chunk_trace_id,
            utterance_id=chunk_utterance_id,
            speaker_name=args.vv_speaker_name,
            cfg_scale=args.vv_cfg_scale,
            chunk_ms=args.vv_chunk_ms,
        )
        client_tts_ms = round((time.perf_counter() - t2) * 1000, 2)

        total_client_tts_ms += client_tts_ms
        total_server_tts_ms += float(vv.get("total_time_ms", 0.0) or 0.0)
        total_chunks_sent += int(vv.get("num_chunks_sent", 0) or 0)

        first_latency = float(vv.get("first_chunk_latency_ms", 0.0) or 0.0)
        first_chunk_latencies.append(first_latency)

        result["tts"]["chunks"].append({
            "chunk_index": i,
            "trace_id": chunk_trace_id,
            "utterance_id": chunk_utterance_id,
            "text": chunk_text,
            "speaker_used": vv.get("speaker_used"),
            "sample_rate": vv.get("sample_rate"),
            "num_chunks_sent": vv.get("num_chunks_sent"),
            "first_chunk_latency_ms": vv.get("first_chunk_latency_ms"),
            "total_time_ms": vv.get("total_time_ms"),
            "client_tts_ms": client_tts_ms,
        })

    result["timings_ms"]["tts_total_client_ms"] = round(total_client_tts_ms, 2)
    result["timings_ms"]["tts_total_server_ms_sum"] = round(total_server_tts_ms, 2)
    result["timings_ms"]["pipeline_total_estimated_ms"] = round(
        result["timings_ms"]["stt_total_client_ms"]
        + result["timings_ms"]["llm_total_ms"]
        + total_client_tts_ms,
        2,
    )

    result["tts"]["num_requests"] = len(chunks)
    result["tts"]["num_chunks_sent_total"] = total_chunks_sent
    result["tts"]["first_chunk_latency_ms_min"] = round(min(first_chunk_latencies), 2)
    result["tts"]["first_chunk_latency_ms_max"] = round(max(first_chunk_latencies), 2)
    result["tts"]["first_chunk_latency_ms_avg"] = round(
        sum(first_chunk_latencies) / len(first_chunk_latencies), 2
    )

    save_json(out_dir / "manifest.json", result)

    stt_srv = result.get("stt", {}).get("timings", {}) or {}

    print("\n=== TEST COMPLETATO ===")
    print(f"Transcript: {transcript}")
    print(f"Reply LLM: {assistant_text}")

    print("\n--- BENCHMARK ---")
    print(f"ASR client total ms: {result['timings_ms']['stt_total_client_ms']} ms")
    if stt_srv:
        print(
            "ASR server timings ms: "
            f"read={stt_srv.get('read_wav_ms', 0)} | "
            f"resample={stt_srv.get('resample_ms', 0)} | "
            f"infer={stt_srv.get('stt_infer_ms', 0)} | "
            f"total={stt_srv.get('total_ms', 0)}"
        )

    print(f"LLM total ms: {result['timings_ms']['llm_total_ms']} ms")
    print(f"Chunk TTS inviati: {len(chunks)}")
    print(f"TTS first chunk latency avg: {result['tts']['first_chunk_latency_ms_avg']} ms")
    print(f"TTS total client ms: {result['timings_ms']['tts_total_client_ms']} ms")
    print(f"Pipeline total estimated ms: {result['timings_ms']['pipeline_total_estimated_ms']} ms")
    print(f"Manifest: {(out_dir / 'manifest.json').resolve()}")

if __name__ == "__main__":
    main()