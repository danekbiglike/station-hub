#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include "yandex_like.h"
#include "station_control.h"
#include "station_discovery.h"
#include "secure_pairing.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using winrt::com_ptr;
using namespace winrt::Windows::Media::Control;

namespace {
constexpr int PORT = 27361;
constexpr int ID_GAME = 100, ID_QUALITY = 101, ID_STOP = 102, ID_KEY = 103, ID_POLICY = 104, ID_STATION = 105;
constexpr int ID_DISCOVER = 106, ID_FOUND = 107, ID_EXIT = 108, ID_HOST = 109;
constexpr int ID_PAIR = 110, ID_FORGET = 111, ID_KEY_FIELD = 112;
constexpr int ID_STARTUP = 113, ID_AUTO_AUDIO = 114, ID_AUTO_MODE = 115, ID_WIDGET_SKIN = 116;
constexpr UINT WM_VOICE_MODE = WM_APP + 1;
constexpr UINT WM_TRAY = WM_APP + 32;
constexpr UINT WM_DISCOVERY = WM_APP + 33;
constexpr UINT WM_PAIRING = WM_APP + 34;
constexpr int ID_TRAY_OPEN = 301, ID_TRAY_WIDGET = 302, ID_TRAY_EXIT = 303;
HWND hostField{}, keyField{}, statusField{}, policyField{}, bridgeField{}, window{};
HWND stationsField{}, discoveryField{}, discoverButton{};
HWND pairingField{}, pairButton{};
HWND startupField{}, autoAudioField{}, autoModeField{};
HWND widgetSkinField{};
bool autoAudioPending=false;
std::wstring preferredStation;
std::thread discoveryThread;
std::atomic_bool discoveryStop{false};
std::thread pairingThread;
std::atomic_bool pairingStop{false};
std::vector<DiscoveredStation> discoveredStations;
bool discoveryRunning=false;
bool pairingRunning=false;
unsigned hostRevision=0;
unsigned keyRevision=0;
std::wstring keyAssociatedHost;
HFONT uiFont{}, headingFont{};
HBRUSH mainBackground{}, fieldBackground{};
NOTIFYICONDATAW tray{};
bool trayAdded=false;
UINT taskbarCreated=0;
std::atomic<int> ambiguityPolicy{3};
std::atomic_bool bridgeOnline{false};
std::wstring preferencesPath;
std::thread worker;
std::shared_ptr<std::atomic_bool> cancel;
std::mutex statusMutex;
std::wstring statusText = L"Передача остановлена";

void status(const wchar_t* s) {
    std::lock_guard lock(statusMutex);
    statusText = s;
}
std::wstring windowText(HWND control) {
    int n = GetWindowTextLengthW(control);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(control, s.data(), n + 1);
    s.resize(n);
    return s;
}
void integer(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}
std::string jsonString(const std::wstring& in) {
    std::string src = winrt::to_string(in.substr(0, 300)), out;
    for (unsigned char c : src) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) out += ' ';
        else out += static_cast<char>(c);
    }
    return out;
}
struct MediaState {
    std::mutex mutex;
    std::string json = "{\"title\":\"\",\"artist\":\"\"}";
    std::vector<uint8_t> artwork;
    uint64_t artworkVersion = 0;
    std::deque<std::string> results;
};

void mediaLoop(std::shared_ptr<MediaState> state, std::shared_ptr<std::atomic_bool> stop) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        std::wstring artworkKey;
        while (!*stop) {
            std::wstring title, artist;
            int64_t positionMs = -1, durationMs = -1;
            bool playing = false, nextEnabled = false;
            try {
                auto session = manager.GetCurrentSession();
                if (session && session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                    playing = true;
                    nextEnabled = session.GetPlaybackInfo().Controls().IsNextEnabled();
                    auto p = session.TryGetMediaPropertiesAsync().get();
                    title = p.Title().c_str(); artist = p.Artist().c_str();
                    auto timeline = session.GetTimelineProperties();
                    positionMs = timeline.Position().count() / 10000;
                    durationMs = timeline.EndTime().count() / 10000;
                    // Windows reports a position snapshot, not a continuously advancing value.
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(winrt::clock::now() - timeline.LastUpdatedTime()).count();
                    if (positionMs >= 0 && elapsed >= 0 && elapsed < 86400000) positionMs += elapsed;
                    if (positionMs < 0) positionMs = -1;
                    if (durationMs <= 0) durationMs = -1;
                    else if (positionMs > durationMs) positionMs = durationMs;
                    std::wstring newKey = title + L"\n" + artist + L"\n" + p.AlbumTitle().c_str();
                    if (newKey != artworkKey) {
                        std::vector<uint8_t> bytes;
                        try {
                            auto thumbnail = p.Thumbnail();
                            if (thumbnail) {
                                auto input = thumbnail.OpenReadAsync().get();
                                auto size = input.Size();
                                if (size > 0 && size <= 131072) {
                                    winrt::Windows::Storage::Streams::DataReader reader(input.GetInputStreamAt(0));
                                    uint32_t loaded = reader.LoadAsync(static_cast<uint32_t>(size)).get();
                                    if (loaded == size) { bytes.resize(loaded); reader.ReadBytes(bytes); }
                                    reader.Close(); input.Close();
                                }
                            }
                        } catch (...) {}
                        artworkKey = newKey;
                        { std::lock_guard lock(state->mutex); state->artwork = std::move(bytes); ++state->artworkVersion; }
                    }
                }
            } catch (...) {}
            const int policy = ambiguityPolicy.load();
            const char* preference = policy == 1 ? "pc" : policy == 2 ? "alice" : policy == 3 ? "station" : "ask";
            auto json = "{\"title\":\"" + jsonString(title) + "\",\"artist\":\"" + jsonString(artist) +
                "\",\"playing\":" + (playing ? "true" : "false") + ",\"next_enabled\":" + (nextEnabled ? "true" : "false") +
                ",\"ambiguity\":\"" + preference + "\",\"position_ms\":" + std::to_string(positionMs) +
                ",\"duration_ms\":" + std::to_string(durationMs) + "}";
            { std::lock_guard lock(state->mutex); state->json = std::move(json); }
            for (int i = 0; i < 20 && !*stop; ++i) std::this_thread::sleep_for(100ms);
        }
    } catch (...) {}
}

