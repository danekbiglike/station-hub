#include "../windows/native/mdns_parser.h"
#include <cassert>
#include <cstdio>

using Bytes=std::vector<uint8_t>;
static void number16(Bytes& b,uint16_t n){b.push_back(uint8_t(n>>8));b.push_back(uint8_t(n));}
static void number32(Bytes& b,uint32_t n){b.push_back(uint8_t(n>>24));b.push_back(uint8_t(n>>16));b.push_back(uint8_t(n>>8));b.push_back(uint8_t(n));}
static void name(Bytes& b,const std::string& value){size_t pos=0;while(pos<value.size()){size_t dot=value.find('.',pos);if(dot==std::string::npos)dot=value.size();b.push_back(uint8_t(dot-pos));b.insert(b.end(),value.begin()+pos,value.begin()+dot);pos=dot+1;}b.push_back(0);}
static void rrheader(Bytes& b,uint16_t type,uint16_t size){number16(b,type);number16(b,1);number32(b,120);number16(b,size);}
static Bytes packet(){
 Bytes b{0,0,0x84,0,0,1,0,4,0,0,0,0};name(b,mdns::serviceName);number16(b,12);number16(b,1);
 b.push_back(0xc0);b.push_back(12);rrheader(b,12,10);size_t instanceOffset=b.size();b.push_back(7);b.insert(b.end(),{'s','t','a','t','i','o','n'});b.push_back(0xc0);b.push_back(12);
 b.push_back(0xc0);b.push_back(uint8_t(instanceOffset));Bytes host;name(host,"speaker.local");rrheader(b,33,uint16_t(6+host.size()));number16(b,0);number16(b,0);number16(b,1961);b.insert(b.end(),host.begin(),host.end());
 b.push_back(0xc0);b.push_back(uint8_t(instanceOffset));const std::string text="deviceId=0000abcd";rrheader(b,16,uint16_t(text.size()+1));b.push_back(uint8_t(text.size()));b.insert(b.end(),text.begin(),text.end());
 name(b,"speaker.local");rrheader(b,1,4);b.insert(b.end(),{192,168,42,11});return b;
}
int main(){
 auto q=mdns::query(mdns::serviceName,12);assert(q.size()>17&&q[q.size()-2]==0x80&&q.back()==1);
 auto b=packet();mdns::State state;assert(mdns::parsePacket(b.data(),b.size(),state));
 auto it=state.instances.find("station._yandexio._tcp.local");assert(it!=state.instances.end());
 assert(it->second.advertised&&it->second.port==1961&&it->second.id=="0000abcd"&&it->second.host=="speaker.local");
 assert(state.addresses.at("speaker.local")==0xc0a82a0b);
 auto second=packet();const std::string oldInstance="station",newInstance="bedroom",oldHost="speaker",newHost="speakex";
 auto instancePos=std::search(second.begin(),second.end(),oldInstance.begin(),oldInstance.end());assert(instancePos!=second.end());std::copy(newInstance.begin(),newInstance.end(),instancePos);
 auto hostPos=second.begin();while((hostPos=std::search(hostPos,second.end(),oldHost.begin(),oldHost.end()))!=second.end()){std::copy(newHost.begin(),newHost.end(),hostPos);hostPos+=newHost.size();}
 second.back()=12;assert(mdns::parsePacket(second.data(),second.size(),state));assert(state.instances.size()==2&&state.addresses.at("speakex.local")==0xc0a82a0c);
 auto original=state;assert(!mdns::parsePacket(b.data(),b.size()-1,state));assert(state.addresses==original.addresses&&state.instances.size()==original.instances.size());
 uint8_t loop[]{0xc0,0};size_t offset=0;std::string decoded;assert(!mdns::readName(loop,sizeof(loop),offset,decoded));
 uint8_t truncated[]{0xc0};offset=0;assert(!mdns::readName(truncated,sizeof(truncated),offset,decoded));
 assert(mdns::privateAddress(0xc0a82a0b)&&mdns::privateAddress(0xac100001)&&!mdns::privateAddress(0x08080808));
 puts("mDNS parsing and malformed-packet checks passed.");
}
