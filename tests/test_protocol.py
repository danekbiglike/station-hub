import importlib.util
import json
import pathlib
import struct
import unittest

path = pathlib.Path(__file__).parents[1] / "windows" / "station_audio.py"
spec = importlib.util.spec_from_file_location("station_audio", path)
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


class ProtocolTest(unittest.TestCase):
    def test_handshake_round_trip(self):
        data = audio.header("0123456789abcdef", 48000, 2)
        magic, key, rate, channels = struct.unpack("!4s16sII", data)
        self.assertEqual((magic, key, rate, channels), (b"SHU1", b"0123456789abcdef", 48000, 2))

    def test_frame_length_and_alignment(self):
        frame = audio.packet(b"\x01\x02\x03\x04" * 20, 2)
        self.assertEqual(struct.unpack("!I", frame[:4])[0], len(frame) - 4)
        with self.assertRaises(ValueError):
            audio.packet(b"\x00", 2)

    def test_mode_handshake(self):
        for mode, expected in (("quality", 0), ("game", 1)):
            packet = audio.header("0123456789abcdef", 48000, 2, mode)
            magic, key, rate, channels, mode_id = struct.unpack("!4s16sIII", packet)
            self.assertEqual((magic, mode_id), (b"SHU2", expected))
        with self.assertRaises(ValueError):
            audio.header("0123456789abcdef", 48000, 2, "unknown")

    def test_rejects_invalid_key_and_format(self):
        for key in ("", "bad", "g" * 16):
            with self.assertRaises(ValueError):
                audio.header(key, 48000, 2)
        with self.assertRaises(ValueError):
            audio.header("0" * 16, 48000, 3)

    def test_metadata_unicode_round_trip(self):
        frame = audio.metadata_packet("Песня", "Исполнитель")
        marker, length = struct.unpack("!iI", frame[:8])
        self.assertEqual(marker, -1)
        self.assertEqual(length, len(frame[8:]))
        self.assertEqual(json.loads(frame[8:]), {"title": "Песня", "artist": "Исполнитель"})


if __name__ == "__main__":
    unittest.main()