struct Socket {
    SOCKET s = INVALID_SOCKET;
    ~Socket() { if (s != INVALID_SOCKET) closesocket(s); }
};
struct EventHandle {
    HANDLE h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~EventHandle() { if (h) CloseHandle(h); }
};
void skipCurrentTrack() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        auto session = manager.GetCurrentSession();
        if (!session || session.GetPlaybackInfo().PlaybackStatus() != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ||
            !session.GetPlaybackInfo().Controls().IsNextEnabled() || !session.TrySkipNextAsync().get()) {
            status(L"Плеер не поддержал переключение трека.");
        }
    } catch (...) { status(L"Не удалось переключить трек на компьютере."); }
}
bool sendAll(SOCKET s, const uint8_t* p, size_t size, std::atomic_bool& stop) {
    auto deadline = std::chrono::steady_clock::now() + 1500ms;
    while (size && !stop) {
        int n = send(s, reinterpret_cast<const char*>(p), static_cast<int>(size), 0);
        if (n > 0) { p += n; size -= static_cast<size_t>(n); continue; }
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            fd_set writefds; FD_ZERO(&writefds); FD_SET(s, &writefds);
            timeval t{0, 20000};
            if (select(0, nullptr, &writefds, nullptr, &t) < 0) return false;
            continue;
        }
        return false;
    }
    return size == 0;
}
bool frame(SOCKET s, const uint8_t* pcm, size_t bytes, std::atomic_bool& stop) {
    thread_local std::vector<uint8_t> data;
    data.clear(); data.reserve(bytes + 4); integer(data, static_cast<uint32_t>(bytes));
    data.insert(data.end(), pcm, pcm + bytes);
    return sendAll(s, data.data(), data.size(), stop);
}
bool metaFrame(SOCKET s, const std::string& json, std::atomic_bool& stop) {
    if (json.size() > 4096) return true;
    std::vector<uint8_t> data; integer(data, 0xffffffff); integer(data, static_cast<uint32_t>(json.size()));
    data.insert(data.end(), json.begin(), json.end());
    return sendAll(s, data.data(), data.size(), stop);
}
bool artworkFrame(SOCKET s, const std::vector<uint8_t>& image, std::atomic_bool& stop) {
    if (image.size() > 131072) return false;
    std::vector<uint8_t> packet; packet.reserve(image.size() + 8);
    integer(packet, 0xfffffffd); integer(packet, static_cast<uint32_t>(image.size()));
    packet.insert(packet.end(), image.begin(), image.end());
    return sendAll(s, packet.data(), packet.size(), stop);
}
bool responseFrame(SOCKET s, const std::string& response, std::atomic_bool& stop) {
    if (response.empty() || response.size() > 128) return false;
    std::vector<uint8_t> packet; integer(packet, 0xfffffffe); integer(packet, static_cast<uint32_t>(response.size()));
    packet.insert(packet.end(), response.begin(), response.end());
    return sendAll(s, packet.data(), packet.size(), stop);
}
bool connectStation(Socket& sock, const std::string& host, int sendBuffer, std::atomic_bool& stop) {
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(host.c_str(), "27361", &hints, &list) != 0) return false;
    for (auto a = list; a && !stop; a = a->ai_next) {
        SOCKET s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nonblocking = 1; ioctlsocket(s, FIONBIO, &nonblocking);
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one), sizeof(one));
        setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&sendBuffer), sizeof(sendBuffer));
        int result = connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
        if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writefds; FD_ZERO(&writefds); FD_SET(s, &writefds); timeval wait{2, 0};
            result = select(0, nullptr, &writefds, nullptr, &wait);
            int error = 0, n = sizeof(error);
            if (result > 0 && getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &n) == 0 && error == 0) result = 0;
            else result = SOCKET_ERROR;
        }
        if (result == 0) { sock.s = s; break; }
        closesocket(s);
    }
    freeaddrinfo(list);
    return sock.s != INVALID_SOCKET;
}

