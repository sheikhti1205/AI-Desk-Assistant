import os
import math
import time
import wave
import secrets
import asyncio
import json
import re
import subprocess
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

from dotenv import load_dotenv
from fastapi import FastAPI, File, Form, HTTPException, Request, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles
from openai import OpenAI
from google import genai
from google.genai import types

try:
    from faster_whisper import WhisperModel
except Exception:
    WhisperModel = None

load_dotenv()

ROOT = Path(__file__).parent
AUDIO_DIR = ROOT / "public" / "audio"
UPLOAD_DIR = ROOT / "uploads"
AUDIO_DIR.mkdir(parents=True, exist_ok=True)
UPLOAD_DIR.mkdir(parents=True, exist_ok=True)

PUBLIC_BASE_URL = os.getenv("PUBLIC_BASE_URL", "http://localhost:3000").rstrip("/")
TEXT_MODEL = os.getenv("OPENAI_TEXT_MODEL", "gpt-4.1-mini")
TRANSCRIBE_MODEL = os.getenv("OPENAI_TRANSCRIBE_MODEL", "gpt-4o-mini-transcribe")
TTS_MODEL = os.getenv("OPENAI_TTS_MODEL", "gpt-4o-mini-tts")
TTS_VOICE = os.getenv("OPENAI_TTS_VOICE", "alloy")
GEMINI_TEXT_MODEL = os.getenv("GEMINI_TEXT_MODEL", "gemini-2.5-flash")
GEMINI_TTS_MODEL = os.getenv("GEMINI_TTS_MODEL", "gemini-2.5-flash-preview-tts")
GEMINI_TTS_VOICE = os.getenv("GEMINI_TTS_VOICE", "Kore")
TOOL_NEURON_BASE_URL = os.getenv("TOOL_NEURON_BASE_URL", os.getenv("LOCAL_OPENAI_BASE_URL", "")).rstrip("/")
TOOL_NEURON_API_KEY = os.getenv("TOOL_NEURON_API_KEY", os.getenv("LOCAL_OPENAI_API_KEY", ""))
TOOL_NEURON_CHAT_MODEL = os.getenv("TOOL_NEURON_CHAT_MODEL", "hf-liquidai-lfm2.5-vl-1.6b-gguf__LFM2.5-VL-1.6B-Q4_0.gguf")
TOOL_NEURON_STT_MODEL = os.getenv("TOOL_NEURON_STT_MODEL", "sherpa-onnx-whisper-tiny")
TOOL_NEURON_TTS_MODEL = os.getenv("TOOL_NEURON_TTS_MODEL", "vits-piper-en_US-amy-low")
TOOL_NEURON_TTS_VOICE = os.getenv("TOOL_NEURON_TTS_VOICE", "amy")
TOOL_NEURON_MAX_TOKENS = int(os.getenv("TOOL_NEURON_MAX_TOKENS", "384"))
BUDDY_MAX_RESPONSE_CHARS = int(os.getenv("BUDDY_MAX_RESPONSE_CHARS", "1600"))
LOCAL_OPENAI_BASE_URL = os.getenv("LOCAL_OPENAI_BASE_URL", "").rstrip("/")
LOCAL_OPENAI_TEXT_MODEL = os.getenv("LOCAL_OPENAI_TEXT_MODEL", "hf-liquidai-lfm2.5-vl-1.6b-gguf__LFM2.5-VL-1.6B-Q4_0.gguf")
OLLAMA_BASE_URL = os.getenv("OLLAMA_BASE_URL", "http://127.0.0.1:11434").rstrip("/")
OLLAMA_TEXT_MODEL = os.getenv("OLLAMA_TEXT_MODEL", "llama3.2:3b")
USE_CLOUD_FALLBACKS = os.getenv("USE_CLOUD_FALLBACKS", "0") == "1"
LOCAL_STT_MODEL = os.getenv("LOCAL_STT_MODEL", "tiny.en")
LOCAL_STT_DEVICE = os.getenv("LOCAL_STT_DEVICE", "cpu")
LOCAL_STT_COMPUTE_TYPE = os.getenv("LOCAL_STT_COMPUTE_TYPE", "int8")
LOCAL_STT_LANGUAGE = os.getenv("LOCAL_STT_LANGUAGE", "en")
local_stt_model = None
tool_neuron_model_cache = None

