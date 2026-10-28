#include "yandex_like.h"
#include <windows.h>
#include <uiautomation.h>
#include <winrt/base.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

using winrt::com_ptr;
namespace {
struct Search { com_ptr<IUIAutomation> automation; std::vector<com_ptr<IUIAutomationElement>> candidates; };

com_ptr<IUIAutomationCondition> named(IUIAutomation* ui, const wchar_t* name) {
    VARIANT value{}; VariantInit(&value); value.vt=VT_BSTR; value.bstrVal=SysAllocString(name);
    com_ptr<IUIAutomationCondition> condition;
    HRESULT hr=ui->CreatePropertyCondition(UIA_NamePropertyId,value,condition.put());
    VariantClear(&value); winrt::check_hresult(hr); return condition;
}
bool isMusicUrl(std::wstring url) {
    std::transform(url.begin(),url.end(),url.begin(),[](wchar_t c){return std::towlower(c);});
    constexpr wchar_t scheme[] = L"https://";
    if (url.compare(0,8,scheme)==0) url.erase(0,8);
    const std::wstring host=L"music.yandex.ru";
    if (url.compare(0,host.size(),host)!=0) return false;
    return url.size()==host.size() || url[host.size()]==L'/' || url[host.size()]==L'?' || url[host.size()]==L'#';
}
bool selectedTabIsMusic(IUIAutomation* ui, IUIAutomationElement* root) {
    VARIANT value{}; VariantInit(&value); value.vt=VT_I4; value.lVal=UIA_EditControlTypeId;
    com_ptr<IUIAutomationCondition> editsCondition;
    if (FAILED(ui->CreatePropertyCondition(UIA_ControlTypePropertyId,value,editsCondition.put()))) return false;
    com_ptr<IUIAutomationElementArray> edits;
    if (FAILED(root->FindAll(TreeScope_Descendants,editsCondition.get(),edits.put()))) return false;
    int n=0; edits->get_Length(&n);
    for (int i=0;i<n;++i) {
        com_ptr<IUIAutomationElement> e; if (FAILED(edits->GetElement(i,e.put()))) continue;
        BSTR rawName=nullptr; e->get_CurrentName(&rawName);
        std::wstring name = rawName ? std::wstring(rawName,SysStringLen(rawName)) : L"";
        if(rawName) SysFreeString(rawName);
        std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return std::towlower(c);});
        if(name.find(L"адрес")==std::wstring::npos && name.find(L"Адрес")==std::wstring::npos && name.find(L"address")==std::wstring::npos) continue;
        com_ptr<IUIAutomationValuePattern> valuePattern;
        if(FAILED(e->GetCurrentPatternAs(UIA_ValuePatternId,__uuidof(IUIAutomationValuePattern),valuePattern.put_void())) || !valuePattern) continue;
        BSTR rawUrl=nullptr; valuePattern->get_CurrentValue(&rawUrl);
        std::wstring url = rawUrl ? std::wstring(rawUrl,SysStringLen(rawUrl)) : L"";
        if(rawUrl) SysFreeString(rawUrl);
        if(isMusicUrl(url)) return true;
    }
    return false;
}
BOOL CALLBACK collect(HWND window, LPARAM context) {
    try {
    wchar_t cls[128]{}; GetClassNameW(window,cls,128);
    if(!IsWindowVisible(window) || std::wstring(cls)!=L"Chrome_WidgetWin_1") return TRUE;
    auto& s=*reinterpret_cast<Search*>(context);
    com_ptr<IUIAutomationElement> root;
    if(FAILED(s.automation->ElementFromHandle(window,root.put())) || !selectedTabIsMusic(s.automation.get(),root.get())) return TRUE;
    auto playerName=named(s.automation.get(),L"Плеер");
    com_ptr<IUIAutomationElementArray> players;
    if(FAILED(root->FindAll(TreeScope_Descendants,playerName.get(),players.put()))) return TRUE;
    int count=0;players->get_Length(&count);
    auto likeName=named(s.automation.get(),L"Нравится");
    auto pauseName=named(s.automation.get(),L"Пауза");
    for(int i=0;i<count;++i){
        com_ptr<IUIAutomationElement> player;players->GetElement(i,player.put());
        com_ptr<IUIAutomationElement> pause;
        if(FAILED(player->FindFirst(TreeScope_Descendants,pauseName.get(),pause.put())) || !pause) continue;
        com_ptr<IUIAutomationElementArray> likes;
        if(FAILED(player->FindAll(TreeScope_Descendants,likeName.get(),likes.put()))) continue;
        int n=0;likes->get_Length(&n);
        for(int j=0;j<n;++j){com_ptr<IUIAutomationElement> button;if(SUCCEEDED(likes->GetElement(j,button.put()))&&button)s.candidates.push_back(std::move(button));}
    }
    } catch (...) { return TRUE; }
    return TRUE;
}
}
LikeResult likeYandexMusic(bool apply) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Search search;
        winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(search.automation.put())));
        EnumWindows(collect,reinterpret_cast<LPARAM>(&search));
        if(search.candidates.empty()) return LikeResult::NoPlayer;
        if(search.candidates.size()!=1) return LikeResult::Failed;
        com_ptr<IUIAutomationTogglePattern> toggle;
        if(FAILED(search.candidates[0]->GetCurrentPatternAs(UIA_TogglePatternId,__uuidof(IUIAutomationTogglePattern),toggle.put_void())) || !toggle) return LikeResult::Failed;
        ToggleState state{};if(FAILED(toggle->get_CurrentToggleState(&state))) return LikeResult::Failed;
        if(state==ToggleState_On) return LikeResult::AlreadyLiked;
        if(state!=ToggleState_Off) return LikeResult::Failed;
        if(!apply) return LikeResult::ReadyUnliked;
        if(FAILED(toggle->Toggle())) return LikeResult::Failed;
        for(int i=0;i<12;++i){Sleep(100);if(SUCCEEDED(toggle->get_CurrentToggleState(&state))&&state==ToggleState_On)return LikeResult::Applied;}
        return LikeResult::Failed;
    } catch (...) { return LikeResult::Failed; }
}
