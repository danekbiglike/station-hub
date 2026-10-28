"""Send the Windows default playback mix to Station Hub on the local network."""
from __future__ import annotations

import argparse
import asyncio
import json
import os
import queue
import socket
import struct
import sys
import threading
import time
from pathlib import Path

MAGIC = b"SHU1"
PORT = 27361
MAX_FRAME = 65536


def header(pairing_key: str, sample_rate: int, channels: int, mode: str | None = None) -> bytes:
    key = pairing_key.strip().lower()
    if len(key) != 16 or any(c not in "0123456789abcdef" for c in key):
        raise ValueError("Ключ должен состоять из 16 шестнадцатеричных символов")
    if not 8000 <= sample_rate <= 96000 or channels not in (1, 2):
        raise ValueError("Неподдерживаемый формат звука")
    if mode is None:
        return struct.pack("!4s16sII", MAGIC, key.encode("ascii"), sample_rate, channels)
    if mode not in ("game", "quality"):
        raise ValueError("Неизвестный режим звука")
    return struct.pack("!4s16sIII", b"SHU2", key.encode("ascii"), sample_rate, channels, int(mode == "game"))


def packet(pcm: bytes, channels: int) -> bytes:
    if channels not in (1, 2) or not pcm or len(pcm) > MAX_FRAME or len(pcm) % (channels * 2):
        raise ValueError("Некорректный звуковой пакет")
    return struct.pack("!I", len(pcm)) + pcm


def metadata_packet(title: str, artist: str) -> bytes:
    payload = json.dumps({"title": title[:300], "artist": artist[:300]}, ensure_ascii=False).encode("utf-8")
    if len(payload) > 4096:
        raise ValueError("Слишком длинные сведения о медиа")
    return struct.pack("!iI", -1, len(payload)) + payload


def watch_windows_media(state: dict, stop: threading.Event) -> None:
    """Best-effort information from the current Windows media session."""
    try:
        from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as Manager
        from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionPlaybackStatus as Status
    except ImportError:
        return

    async def watch():
        manager = await Manager.request_async()
        while not stop.is_set():
            try:
                session = manager.get_current_session()
                if session and session.get_playback_info().playback_status == Status.PLAYING:
                    props = await session.try_get_media_properties_async()
                    state["metadata"] = (props.title or "", props.artist or "")
                else:
                    state["metadata"] = ("", "")
            except Exception:
                state["metadata"] = ("", "")
            await asyncio.sleep(2)
    try:
        asyncio.run(watch())
    except Exception:
        pass  # Audio continues if Windows does not provide media information.


def run(host: str, pairing_key: str, port: int = PORT, mode: str = "quality") -> None:
    libdir = Path(__file__).resolve().parents[2] / "tools" / "pc-audio-libs"
    if libdir.is_dir():
        sys.path.insert(0, str(libdir))
    import pyaudiowpatch as pyaudio

    audio = pyaudio.PyAudio()
    stream = None
    captured: queue.Queue[bytes] = queue.Queue(maxsize=3 if mode == "game" else 4)
    media_state = {"metadata": ("", "")}
    stop_metadata = threading.Event()
    metadata_worker = threading.Thread(target=watch_windows_media, args=(media_state, stop_metadata), daemon=True)
    try:
        device = audio.get_default_wasapi_loopback()
        rate = int(device["defaultSampleRate"])
        channels = min(2, int(device["maxInputChannels"]))
        if channels < 1:
            raise RuntimeError("Выход Windows не поддерживает захват звука")
        packet_header = header(pairing_key, rate, channels, mode)
        frames = max(128, rate // (100 if mode == "game" else 50))
        def on_audio(in_data, frame_count, time_info, status_flags):
            try:
                captured.put_nowait(in_data)
            except queue.Full:
                try:
                    captured.get_nowait()
                    captured.put_nowait(in_data)
                except (queue.Empty, queue.Full):
                    pass
            return (None, pyaudio.paContinue)

        stream = audio.open(format=pyaudio.paInt16, channels=channels, rate=rate,
                            input=True, input_device_index=device["index"], frames_per_buffer=frames,
                            stream_callback=on_audio)
        metadata_worker.start()
        print(f"Передача звука с «{device['name']}» на {host}:{port}. Для остановки нажмите Ctrl+C.", flush=True)
        while True:
            try:
                with socket.create_connection((host, port), timeout=5) as connection:
                    connection.settimeout(5)
                    connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    connection.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096 if mode == "game" else 8192)
                    connection.sendall(packet_header)
                    while not captured.empty():
                        try:
                            captured.get_nowait()
                        except queue.Empty:
                            break
                    print("Станция подключена.", flush=True)
                    last_metadata = None
                    while True:
                        metadata = media_state["metadata"]
                        if metadata != last_metadata:
                            connection.sendall(metadata_packet(*metadata))
                            last_metadata = metadata
                        try:
                            # Callbacks are not scheduled at perfectly regular 20 ms
                            # intervals. Allow jitter before substituting silence.
                            pcm = captured.get(timeout=max(0.12, 3 * frames / rate))
                        except queue.Empty:
                            pcm = bytes(frames * channels * 2)
                        connection.sendall(packet(pcm, channels))
            except (OSError, EOFError) as exc:
                print(f"Связь потеряна: {exc}. Повторная попытка через 3 секунды.", flush=True)
                time.sleep(3)
    finally:
        stop_metadata.set()
        if stream is not None:
            stream.stop_stream()
            stream.close()
        audio.terminate()


def main() -> None:
    parser = argparse.ArgumentParser(description="Передача звука Windows на Station Hub")
    parser.add_argument("host", help="IP Станции в домашней сети")
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--key", default=os.environ.get("STATION_HUB_KEY"), help="16-значный ключ на экране Станции")
    parser.add_argument("--mode", choices=("game", "quality"), default="quality", help="Режим звука")
    args = parser.parse_args()
    key = args.key or input("Ключ с экрана Станции: ").strip()
    try:
        run(args.host, key, args.port, args.mode)
    except KeyboardInterrupt:
        print("\nПередача остановлена.")
    except Exception as exc:
        print(f"Не удалось начать передачу: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
