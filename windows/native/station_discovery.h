#pragma once
#include <atomic>
#include <string>
#include <vector>

struct DiscoveredStation {
    std::wstring address;
    std::wstring label;
    std::wstring identity; // device ID, used locally for the encrypted key cache
};

// A bounded mDNS browse. It sends no pairing key and never opens the audio or
// control port. The stop flag allows the app to close without waiting for it.
std::vector<DiscoveredStation> discoverStations(const std::atomic_bool& stop);
