#include <winsock2.h>
#include <windows.h>
#include "../windows/native/secure_pairing.h"
#include <cassert>
#include <cstdio>
int main(){
 WSADATA ws{};assert(WSAStartup(MAKEWORD(2,2),&ws)==0);
 wchar_t cwd[32768]{};assert(GetCurrentDirectoryW(32768,cwd));std::wstring stub=std::wstring(cwd)+L"\\fake-adb.exe";
 assert(SetEnvironmentVariableW(L"STATION_HUB_ADB",stub.c_str()));std::atomic_bool stop{false};
 auto result=readKeyThroughAdb(L"127.0.0.1",stop);assert(result.key==L"0123456789abcdef");
 assert(SetEnvironmentVariableW(L"STATION_HUB_TEST_BAD_RESPONSE",L"1"));assert(readKeyThroughAdb(L"127.0.0.1",stop).key.empty());SetEnvironmentVariableW(L"STATION_HUB_TEST_BAD_RESPONSE",nullptr);
 assert(readKeyThroughAdb(L"not-an-ip",stop).key.empty());stop=true;assert(readKeyThroughAdb(L"127.0.0.1",stop).key.empty());
 std::wstring id=L"test-"+std::to_wstring(GetCurrentProcessId()),loaded;
 assert(!loadSavedPairingKey(id,loaded));assert(savePairingKey(id,L"0123456789abcdef"));assert(loadSavedPairingKey(id,loaded)&&loaded==L"0123456789abcdef");
 assert(forgetPairingKey(id));loaded.clear();assert(!loadSavedPairingKey(id,loaded));assert(!savePairingKey(L"../bad",L"0123456789abcdef"));
 WSACleanup();puts("ADB capture and protected credential checks passed.");
}
