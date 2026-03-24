from __future__ import annotations

import io
import os
import threading
import time
import uuid
import wave

import numpy as np
import requests
import sounddevice as sd
from dotenv import load_dotenv

from avatar_client.angy_multiagent import AngyMultiAgent, LlmConfig, RagConfig
from avatar_client.cli import build_parser
from avatar_client.dsp import preprocess_mic_pcm16
from avatar_client.greeting_http_server import GreetingRequestServer
from avatar_client.logger import log_event
from avatar_client.unreal_ack_server import UnrealAckServer
from avatar_client.vad import MicVadSegmenter, VadConfig
from avatar_client.vibevoice_http_client import vibevoice_tts_to_unreal


def pcm16_mono_to_wav_bytes(pcm16: np.ndarray, sample_rate: int) -> bytes:
    if pcm16.dtype != np.int16:
        pcm16 = pcm16.astype(np.int16)

    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm16.tobytes())
    return buf.getvalue()


def nemo_stt_bytes(
    session: requests.Session,
    nemo_url: str,
    wav_bytes: bytes,
    trace_id: str,
    timeout_s: float = 60.0,
):
    url = nemo_url.rstrip("/") + "/stt"
    files = {"file": ("mic.wav", wav_bytes, "audio/wav")}
    data = {"trace_id": trace_id}
    headers = {"X-Trace-Id": trace_id}

    response = session.post(
        url,
        files=files,
        data=data,
        headers=headers,
        timeout=timeout_s,
    )
    response.raise_for_status()

    payload = response.json()

    raw_text = payload.get("text")
    if isinstance(raw_text, dict):
        text = raw_text.get("text") or raw_text.get("transcript") or ""
    else:
        text = raw_text or payload.get("transcript") or ""

    timings = payload.get("server_timings") or payload.get("timings") or {}
    return text, timings, payload


