#pragma once
#include <atomic>
#include <string>

struct PairingResult { std::wstring key; std::wstring error; };

// An authorized ADB shell can query the provider. No generic root shell, UI
// screenshot, network-wide scan, or log containing the key is used.
PairingResult readKeyThroughAdb(const std::wstring& host,const std::atomic_bool& stop);
bool loadSavedPairingKey(const std::wstring& identity,std::wstring& key);
bool savePairingKey(const std::wstring& identity,const std::wstring& key);
bool forgetPairingKey(const std::wstring& identity);