enum class SampleType { Float32, Pcm16, Pcm24, Pcm32 };
bool format(const WAVEFORMATEX* f, SampleType& type) {
    WORD tag = f->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        const auto* ex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        if (ex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (ex->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) tag = WAVE_FORMAT_PCM;
        else return false;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && f->wBitsPerSample == 32) type = SampleType::Float32;
    else if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 16) type = SampleType::Pcm16;
    else if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 24) type = SampleType::Pcm24;
    else if (tag == WAVE_FORMAT_PCM && f->wBitsPerSample == 32) type = SampleType::Pcm32;
    else return false;
    return true;
}
int16_t sample(const BYTE* p, SampleType type) {
    if (type == SampleType::Float32) {
        float v; std::memcpy(&v, p, 4);
        if (!std::isfinite(v)) v = 0;
        return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    if (type == SampleType::Pcm16) { int16_t v; std::memcpy(&v, p, 2); return v; }
    if (type == SampleType::Pcm24) { uint16_t high = static_cast<uint16_t>(p[1]) | static_cast<uint16_t>(p[2] << 8); return static_cast<int16_t>(high); }
    int32_t v; std::memcpy(&v, p, 4); return static_cast<int16_t>(v >> 16);
}

void audioLoop(std::string host, std::string key, bool game, std::shared_ptr<std::atomic_bool> stop) {
    try {
        wchar_t diagPath[1024]{};
        std::ofstream diag;
        if (GetEnvironmentVariableW(L"STATION_HUB_DIAGNOSTICS", diagPath, 1024))
            diag.open(std::filesystem::path(diagPath), std::ios::out | std::ios::app);
        uint64_t capturePackets = 0, discontinuities = 0, silentPackets = 0, reconnects = 0;
        int64_t maxCaptureGap = 0;
        std::atomic<int64_t> maxSendTime{0};
        std::atomic<uint64_t> droppedPackets{0};
        auto reportAt = std::chrono::steady_clock::now() + 5s;
        auto lastCapture = std::chrono::steady_clock::now();
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto media = std::make_shared<MediaState>();
        std::thread(mediaLoop, media, stop).detach();
        auto enumerator = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
        com_ptr<IMMDevice> device; winrt::check_hresult(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()));
        com_ptr<IAudioClient> client;
        winrt::check_hresult(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void()));
        WAVEFORMATEX* rawFormat = nullptr; winrt::check_hresult(client->GetMixFormat(&rawFormat));
        auto releaseFormat = std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)>(rawFormat, &CoTaskMemFree);
        SampleType type{};
        if (!format(rawFormat, type) || rawFormat->nChannels == 0 || rawFormat->nSamplesPerSec < 8000 || rawFormat->nSamplesPerSec > 96000)
            throw std::runtime_error("unsupported output format");
        const int channels = rawFormat->nChannels == 1 ? 1 : 2;
        const auto rate = rawFormat->nSamplesPerSec;
        EventHandle audioReady;
        if (!audioReady.h) throw std::runtime_error("event creation failed");
        // WASAPI loopback captures the selected output mix, not a microphone and
        // not a virtual playback device. Packet capture must remain independent
        // from potentially slow network writes to avoid blocking the audio thread.
        winrt::check_hresult(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, rawFormat, nullptr));
        winrt::check_hresult(client->SetEventHandle(audioReady.h));
        com_ptr<IAudioCaptureClient> capture;
        winrt::check_hresult(client->GetService(__uuidof(IAudioCaptureClient), capture.put_void()));
        winrt::check_hresult(client->Start());
        std::vector<int16_t> pcm;
        while (!*stop) {
            bridgeOnline = false;
            Socket sock;
            ++reconnects;
            status(L"Подключение к Станции…");
            if (!connectStation(sock, host, game ? 4096 : 8192, *stop)) {
                for (int i = 0; i < 20 && !*stop; ++i) std::this_thread::sleep_for(100ms);
                continue;
            }
            // Discard stale capture data accumulated during a disconnection.
            UINT32 pending = 0;
            while (!*stop && SUCCEEDED(capture->GetNextPacketSize(&pending)) && pending) {
                BYTE* discarded = nullptr; UINT32 count = 0; DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&discarded, &count, &flags, nullptr, nullptr))) break;
                capture->ReleaseBuffer(count);
            }
            // SHU2 + pairing key + rate/channels/mode (big-endian). See PROTOCOL.md.
            std::vector<uint8_t> hello{'S','H','U','2'};
            hello.insert(hello.end(), key.begin(), key.end());
            integer(hello, rate); integer(hello, channels); integer(hello, game ? 1 : 0);
            if (!sendAll(sock.s, hello.data(), hello.size(), *stop)) continue;
            status(game ? L"Игровой режим · передача запущена" : L"Качественный режим · передача запущена");
            std::string lastMeta;
            uint64_t lastArtworkVersion = 0;
            auto lastPacket = std::chrono::steady_clock::now();
            std::atomic_bool alive{true};
            std::mutex queueMutex;
            std::condition_variable queueChanged;
            std::deque<std::vector<uint8_t>> queue;
            size_t queueBytes = 0;
            const size_t maxQueueBytes = static_cast<size_t>(rate) * channels * 2 * (game ? 60 : 200) / 1000;
            const size_t chunkBytes = static_cast<size_t>(rate / (game ? 100 : 50)) * channels * 2;
            // Bounded queue deliberately drops the oldest audio on congestion:
            // accumulating seconds of stale sound is worse than a brief gap.
            auto enqueue = [&](std::vector<uint8_t> part) {
                std::lock_guard lock(queueMutex);
                while (!queue.empty() && queueBytes + part.size() > maxQueueBytes) {
                    queueBytes -= queue.front().size(); queue.pop_front(); ++droppedPackets;
                }
                queueBytes += part.size(); queue.push_back(std::move(part));
                queueChanged.notify_one();
            };
            std::thread network([&] {
                while (alive && !*stop) {
                    std::vector<uint8_t> part;
                    {
                        std::unique_lock lock(queueMutex);
                        queueChanged.wait_for(lock, 100ms, [&] { return !queue.empty() || !alive || *stop; });
                        if (!alive || *stop) break;
                        if (!queue.empty()) { part = std::move(queue.front()); queue.pop_front(); queueBytes -= part.size(); }
                    }
                    std::string json, response; std::vector<uint8_t> image; uint64_t imageVersion = 0;
                    { std::lock_guard lock(media->mutex); json = media->json;
                      if (media->artworkVersion != lastArtworkVersion) { imageVersion = media->artworkVersion; image = media->artwork; }
                      if (!media->results.empty()) { response = std::move(media->results.front()); media->results.pop_front(); } }
                    if (json != lastMeta) {
                        if (!metaFrame(sock.s, json, *stop)) { alive = false; break; }
                        lastMeta = json;
                    }
                    if (imageVersion && imageVersion != lastArtworkVersion) {
                        if (!artworkFrame(sock.s, image, *stop)) { alive = false; break; }
                        lastArtworkVersion = imageVersion;
                    }
                    if (!response.empty() && !responseFrame(sock.s, response, *stop)) { alive = false; break; }
                    if (!part.empty()) {
                        auto sendStart = std::chrono::steady_clock::now();
                        if (!frame(sock.s, part.data(), part.size(), *stop)) alive = false;
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sendStart).count();
                        auto old = maxSendTime.load();
                        while (elapsed > old && !maxSendTime.compare_exchange_weak(old, elapsed)) {}
                    }
                }
                queueChanged.notify_all();
            });
            std::thread control([&] {
                std::vector<uint8_t> received;
                while (alive && !*stop) {
                    fd_set readfds; FD_ZERO(&readfds); FD_SET(sock.s, &readfds); timeval timeout{0, 100000};
                    int ready = select(0, &readfds, nullptr, nullptr, &timeout);
                    if (ready < 0) { alive = false; break; }
                    if (!ready) continue;
                    uint8_t block[256];
                    int n = recv(sock.s, reinterpret_cast<char*>(block), sizeof(block), 0);
                    if (n <= 0) {
                        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
                        alive = false; break;
                    }
                    received.insert(received.end(), block, block + n);
                    while (received.size() >= 4) {
                        uint32_t length = (uint32_t(received[0]) << 24) | (uint32_t(received[1]) << 16) |
                                          (uint32_t(received[2]) << 8) | uint32_t(received[3]);
                        if (length == 0 || length > 64) { alive = false; break; }
                        if (received.size() < 4 + length) break;
                        std::string command(received.begin() + 4, received.begin() + 4 + length);
                        received.erase(received.begin(), received.begin() + 4 + length);
                        if (command == "MODE:GAME" && !game) PostMessageW(window, WM_VOICE_MODE, ID_GAME, 0);
                        else if (command == "MODE:QUALITY" && game) PostMessageW(window, WM_VOICE_MODE, ID_QUALITY, 0);
                        else if (command == "BRIDGE:ON") bridgeOnline = true;
                        else if (command == "BRIDGE:OFF") bridgeOnline = false;
                        else if (command == "MEDIA:NEXT") std::thread(skipCurrentTrack).detach();
                        else if (command == "MEDIA:LIKE") {
                            std::thread([media] {
                                LikeResult result = likeYandexMusic(true);
                                const char* code = result == LikeResult::Applied ? "LIKE:OK" :
                                                   result == LikeResult::AlreadyLiked ? "LIKE:ALREADY" :
                                                   result == LikeResult::NoPlayer ? "LIKE:NO_PLAYER" : "LIKE:FAILED";
                                std::lock_guard lock(media->mutex); media->results.emplace_back(code);
                            }).detach();
                        }
                    }
                }
                queueChanged.notify_all();
            });
            while (alive && !*stop) {
                UINT32 available = 0;
                if (FAILED(capture->GetNextPacketSize(&available))) { alive = false; break; }
                while (available && alive && !*stop) {
                    BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                    if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) { alive = false; break; }
                    auto now = std::chrono::steady_clock::now();
                    if (capturePackets) maxCaptureGap = std::max(maxCaptureGap, std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCapture).count());
                    lastCapture = now;
                    ++capturePackets;
                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++discontinuities;
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) ++silentPackets;
                    const size_t stride = rawFormat->nBlockAlign, sampleBytes = rawFormat->wBitsPerSample / 8;
                    pcm.resize(static_cast<size_t>(frames) * channels);
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                        for (size_t i = 0; i < frames; ++i)
                            for (int ch = 0; ch < channels; ++ch)
                                pcm[i * channels + ch] = sample(data + i * stride + ch * sampleBytes, type);
                    } else std::fill(pcm.begin(), pcm.end(), int16_t{0});
                    capture->ReleaseBuffer(frames);
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(pcm.data());
                    size_t remaining = pcm.size() * sizeof(int16_t);
                    while (remaining && alive) {
                        size_t n = std::min<size_t>(remaining, chunkBytes);
                        enqueue(std::vector<uint8_t>(p, p + n));
                        p += n; remaining -= n;
                    }
                    lastPacket = std::chrono::steady_clock::now();
                    if (FAILED(capture->GetNextPacketSize(&available))) { alive = false; break; }
                }
                if (std::chrono::steady_clock::now() - lastPacket >= 120ms) {
                    std::vector<uint8_t> silence(static_cast<size_t>(rate / (game ? 100 : 50)) * channels * 2);
                    enqueue(std::move(silence));
                    lastPacket = std::chrono::steady_clock::now();
                }
                if (diag && std::chrono::steady_clock::now() >= reportAt) {
                    diag << "packets=" << capturePackets << " gaps_ms=" << maxCaptureGap << " discontinuities=" << discontinuities
                         << " silence_packets=" << silentPackets << " send_max_ms=" << maxSendTime.exchange(0)
                         << " dropped=" << droppedPackets.exchange(0) << " connections=" << reconnects << '\n' << std::flush;
                    capturePackets = discontinuities = silentPackets = reconnects = 0;
                    maxCaptureGap = 0;
                    reportAt = std::chrono::steady_clock::now() + 5s;
                }
                WaitForSingleObject(audioReady.h, game ? 20 : 40);
            }
            alive = false;
            queueChanged.notify_all();
            network.join();
            control.join();
        }
        client->Stop();
    } catch (...) { status(L"Не удалось захватить звук. Проверьте устройство вывода Windows."); }
}

