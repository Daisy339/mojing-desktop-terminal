"""O'Clock cloud voice gateway.

Secrets are read from environment variables only. The default route remains
audio-echo until all three cloud providers are configured.
"""
import argparse
import hmac
import io
import json
import os
import re
import threading
import wave
import tempfile
import audioop
from pathlib import Path
import urllib.request
import urllib.error
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

RATE = 24000
MAX_BYTES = RATE * 2 * 10


def cloud_error_summary(stage, exc):
    details = [f"stage={stage}", f"type={type(exc).__name__}"]
    if type(exc).__name__ == "RequestFailure":
        for field in ("name", "http_code", "request_id", "message"):
            value = getattr(exc, field, None)
            if value is None:
                continue
            value = str(value)
            for key_name in ("DASHSCOPE_API_KEY", "DEEPSEEK_API_KEY",
                             "OCLOCK_GATEWAY_TOKEN"):
                secret = os.environ.get(key_name)
                if secret:
                    value = value.replace(secret, "[redacted]")
            value = re.sub(r"sk-[A-Za-z0-9._-]+", "[redacted]", value)
            details.append(f"{field}={' '.join(value.split())[:240]}")
    return " ".join(details)


def wav_bytes(pcm, rate=RATE):
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(pcm)
    return output.getvalue()


def pcm_to_wav(pcm):
    if not pcm or len(pcm) % 2 or len(pcm) > MAX_BYTES:
        raise ValueError("Expected 1-10 seconds or less of nonempty PCM16 mono audio")
    return wav_bytes(pcm)


def resample_wav(pcm, from_rate, to_rate):
    converted, _ = audioop.ratecv(pcm, 2, 1, from_rate, to_rate, None)
    return wav_bytes(converted, to_rate)


def cloud_configured():
    return all(os.environ.get(name) for name in (
        "DASHSCOPE_API_KEY", "DEEPSEEK_API_KEY")) and bool(
            os.environ.get("DASHSCOPE_WS_URL"))


def cloud_checks():
    return {name: bool(os.environ.get(name)) for name in (
        "DASHSCOPE_API_KEY", "DEEPSEEK_API_KEY", "DASHSCOPE_WS_URL")}


def transcribe(pcm):
    import dashscope
    from dashscope.audio.asr import Recognition
    dashscope.api_key = os.environ["DASHSCOPE_API_KEY"]
    dashscope.base_websocket_api_url = os.environ["DASHSCOPE_WS_URL"]
    # Paraformer 8k-v2 requires an 8 kHz WAV input. audioop is used only for
    # this gateway conversion; the ESP keeps its native 24 kHz PCM path.
    wav_data = resample_wav(pcm, RATE, 8000)
    with tempfile.TemporaryDirectory() as directory:
        audio_path = Path(directory) / "recording.wav"
        audio_path.write_bytes(wav_data)
        result = Recognition(model=os.environ.get(
            "DASHSCOPE_ASR_MODEL", "paraformer-realtime-8k-v2"),
            format="wav", sample_rate=8000, callback=None).call(str(audio_path))
    if result.status_code != HTTPStatus.OK:
        raise RuntimeError("ASR request failed")
    sentences = result.get_sentence() or []
    return "".join(item.get("text", "") for item in sentences).strip()


def answer(text):
    payload = json.dumps({
        "model": os.environ.get("DEEPSEEK_MODEL", "deepseek-chat"),
        "messages": [
            {"role": "system", "content": "你是O'Clock桌面终端助手。用简洁中文完整回答，通常不超过150字。"},
            {"role": "user", "content": text},
        ],
        "stream": False,
        "max_tokens": 512,
    }).encode()
    request = urllib.request.Request(
        os.environ.get("DEEPSEEK_BASE_URL", "https://api.deepseek.com") + "/chat/completions",
        data=payload,
        headers={"Authorization": "Bearer " + os.environ["DEEPSEEK_API_KEY"],
                 "Content-Type": "application/json"},
        method="POST")
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            body = json.loads(response.read())
    except (urllib.error.URLError, ValueError) as exc:
        raise RuntimeError("LLM request failed") from exc
    choice = body["choices"][0]
    if choice.get("finish_reason") == "length":
        print("Voice answer reached the LLM token limit", flush=True)
    return choice["message"]["content"].strip()


