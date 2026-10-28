#include <windows.h>
#include <cstdio>
#include <string>
int wmain(int argc,wchar_t** argv){
 std::wstring args;for(int i=1;i<argc;i++){args+=argv[i];args+=L' ';}
 if(args.find(L"get-state")!=std::wstring::npos){std::puts("device");return 0;}
 if(args.find(L"content query")!=std::wstring::npos){wchar_t bad[8]{};if(GetEnvironmentVariableW(L"STATION_HUB_TEST_BAD_RESPONSE",bad,8))std::puts("No result found.");else std::puts("Row: 0 pairing_key=0123456789abcdef");return 0;}
 return 1;
}