std::wstring stationIdentity(const std::wstring& host);
void stopAudio() {
    if (cancel) *cancel = true;
    if (worker.joinable()) worker.join();
    cancel.reset();
    status(L"Передача остановлена");
    bridgeOnline = false;
}
void startAudio(bool game) {
    std::wstring host = windowText(hostField), key = windowText(keyField);
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
    if (host.empty() || key.size() != 16 || key.find_first_not_of(L"0123456789abcdef") != std::wstring::npos) {
        MessageBoxW(window, L"Укажите адрес Станции и её ключ из 16 символов.", L"Station Hub", MB_OK | MB_ICONWARNING);
        return;
    }
    if(!preferencesPath.empty())WritePrivateProfileStringW(L"connection",L"host",host.c_str(),preferencesPath.c_str());
    auto identity=stationIdentity(host);
    if(!preferencesPath.empty()&&!identity.empty()){preferredStation=identity;WritePrivateProfileStringW(L"connection",L"station_id",identity.c_str(),preferencesPath.c_str());}
    stopAudio();
    cancel = std::make_shared<std::atomic_bool>(false);
    worker = std::thread(audioLoop, winrt::to_string(host), winrt::to_string(key), game, cancel);
}
// Windows invokes this exact command for the current user, never system-wide.
std::wstring startupCommand() {
    wchar_t path[32768]{}; DWORD n=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    return n && n<std::size(path) ? L"\""+std::wstring(path,n)+L"\" --startup" : L"";
}
constexpr wchar_t startupPath[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool startupEnabled() {
    HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,startupPath,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
    wchar_t value[32768]{};DWORD bytes=sizeof(value),type=0;
    LONG result=RegQueryValueExW(key,L"StationHub",nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes);RegCloseKey(key);
    return result==ERROR_SUCCESS&&type==REG_SZ&&value[std::size(value)-1]==0&&startupCommand()==value;
}
bool setStartup(bool enabled) {
    HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,startupPath,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    wchar_t existing[32768]{};DWORD bytes=sizeof(existing),type=0;
    LONG found=RegQueryValueExW(key,L"StationHub",nullptr,&type,reinterpret_cast<BYTE*>(existing),&bytes);
    auto command=startupCommand();LONG result=ERROR_ACCESS_DENIED;
    // Do not overwrite or delete a registration belonging to a different copy.
    if(!command.empty()&&(found==ERROR_FILE_NOT_FOUND||(found==ERROR_SUCCESS&&type==REG_SZ&&existing[std::size(existing)-1]==0&&command==existing))) {
        result=enabled?RegSetValueExW(key,L"StationHub",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))):found==ERROR_FILE_NOT_FOUND?ERROR_SUCCESS:RegDeleteValueW(key,L"StationHub");
    }
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
bool runAdb(const std::wstring& exe, const std::wstring& args) {
    std::wstring command = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    DWORD result = WaitForSingleObject(pi.hProcess, 6000), exitCode = 1;
    if (result == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return result == WAIT_OBJECT_0 && exitCode == 0;
}
void showKey() {
    std::wstring host = windowText(hostField);
    IN_ADDR address{};
    if (InetPtonW(AF_INET, host.c_str(), &address) != 1) {
        MessageBoxW(window, L"Для показа ключа укажите IPv4-адрес Станции.", L"Station Hub", MB_OK | MB_ICONWARNING);
        return;
    }
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::filesystem::path root(path);
    for (int i = 0; i < 5; ++i) root = root.parent_path();
    std::wstring adb = (root / L"tools" / L"platform-tools" / L"adb.exe").wstring();
    wchar_t configured[MAX_PATH]{};
    if(GetEnvironmentVariableW(L"STATION_HUB_ADB",configured,MAX_PATH))adb=configured;
    else if(!std::filesystem::exists(adb)){wchar_t found[MAX_PATH]{};if(SearchPathW(nullptr,L"adb.exe",nullptr,MAX_PATH,found,nullptr))adb=found;else adb=L"adb.exe";}
    const std::wstring endpoint = host + L":5555";
    std::thread([adb, endpoint] {
        if (!runAdb(adb, L"connect " + endpoint) ||
            !runAdb(adb, L"-s " + endpoint + L" shell am start -n ru.stationhub.app/.MainActivity")) {
            status(L"Не удалось открыть ключ: проверьте доступ к Станции.");
        } else status(L"Введите ключ, показанный на телевизоре.");
    }).detach();
}
void addTrayIcon(HWND hwnd) {
    tray={}; tray.cbSize=sizeof(tray); tray.hWnd=hwnd; tray.uID=1;
    tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; tray.uCallbackMessage=WM_TRAY;
    tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    wcscpy_s(tray.szTip,L"Station Hub");
    trayAdded=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;
}
void showMainWindow(HWND hwnd) { ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd); }
void showWidget(HWND hwnd) { auto host=windowText(hostField);IN_ADDR address{};if(!preferencesPath.empty()&&InetPtonW(AF_INET,host.c_str(),&address)==1)WritePrivateProfileStringW(L"connection",L"host",host.c_str(),preferencesPath.c_str());showStationControl(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd,GWLP_HINSTANCE)),hwnd,host,windowText(keyField)); }
std::wstring stationIdentity(const std::wstring& host){for(const auto& station:discoveredStations)if(station.address==host)return station.identity;return {};}
void maybeStartAutomaticAudio(){
    if(!autoAudioPending||cancel)return;
    auto host=windowText(hostField),key=windowText(keyField);
    if(host.empty()||key.size()!=16||key.find_first_not_of(L"0123456789abcdefABCDEF")!=std::wstring::npos)return;
    autoAudioPending=false;int mode=static_cast<int>(SendMessageW(autoModeField,CB_GETCURSEL,0,0));startAudio(mode==0);
}
struct PairingMessage { PairingResult result; std::wstring host,identity; unsigned hostRevision=0,keyRevision=0; };
void startPairing(bool automatic){
    if(pairingRunning)return;
    auto host=windowText(hostField);IN_ADDR address{};
    if(InetPtonW(AF_INET,host.c_str(),&address)!=1){if(!automatic)SetWindowTextW(pairingField,L"Сначала выберите Станцию или укажите её адрес.");return;}
    if(automatic&&!windowText(keyField).empty()){maybeStartAutomaticAudio();return;}
    auto identity=stationIdentity(host);std::wstring cached;
    if(automatic&&loadSavedPairingKey(identity,cached)){
        SetWindowTextW(keyField,cached.c_str());keyAssociatedHost=host;SecureZeroMemory(cached.data(),cached.size()*sizeof(wchar_t));
        SetWindowTextW(pairingField,L"Ключ загружен из защищённого хранилища Windows.");maybeStartAutomaticAudio();return;
    }
    if(pairingThread.joinable())pairingThread.join();
    pairingRunning=true;pairingStop=false;EnableWindow(pairButton,FALSE);SetWindowTextW(pairingField,L"Получаем ключ через разрешённый доступ ADB…");
    auto message=std::make_unique<PairingMessage>();message->host=host;message->identity=identity;message->hostRevision=hostRevision;message->keyRevision=keyRevision;HWND destination=window;
    pairingThread=std::thread([destination,message=std::move(message)]() mutable {
        try{message->result=readKeyThroughAdb(message->host,pairingStop);}catch(...){message->result.error=L"Не удалось получить ключ через ADB.";}
        if(!pairingStop&&PostMessageW(destination,WM_PAIRING,0,reinterpret_cast<LPARAM>(message.get())))message.release();
        else if(!message->result.key.empty())SecureZeroMemory(message->result.key.data(),message->result.key.size()*sizeof(wchar_t));
    });
}
struct DiscoveryResult { std::vector<DiscoveredStation> stations; unsigned hostRevision; };
void startDiscovery(){
    if(discoveryRunning)return;
    if(discoveryThread.joinable())discoveryThread.join();
    discoveryRunning=true;discoveryStop=false;EnableWindow(discoverButton,FALSE);
    SetWindowTextW(discoveryField,L"Ищем Станции в локальной сети…");
    unsigned revision=hostRevision;HWND destination=window;
    discoveryThread=std::thread([destination,revision]{
        auto result=std::make_unique<DiscoveryResult>();result->hostRevision=revision;
        try{result->stations=discoverStations(discoveryStop);}catch(...){}
        if(!discoveryStop&&PostMessageW(destination,WM_DISCOVERY,0,reinterpret_cast<LPARAM>(result.get())))result.release();
    });
}
void paintMainButton(const DRAWITEMSTRUCT& item) {
    using namespace Gdiplus;
    int w=item.rcItem.right-item.rcItem.left,h=item.rcItem.bottom-item.rcItem.top;
    HDC mem=CreateCompatibleDC(item.hDC);HBITMAP bmp=CreateCompatibleBitmap(item.hDC,w,h);
    if(!mem||!bmp){if(bmp)DeleteObject(bmp);if(mem)DeleteDC(mem);return;}
    HGDIOBJ old=SelectObject(mem,bmp);
    {
        Graphics g(mem);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        SolidBrush background(Color(255,20,20,23));g.FillRectangle(&background,0,0,w,h);
        bool primary=item.CtlID==ID_STATION,pressed=(item.itemState&ODS_SELECTED)!=0;
        Color color=primary?(pressed?Color(255,230,210,0):Color(255,255,229,0)):(pressed?Color(255,61,61,66):Color(255,43,43,48));
        SolidBrush fill(color);GraphicsPath shape;float x=1,y=1,ww=float(w-2),hh=float(h-2),d=16;
        shape.AddArc(x,y,d,d,180,90);shape.AddArc(x+ww-d,y,d,d,270,90);shape.AddArc(x+ww-d,y+hh-d,d,d,0,90);shape.AddArc(x,y+hh-d,d,d,90,90);shape.CloseFigure();g.FillPath(&fill,&shape);
        wchar_t text[160]{};GetWindowTextW(item.hwndItem,text,160);FontFamily family(L"Segoe UI");Font font(&family,14,primary?FontStyleBold:FontStyleRegular,UnitPixel);
        SolidBrush ink(primary?Color(255,25,25,20):Color(255,240,240,242));StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisCharacter);format.SetFormatFlags(StringFormatFlagsNoWrap);
        g.DrawString(text,-1,&font,RectF(8,0,float(w-16),float(h)),&format,&ink);
        if(item.itemState&ODS_FOCUS){Pen focus(primary?Color(180,25,25,20):Color(160,255,255,255),1);g.DrawPath(&focus,&shape);}
    }
    BitBlt(item.hDC,0,0,w,h,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);
}
LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated && msg==taskbarCreated) { addTrayIcon(hwnd); return 0; }
    switch (msg) {
    case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(205,205,212));return reinterpret_cast<LRESULT>(mainBackground);}
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{HDC dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,RGB(40,40,45));SetTextColor(dc,RGB(245,245,245));return reinterpret_cast<LRESULT>(fieldBackground);}
    case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){paintMainButton(*item);return TRUE;}return DefWindowProcW(hwnd,msg,wp,lp);}
    case WM_CLOSE:
        // Do not hide the only window if Explorer could not provide a tray icon.
        ShowWindow(hwnd,trayAdded?SW_HIDE:SW_MINIMIZE);return 0;
    case WM_DISCOVERY:{
        std::unique_ptr<DiscoveryResult> result(reinterpret_cast<DiscoveryResult*>(lp));
        discoveryRunning=false;if(discoveryThread.joinable())discoveryThread.join();EnableWindow(discoverButton,TRUE);
        discoveredStations=std::move(result->stations);SendMessageW(stationsField,CB_RESETCONTENT,0,0);
        if(discoveredStations.empty()){
            SendMessageW(stationsField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Станции не найдены"));SendMessageW(stationsField,CB_SETCURSEL,0,0);
            SetWindowTextW(discoveryField,L"Не найдены. Проверьте общую сеть или введите адрес вручную.");
        }else{
            auto current=windowText(hostField);int selected=-1;
            int preferred=-1;
            for(size_t i=0;i<discoveredStations.size();++i){const auto& station=discoveredStations[i];auto text=station.label+L"  ·  "+station.address;SendMessageW(stationsField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(station.address==current)selected=static_cast<int>(i);if(!preferredStation.empty()&&station.identity==preferredStation)preferred=static_cast<int>(i);}
            if(autoAudioPending&&!preferredStation.empty()){
                selected=preferred;
                if(selected>=0&&result->hostRevision==hostRevision)SetWindowTextW(hostField,discoveredStations[static_cast<size_t>(selected)].address.c_str());
            }
            if(selected<0&&current.empty()&&discoveredStations.size()==1&&result->hostRevision==hostRevision&&!cancel){selected=0;SetWindowTextW(hostField,discoveredStations[0].address.c_str());}
            if(selected>=0)SendMessageW(stationsField,CB_SETCURSEL,selected,0);
            auto message=discoveredStations.size()==1?(selected>=0?L"Станция найдена.":L"Станция найдена — выберите её в списке."):L"Найдено несколько Станций — выберите нужную в списке.";
            SetWindowTextW(discoveryField,message);
            if(selected>=0)startPairing(true);
        }
        return 0;
    }
    case WM_PAIRING:{
        std::unique_ptr<PairingMessage> result(reinterpret_cast<PairingMessage*>(lp));pairingRunning=false;if(pairingThread.joinable())pairingThread.join();EnableWindow(pairButton,TRUE);
        if(result->hostRevision!=hostRevision||result->keyRevision!=keyRevision||windowText(hostField)!=result->host){SetWindowTextW(pairingField,L"Адрес или ключ изменился; результат не применён.");}
        else if(result->result.key.empty())SetWindowTextW(pairingField,result->result.error.c_str());
        else{
            bool saved=savePairingKey(result->identity,result->result.key);SetWindowTextW(keyField,result->result.key.c_str());keyAssociatedHost=result->host;
            SetWindowTextW(pairingField,saved?L"Ключ получен и защищённо сохранён в Windows.":result->identity.empty()?L"Ключ получен. Для сохранения выберите Станцию.":L"Ключ получен, но сохранить его не удалось.");
            maybeStartAutomaticAudio();
        }
        if(!result->result.key.empty())SecureZeroMemory(result->result.key.data(),result->result.key.size()*sizeof(wchar_t));return 0;
    }
    case WM_TRAY:
        if (lp==WM_LBUTTONDBLCLK) showMainWindow(hwnd);
        else if (lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) {
            HMENU menu=CreatePopupMenu();
            AppendMenuW(menu,MF_STRING,ID_TRAY_WIDGET,L"Открыть виджет Станции");
            AppendMenuW(menu,MF_STRING,ID_TRAY_OPEN,L"Открыть Station Hub");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,ID_TRAY_EXIT,L"Завершить Station Hub");
            POINT pt{};GetCursorPos(&pt);SetForegroundWindow(hwnd);
            int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,nullptr);
            DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
            if(selected==ID_TRAY_WIDGET)showWidget(hwnd);
            else if(selected==ID_TRAY_OPEN)showMainWindow(hwnd);
            else if(selected==ID_TRAY_EXIT)DestroyWindow(hwnd);
        }
        return 0;
    case WM_VOICE_MODE:
        if (wp == ID_GAME) startAudio(true);
        else if (wp == ID_QUALITY) startAudio(false);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_GAME: autoAudioPending=false;startAudio(true); break;
        case ID_QUALITY: autoAudioPending=false;startAudio(false); break;
        case ID_STOP: autoAudioPending=false;stopAudio(); break;
        case ID_STARTUP:{bool enabled=SendMessageW(startupField,BM_GETCHECK,0,0)==BST_CHECKED;if(!setStartup(enabled)){SendMessageW(startupField,BM_SETCHECK,startupEnabled()?BST_CHECKED:BST_UNCHECKED,0);MessageBoxW(hwnd,L"Не удалось изменить автозапуск Windows. Возможно, он занят другой копией Station Hub.",L"Station Hub",MB_OK|MB_ICONWARNING);}break;}
        case ID_AUTO_AUDIO:{bool enabled=SendMessageW(autoAudioField,BM_GETCHECK,0,0)==BST_CHECKED;if(!preferencesPath.empty())WritePrivateProfileStringW(L"startup",L"audio",enabled?L"1":L"0",preferencesPath.c_str());if(!enabled)autoAudioPending=false;break;}
        case ID_AUTO_MODE:if(HIWORD(wp)==CBN_SELCHANGE&&!preferencesPath.empty()){int mode=static_cast<int>(SendMessageW(autoModeField,CB_GETCURSEL,0,0));WritePrivateProfileStringW(L"startup",L"mode",mode==0?L"game":L"quality",preferencesPath.c_str());}break;
        case ID_WIDGET_SKIN:if(HIWORD(wp)==CBN_SELCHANGE){int skin=static_cast<int>(SendMessageW(widgetSkinField,CB_GETCURSEL,0,0));if(skin>=0&&skin<=2)setStationWidgetSkin(skin);}break;
        case ID_EXIT: DestroyWindow(hwnd);break;
        case ID_DISCOVER: startDiscovery();break;
        case ID_HOST: if(HIWORD(wp)==EN_CHANGE){++hostRevision;if(keyField&&!keyAssociatedHost.empty()&&windowText(hostField)!=keyAssociatedHost){SetWindowTextW(keyField,L"");keyAssociatedHost.clear();}}break;
        case ID_KEY_FIELD:if(HIWORD(wp)==EN_CHANGE){++keyRevision;keyAssociatedHost=keyField&&!windowText(keyField).empty()?windowText(hostField):L"";if(pairingField&&!keyAssociatedHost.empty())SetWindowTextW(pairingField,L"Ключ указан.");}break;
        case ID_PAIR:startPairing(false);break;
        case ID_FORGET:{auto host=windowText(hostField);auto id=stationIdentity(host);if(id.empty()){SetWindowTextW(pairingField,L"Выберите найденную Станцию, чтобы забыть её сохранённый ключ.");break;}++keyRevision;if(!forgetPairingKey(id)&&GetLastError()!=ERROR_NOT_FOUND){SetWindowTextW(pairingField,L"Не удалось удалить сохранённый ключ из Windows.");break;}SetWindowTextW(keyField,L"");SetWindowTextW(pairingField,L"Ключ удалён из Windows; на Станции он не изменён.");break;}
        case ID_FOUND:
            if(HIWORD(wp)==CBN_SELCHANGE&&!discoveredStations.empty()){
                int choice=static_cast<int>(SendMessageW(stationsField,CB_GETCURSEL,0,0));
                if(choice>=0&&static_cast<size_t>(choice)<discoveredStations.size()){
                    const auto& station=discoveredStations[static_cast<size_t>(choice)];SetWindowTextW(hostField,station.address.c_str());
                    if(cancel)SetWindowTextW(discoveryField,L"Адрес выбран. Режим передачи нужно запустить заново.");
                    startPairing(true);
                }
            }
            break;
        case ID_KEY: showKey(); break;
        case ID_STATION: showWidget(hwnd); break;
        case ID_POLICY:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int choice = static_cast<int>(SendMessageW(policyField, CB_GETCURSEL, 0, 0));
                if (choice >= 0 && choice <= 3) {
                    ambiguityPolicy = choice;
                    if (!preferencesPath.empty()) {
                        std::wstring value = std::to_wstring(choice);
                        WritePrivateProfileStringW(L"voice", L"when_both_play", value.c_str(), preferencesPath.c_str());
                    }
                }
            }
            break;
        }
        return 0;
    case WM_TIMER: {
        static unsigned retryTicks=0;
        if(autoAudioPending&&!discoveryRunning&&!pairingRunning&&++retryTicks>=20){retryTicks=0;startDiscovery();}
        std::lock_guard lock(statusMutex);
        if(windowText(statusField)!=statusText)SetWindowTextW(statusField, statusText.c_str());
        const wchar_t* bridge=bridgeOnline ? L"Голосовой мост: запущен" : L"Голосовой мост: нет связи";
        if(windowText(bridgeField)!=bridge)SetWindowTextW(bridgeField,bridge);
        return 0;
    }
    case WM_DESTROY:{discoveryStop=true;pairingStop=true;if(discoveryThread.joinable())discoveryThread.join();if(pairingThread.joinable())pairingThread.join();MSG queued{};while(PeekMessageW(&queued,hwnd,WM_DISCOVERY,WM_DISCOVERY,PM_REMOVE))delete reinterpret_cast<DiscoveryResult*>(queued.lParam);while(PeekMessageW(&queued,hwnd,WM_PAIRING,WM_PAIRING,PM_REMOVE)){auto result=reinterpret_cast<PairingMessage*>(queued.lParam);if(result&&!result->result.key.empty())SecureZeroMemory(result->result.key.data(),result->result.key.size()*sizeof(wchar_t));delete result;}if(trayAdded){Shell_NotifyIconW(NIM_DELETE,&tray);trayAdded=false;} stopAudio(); PostQuitMessage(0); return 0;}
    default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
