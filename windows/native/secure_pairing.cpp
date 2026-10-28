#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincred.h>
#include "secure_pairing.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {
bool validKey(const std::wstring& key){return key.size()==16&&std::all_of(key.begin(),key.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});}
std::wstring targetFor(const std::wstring& id){
 if(id.size()<4||id.size()>128||!std::all_of(id.begin(),id.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'z')||c==L'-'||c==L'_';}))return {};
 return L"StationHub/Station/"+id;
}
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
std::wstring findAdb(){
 wchar_t configured[32768]{};DWORD count=GetEnvironmentVariableW(L"STATION_HUB_ADB",configured,32768);
 if(count>0&&count<32768)return configured;
 wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::filesystem::path root(executable);
 for(int i=0;i<5;++i)root=root.parent_path();auto bundled=root/L"tools"/L"platform-tools"/L"adb.exe";
 std::error_code ec;if(std::filesystem::exists(bundled,ec)&&!ec)return bundled.wstring();
 wchar_t found[32768]{};count=SearchPathW(nullptr,L"adb.exe",nullptr,32768,found,nullptr);
 return count>0&&count<32768?std::wstring(found):std::wstring{};
}
// Read a small bounded response from a child process without ever writing it to
// a file or the console. Drain concurrently so a misbehaving child cannot hang
// on a full pipe; only our own child can be stopped on cancellation/timeout.
bool runCapture(const std::wstring& exe,const std::wstring& args,const std::atomic_bool& stop,std::string& output){
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE readRaw=nullptr,writeRaw=nullptr;
 if(!CreatePipe(&readRaw,&writeRaw,&sa,0))return false;Handle read{readRaw},write{writeRaw};
 if(!SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))return false;
 Handle input{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr)};
 if(input.value==INVALID_HANDLE_VALUE)return false;
 STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;si.wShowWindow=SW_HIDE;
 si.hStdInput=input.value;si.hStdOutput=write.value;si.hStdError=write.value;
 PROCESS_INFORMATION pi{};std::wstring command=L"\""+exe+L"\" "+args;
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;
 Handle process{pi.hProcess},thread{pi.hThread};CloseHandle(write.value);write.value=nullptr;
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);output.clear();bool finished=false,overflow=false;
 for(;;){
  DWORD available=0;if(!PeekNamedPipe(read.value,nullptr,0,nullptr,&available,nullptr))available=0;
  while(available){char buffer[1024];DWORD count=0;DWORD want=std::min<DWORD>(available,sizeof(buffer));
   if(!ReadFile(read.value,buffer,want,&count,nullptr)||!count)break;
   if(output.size()+count>8192){overflow=true;break;}output.append(buffer,count);available-=count;
  }
  if(overflow||stop||std::chrono::steady_clock::now()>=deadline)break;
  if(WaitForSingleObject(process.value,25)==WAIT_OBJECT_0){finished=true;DWORD remaining=0;if(!PeekNamedPipe(read.value,nullptr,0,nullptr,&remaining,nullptr)||!remaining)break;}
 }
 if(!finished){TerminateProcess(process.value,1);WaitForSingleObject(process.value,1000);return false;}
 if(stop)return false;
 DWORD exitCode=1;return !overflow&&GetExitCodeProcess(process.value,&exitCode)&&exitCode==0;
}
std::wstring parseKey(const std::string& text){
 const std::string marker="pairing_key=";auto at=text.find(marker);if(at==std::string::npos)return {};at+=marker.size();
 if(at+16>text.size())return {};std::wstring value;for(size_t i=0;i<16;i++){unsigned char c=text[at+i];if(c>='A'&&c<='F')c=static_cast<unsigned char>(c-'A'+'a');value+=wchar_t(c);}
 if(at+16<text.size()&&text[at+16]!='\r'&&text[at+16]!='\n'&&text[at+16]!=' '&&text[at+16]!=',')return {};
 return validKey(value)?value:std::wstring{};
}
}

PairingResult readKeyThroughAdb(const std::wstring& host,const std::atomic_bool& stop){
 if(stop)return {{},L"Поиск ключа отменён."};
 IN_ADDR ip{};if(InetPtonW(AF_INET,host.c_str(),&ip)!=1)return {{},L"Выберите Станцию или укажите её IPv4-адрес."};
 auto adb=findAdb();if(adb.empty())return {{},L"Не найден ADB. Установите Android platform-tools или введите ключ вручную."};
 const std::wstring endpoint=host+L":5555";std::string output;
 struct OutputGuard{std::string& text;~OutputGuard(){if(!text.empty())SecureZeroMemory(text.data(),text.size());}} guard{output};
 if(!runCapture(adb,L"-s "+endpoint+L" get-state",stop,output)||output.find("device")==std::string::npos){
  if(stop)return {{},L"Поиск ключа отменён."};
  runCapture(adb,L"connect "+endpoint,stop,output);
  if(!runCapture(adb,L"-s "+endpoint+L" get-state",stop,output)||output.find("device")==std::string::npos)return {{},L"Нет доступа к отладке Станции. Проверьте ADB или введите ключ вручную."};
 }
 if(!runCapture(adb,L"-s "+endpoint+L" shell content query --uri content://ru.stationhub.app.pairing/key --projection pairing_key",stop,output))return {{},L"Не удалось получить ключ через ADB. Обновите Station Hub на Станции или введите ключ вручную."};
 auto key=parseKey(output);
 if(key.empty())return {{},L"Ключ через ADB недоступен. Обновите Station Hub на Станции или введите ключ вручную."};
 return {std::move(key),{}};
}
bool loadSavedPairingKey(const std::wstring& id,std::wstring& key){
 auto target=targetFor(id);if(target.empty())return false;PCREDENTIALW credential=nullptr;
 if(!CredReadW(target.c_str(),CRED_TYPE_GENERIC,0,&credential))return false;
 std::wstring value;if(credential->CredentialBlobSize==16&&credential->CredentialBlob)for(DWORD i=0;i<16;i++)value+=wchar_t(credential->CredentialBlob[i]);
 if(credential->CredentialBlob&&credential->CredentialBlobSize)SecureZeroMemory(credential->CredentialBlob,credential->CredentialBlobSize);
 CredFree(credential);if(!validKey(value))return false;key=std::move(value);return true;
}
bool savePairingKey(const std::wstring& id,const std::wstring& key){
 auto target=targetFor(id);if(target.empty()||!validKey(key))return false;BYTE bytes[16];for(size_t i=0;i<16;i++)bytes[i]=static_cast<BYTE>(key[i]);
 CREDENTIALW credential{};credential.Type=CRED_TYPE_GENERIC;credential.TargetName=const_cast<LPWSTR>(target.c_str());credential.CredentialBlobSize=sizeof(bytes);credential.CredentialBlob=bytes;credential.Persist=CRED_PERSIST_LOCAL_MACHINE;
 bool ok=CredWriteW(&credential,0)!=FALSE;SecureZeroMemory(bytes,sizeof(bytes));return ok;
}
bool forgetPairingKey(const std::wstring& id){auto target=targetFor(id);return !target.empty()&&CredDeleteW(target.c_str(),CRED_TYPE_GENERIC,0)!=FALSE;}