app = FastAPI(title="AI Buddy Server")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)
app.mount("/audio", StaticFiles(directory=AUDIO_DIR), name="audio")


def require_openai_key() -> None:
    if not os.getenv("OPENAI_API_KEY"):
        raise HTTPException(status_code=503, detail="OPENAI_API_KEY is not configured")


def openai_client() -> OpenAI:
    require_openai_key()
    return OpenAI(api_key=os.getenv("OPENAI_API_KEY"), timeout=20.0, max_retries=1)


def local_openai_client() -> OpenAI | None:
    if not LOCAL_OPENAI_BASE_URL or not os.getenv("LOCAL_OPENAI_API_KEY"):
        return None
    return OpenAI(
        api_key=os.getenv("LOCAL_OPENAI_API_KEY"),
        base_url=LOCAL_OPENAI_BASE_URL,
        timeout=12.0,
        max_retries=0,
    )


def tool_neuron_configured() -> bool:
    return bool(TOOL_NEURON_BASE_URL and TOOL_NEURON_API_KEY)


def tool_neuron_url(path: str) -> str:
    return f"{TOOL_NEURON_BASE_URL}/{path.lstrip('/')}"


def tool_neuron_headers(content_type: str | None = "application/json") -> dict[str, str]:
    headers = {"Authorization": f"Bearer {TOOL_NEURON_API_KEY}"}
    if content_type:
        headers["Content-Type"] = content_type
    return headers


def gemini_client() -> genai.Client:
    if not os.getenv("GEMINI_API_KEY"):
        raise HTTPException(status_code=503, detail="GEMINI_API_KEY is not configured")
    return genai.Client(api_key=os.getenv("GEMINI_API_KEY"))


def ollama_configured() -> bool:
    return bool(OLLAMA_BASE_URL and OLLAMA_TEXT_MODEL)


def tool_neuron_models_sync() -> dict:
    req = urllib.request.Request(
        tool_neuron_url("/models"),
        headers=tool_neuron_headers(content_type=None),
        method="GET",
    )
    with urllib.request.urlopen(req, timeout=12) as response:
        return json.loads(response.read().decode("utf-8", errors="replace"))


def tool_neuron_model_id(kind: str, configured: str) -> str:
    global tool_neuron_model_cache
    if not tool_neuron_configured():
        return configured
    if tool_neuron_model_cache is None:
        tool_neuron_model_cache = tool_neuron_models_sync().get("data", [])
    models = tool_neuron_model_cache
    ids = {m.get("id") for m in models}
    if configured in ids:
        return configured

    def matches(model: dict) -> bool:
        model_type = str(model.get("type", "")).lower()
        owner = str(model.get("owned_by", "")).lower()
        if kind == "chat":
            return model_type in {"gguf", "chat"} or "chat" in owner
        return model_type == kind or f"tool-neuron-{kind}" in owner

    candidates = [m for m in models if matches(m)]
    for model in candidates:
        if model.get("default"):
            return model.get("id", configured)
    if candidates:
        return candidates[0].get("id", configured)
    return configured


