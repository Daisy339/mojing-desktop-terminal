import http.client
import io
import json
import os
import threading
import unittest
import wave
from unittest.mock import patch

from server import MAX_BYTES, Server, cloud_error_summary, wav_bytes


class GatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = Server(("127.0.0.1", 0), "local-test-token")
        cls.worker = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.worker.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.worker.join()

    def request(self, path, body=None, token="local-test-token", length=None):
        conn = http.client.HTTPConnection(*self.server.server_address, timeout=3)
        headers = {"Content-Type": "application/octet-stream",
                   "Authorization": "Bearer " + token}
        if length is not None:
            headers["Content-Length"] = str(length)
        conn.request("GET" if body is None else "POST", path, body, headers)
        response = conn.getresponse()
        result = response.status, response.read()
        conn.close()
        return result

    def test_raw_audio_response_is_available_for_device(self):
        pcm = b"\x8a\x05" * (24000 * 5)
        expected = wav_bytes(b"\x01\x02" * (22050 * 20), 22050)
        with patch.dict(os.environ, {"DASHSCOPE_API_KEY": "test",
                                  "DEEPSEEK_API_KEY": "test",
                                  "DASHSCOPE_WS_URL": "wss://example.invalid"}), \
             patch("server.transcribe", return_value="测试问题"), \
             patch("server.answer", return_value="测试回答"), \
             patch("server.synthesize", return_value=expected):
            conn = http.client.HTTPConnection(*self.server.server_address, timeout=3)
            conn.request("POST", "/audio/ask", pcm, {
                "Content-Type": "application/octet-stream",
                "Authorization": "Bearer local-test-token",
                "X-OClock-Audio": "raw"})
            response = conn.getresponse()
            self.assertEqual(response.status, 200)
            self.assertEqual(response.getheader("Content-Type"), "audio/wav")
            self.assertEqual(response.read(), expected)
            conn.close()

    def test_health_explicitly_reports_no_cloud(self):
        status, body = self.request("/health")
        self.assertEqual(status, 200)
        self.assertFalse(json.loads(body)["cloud_configured"])

    def test_cloud_failure_reports_stage_without_credentials(self):
        class RequestFailure(Exception):
            name = "InvalidVoice"
            http_code = 400
            request_id = "request-123"
            message = "voice rejected for sk-private-value"

        with patch.dict(os.environ, {"DASHSCOPE_API_KEY": "sk-private-value"}):
            summary = cloud_error_summary("tts", RequestFailure())
        self.assertIn("stage=tts", summary)
        self.assertIn("name=InvalidVoice", summary)
        self.assertIn("request_id=request-123", summary)
        self.assertNotIn("sk-private-value", summary)

    def test_tts_failure_returns_stage(self):
        with patch.dict(os.environ, {"DASHSCOPE_API_KEY": "test",
                                  "DEEPSEEK_API_KEY": "test",
                                  "DASHSCOPE_WS_URL": "wss://example.invalid"}), \
             patch("server.transcribe", return_value="测试问题"), \
             patch("server.answer", return_value="测试回答"), \
             patch("server.synthesize", side_effect=RuntimeError("failed")):
            status, body = self.request("/audio/ask", b"\0\0")
        self.assertEqual(status, 502)
        self.assertEqual(json.loads(body)["stage"], "tts")

    def test_cloud_route_is_clear_when_not_configured(self):
        self.assertEqual(self.request("/audio/ask", b"\0\0")[0], 503)

    def test_five_second_recording_is_preserved(self):
        pcm = b"\x8a\x05" * (24000 * 5)
        status, body = self.request("/audio/echo", pcm)
        self.assertEqual(status, 200)
        with wave.open(io.BytesIO(body), "rb") as wav:
            self.assertEqual((wav.getframerate(), wav.getnchannels(), wav.getsampwidth()),
                             (24000, 1, 2))
            self.assertEqual(wav.readframes(wav.getnframes()), pcm)

    def test_bad_token_is_rejected(self):
        self.assertEqual(self.request("/audio/echo", b"\0\0", token="wrong")[0], 401)

    def test_invalid_and_oversized_audio_is_rejected_before_read(self):
        for length in (0, 1, MAX_BYTES + 2):
            self.assertEqual(self.request("/audio/echo", b"", length=length)[0], 400)


if __name__ == "__main__":
    unittest.main()
