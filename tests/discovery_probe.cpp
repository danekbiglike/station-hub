#include <winsock2.h>
#include "../windows/native/station_discovery.h"
#include <atomic>
#include <cstdio>
int main(){WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w))return 1;std::atomic_bool stop{false};auto results=discoverStations(stop);std::printf("Stations found: %zu\n",results.size());for(const auto& s:results)std::printf("%ls\n",s.address.c_str());WSACleanup();}
