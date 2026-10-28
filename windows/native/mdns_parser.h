#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mdns {
constexpr const char* serviceName="_yandexio._tcp.local";
struct Instance { std::string host, id, platform; uint16_t port=0; bool advertised=false; };
struct State { std::map<std::string,Instance> instances; std::map<std::string,uint32_t> addresses; };
inline uint16_t be16(const uint8_t* p){return uint16_t((uint16_t(p[0])<<8)|p[1]);}
inline uint32_t be32(const uint8_t* p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
inline std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}

// RFC 1035 compressed names can point backwards or form cycles. Keep a hop
// bound, check every offset, and never treat packet data as a C string.
inline bool readName(const uint8_t* p,size_t n,size_t& cursor,std::string& name){
 size_t at=cursor,after=0;bool jumped=false;std::set<size_t> visited;name.clear();
 for(int hops=0;hops<128;hops++){
  if(at>=n||!visited.insert(at).second)return false;
  uint8_t len=p[at++];
  if((len&0xc0)==0xc0){if(at>=n)return false;size_t target=((size_t(len&0x3f)<<8)|p[at++]);if(target>=n)return false;if(!jumped){after=at;jumped=true;}at=target;continue;}
  if(len&0xc0)return false;
  if(len==0){cursor=jumped?after:at;name=lower(name);return name.size()<=253;}
  if(len>63||at+len>n||name.size()+len+1>254)return false;
  if(!name.empty())name.push_back('.');name.append(reinterpret_cast<const char*>(p+at),len);at+=len;
 }
 return false;
}
inline bool instanceName(const std::string& s){const std::string suffix=std::string(".")+serviceName;return s.size()>suffix.size()&&s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0;}
inline bool parsePacket(const uint8_t* p,size_t n,State& state){
 if(n<12||n>9000||(be16(p+2)&0x8000)==0)return false;
 unsigned questions=be16(p+4),records=unsigned(be16(p+6))+be16(p+8)+be16(p+10);
 if(questions>64||records>256)return false;
 size_t at=12;std::string name;
 for(unsigned i=0;i<questions;i++){if(!readName(p,n,at,name)||at+4>n)return false;at+=4;}
 // Work on a copy: malformed trailing records cannot poison a valid state.
 State next=state;
 for(unsigned i=0;i<records;i++){
  if(!readName(p,n,at,name)||at+10>n)return false;
  uint16_t type=be16(p+at),klass=be16(p+at+2),len=be16(p+at+8);uint32_t ttl=be32(p+at+4);at+=10;
  if(at+len>n)return false;size_t end=at+len;
  if((klass&0x7fff)==1&&ttl>0){
   if(type==12&&name==serviceName){size_t pos=at;std::string target;if(!readName(p,n,pos,target)||pos>end)return false;if(instanceName(target))next.instances[target].advertised=true;}
   else if(type==33&&instanceName(name)){
    if(len<7)return false;size_t pos=at+6;std::string host;if(!readName(p,n,pos,host)||pos>end)return false;
    auto& item=next.instances[name];item.advertised=true;item.host=host;item.port=be16(p+at+4);
   }else if(type==1&&len==4){next.addresses[name]=be32(p+at);}
   else if(type==16&&instanceName(name)){
    auto& item=next.instances[name];size_t pos=at;
    while(pos<end){size_t count=p[pos++];if(pos+count>end)return false;std::string field(reinterpret_cast<const char*>(p+pos),count);pos+=count;auto eq=field.find('=');if(eq==std::string::npos)continue;auto key=lower(field.substr(0,eq));auto value=field.substr(eq+1);if(key=="deviceid"&&value.size()<=128)item.id=value;else if(key=="platform"&&value.size()<=64)item.platform=value;}
   }
  }
  at=end;
 }
 if(next.instances.size()>128||next.addresses.size()>512)return false;
 state=std::move(next);return true;
}

inline std::vector<uint8_t> query(const std::string& name,uint16_t type){
 std::vector<uint8_t> out{0,0,0,0,0,1,0,0,0,0,0,0};size_t at=0;
 while(at<name.size()){auto dot=name.find('.',at);size_t end=dot==std::string::npos?name.size():dot;size_t len=end-at;if(!len||len>63)return {};out.push_back(static_cast<uint8_t>(len));out.insert(out.end(),name.begin()+at,name.begin()+end);if(dot==std::string::npos)break;at=dot+1;}
 out.insert(out.end(),{0,static_cast<uint8_t>(type>>8),static_cast<uint8_t>(type),0x80,1});return out; // QU: reply to our ephemeral port
}
inline bool privateAddress(uint32_t ip){return (ip>>24)==10||(ip>>20)==0xac1||(ip>>16)==0xc0a8||(ip>>16)==0xa9fe;}
} // namespace mdns