BUDDY_SYSTEM_PROMPT = """
You are Buddy, the voice and personality of a small ESP32-S3 desk companion.

The user speaks to you through a microphone and hears you through text-to-speech.
Answer like a present, capable companion: warm, direct, practical, and a little lively
when the moment allows. Do not act like a generic chatbot. Do not say you are only
a language model. Do not mention model names, vendors, policies, tokens, prompts, or
implementation details unless the user explicitly asks about the system.

Style:
- Give a real answer, not a tiny canned reply.
- For normal questions, use a few useful sentences.
- For explanations, give clear steps or compact bullets if that helps.
- For casual speech, sound natural and conversational.
- Match the user's language and tone when possible.
- Avoid markdown tables unless the user asks.
- Keep spoken responses easy to listen to: no huge walls of text, no rambling, no hidden reasoning.
- If you are unsure, say what you can infer and ask one useful follow-up only when needed.

The device is small, but the phone/server handles the AI work, so you do not need to
artificially limit answers to one sentence.
""".strip()


def ollama_generate_text_sync(prompt: str) -> str | None:
    payload = {
        "model": OLLAMA_TEXT_MODEL,
        "prompt": f"{BUDDY_SYSTEM_PROMPT}\n\nUser/request: {prompt}",
        "stream": False,
        "options": {"temperature": 0.8, "num_predict": max(256, TOOL_NEURON_MAX_TOKENS)},
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"{OLLAMA_BASE_URL}/api/generate",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=20) as response:
        body = json.loads(response.read().decode("utf-8", errors="replace"))
    text = body.get("response", "")
    return clean_buddy_text(text) if text else None


def tool_neuron_generate_text_sync(prompt: str) -> str | None:
    payload = {
        "model": tool_neuron_model_id("chat", TOOL_NEURON_CHAT_MODEL),
        "messages": [
            {"role": "system", "content": BUDDY_SYSTEM_PROMPT},
            {"role": "user", "content": f"/no_think\n{prompt}"},
        ],
        "stream": False,
        "temperature": 0.7,
        "max_tokens": TOOL_NEURON_MAX_TOKENS,
    }
    req = urllib.request.Request(
        tool_neuron_url("/chat/completions"),
        data=json.dumps(payload).encode("utf-8"),
        headers=tool_neuron_headers(),
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=35) as response:
        body = json.loads(response.read().decode("utf-8", errors="replace"))
    text = body.get("choices", [{}])[0].get("message", {}).get("content", "")
    return clean_buddy_text(text) if text else None
