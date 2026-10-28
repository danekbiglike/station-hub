#include <winsock2.h>
#include <ws2tcpip.h>
#include "station_control.h"
#include "volume_intent.h"
#include "widget_layout.h"
#include <winhttp.h>
#include <gdiplus.h>
#include "widget_art.h"
#include "widget_pill.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <array>
#include <cstring>

using namespace std::chrono_literals;
using namespace winrt::Windows::Data::Json;
namespace {
constexpr UINT UPDATE = WM_APP + 75, ART = WM_APP + 76;
constexpr int PREV=201, PLAY=202, NEXT=203, LIKE=204, DISLIKE=205, DOWN=206, UP=207, UNLIKE=208, UNDISLIKE=209, PIN=210, CLOSE=211;
constexpr int WIDTH=390, HEIGHT=604;
struct Snapshot {
 bool connected=false, known=false, hasMedia=false, playing=false;
 int volume=-1, outputVolume=-1, outputMax=-1; long long position=-1, duration=-1;
 std::wstring title, artist, art, notice, trackToken, rating, ratingStatus;
};
struct Panel {
 HWND hwnd{},owner{},cover{},title{},artist{},state{},time{},progress{},volume{},connection{},play{};
 HFONT titleFont{}, artistFont{}, smallFont{}; HBITMAP bitmap{}; HBRUSH background{};
 std::string host,key; std::thread worker, imageWorker;
 std::mutex mutex; std::condition_variable wake; std::deque<std::string> commands;
 Snapshot snapshot; std::atomic_bool stop{false}; std::atomic<unsigned> artVersion{0};
 Snapshot displayed; bool haveDisplayed=false;
 std::chrono::steady_clock::time_point noticeUntil{};
 std::wstring lastArt;
 bool scrubbing=false; long long scrubDuration=0; std::wstring scrubToken;
 bool pinned=false;
 int skin=0;
 std::array<HWND,9> buttons{};
 HWND tooltip{};
 bool rendering=false;
 bool volumeRendering=false;
 RECT seekRect{};
 int hover=-1, focused=-1;
 int pressed=-1;
 bool seeking=false, volumeOpen=false, volumeDragging=false;
 HWND volumePopup{}; int volumePreview=-1;
 VolumeIntent volumeIntent;
 long long seekDuration=0, previewMs=0;
 std::wstring seekToken;
 COLORREF tint=RGB(48,45,32);
 COLORREF accent=RGB(140,58,160);
 std::unique_ptr<Gdiplus::Bitmap> glowCache;
 int glowSkin=-1;
 COLORREF glowColor{};
 std::unique_ptr<Gdiplus::Bitmap> backgroundCache;
 int backgroundWidth=0,backgroundHeight=0,backgroundSkin=-1;
 COLORREF backgroundTint{},backgroundAccent{};
 bool hoverSeek=false;
};
Panel* panel=nullptr;
std::wstring clockText(long long value) {
 if(value<0)return L"—:—"; long long seconds=value/1000; wchar_t out[40];
 if(seconds>=3600)swprintf_s(out,L"%lld:%02lld:%02lld",seconds/3600,(seconds/60)%60,seconds%60);
 else swprintf_s(out,L"%lld:%02lld",seconds/60,seconds%60);return out;
}
std::string request(const Panel& p,const std::string& command) {
 SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)return {};
 struct Close {SOCKET s; ~Close(){closesocket(s);}} close{s};
 u_long nonblocking=1;ioctlsocket(s,FIONBIO,&nonblocking);
 sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(27363);if(InetPtonA(AF_INET,p.host.c_str(),&a.sin_addr)!=1)return {};
 int r=connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a));if(r==SOCKET_ERROR&&WSAGetLastError()!=WSAEWOULDBLOCK)return {};
 fd_set w;FD_ZERO(&w);FD_SET(s,&w);timeval wait{1,0};if(select(0,nullptr,&w,nullptr,&wait)<=0)return {};
 int err=0;int len=sizeof(err);if(getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&err),&len)!=0||err)return {};
 nonblocking=0;ioctlsocket(s,FIONBIO,&nonblocking);DWORD ms=1800;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));
 std::string line=p.key+" "+command+"\n";size_t pos=0;while(pos<line.size()) {int n=send(s,line.data()+pos,static_cast<int>(line.size()-pos),0);if(n<=0)return {};pos+=n;}
 std::string response;char b[1024];while(response.size()<8192){int n=recv(s,b,sizeof(b),0);if(n<=0)break;response.append(b,n);if(response.find('\n')!=std::string::npos)break;}
 if(response.empty()||response.size()>=8192||response.find('\n')==std::string::npos)return {};response.resize(response.find('\n'));return response;
}
std::wstring getString(const JsonObject& o,wchar_t const* key) { return std::wstring(o.GetNamedString(key,L"").c_str()); }
bool getBool(const JsonObject& o,wchar_t const* key) { return o.GetNamedBoolean(key,false); }
long long getNumber(const JsonObject& o,wchar_t const* key) { double x=o.GetNamedNumber(key,-1);return x>=-1&&x<604800000?static_cast<long long>(x):-1; }
std::vector<unsigned char> download(std::wstring url) {
 auto mark=url.find(L"%%"); if(mark!=std::wstring::npos)url.replace(mark,2,L"400x400");
 if(url.rfind(L"https://",0)!=0)url=L"https://"+url;
 URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=DWORD(-1);parts.dwUrlPathLength=DWORD(-1);parts.dwExtraInfoLength=DWORD(-1);
 if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||parts.nPort!=443)return {};
 std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
 if(host!=L"avatars.yandex.net"&&host!=L"avatars.mds.yandex.net")return {};
 std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
 HINTERNET session=WinHttpOpen(L"StationHub/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,nullptr,nullptr,0);if(!session)return {};
 WinHttpSetTimeouts(session,1500,1500,2000,2000);HINTERNET conn=WinHttpConnect(session,host.c_str(),443,0);HINTERNET req=conn?WinHttpOpenRequest(conn,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
 std::vector<unsigned char> out;
 if(req){DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(req,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
  if(WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(req,nullptr)){
   DWORD status=0,n=sizeof(status);if(WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&n,WINHTTP_NO_HEADER_INDEX)&&status==200){
    for(;;){DWORD available=0;if(!WinHttpQueryDataAvailable(req,&available)||!available)break;if(out.size()+available>1024*1024){out.clear();break;}size_t old=out.size();out.resize(old+available);DWORD got=0;if(!WinHttpReadData(req,out.data()+old,available,&got)){out.clear();break;}out.resize(old+got);if(!got)break;}
   }
  }WinHttpCloseHandle(req);
 }
 if(conn)WinHttpCloseHandle(conn);WinHttpCloseHandle(session);return out;
}
HBITMAP bitmapFromBytes(const std::vector<unsigned char>& data) {
 if(data.empty())return nullptr;HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,data.size());if(!mem)return nullptr;void* ptr=GlobalLock(mem);if(!ptr){GlobalFree(mem);return nullptr;}memcpy(ptr,data.data(),data.size());GlobalUnlock(mem);
 IStream* stream=nullptr;if(FAILED(CreateStreamOnHGlobal(mem,TRUE,&stream))){GlobalFree(mem);return nullptr;}
 HBITMAP result=nullptr;{Gdiplus::Bitmap bitmap(stream);if(bitmap.GetLastStatus()==Gdiplus::Ok&&bitmap.GetWidth()<=4096&&bitmap.GetHeight()<=4096){Gdiplus::Bitmap thumb(160,160,PixelFormat32bppARGB);Gdiplus::Graphics g(&thumb);g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);g.DrawImage(&bitmap,0,0,160,160);thumb.GetHBITMAP(Gdiplus::Color(255,25,26,36),&result);}}stream->Release();return result;
}
void refreshArt(Panel* p,const std::wstring& url) {
 if(url==p->lastArt)return;p->lastArt=url;unsigned generation=++p->artVersion;
 if(p->imageWorker.joinable())p->imageWorker.join();
 if(url.empty()){PostMessageW(p->hwnd,ART,generation,0);return;}
 p->imageWorker=std::thread([p,url,generation]{auto b=bitmapFromBytes(download(url));if(p->stop||!PostMessageW(p->hwnd,ART,generation,reinterpret_cast<LPARAM>(b))){if(b)DeleteObject(b);}});
}
void worker(Panel* p) {
 winrt::init_apartment(winrt::apartment_type::multi_threaded);
 auto next=std::chrono::steady_clock::now();
 while(!p->stop){
  std::string command;
  {std::unique_lock lock(p->mutex);p->wake.wait_until(lock,next,[&]{return p->stop||!p->commands.empty();});if(p->stop)break;if(!p->commands.empty()){command=std::move(p->commands.front());p->commands.pop_front();}}
  if(!command.empty()){
   auto raw=request(*p,command);std::wstring notice=L"Нет связи со Станцией";
   try{if(!raw.empty()){auto j=JsonObject::Parse(winrt::to_hstring(raw));auto r=getString(j,L"result");notice=r==L"SENT"?L"Команда отправлена":r==L"QUEUED"?L"Обновляем оценку…":r==L"NO_MEDIA"?L"На Станции нет доступного трека":r==L"RATE_LIMIT"?L"Подождите немного":L"Команда недоступна";}}catch(...){}
   {std::lock_guard lock(p->mutex);p->snapshot.notice=notice;p->noticeUntil=std::chrono::steady_clock::now()+4s;}PostMessageW(p->hwnd,UPDATE,0,0);next=std::chrono::steady_clock::now();continue;
  }
  if(std::chrono::steady_clock::now()<next)continue;
  auto raw=request(*p,"STATE");Snapshot value;try{if(!raw.empty()){auto j=JsonObject::Parse(winrt::to_hstring(raw));value.connected=getString(j,L"result")==L"OK";value.known=getBool(j,L"known");value.hasMedia=getBool(j,L"has_media");value.playing=getBool(j,L"playing");value.title=getString(j,L"title");value.artist=getString(j,L"artist");value.art=getString(j,L"artwork");value.trackToken=getString(j,L"track_token");value.rating=getString(j,L"rating");value.ratingStatus=getString(j,L"rating_status");value.position=getNumber(j,L"position_ms");value.duration=getNumber(j,L"duration_ms");value.volume=static_cast<int>(getNumber(j,L"volume"));}}catch(...){value.connected=false;}
  {std::lock_guard lock(p->mutex);if(std::chrono::steady_clock::now()<p->noticeUntil)value.notice=p->snapshot.notice;p->snapshot=value;}PostMessageW(p->hwnd,UPDATE,0,0);
  refreshArt(p,value.connected?value.art:L"");next=std::chrono::steady_clock::now()+1500ms;
 }
 if(p->imageWorker.joinable())p->imageWorker.join();winrt::uninit_apartment();
}
#include "station_widget_ui.inl"