def synthesize(text):
    import dashscope
    from dashscope.audio.tts_v2 import AudioFormat, SpeechSynthesizer
    dashscope.api_key = os.environ["DASHSCOPE_API_KEY"]
    if os.environ.get("DASHSCOPE_WS_URL"):
        dashscope.base_websocket_api_url = os.environ["DASHSCOPE_WS_URL"]
    if os.environ.get("DASHSCOPE_HTTP_URL"):
        dashscope.base_http_api_url = os.environ["DASHSCOPE_HTTP_URL"]
    synthesizer = SpeechSynthesizer(
        model=os.environ.get("DASHSCOPE_TTS_MODEL", "cosyvoice-v3-flash"),
        voice=os.environ.get("DASHSCOPE_TTS_VOICE", "longanyang"),
        format=AudioFormat.PCM_22050HZ_MONO_16BIT)
    audio = synthesizer.call(text)
    if not audio:
        raise RuntimeError("TTS returned no audio")
    return wav_bytes(audio, 22050)


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, token):
        self.token = token
        self.audio_slot = threading.BoundedSemaphore(1)
        super().__init__(address, Handler)


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, fmt, *args):
        # Do not log headers, bodies, credentials or transcribed speech.
        pass

    def reply(self, status, payload, content_type="application/json"):
        if content_type == "application/json":
            payload = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/health":
            configured = cloud_configured()
            self.reply(200, {"status": "ok", "gateway_version": "cloud-v1",
                             "mode": "audio-echo",
                             "sample_rate": RATE, "channels": 1,
                             "sample_bits": 16, "cloud_configured": configured,
                             "cloud_checks": cloud_checks(),
                             "mode": "cloud-voice" if configured else "audio-echo"})
        else:
            self.reply(404, {"error": "not_found"})

    def do_POST(self):
        if self.path not in ("/audio/echo", "/audio/ask"):
            self.reply(404, {"error": "not_found"})
            return
        supplied = self.headers.get("Authorization", "")
        if self.server.token and not hmac.compare_digest(
                supplied.encode(), ("Bearer " + self.server.token).encode()):
            self.reply(401, {"error": "unauthorized"})
            return
        if self.headers.get("Transfer-Encoding"):
            self.reply(400, {"error": "content_length_required"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if length <= 0 or length % 2 or length > MAX_BYTES:
            self.reply(400, {"error": "invalid_pcm_length"})
            return
        if self.headers.get("Content-Type", "").split(";")[0] != "application/octet-stream":
            self.reply(415, {"error": "expected_pcm16le_24000_mono"})
            return
        if not self.server.audio_slot.acquire(blocking=False):
            self.reply(503, {"error": "busy"})
            return
        stage = "receive"
        try:
            pcm = self.rfile.read(length)
            if len(pcm) != length:
                self.reply(400, {"error": "incomplete_audio"})
                return
            if self.path == "/audio/echo":
                self.reply(200, pcm_to_wav(pcm), "audio/wav")
                return
            if not cloud_configured():
                self.reply(503, {"error": "cloud_not_configured"})
                return
            stage = "asr"
            transcript = transcribe(pcm)
            if not transcript:
                self.reply(422, {"error": "empty_transcript"})
                return
            stage = "llm"
            response_text = answer(transcript)
            stage = "tts"
            audio = synthesize(response_text)
            print(f"Voice response ready: answer_chars={len(response_text)} "
                  f"wav_bytes={len(audio)}", flush=True)
            stage = "send"
            if self.headers.get("X-OClock-Audio") == "raw":
                self.reply(200, audio, "audio/wav")
            else:
                self.reply(200, {"transcript": transcript, "answer": response_text,
                                 "audio": __import__("base64").b64encode(audio).decode()})
        except (TimeoutError, ConnectionError):
            self.close_connection = True
        except Exception as exc:
            print("Voice request failed: " + cloud_error_summary(stage, exc), flush=True)
            self.reply(502, {"error": "cloud_request_failed", "stage": stage})
        finally:
            self.server.audio_slot.release()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    token = os.environ.get("OCLOCK_GATEWAY_TOKEN", "")
    if args.host not in ("127.0.0.1", "localhost", "::1") and not token:
        parser.error("Set OCLOCK_GATEWAY_TOKEN before enabling LAN access")
    server = Server((args.host, args.port), token)
    print(f"O'Clock voice gateway: http://{args.host}:{args.port}")
    print("Routes: /health, /audio/echo, /audio/ask")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