def raw8_to_wav(raw_path: Path) -> Path:
    wav_path = raw_path.with_suffix(".wav")
    raw = raw_path.read_bytes()
    with wave.open(str(wav_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(8000)
        wav.writeframes(raw)
    return wav_path


def save_raw8_debug_wav(raw_path: Path) -> Path:
    wav_path = raw_path.with_suffix(".debug.wav")
    raw = raw_path.read_bytes()
    with wave.open(str(wav_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(5333)
        wav.writeframes(raw)
    return wav_path


def local_stt_configured() -> bool:
    return WhisperModel is not None and bool(LOCAL_STT_MODEL)


def local_whisper_model():
    global local_stt_model
    if local_stt_model is None:
        if WhisperModel is None:
            raise RuntimeError("faster-whisper is not installed")
        local_stt_model = WhisperModel(
            LOCAL_STT_MODEL,
            device=LOCAL_STT_DEVICE,
            compute_type=LOCAL_STT_COMPUTE_TYPE,
        )
    return local_stt_model


def transcribe_local_whisper_sync(wav_path: Path) -> str:
    model = local_whisper_model()
    segments, info = model.transcribe(
        str(wav_path),
        language=LOCAL_STT_LANGUAGE,
        beam_size=1,
        best_of=1,
        temperature=0.0,
        condition_on_previous_text=False,
        no_speech_threshold=0.55,
        log_prob_threshold=-1.0,
        compression_ratio_threshold=2.4,
        vad_filter=True,
        vad_parameters={"min_silence_duration_ms": 500},
    )
    text = " ".join(segment.text.strip() for segment in segments).strip()
    if not text:
        return ""
    # Tiny models can hallucinate stock captions on silence. Keep the fallback
    # path cleaner by rejecting common non-user phrases.
    lowered = text.lower()
    bad_phrases = ("thanks for watching", "subscribe", "music", "subtitle")
    if any(phrase in lowered for phrase in bad_phrases):
        return ""
    return text


def tool_neuron_transcribe_sync(wav_path: Path) -> str:
    boundary = f"----ai-buddy-{secrets.token_hex(12)}"
    wav_bytes = wav_path.read_bytes()
    parts = [
        (
            f"--{boundary}\r\n"
            'Content-Disposition: form-data; name="model"\r\n\r\n'
            f"{tool_neuron_model_id('stt', TOOL_NEURON_STT_MODEL)}\r\n"
        ).encode("utf-8"),
        (
            f"--{boundary}\r\n"
            'Content-Disposition: form-data; name="file"; filename="voice.wav"\r\n'
            "Content-Type: audio/wav\r\n\r\n"
        ).encode("utf-8"),
        wav_bytes,
        f"\r\n--{boundary}--\r\n".encode("utf-8"),
    ]
    req = urllib.request.Request(
        tool_neuron_url("/audio/transcriptions"),
        data=b"".join(parts),
        headers=tool_neuron_headers(f"multipart/form-data; boundary={boundary}"),
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=60) as response:
        body = json.loads(response.read().decode("utf-8", errors="replace"))
    return clean_transcript(body.get("text", ""))


def clean_transcript(text: str) -> str:
    text = " ".join(text.replace("\n", " ").split()).strip()
    if not text:
        return ""
    lowered = text.lower()
    bad_phrases = ("thanks for watching", "subscribe", "music", "subtitle")
    if any(phrase in lowered for phrase in bad_phrases):
        return ""
    words = lowered.split()
    if len(words) > 45:
        unique_ratio = len(set(words)) / max(1, len(words))
        if unique_ratio < 0.28:
            return ""
    repeated_pairs = ("i think", "i don't know", "thank you", "hello hello")
    if any(lowered.count(pair) >= 4 for pair in repeated_pairs):
        return ""
    return text[:220]


async def generate_text(prompt: str) -> str:
    text, _provider = await generate_text_with_provider(prompt)
    return text


def gemini_generate_text_sync(prompt: str) -> str | None:
    client = gemini_client()
    response = client.models.generate_content(
        model=GEMINI_TEXT_MODEL,
        contents=f"{BUDDY_SYSTEM_PROMPT}\n\nUser/request: {prompt}",
    )
    return clean_buddy_text(response.text) if response.text else None


def clean_buddy_text(text: str) -> str:
    text = re.sub(r"<think>.*?</think>", " ", text, flags=re.IGNORECASE | re.DOTALL)
    if text.lower().lstrip().startswith("<think>"):
        text = ""
    text = " ".join(text.replace("\n", " ").split()).strip(" \"'")
    if BUDDY_MAX_RESPONSE_CHARS > 0 and len(text) > BUDDY_MAX_RESPONSE_CHARS:
        text = text[:BUDDY_MAX_RESPONSE_CHARS].rsplit(" ", 1)[0].rstrip(".,;:") + "."
    return text or "I am here and ready."


async def generate_text_with_provider(prompt: str) -> tuple[str, str]:
    if tool_neuron_configured():
        try:
            text = await asyncio.to_thread(tool_neuron_generate_text_sync, prompt)
            if text:
                return text, "tool_neuron"
        except Exception as exc:
            print(f"Tool Neuron text failed, falling back: {exc}")

    if ollama_configured():
        try:
            text = await asyncio.to_thread(ollama_generate_text_sync, prompt)
            if text:
                return text, "ollama"
        except Exception as exc:
            print(f"Ollama text failed, falling back: {exc}")

    local_client = local_openai_client()
    if local_client:
        try:
            response = local_client.chat.completions.create(
                model=LOCAL_OPENAI_TEXT_MODEL,
                messages=[
                    {"role": "system", "content": BUDDY_SYSTEM_PROMPT},
                    {"role": "user", "content": prompt},
                ],
                temperature=0.8,
                max_tokens=TOOL_NEURON_MAX_TOKENS,
            )
            text = response.choices[0].message.content
            if text:
                return clean_buddy_text(text), "local_openai"
        except Exception as exc:
            print(f"Local OpenAI-compatible text failed, falling back: {exc}")

    if USE_CLOUD_FALLBACKS and os.getenv("GEMINI_API_KEY"):
        try:
            text = await asyncio.to_thread(gemini_generate_text_sync, prompt)
            if text:
                return text, "gemini"
        except Exception as exc:
            print(f"Gemini text failed, falling back: {exc}")

    if USE_CLOUD_FALLBACKS and os.getenv("OPENAI_API_KEY"):
        try:
            response = openai_client().responses.create(
                model=TEXT_MODEL,
                input=[
                    {"role": "system", "content": BUDDY_SYSTEM_PROMPT},
                    {"role": "user", "content": prompt},
                ],
            )
            if response.output_text:
                return clean_buddy_text(response.output_text), "openai"
        except Exception as exc:
            print(f"OpenAI text failed, falling back: {exc}")

    return local_reply(prompt), "local"


def local_reply(prompt: str) -> str:
    prompt_l = prompt.lower()
    if "user said" in prompt_l:
        return "I heard you. My cloud brain is not fully connected yet, but I am listening."
    if "button" in prompt_l:
        return "Hello from local buddy mode. The button works, and I am ready for the server."
    return "I am here with you, even in fallback mode."


def write_tone_wav(text: str) -> str:
    filename = f"{int(time.time())}-{secrets.token_hex(4)}-local.wav"
    out_path = AUDIO_DIR / filename
    sample_rate = 8000
    duration = min(2.4, max(0.7, len(text) / 42))
    frames = bytearray()
    for i in range(int(sample_rate * duration)):
        t = i / sample_rate
        env = min(1.0, i / 800) * min(1.0, (sample_rate * duration - i) / 1200)
        value = 128 + int(42 * env * math.sin(2 * math.pi * 440 * t))
        frames.append(max(0, min(255, value)))
    with wave.open(str(out_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(sample_rate)
        wav.writeframes(bytes(frames))
    return f"/audio/{filename}"


def write_esp_wav_from_samples(samples: list[int], out_path: Path, src_rate: int) -> None:
    if not samples:
        samples = [128] * 800
        src_rate = 8000
    ratio = max(1, round(src_rate / 8000))
    frames = bytearray(samples[::ratio])
    with wave.open(str(out_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(8000)
        wav.writeframes(bytes(frames))


def convert_wav_to_u8_8k(src_path: Path, out_path: Path) -> None:
    with wave.open(str(src_path), "rb") as src:
        channels = src.getnchannels()
        width = src.getsampwidth()
        rate = src.getframerate()
        raw = src.readframes(src.getnframes())

    samples: list[int] = []
    frame_width = channels * width
    for index in range(0, len(raw) - frame_width + 1, frame_width):
        if width == 1:
            value = raw[index]
        elif width == 2:
            sample = int.from_bytes(raw[index:index + 2], "little", signed=True)
            value = 128 + (sample // 256)
        elif width == 4:
            sample = int.from_bytes(raw[index:index + 4], "little", signed=True)
            value = 128 + (sample // 16777216)
        else:
            value = 128
        samples.append(max(0, min(255, value)))

    write_esp_wav_from_samples(samples, out_path, rate)


def slow_u8_8k_wav_in_place(path: Path, factor: float = 1.5) -> None:
    """Stretch ESP-friendly 8-bit mono WAV audio without changing playback rate."""
    if factor <= 1.01:
        return
    with wave.open(str(path), "rb") as src:
        channels = src.getnchannels()
        width = src.getsampwidth()
        rate = src.getframerate()
        frames = src.readframes(src.getnframes())
    if channels != 1 or width != 1 or rate != 8000 or not frames:
        return

    stretched = bytearray()
    for i in range(max(1, int(len(frames) * factor))):
        src_pos = i / factor
        left = int(src_pos)
        right = min(len(frames) - 1, left + 1)
        frac = src_pos - left
        sample = int(frames[left] * (1.0 - frac) + frames[right] * frac)
        stretched.append(max(0, min(255, sample)))

    with wave.open(str(path), "wb") as dst:
        dst.setnchannels(1)
        dst.setsampwidth(1)
        dst.setframerate(8000)
        dst.writeframes(bytes(stretched))


def tool_neuron_synthesize_speech_sync(text: str, out_path: Path) -> bool:
    payload = {
        "model": tool_neuron_model_id("tts", TOOL_NEURON_TTS_MODEL),
        "voice": TOOL_NEURON_TTS_VOICE,
        "input": text,
        "response_format": "wav",
    }
    req = urllib.request.Request(
        tool_neuron_url("/audio/speech"),
        data=json.dumps(payload).encode("utf-8"),
        headers=tool_neuron_headers(),
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=45) as response:
        audio = response.read()
    if not audio:
        return False
    with tempfile.TemporaryDirectory() as temp_dir:
        raw_wav = Path(temp_dir) / "tool_neuron.wav"
        raw_wav.write_bytes(audio)
        convert_wav_to_u8_8k(raw_wav, out_path)
    return True


def windows_sapi_tts_sync(text: str, out_path: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp_dir:
        text_path = Path(temp_dir) / "tts.txt"
        raw_wav = Path(temp_dir) / "sapi.wav"
        text_path.write_text(text, encoding="utf-8")
        script = (
            "Add-Type -AssemblyName System.Speech; "
            f"$text = Get-Content -Raw -LiteralPath '{text_path}'; "
            "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
            "$s.Rate = 1; $s.Volume = 95; "
            f"$s.SetOutputToWaveFile('{raw_wav}'); "
            "$s.Speak($text); $s.Dispose();"
        )
        result = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
            capture_output=True,
            text=True,
            timeout=25,
        )
        if result.returncode != 0 or not raw_wav.exists():
            print(f"Windows SAPI TTS failed: {result.stderr.strip()}")
            return False
        convert_wav_to_u8_8k(raw_wav, out_path)
        return True


def pcm16_24k_to_u8_8k_wav(pcm: bytes, out_path: Path) -> None:
    """Convert Gemini TTS raw 24 kHz signed 16-bit PCM to ESP32-friendly WAV."""
    frames = bytearray()
    # Downsample 24 kHz -> 8 kHz by taking every third 16-bit sample.
    for index in range(0, len(pcm) - 1, 6):
        sample = int.from_bytes(pcm[index:index + 2], "little", signed=True)
        frames.append(max(0, min(255, 128 + (sample // 256))))
    with wave.open(str(out_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(8000)
        wav.writeframes(bytes(frames))


def extract_gemini_audio_bytes(response) -> bytes | None:
    for candidate in getattr(response, "candidates", []) or []:
        content = getattr(candidate, "content", None)
        for part in getattr(content, "parts", []) or []:
            inline_data = getattr(part, "inline_data", None)
            data = getattr(inline_data, "data", None)
            if data:
                return data
    return None


def gemini_synthesize_speech_sync(text: str) -> bytes | None:
    client = gemini_client()
    response = client.models.generate_content(
        model=GEMINI_TTS_MODEL,
        contents=text,
        config=types.GenerateContentConfig(
            response_modalities=["AUDIO"],
            speech_config=types.SpeechConfig(
                voice_config=types.VoiceConfig(
                    prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name=GEMINI_TTS_VOICE)
                )
            ),
        ),
    )
    return extract_gemini_audio_bytes(response)


async def synthesize_speech(text: str) -> str:
    url, _provider = await synthesize_speech_with_provider(text)
    return url


async def synthesize_speech_with_provider(text: str) -> tuple[str, str]:
    filename = f"{int(time.time())}-{secrets.token_hex(4)}.wav"
    out_path = AUDIO_DIR / filename

    if tool_neuron_configured():
        try:
            if await asyncio.to_thread(tool_neuron_synthesize_speech_sync, text, out_path):
                return f"/audio/{filename}", "tool_neuron"
        except Exception as exc:
            print(f"Tool Neuron TTS failed, falling back: {exc}")

    try:
        if await asyncio.to_thread(windows_sapi_tts_sync, text, out_path):
            return f"/audio/{filename}", "windows_sapi"
    except Exception as exc:
        print(f"Windows SAPI TTS failed, falling back: {exc}")

    if USE_CLOUD_FALLBACKS and os.getenv("GEMINI_API_KEY"):
        try:
            audio = await asyncio.to_thread(gemini_synthesize_speech_sync, text)
            if audio:
                pcm16_24k_to_u8_8k_wav(audio, out_path)
                return f"/audio/{filename}", "gemini"
        except Exception as exc:
            print(f"Gemini TTS failed, falling back: {exc}")

    if USE_CLOUD_FALLBACKS and os.getenv("OPENAI_API_KEY"):
        try:
            speech = openai_client().audio.speech.create(
                model=TTS_MODEL,
                voice=TTS_VOICE,
                input=text,
                response_format="wav",
            )
            out_path.write_bytes(speech.read())
            return f"/audio/{filename}", "openai"
        except Exception as exc:
            print(f"OpenAI TTS failed, falling back: {exc}")

    return write_tone_wav(text), "local_tone"


async def transcribe_raw_audio(raw_path: Path) -> str:
    text, _provider = await transcribe_raw_audio_with_provider(raw_path)
    return text


async def transcribe_raw_audio_with_provider(raw_path: Path) -> tuple[str, str]:
    wav_path = raw8_to_wav(raw_path)
    try:
        if tool_neuron_configured():
            try:
                text = await asyncio.to_thread(tool_neuron_transcribe_sync, wav_path)
                if text:
                    return text, "tool_neuron"
                return "voice message received", "tool_neuron_empty"
            except Exception as exc:
                print(f"Tool Neuron STT failed, falling back: {exc}")

        if local_stt_configured():
            try:
                text = clean_transcript(await asyncio.to_thread(transcribe_local_whisper_sync, wav_path))
                if text:
                    return text, "local_whisper"
                return "voice message received", "local_whisper_empty"
            except Exception as exc:
                print(f"Local Whisper transcription failed, falling back: {exc}")

        if USE_CLOUD_FALLBACKS and os.getenv("OPENAI_API_KEY"):
            try:
                with wav_path.open("rb") as audio_file:
                    result = openai_client().audio.transcriptions.create(
                        model=TRANSCRIBE_MODEL,
                        file=audio_file,
                    )
                return clean_transcript(result.text or ""), "openai"
            except Exception as exc:
                print(f"OpenAI transcription failed, falling back: {exc}")
        # Placeholder keeps the full hardware/server loop alive even when no STT
        # provider is configured.
        return "voice message received", "local_placeholder"
    finally:
        wav_path.unlink(missing_ok=True)


@app.get("/api/health")
def health():
    return {
        "ok": True,
        "cloud_fallbacks_enabled": USE_CLOUD_FALLBACKS,
        "tool_neuron_configured": tool_neuron_configured(),
        "tool_neuron_base_url": TOOL_NEURON_BASE_URL,
        "tool_neuron_chat_model": TOOL_NEURON_CHAT_MODEL,
        "tool_neuron_stt_model": TOOL_NEURON_STT_MODEL,
        "tool_neuron_tts_model": TOOL_NEURON_TTS_MODEL,
        "ollama_configured": ollama_configured(),
        "local_stt_configured": local_stt_configured(),
        "local_stt_model": LOCAL_STT_MODEL,
        "gemini_configured": bool(os.getenv("GEMINI_API_KEY")),
        "openai_configured": bool(os.getenv("OPENAI_API_KEY")),
        "local_openai_configured": bool(local_openai_client()),
        "local_fallback": True,
        "text_order": ["tool_neuron", "ollama", "local_openai", "gemini_if_enabled", "openai_if_enabled", "local"],
        "tts_order": ["tool_neuron", "windows_sapi", "gemini_if_enabled", "openai_if_enabled", "local_tone"],
        "transcribe_order": ["tool_neuron", "local_whisper", "openai_if_enabled", "local_placeholder"],
    }


@app.get("/api/tool-neuron-models")
async def tool_neuron_models():
    if not tool_neuron_configured():
        raise HTTPException(status_code=503, detail="Tool Neuron is not configured")
    models = await asyncio.to_thread(tool_neuron_models_sync)
    grouped: dict[str, list[dict]] = {}
    for model in models.get("data", []):
        model_type = model.get("type") or model.get("owned_by") or "unknown"
        grouped.setdefault(model_type, []).append(
            {
                "id": model.get("id"),
                "owned_by": model.get("owned_by"),
                "type": model.get("type"),
            }
        )
    return {
        "ok": True,
        "configured": {
            "chat": TOOL_NEURON_CHAT_MODEL,
            "stt": TOOL_NEURON_STT_MODEL,
            "tts": TOOL_NEURON_TTS_MODEL,
            "tts_voice": TOOL_NEURON_TTS_VOICE,
        },
        "models": models.get("data", []),
        "grouped": grouped,
    }


@app.post("/api/default-message")
async def default_message(payload: dict):
    mood = payload.get("mood", "IDLE")
    level = int(float(payload.get("mic_level", 0)))
    text, text_provider = await generate_text_with_provider(
        f"The user pressed my button. Mood: {mood}. Mic level: {level}. "
        "Say something friendly and useful."
    )
    audio_url, tts_provider = await synthesize_speech_with_provider(text)
    return {
        "text": text,
        "audio_url": f"{PUBLIC_BASE_URL}{audio_url}",
        "mood": "happy",
        "providers": {"text": text_provider, "tts": tts_provider},
    }


@app.post("/api/voice-message")
async def voice_message(
    request: Request,
    device_id: str = Form("unknown"),
    audio: UploadFile | None = File(None),
):
    raw_path = UPLOAD_DIR / f"{int(time.time())}-{secrets.token_hex(4)}-voice.raw"
    try:
        raw_bytes = await audio.read() if audio else await request.body()
        raw_path.write_bytes(raw_bytes)
        debug_wav = save_raw8_debug_wav(raw_path)
        print(f"Saved ESP32 voice upload: {raw_path} and {debug_wav} ({len(raw_bytes)} bytes)")
        transcript, stt_provider = await transcribe_raw_audio_with_provider(raw_path)
        text, text_provider = await generate_text_with_provider(
            f'The user said: "{transcript}". Answer like a small helpful desk buddy.'
        )
        audio_url, tts_provider = await synthesize_speech_with_provider(text)
        if request.headers.get("content-type", "").startswith("application/octet-stream"):
            return {
                "transcript": transcript,
                "text": text,
                "audio_url": audio_url,
                "providers": {"stt": stt_provider, "text": text_provider, "tts": tts_provider},
            }
        return {
            "transcript": transcript,
            "text": text,
            "audio_url": f"{PUBLIC_BASE_URL}{audio_url}",
            "mood": "happy",
            "providers": {"stt": stt_provider, "text": text_provider, "tts": tts_provider},
        }
    finally:
        pass


@app.exception_handler(Exception)
async def exception_handler(_request, exc: Exception):
    print(exc)
    return JSONResponse(status_code=500, content={"error": str(exc)})
