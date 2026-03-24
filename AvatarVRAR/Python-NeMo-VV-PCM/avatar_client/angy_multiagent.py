import os
import re
import time
import pickle
import textwrap
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

try:
    import faiss  # type: ignore
except Exception as e:
    raise RuntimeError(
        "Faiss non disponibile. Installa 'faiss-cpu' nel venv Python che esegue il client."
    ) from e

from dotenv import load_dotenv
from openai import OpenAI


@dataclass
class RagConfig:
    rag_dir: str = "rag"
    index_file: str = "contenuti_faiss_index.faiss"
    metadata_file: str = "contenuti_metadata.pkl"
    top_k: int = 5


@dataclass
class LlmConfig:
    router_model: str = "gpt-4o-mini"     # Orchestratore + cleaner
    agent_model: str = "gpt-4o-mini"      # Agenti
    embedding_model: str = "text-embedding-3-small"
    max_history_messages: int = 20


class AngyMultiAgent:
    """
    Versione integrabile nella pipeline voce.
    - Cleaner (LLM) per ripulire input STT
    - Orchestratore (LLM) per scegliere: RAG vs GEN
    - Agente RAG: recupero da FAISS + metadata in ./rag
    - Agente GEN: conversazione “safe”
    """

    def __init__(
        self,
        openai_api_key: Optional[str] = None,
        rag: Optional[RagConfig] = None,
        llm: Optional[LlmConfig] = None,
    ):
        load_dotenv()
        api_key = openai_api_key or os.getenv("OPENAI_API_KEY", "")
        if not api_key:
            raise RuntimeError("OPENAI_API_KEY mancante (.env o env) oppure passalo da CLI.")

        self.client = OpenAI(api_key=api_key)

        self.rag_cfg = rag or RagConfig()
        self.llm_cfg = llm or LlmConfig()

        self.index, self.metadata = self._load_rag(self.rag_cfg)
        self.history: List[Dict[str, str]] = []

    # ----------------------------
    # Normalizzazione input testo
    # ----------------------------
    def _normalize_user_text(self, value: Any) -> str:
        if value is None:
            return ""

        if isinstance(value, str):
            return value.strip()

        if isinstance(value, (int, float, bool)):
            return str(value).strip()

        if isinstance(value, list):
            for item in value:
                out = self._normalize_user_text(item)
                if out:
                    return out
            return ""

        if isinstance(value, dict):
            # chiavi più probabili
            for key in (
                "text",
                "transcript",
                "utterance",
                "prediction",
                "pred_text",
                "normalized_text",
                "content",
            ):
                if key in value:
                    out = self._normalize_user_text(value.get(key))
                    if out:
                        return out

            # contenitori annidati comuni
            for key in (
                "result",
                "results",
                "data",
                "response",
                "hypotheses",
                "alternatives",
                "payload",
            ):
                if key in value:
                    out = self._normalize_user_text(value.get(key))
                    if out:
                        return out

            return ""

        return str(value).strip()

    def _normalize_for_match(self, text: str) -> str:
        t = (text or "").lower().strip()
        t = re.sub(r"[^\w\sàèéìòù]", " ", t, flags=re.UNICODE)
        t = re.sub(r"\s+", " ", t).strip()
        return t

    def _is_greeting_or_smalltalk(self, text: str) -> bool:
        t = self._normalize_for_match(text)
        if not t:
            return False

        patterns = [
            "ciao",
            "ehi",
            "hey",
            "salve",
            "buongiorno",
            "buonasera",
            "come va",
            "come stai",
            "come te la passi",
            "tutto bene",
        ]
        return any(p in t for p in patterns)

    # ----------------------------
    # RAG load
    # ----------------------------
    def _resolve_rag_path(self, rag_dir: str, filename: str) -> str:
        cand1 = os.path.join(os.getcwd(), rag_dir, filename)
        if os.path.exists(cand1):
            return cand1

        here = os.path.dirname(os.path.abspath(__file__))
        cand2 = os.path.abspath(os.path.join(here, "..", rag_dir, filename))
        if os.path.exists(cand2):
            return cand2

        return os.path.join(rag_dir, filename)

    def _load_rag(self, cfg: RagConfig):
        index_path = self._resolve_rag_path(cfg.rag_dir, cfg.index_file)
        meta_path = self._resolve_rag_path(cfg.rag_dir, cfg.metadata_file)

        if not os.path.exists(index_path):
            raise RuntimeError(f"RAG index non trovato: {index_path}")
        if not os.path.exists(meta_path):
            raise RuntimeError(f"RAG metadata non trovati: {meta_path}")

        index = faiss.read_index(index_path)
        with open(meta_path, "rb") as f:
            metadata = pickle.load(f)

        if not isinstance(metadata, list):
            raise RuntimeError("Metadata RAG: formato non valido (atteso list).")

        return index, metadata

    # ----------------------------
    # Utils
    # ----------------------------
    def _trim_history(self):
        m = self.llm_cfg.max_history_messages
        if len(self.history) > m:
            self.history = self.history[-m:]

    def _safe_chat_completion(self, model: str, messages: list, temperature: float = 0.2) -> str:
        last_error = None
        for attempt in range(3):
            try:
                resp = self.client.chat.completions.create(
                    model=model,
                    messages=messages,
                    temperature=temperature,
                    timeout=30,
                )
                content = resp.choices[0].message.content if resp.choices else ""
                return (content or "").strip() or "Non sono riuscito a generare una risposta valida."
            except Exception as e:
                last_error = e
                time.sleep(0.5 * (2 ** attempt))

        print(f"⚠️ Errore OpenAI ({model}): {last_error}")
        return "C'è stato un problema tecnico. Riprova tra poco."

    def _safe_embedding(self, text: str) -> Optional[List[float]]:
        text = text[:8000] if text and len(text) > 8000 else text
        last_error = None
        for attempt in range(3):
            try:
                emb = self.client.embeddings.create(
                    model=self.llm_cfg.embedding_model,
                    input=text,
                    timeout=30
                )
                return emb.data[0].embedding if emb.data else None
            except Exception as e:
                last_error = e
                time.sleep(0.5 * (2 ** attempt))

        print(f"⚠️ Errore embedding: {last_error}")
        return None

    # ----------------------------
    # RAG search
    # ----------------------------
    def _metadata_to_text(self, item: Any) -> str:
        if isinstance(item, str):
            return item
        if isinstance(item, dict):
            for k in ("text", "content", "chunk", "page_content"):
                if k in item and isinstance(item[k], str):
                    return item[k]
            return str(item)
        return str(item)

    def cerca_blocchi_simili(self, query: str, k: Optional[int] = None) -> List[str]:
        k = k or self.rag_cfg.top_k
        vec_raw = self._safe_embedding(query)
        if vec_raw is None:
            return []

        vec = np.array(vec_raw, dtype="float32").reshape(1, -1)
        _, I = self.index.search(vec, k)

        out: List[str] = []
        for idx in I[0]:
            if 0 <= int(idx) < len(self.metadata):
                out.append(self._metadata_to_text(self.metadata[int(idx)]))
        return out

    # ----------------------------
    # Cleaner
    # ----------------------------
    def pulisci_input(self, user_input: Any) -> Optional[str]:
        user_input = self._normalize_user_text(user_input)
        if not user_input:
            return None

        low = user_input.strip()
        if len(low) <= 2:
            return None

        # I saluti / small talk non devono essere scartati
        if self._is_greeting_or_smalltalk(user_input):
            return user_input

        prompt = [
            {
                "role": "system",
                "content": textwrap.dedent(
                    """\
                    Sei un filtro di input per un chatbot educativo sulla sicurezza digitale e sull'ITS Cadmo.

                    Il tuo compito è:
                    1) ripulire l'input dell'utente da rumore STT, intercalari e frammenti inutili
                    2) mantenere intatto il significato sintetizzando la risposta
                    3) restituire una riformulazione chiara e breve

                    IMPORTANTE:
                    - Se l'input è un saluto, una chiacchiera semplice o small talk, NON rispondere NONE.
                      Restituisci il testo ripulito.
                    - Rispondi con NONE solo se l'input non contiene davvero nulla di utile
                      (solo rumore, sillabe spezzate, risate, parole casuali senza significato).

                    OUTPUT:
                    - solo il testo ripulito
                    - oppure NONE
                    """
                ).strip(),
            },
            {"role": "user", "content": user_input},
        ]

        out = self._safe_chat_completion(self.llm_cfg.router_model, prompt, temperature=0.0).strip()
        if not out or out.upper() == "NONE":
            return None
        return out

    # ----------------------------
    # Orchestratore
    # ----------------------------
    def orchestratore(self, conversation_history: List[Dict[str, str]]) -> str:
        prompt = [
            {
                "role": "system",
                "content": textwrap.dedent(
                    """\
                    Sei l'Orchestratore di Angy.

                    RUOLO:
                    Il tuo unico compito è decidere a quale agente inoltrare l'ultima richiesta dell'utente.
                    NON devi spiegare, NON devi rispondere, NON devi commentare.

                    CRITERI:
                    Usa CALL:RAG per domande su cybersecurity/sicurezza digitale, ITS Cadmo, percorsi formativi IT/digitale.
                    Usa CALL:GEN per saluti, chiacchiere, supporto emotivo non tecnico o domande fuori dominio.

                    REGOLA D'ORO:
                    Se hai il minimo dubbio tra RAG e GEN, scegli SEMPRE CALL:RAG.

                    OUTPUT:
                    Una sola riga:
                    CALL:RAG:<riformulazione neutra>
                    oppure
                    CALL:GEN:<riformulazione neutra>

                    REGOLE:
                    - una sola riga
                    - niente virgolette/emoji
                    - niente testo extra
                    """
                ).strip(),
            }
        ] + conversation_history

        return self._safe_chat_completion(self.llm_cfg.router_model, prompt, temperature=0.0).strip()

    def normalizza_decision(self, decision: str) -> str:
        d = (decision or "").strip()
        if d.startswith("CALL:RAG:") or d.startswith("CALL:GEN:"):
            return d

        low = d.lower()
        if any(w in low for w in [
            "dove", "come", "posso", "its", "cadmo", "studiare",
            "imparare", "corso", "sicurezza", "password", "phishing"
        ]):
            return f"CALL:RAG:{d}"

        return f"CALL:GEN:{d}"

    # ----------------------------
    # Agenti
    # ----------------------------
    def agente_rag(self, query_per_ricerca: str) -> str:
        blocchi = self.cerca_blocchi_simili(query_per_ricerca, k=self.rag_cfg.top_k)
        contesto = "\n---\n".join(blocchi) if blocchi else "Nessuna informazione rilevante trovata nei documenti."

        prompt = [
            {
                "role": "system",
                "content": textwrap.dedent(
                    """\
                    Sei l'Agente Informativo di Angy.

                    RUOLO:
                    Rispondi esclusivamente usando le informazioni presenti nel contesto fornito.
                    Il contesto contiene documenti su:
                    - Sicurezza digitale / cybersecurity
                    - ITS Cadmo

                    DESTINATARI:
                    13-18 anni, linguaggio semplice.

                    REGOLE:
                    - Usa SOLO il contesto
                    - Non inventare dettagli (email, numeri, sedi, date) se non presenti
                    - Non dare istruzioni di hacking o attività illegali
                    - Se manca la risposta nel contesto, dillo chiaramente e dai solo consigli generali di buon senso.
                    """
                ).strip(),
            },
            {"role": "system", "content": f"📚 Contesto documentale:\n{contesto}"},
        ] + self.history

        return self._safe_chat_completion(self.llm_cfg.agent_model, prompt, temperature=0.2)

    def agente_generico(self) -> str:
        prompt = [
            {
                "role": "system",
                "content": textwrap.dedent(
                    """\
                    Sei Angy, assistente virtuale educativo per ragazzi (13-18).

                    PUOI:
                    - chiacchierare, salutare
                    - dare consigli semplici e sicuri su sicurezza digitale
                    - spiegare come funziona il chatbot

                    NON PUOI:
                    - istruzioni tecniche avanzate
                    - bypass/hacking/attività illegali
                    - chiedere dati personali

                    STILE:
                    italiano semplice, amichevole, breve.

                    REGOLA:
                    sintetizza il concetto in massimo 50 parole
                    """
                ).strip(),
            }
        ] + self.history

        return self._safe_chat_completion(self.llm_cfg.agent_model, prompt, temperature=0.5)

    # ----------------------------
    # Public API
    # ----------------------------
    def reply(self, user_text: Any) -> Tuple[str, Dict[str, Any]]:
        """
        Ritorna (assistant_text, debug_info)
        debug_info: route, cleaned_query, routed_query
        """
        user_text = self._normalize_user_text(user_text)

        # bypass cleaner aggressivo per saluti / small talk
        if self._is_greeting_or_smalltalk(user_text):
            self.history.append({"role": "user", "content": user_text})
            self._trim_history()

            assistant = self.agente_generico()

            self.history.append({"role": "assistant", "content": assistant})
            self._trim_history()

            return assistant, {
                "route": "GEN",
                "cleaned_query": user_text,
                "routed_query": user_text,
                "decision_raw": "HEURISTIC_GREETING",
            }

        cleaned = self.pulisci_input(user_text)
        if cleaned is None:
            assistant = "Scusa, non ho capito bene. Puoi ripetere la domanda?"
            self.history.append({"role": "user", "content": user_text})
            self.history.append({"role": "assistant", "content": assistant})
            self._trim_history()
            return assistant, {
                "route": "GEN_FALLBACK",
                "cleaned_query": None,
                "routed_query": None,
                "decision_raw": None,
            }

        self.history.append({"role": "user", "content": user_text})
        self._trim_history()

        base = self.history[-5:-1]
        history_for_router = base + [{"role": "user", "content": cleaned}]
        decision = self.normalizza_decision(self.orchestratore(history_for_router))

        route = "GEN"
        routed_query = cleaned

        if decision.startswith("CALL:RAG:"):
            route = "RAG"
            routed_query = decision[len("CALL:RAG:"):].strip() or cleaned
            assistant = self.agente_rag(routed_query)
        else:
            route = "GEN"
            routed_query = decision[len("CALL:GEN:"):].strip() if decision.startswith("CALL:GEN:") else cleaned
            assistant = self.agente_generico()

        self.history.append({"role": "assistant", "content": assistant})
        self._trim_history()

        return assistant, {
            "route": route,
            "cleaned_query": cleaned,
            "routed_query": routed_query,
            "decision_raw": decision,
        }