def main() -> None:
    load_dotenv()
    args = build_parser().parse_args()

    if args.list_devices:
        print(sd.query_devices())
        return

    os.makedirs(args.out_dir, exist_ok=True)

    ack = UnrealAckServer(args.ack_host, args.ack_port, args.ack_route)
    ack.start()

    temp_disable_mic_gating = bool(args.disable_mic_gating)
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

    vad_cfg = VadConfig(
        samplerate=args.samplerate,
        frame_ms=args.frame_ms,
        vad_aggr=args.vad_aggr,
        pre_roll_ms=args.pre_roll_ms,
        silence_ms=args.silence_ms,
        min_speech_ms=args.min_speech_ms,
        max_segment_s=args.max_segment_s,
    )
    segmenter = MicVadSegmenter(device=args.device, channels=args.channels, cfg=vad_cfg)

    # Gate per impedire che il loop microfono riparta mentre il greeting è attivo.
    greeting_gate = threading.Event()

    # Stato TTS attivo: impedisce di accettare greeting mentre l’avatar è già impegnato.
    tts_pipeline_busy = threading.Event()

    # Protegge l’accettazione del greeting.
    greeting_lock = threading.Lock()

    def queue_greeting_job(payload: dict) -> tuple[int, dict]:
        text = (payload.get("text") or "").strip()
        if not text:
            return 400, {"ok": False, "error": "missing_text"}

        speaker_name = (payload.get("speaker_name") or args.vv_speaker_name).strip()
        is_startup_greeting = bool(payload.get("is_startup_greeting", False))
        trace_id = (payload.get("trace_id") or uuid.uuid4().hex[:12]).strip()
        utterance_id = (payload.get("utterance_id") or uuid.uuid4().hex[:10]).strip()

        with greeting_lock:
            if greeting_gate.is_set() or tts_pipeline_busy.is_set():
                return 409, {"ok": False, "error": "avatar_busy"}

            greeting_gate.set()
            tts_pipeline_busy.set()

        def _worker() -> None:
            local_session = requests.Session()
            ack_registered = False
            tts_ok = False
            t0_global = time.perf_counter()

            try:
                log_event(
                    args.log_file,
                    trace_id,
                    "greeting.accepted",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                        "speaker_name": speaker_name,
                        "is_startup_greeting": is_startup_greeting,
                        "text": text,
                    },
                )

                if not temp_disable_mic_gating:
                    ack.register(utterance_id)
                    ack_registered = True

                vv = vibevoice_tts_to_unreal(
                    session=local_session,
                    vibevoice_url=args.vibevoice_url,
                    text=text,
                    trace_id=trace_id,
                    utterance_id=utterance_id,
                    speaker_name=speaker_name,
                    cfg_scale=float(args.vv_cfg_scale),
                    chunk_ms=int(args.vv_chunk_ms),
                    timeout_s=float(args.vibevoice_timeout_s),
                )
                tts_ok = True

                log_event(
                    args.log_file,
                    trace_id,
                    "greeting.vibevoice.to_unreal.done",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                        "bridge_response": vv,
                    },
                )

                if temp_disable_mic_gating:
                    log_event(
                        args.log_file,
                        trace_id,
                        "greeting.mic_gating.disabled",
                        t0_global,
                        {
                            "utterance_id": utterance_id,
                        },
                    )
                elif tts_ok and ack_registered:
                    log_event(
                        args.log_file,
                        trace_id,
                        "greeting.wait_unreal_ack",
                        t0_global,
                        {
                            "utterance_id": utterance_id,
                            "timeout_s": args.ack_timeout_s,
                        },
                    )

                    ok = ack.wait(utterance_id, timeout_s=float(args.ack_timeout_s))
                    if ok:
                        payload_ack = ack.payload(utterance_id)
                        time.sleep(args.mic_resume_pad_ms / 1000.0)
                        log_event(
                            args.log_file,
                            trace_id,
                            "greeting.unreal_ack_received",
                            t0_global,
                            {
                                "utterance_id": utterance_id,
                                "pad_ms": args.mic_resume_pad_ms,
                                "ack_payload": payload_ack,
                            },
                        )
                    else:
                        log_event(
                            args.log_file,
                            trace_id,
                            "greeting.unreal_ack_timeout",
                            t0_global,
                            {
                                "utterance_id": utterance_id,
                            },
                        )

            except Exception as exc:
                log_event(
                    args.log_file,
                    trace_id,
                    "greeting.error",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                        "error": str(exc),
                        "text": text,
                    },
                )
            finally:
                if ack_registered:
                    ack.clear(utterance_id)

                try:
                    local_session.close()
                except Exception:
                    pass

                tts_pipeline_busy.clear()
                greeting_gate.clear()

                log_event(
                    args.log_file,
                    trace_id,
                    "greeting.done",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                        "tts_ok": tts_ok,
                    },
                )

        threading.Thread(
            target=_worker,
            name=f"greeting-{utterance_id}",
            daemon=True,
        ).start()

        return 202, {
            "ok": True,
            "accepted": True,
            "trace_id": trace_id,
            "utterance_id": utterance_id,
            "is_startup_greeting": is_startup_greeting,
        }

    greeting_server = GreetingRequestServer(
        host=os.getenv("GREETING_HOST", "127.0.0.1"),
        port=int(os.getenv("GREETING_PORT", "8011")),
        route=os.getenv("GREETING_ROUTE", "/greeting/start"),
        on_request=queue_greeting_job,
    )
    greeting_server.start()

    print("🎙️  Loop attivo:")
    print("   Mic → STT NeMo → (Cleaner+Orchestratore+RAG/GEN) → VibeVoice bridge → Unreal")
    if temp_disable_mic_gating:
        print("   Mic SEMPRE libero (ACK Unreal ignorato)")
    else:
        print("   Mic riapre SOLO dopo ACK Unreal (fine playback)")
    print("   CTRL+C per fermare\n")
    print(f"🗣️ VibeVoice bridge: {args.vibevoice_url}")
    print(f"🗣️ Speaker: {args.vv_speaker_name}")
    print(f"🎛️ CFG scale: {args.vv_cfg_scale}")
    print(f"⏱️ VV chunk ms: {args.vv_chunk_ms}")
    print(f"👋 Greeting server: http://{os.getenv('GREETING_HOST', '127.0.0.1')}:{int(os.getenv('GREETING_PORT', '8011'))}{os.getenv('GREETING_ROUTE', '/greeting/start')}")

    try:
        while True:
            # Se è in corso un greeting, non riaprire il loop mic.
            while greeting_gate.is_set():
                time.sleep(0.05)

            trace_id = uuid.uuid4().hex[:12]
            utterance_id = uuid.uuid4().hex[:10]
            t0_global = time.perf_counter()

            if args.trace_print:
                print(f"\nTRACE_ID={trace_id} UTTERANCE_ID={utterance_id}")

            log_event(
                args.log_file,
                trace_id,
                "turn.start",
                t0_global,
                {
                    "nemo_url": args.nemo_url,
                    "rag_dir": args.rag_dir,
                    "rag_k": args.rag_k,
                    "router_model": args.router_model,
                    "agent_model": args.agent_model,
                    "vibevoice_url": args.vibevoice_url,
                    "vv_speaker_name": args.vv_speaker_name,
                    "vv_cfg_scale": args.vv_cfg_scale,
                    "vv_chunk_ms": args.vv_chunk_ms,
                    "temp_disable_mic_gating": temp_disable_mic_gating,
                },
            )

            # 1) Capture mic
            segmenter.reset_state()
            log_event(args.log_file, trace_id, "mic.segment_start_wait", t0_global)

            segmenter.start()
            seg_bytes, rec_time = segmenter.next_segment()
            segmenter.stop()

            log_event(
                args.log_file,
                trace_id,
                "mic.segment_captured",
                t0_global,
                {
                    "rec_time_s": round(rec_time, 3),
                    "bytes": len(seg_bytes),
                },
            )

            pcm16 = np.frombuffer(seg_bytes, dtype=np.int16)
            pcm16_clean = preprocess_mic_pcm16(
                pcm16,
                args.samplerate,
                hp_cutoff=args.hp_cutoff,
                gate_thr=args.gate,
            )

            wav_bytes = pcm16_mono_to_wav_bytes(pcm16_clean, args.samplerate)
            log_event(
                args.log_file,
                trace_id,
                "wav.in_memory",
                t0_global,
                {
                    "bytes": len(wav_bytes),
                    "samplerate": args.samplerate,
                    "channels": 1,
                },
            )

            # 2) STT
            t_stt0 = time.perf_counter()
            user_text, stt_timings, stt_raw = nemo_stt_bytes(
                session=session,
                nemo_url=args.nemo_url,
                wav_bytes=wav_bytes,
                trace_id=trace_id,
                timeout_s=float(args.stt_timeout_s),
            )
            t_stt_ms = round((time.perf_counter() - t_stt0) * 1000, 2)

            log_event(
                args.log_file,
                trace_id,
                "stt.done",
                t0_global,
                {
                    "stt_ms": t_stt_ms,
                    "server_timings": stt_timings,
                    "text": user_text,
                    "raw_response": stt_raw,
                },
            )

            user_text = (user_text or "").strip()
            if not user_text:
                log_event(args.log_file, trace_id, "turn.empty_stt", t0_global)
                log_event(args.log_file, trace_id, "turn.done", t0_global)
                continue

            if args.save_txt:
                with open(
                    os.path.join(args.out_dir, f"{trace_id}_user.txt"),
                    "w",
                    encoding="utf-8",
                ) as f:
                    f.write(user_text)

            # 3) LLM
            t_llm0 = time.perf_counter()
            assistant_text, dbg = angy.reply(user_text)
            t_llm_ms = round((time.perf_counter() - t_llm0) * 1000, 2)

            assistant_text = (assistant_text or "").strip()
            if not assistant_text:
                assistant_text = "Scusa, non ho una risposta pronta in questo momento."

            log_event(
                args.log_file,
                trace_id,
                "llm.multiagent.done",
                t0_global,
                {
                    "llm_ms": t_llm_ms,
                    "route": dbg.get("route"),
                    "cleaned_query": dbg.get("cleaned_query"),
                    "routed_query": dbg.get("routed_query"),
                    "decision_raw": dbg.get("decision_raw"),
                    "reply_len": len(assistant_text),
                    "reply": assistant_text,
                },
            )

            if args.save_txt:
                with open(
                    os.path.join(args.out_dir, f"{trace_id}_assistant.txt"),
                    "w",
                    encoding="utf-8",
                ) as f:
                    f.write(assistant_text)

            log_event(
                args.log_file,
                trace_id,
                "tts.request.prepared",
                t0_global,
                {
                    "utterance_id": utterance_id,
                    "reply_len": len(assistant_text),
                    "preview": assistant_text[:200],
                },
            )

            ack_registered = False
            tts_ok = False
            tts_pipeline_busy.set()

            try:
                if not temp_disable_mic_gating:
                    ack.register(utterance_id)
                    ack_registered = True

                # 4) TTS bridge -> Unreal
                t_tts0 = time.perf_counter()
                vv = vibevoice_tts_to_unreal(
                    session=session,
                    vibevoice_url=args.vibevoice_url,
                    text=assistant_text,
                    trace_id=trace_id,
                    utterance_id=utterance_id,
                    speaker_name=args.vv_speaker_name,
                    cfg_scale=float(args.vv_cfg_scale),
                    chunk_ms=int(args.vv_chunk_ms),
                    timeout_s=float(args.vibevoice_timeout_s),
                )
                t_tts_ms = round((time.perf_counter() - t_tts0) * 1000, 2)
                tts_ok = True

                log_event(
                    args.log_file,
                    trace_id,
                    "vibevoice.to_unreal.done",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                        "tts_ms": t_tts_ms,
                        "speaker_used": vv.get("speaker_used"),
                        "sample_rate": vv.get("sample_rate"),
                        "num_chunks_sent": vv.get("num_chunks_sent"),
                        "first_chunk_latency_ms": vv.get("first_chunk_latency_ms"),
                        "total_time_ms": vv.get("total_time_ms"),
                        "bridge_response": vv,
                    },
                )

            except Exception as exc:
                log_event(
                    args.log_file,
                    trace_id,
                    "vibevoice.to_unreal.error",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                        "error": str(exc),
                        "text": assistant_text,
                    },
                )

            # 5) ACK / mic gating
            if temp_disable_mic_gating:
                log_event(
                    args.log_file,
                    trace_id,
                    "mic.gating.disabled",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                    },
                )
            elif tts_ok and ack_registered:
                log_event(
                    args.log_file,
                    trace_id,
                    "mic.wait_unreal_ack",
                    t0_global,
                    {
                        "utterance_id": utterance_id,
                        "timeout_s": args.ack_timeout_s,
                    },
                )

                ok = ack.wait(utterance_id, timeout_s=float(args.ack_timeout_s))
                if ok:
                    payload = ack.payload(utterance_id)
                    time.sleep(args.mic_resume_pad_ms / 1000.0)
                    log_event(
                        args.log_file,
                        trace_id,
                        "mic.unreal_ack_received",
                        t0_global,
                        {
                            "utterance_id": utterance_id,
                            "pad_ms": args.mic_resume_pad_ms,
                            "ack_payload": payload,
                        },
                    )
                else:
                    log_event(
                        args.log_file,
                        trace_id,
                        "mic.unreal_ack_timeout",
                        t0_global,
                        {
                            "utterance_id": utterance_id,
                        },
                    )

            if ack_registered:
                ack.clear(utterance_id)

            tts_pipeline_busy.clear()
            log_event(args.log_file, trace_id, "turn.done", t0_global)

    except KeyboardInterrupt:
        print("\n🛑 Stop.")
    finally:
        try:
            segmenter.stop()
        except Exception:
            pass

        try:
            greeting_server.stop()
        except Exception:
            pass

        try:
            ack.stop()
        except Exception:
            pass

        try:
            session.close()
        except Exception:
            pass