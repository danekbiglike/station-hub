#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include "station_discovery.h"
#include "mdns_parser.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {
struct Interface { SOCKET socket=INVALID_SOCKET; uint32_t network=0,mask=0; };
struct Sockets {
 std::vector<Interface> items;
 Sockets()=default;Sockets(const Sockets&)=delete;Sockets& operator=(const Sockets&)=delete;
 Sockets(Sockets&& other) noexcept:items(std::move(other.items)){other.items.clear();}
 ~Sockets(){for(auto& i:items)if(i.socket!=INVALID_SOCKET)closesocket(i.socket);}
};

Sockets openInterfaces(){
 Sockets result;ULONG size=15000;std::vector<unsigned char> buffer(size);
 ULONG flags=GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER;
 ULONG rc=GetAdaptersAddresses(AF_INET,flags,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
 if(rc==ERROR_BUFFER_OVERFLOW&&size<=1024*1024){buffer.resize(size);rc=GetAdaptersAddresses(AF_INET,flags,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);}
 if(rc!=NO_ERROR)return result;
 std::set<uint32_t> seen;
 for(auto a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());a&&result.items.size()<32;a=a->Next){
  if(a->OperStatus!=IfOperStatusUp||a->IfType==IF_TYPE_SOFTWARE_LOOPBACK)continue;
  for(auto u=a->FirstUnicastAddress;u&&result.items.size()<32;u=u->Next){
   if(!u->Address.lpSockaddr||u->Address.lpSockaddr->sa_family!=AF_INET)continue;
   auto addr=reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;uint32_t host=ntohl(addr.s_addr);
   if(!mdns::privateAddress(host)||!seen.insert(host).second)continue;
   SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET)continue;
   sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr=addr;local.sin_port=0;
   if(bind(s,reinterpret_cast<sockaddr*>(&local),sizeof(local))==SOCKET_ERROR||setsockopt(s,IPPROTO_IP,IP_MULTICAST_IF,reinterpret_cast<const char*>(&addr),sizeof(addr))==SOCKET_ERROR){closesocket(s);continue;}
   int ttl=255;setsockopt(s,IPPROTO_IP,IP_MULTICAST_TTL,reinterpret_cast<const char*>(&ttl),sizeof(ttl));
   u_long nonblocking=1;ioctlsocket(s,FIONBIO,&nonblocking);
   unsigned bits=std::min<unsigned>(u->OnLinkPrefixLength,32);uint32_t mask=bits?0xffffffffu<<(32-bits):0;
   result.items.push_back({s,host&mask,mask});
  }
 }
 return result;
}
void sendQuery(const Sockets& sockets,const std::string& name,uint16_t type){
 auto packet=mdns::query(name,type);if(packet.empty())return;
 sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(5353);InetPtonA(AF_INET,"224.0.0.251",&target.sin_addr);
 for(const auto& i:sockets.items)sendto(i.socket,reinterpret_cast<const char*>(packet.data()),static_cast<int>(packet.size()),0,reinterpret_cast<sockaddr*>(&target),sizeof(target));
}
bool isLocal(uint32_t ip,const Sockets& sockets){
 if(!mdns::privateAddress(ip))return false;
 for(const auto& i:sockets.items)if(i.mask&&(ip&i.mask)==i.network&&ip!=i.network&&ip!=(i.network|~i.mask))return true;
 return false;
}
std::wstring labelFor(const mdns::Instance& item){
 std::wstring label=L"Яндекс Станция";
 // The short suffix helps distinguish stations without displaying full IDs.
 if(item.id.size()>=4&&std::all_of(item.id.end()-4,item.id.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z');})){
  label+=L" · ";for(auto c=item.id.end()-4;c!=item.id.end();++c)label+=wchar_t(static_cast<unsigned char>(*c));
 }
 return label;
}
std::wstring identityFor(const mdns::Instance& item){
 if(item.id.size()<4||item.id.size()>128)return {};
 std::wstring result;for(unsigned char c:item.id){if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_'))return {};if(c>='A'&&c<='Z')c=static_cast<unsigned char>(c-'A'+'a');result+=wchar_t(c);}return result;
}
} // namespace

std::vector<DiscoveredStation> discoverStations(const std::atomic_bool& stop){
 using namespace std::chrono_literals;
 auto sockets=openInterfaces();if(sockets.items.empty())return {};
 mdns::State state;std::set<std::string> requested;auto started=std::chrono::steady_clock::now();auto nextBrowse=started;
 while(!stop&&std::chrono::steady_clock::now()-started<3s){
  auto now=std::chrono::steady_clock::now();if(now>=nextBrowse){sendQuery(sockets,mdns::serviceName,12);nextBrowse=now+1200ms;}
  for(const auto& [name,item]:state.instances){
   if(item.advertised&&item.host.empty()&&requested.insert("srv:"+name).second)sendQuery(sockets,name,33);
   if(!item.host.empty()&&!state.addresses.count(item.host)&&requested.insert("a:"+item.host).second)sendQuery(sockets,item.host,1);
   if(item.advertised&&item.id.empty()&&requested.insert("txt:"+name).second)sendQuery(sockets,name,16);
  }
  fd_set readfds;FD_ZERO(&readfds);for(const auto& i:sockets.items)FD_SET(i.socket,&readfds);timeval timeout{0,150000};
  if(select(0,&readfds,nullptr,nullptr,&timeout)<=0)continue;
  for(const auto& i:sockets.items)if(FD_ISSET(i.socket,&readfds)){
   for(;;){uint8_t bytes[9000];sockaddr_in from{};int len=sizeof(from);int n=recvfrom(i.socket,reinterpret_cast<char*>(bytes),sizeof(bytes),0,reinterpret_cast<sockaddr*>(&from),&len);if(n<=0)break;
    if(ntohs(from.sin_port)==5353&&isLocal(ntohl(from.sin_addr.s_addr),sockets))mdns::parsePacket(bytes,static_cast<size_t>(n),state);
   }
  }
 }
 std::map<uint32_t,DiscoveredStation> unique;
 for(const auto& [name,item]:state.instances){
  if(!item.advertised||!item.port||item.host.empty())continue;auto found=state.addresses.find(item.host);
  if(found==state.addresses.end()||!isLocal(found->second,sockets))continue;
  in_addr address{};address.s_addr=htonl(found->second);wchar_t ip[INET_ADDRSTRLEN]{};
  if(!InetNtopW(AF_INET,&address,ip,INET_ADDRSTRLEN))continue;
  unique.emplace(found->second,DiscoveredStation{ip,labelFor(item),identityFor(item)});
 }
 std::vector<DiscoveredStation> out;for(auto& [ip,item]:unique)out.push_back(std::move(item));return out;
}