HWND add(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0) {
    HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    if(id==ID_KEY||id==ID_PAIR||id==ID_FORGET||id==ID_GAME||id==ID_QUALITY||id==ID_STOP||id==ID_STATION||id==ID_DISCOVER||id==ID_EXIT)SetWindowRgn(c,CreateRoundRectRgn(0,0,w+1,h+1,14,14),FALSE);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont?uiFont:GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return c;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    WSADATA ws{}; if (WSAStartup(MAKEWORD(2,2), &ws) != 0) return 1;
    SetProcessDPIAware();
    Gdiplus::GdiplusStartupInput graphicsInput;ULONG_PTR graphicsToken{};if(Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr)!=Gdiplus::Ok)return 1;
    mainBackground=CreateSolidBrush(RGB(20,20,23));fieldBackground=CreateSolidBrush(RGB(40,40,45));
    uiFont=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    headingFont=CreateFontW(-23,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = procedure; wc.lpszClassName = L"StationHubNative";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = mainBackground;
    if (!RegisterClassW(&wc)) return 1;
    window = CreateWindowW(wc.lpszClassName, L"Station Hub — звук с компьютера", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, 460, 825, nullptr, nullptr, instance, nullptr);
    BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
    HWND heading=add(window,L"STATIC",L"Station Hub",0,24,18,390,30);SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),TRUE);
    add(window,L"STATIC",L"Музыка и звук компьютера на Станции",0,24,52,390,22);
    add(window, L"STATIC", L"Адрес Станции", 0, 24, 91, 145, 22);
    hostField = add(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 178, 88, 156, 26, ID_HOST);
    discoverButton=add(window,L"BUTTON",L"Найти",BS_OWNERDRAW | WS_TABSTOP,342,87,82,28,ID_DISCOVER);
    wchar_t host[256]{}; if (GetEnvironmentVariableW(L"STATION_HUB_HOST", host, 256)) SetWindowTextW(hostField, host);
    add(window,L"STATIC",L"Станции в сети",0,24,126,145,22);
    stationsField=add(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,178,123,246,160,ID_FOUND);
    SendMessageW(stationsField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Поиск Станций…"));SendMessageW(stationsField,CB_SETCURSEL,0,0);
    discoveryField=add(window,L"STATIC",L"",0,24,155,400,20);
    add(window, L"STATIC", L"Ключ Станции", 0, 24, 184, 145, 22);
    keyField = add(window, L"EDIT", L"", WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP, 178, 181, 156, 26,ID_KEY_FIELD);
    add(window,L"BUTTON",L"Забыть",BS_OWNERDRAW | WS_TABSTOP,342,180,82,28,ID_FORGET);
    pairButton=add(window,L"BUTTON",L"Получить без экрана",BS_OWNERDRAW | WS_TABSTOP,24,216,196,36,ID_PAIR);
    add(window, L"BUTTON", L"Показать ключ на ТВ", BS_OWNERDRAW | WS_TABSTOP, 228, 216, 196, 36, ID_KEY);
    pairingField=add(window,L"STATIC",L"",0,24,259,400,40);
    add(window, L"STATIC", L"Передача звука", 0, 24, 315, 400, 22);
    add(window, L"BUTTON", L"Игровой режим", BS_OWNERDRAW | WS_TABSTOP, 24, 341, 196, 38, ID_GAME);
    add(window, L"BUTTON", L"Качественный режим", BS_OWNERDRAW | WS_TABSTOP, 228, 341, 196, 38, ID_QUALITY);
    add(window, L"BUTTON", L"Остановить передачу", BS_OWNERDRAW | WS_TABSTOP, 24, 386, 400, 36, ID_STOP);
    add(window, L"STATIC", L"Если источник не указан", 0, 24, 439, 400, 20);
    policyField = add(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 24, 464, 400, 140, ID_POLICY);
    SendMessageW(policyField, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Спросить, какой источник"));
    SendMessageW(policyField, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Выбирать компьютер"));
    SendMessageW(policyField, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Выбирать Станцию"));
    SendMessageW(policyField, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Обычное поведение Станции"));
    wchar_t appData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH)) {
        std::filesystem::path dir = std::filesystem::path(appData) / L"StationHub";
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        if (!ec) preferencesPath = (dir / L"preferences.ini").wstring();
    }
    if (!preferencesPath.empty()) ambiguityPolicy = std::clamp<int>(GetPrivateProfileIntW(L"voice", L"when_both_play", 3, preferencesPath.c_str()), 0, 3);
    if(!host[0]&&!preferencesPath.empty()){wchar_t savedHost[256]{};GetPrivateProfileStringW(L"connection",L"host",L"",savedHost,256,preferencesPath.c_str());SetWindowTextW(hostField,savedHost);}
    if(!preferencesPath.empty()){wchar_t id[256]{};GetPrivateProfileStringW(L"connection",L"station_id",L"",id,256,preferencesPath.c_str());preferredStation=id;}
    SendMessageW(policyField, CB_SETCURSEL, ambiguityPolicy.load(), 0);
    bridgeField = add(window, L"STATIC", L"Голосовой мост: нет связи", 0, 24, 502, 400, 22);
    statusField = add(window, L"STATIC", L"Передача остановлена", 0, 24, 528, 400, 24);
    startupField=add(window,L"BUTTON",L"Запускать Station Hub вместе с Windows",BS_AUTOCHECKBOX|WS_TABSTOP,24,558,400,24,ID_STARTUP);
    SendMessageW(startupField,BM_SETCHECK,startupEnabled()?BST_CHECKED:BST_UNCHECKED,0);
    autoAudioField=add(window,L"BUTTON",L"Подключаться и передавать звук при запуске",BS_AUTOCHECKBOX|WS_TABSTOP,24,588,400,24,ID_AUTO_AUDIO);
    bool autoAudio=!preferencesPath.empty()&&GetPrivateProfileIntW(L"startup",L"audio",0,preferencesPath.c_str())!=0;
    SendMessageW(autoAudioField,BM_SETCHECK,autoAudio?BST_CHECKED:BST_UNCHECKED,0);
    add(window,L"STATIC",L"Режим при запуске",0,24,620,180,22);
    autoModeField=add(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,202,617,222,100,ID_AUTO_MODE);
    SendMessageW(autoModeField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Игровой"));SendMessageW(autoModeField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Качественный"));
    wchar_t autoMode[16]{};if(!preferencesPath.empty())GetPrivateProfileStringW(L"startup",L"mode",L"quality",autoMode,16,preferencesPath.c_str());
    SendMessageW(autoModeField,CB_SETCURSEL,std::wstring(autoMode)==L"game"?0:1,0);
    add(window,L"STATIC",L"Оформление виджета",0,24,653,180,22);
    widgetSkinField=add(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,202,650,222,100,ID_WIDGET_SKIN);
    SendMessageW(widgetSkinField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Station Hub"));SendMessageW(widgetSkinField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Минималистичный"));SendMessageW(widgetSkinField,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"В стиле Яндекс Музыки"));
    SendMessageW(widgetSkinField,CB_SETCURSEL,stationWidgetSkin(),0);
    add(window, L"BUTTON", L"Открыть виджет музыки", BS_OWNERDRAW | WS_TABSTOP, 24, 689, 400, 42, ID_STATION);
    add(window, L"BUTTON", L"Выйти из Station Hub", BS_OWNERDRAW | WS_TABSTOP, 24, 738, 400, 34, ID_EXIT);
    wchar_t key[32]{}; if (GetEnvironmentVariableW(L"STATION_HUB_KEY", key, 32) == 16) SetWindowTextW(keyField, key);
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");addTrayIcon(window);
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool loginStart=argc==2&&argv&&std::wstring(argv[1])==L"--startup";if(argv)LocalFree(argv);
    SetTimer(window, 1, 500, nullptr); ShowWindow(window,loginStart&&trayAdded?SW_HIDE:show); UpdateWindow(window);
    // A one-process override lets an updater preserve a deliberately stopped
    // stream without changing the user's persistent startup preference.
    wchar_t mode[16]{},suppress[4]{};
    bool suppressAudio=GetEnvironmentVariableW(L"STATION_HUB_SUPPRESS_AUDIO_ONCE",suppress,4)&&std::wstring(suppress)==L"1";
    if(!suppressAudio){
        if (GetEnvironmentVariableW(L"STATION_HUB_AUTOSTART", mode, 16)) startAudio(std::wstring(mode) == L"game");
        else {autoAudioPending=autoAudio;if(autoAudioPending&&!windowText(keyField).empty())maybeStartAutomaticAudio();}
    }
    startDiscovery();
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DeleteObject(uiFont);DeleteObject(headingFont);DeleteObject(mainBackground);DeleteObject(fieldBackground);Gdiplus::GdiplusShutdown(graphicsToken);WSACleanup(); return 0;
}
