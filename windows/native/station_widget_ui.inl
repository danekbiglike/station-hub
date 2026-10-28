// Responsive, double-buffered music widget. All playback remains in Station Hub's
// existing native worker; the window never modifies Station volume by itself.
constexpr int VOLUME_BUTTON=212;
constexpr int MORE_BUTTON=213;
constexpr UINT WIDGET_TIMER=3;
const int buttonIds[]={PREV,PLAY,NEXT,LIKE,DISLIKE,VOLUME_BUTTON,MORE_BUTTON,PIN,CLOSE};
constexpr int focusOrder[]={B_MENU,B_DISLIKE,B_PREV,B_PLAY,B_NEXT,B_LIKE,B_VOLUME,B_PIN,B_CLOSE};

void enqueue(Panel* p,const std::string& cmd){std::lock_guard lock(p->mutex);if(p->commands.size()<4){p->commands.emplace_back(cmd);p->wake.notify_one();}}
void ratingAction(Panel* p,const char* action,const Snapshot& s){if(!s.trackToken.empty())enqueue(p,std::string(action)+" "+winrt::to_string(s.trackToken));}
std::wstring widgetPrefs(){wchar_t path[MAX_PATH]{};if(!GetEnvironmentVariableW(L"LOCALAPPDATA",path,MAX_PATH))return {};std::wstring dir=std::wstring(path)+L"\\Station Hub";CreateDirectoryW(dir.c_str(),nullptr);return dir+L"\\widget.ini";}
void roundFill(Gdiplus::Graphics& g,Gdiplus::Brush& b,Gdiplus::RectF r,float radius){using namespace Gdiplus;if(r.Width<=0||r.Height<=0)return;radius=std::min(radius,std::min(r.Width,r.Height)/2);float d=radius*2;GraphicsPath p;p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();g.FillPath(&b,&p);}
void label(Gdiplus::Graphics& g,const std::wstring& text,RECT r,float fontSize,Gdiplus::Color color,bool bold=false,bool center=false){using namespace Gdiplus;if(width(r)<=0||height(r)<=0)return;FontFamily family(L"Segoe UI");Font font(&family,fontSize,bold?FontStyleBold:FontStyleRegular,UnitPixel);StringFormat fmt;fmt.SetAlignment(center?StringAlignmentCenter:StringAlignmentNear);fmt.SetLineAlignment(StringAlignmentCenter);fmt.SetTrimming(StringTrimmingEllipsisCharacter);fmt.SetFormatFlags(StringFormatFlagsNoWrap);SolidBrush brush(color);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.DrawString(text.c_str(),-1,&font,RectF(float(r.left),float(r.top),float(width(r)),float(height(r))),&fmt,&brush);}
COLORREF artTint(HBITMAP bitmap){using namespace Gdiplus;if(!bitmap)return RGB(68,62,31);Bitmap bmp(bitmap,nullptr);if(bmp.GetLastStatus()!=Ok)return RGB(68,62,31);long r=0,g=0,b=0,n=0;for(UINT y=8;y<bmp.GetHeight();y+=16)for(UINT x=8;x<bmp.GetWidth();x+=16){Color c;if(bmp.GetPixel(x,y,&c)!=Ok)continue;r+=c.GetR();g+=c.GetG();b+=c.GetB();n++;}if(!n)return RGB(68,62,31);return RGB(std::clamp(int(r/n*.35)+22,30,105),std::clamp(int(g/n*.35)+20,28,100),std::clamp(int(b/n*.35)+20,28,100));}
// Sample a small palette only when the cover changes; no image assets from
// the official application are copied into this project.
COLORREF artAccent(HBITMAP bitmap){using namespace Gdiplus;if(!bitmap)return RGB(140,58,160);Bitmap bmp(bitmap,nullptr);if(bmp.GetLastStatus()!=Ok)return RGB(140,58,160);int best=0;COLORREF result=RGB(140,58,160);for(UINT y=8;y<bmp.GetHeight();y+=16)for(UINT x=8;x<bmp.GetWidth();x+=16){Color c;if(bmp.GetPixel(x,y,&c)!=Ok)continue;int r=c.GetR(),g=c.GetG(),b=c.GetB(),hi=std::max({r,g,b}),lo=std::min({r,g,b});int score=(hi-lo)*2+hi/3;if(hi-lo>=35&&hi>55&&score>best){best=score;result=RGB(r,g,b);}}return result;}
#include "widget_render.inl"
void placeButtons(Panel* p,const Layout& l){
 p->seekRect=l.seek;
 if(p->tooltip)for(size_t i=0;i<l.button.size();++i){TOOLINFOW ti{};ti.cbSize=sizeof(ti);ti.hwnd=p->hwnd;ti.uId=buttonIds[i];ti.rect=l.button[i];SendMessageW(p->tooltip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&ti));}
}
void submitVolume(Panel* p,std::optional<int> value){if(value)enqueue(p,"VOLUME_SET "+std::to_string(*value));}
void reconcileVolume(Panel* p,const Snapshot& s){
 submitVolume(p,p->volumeIntent.observe(s.connected,s.volume,VolumeIntent::Clock::now()));
}
void updateButtons(Panel* p,const Snapshot& s){bool has=s.connected&&s.known&&s.hasMedia;bool ratingKnown=s.rating==L"LIKE"||s.rating==L"DISLIKE"||s.rating==L"NONE";bool rating=has&&ratingKnown&&s.ratingStatus!=L"PENDING"&&!s.trackToken.empty();for(int i:{B_PREV,B_PLAY,B_NEXT})EnableWindow(p->buttons[i],has);EnableWindow(p->buttons[B_LIKE],rating&&(s.playing||s.rating==L"LIKE"));EnableWindow(p->buttons[B_DISLIKE],rating&&(s.playing||s.rating==L"DISLIKE"));SetWindowTextW(p->buttons[B_PLAY],s.playing?L"Пауза":L"Продолжить");SetWindowTextW(p->buttons[B_LIKE],s.rating==L"LIKE"?L"Убрать лайк":L"Нравится");SetWindowTextW(p->buttons[B_DISLIKE],s.rating==L"DISLIKE"?L"Убрать дизлайк":L"Не нравится");for(HWND b:p->buttons)InvalidateRect(b,nullptr,FALSE);}
void renderVolume(Panel* p,HWND w){
 using namespace Gdiplus;
 if(p->volumeRendering)return;p->volumeRendering=true;struct Guard{bool& flag;~Guard(){flag=false;}} guard{p->volumeRendering};
 RECT r{};GetClientRect(w,&r);int cw=width(r),ch=height(r);if(cw<=0||ch<=0)return;constexpr int scale=2;Bitmap canvas(cw*scale,ch*scale,PixelFormat32bppPARGB);if(canvas.GetLastStatus()!=Ok)return;
 {Graphics g(&canvas);g.Clear(Color(0,0,0,0));g.ScaleTransform(float(scale),float(scale));g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
  SolidBrush back(Color(255,32,32,36));roundFill(g,back,RectF(0,0,float(cw),float(ch)),12);
  Snapshot s;{std::lock_guard lock(p->mutex);s=p->snapshot;}int desired=p->volumeIntent.desired();int value=p->volumeDragging?p->volumePreview:desired>=0?desired:s.volume;
  label(g,value>=0?std::to_wstring(value)+L" / 10":L"—",rect(0,6,cw,23),13,Color(255,255,255,255),true,true);
  SolidBrush track(Color(80,255,255,255)),fill(Color(255,245,245,245));roundFill(g,track,RectF(25,38,8,112),4);if(value>=0){float fillHeight=float(std::clamp(value,0,10))*11.2f;roundFill(g,fill,RectF(25,150-fillHeight,8,fillHeight),4);}
  Pen pen(Color(220,255,255,255),1.5f);PointF pts[4]={{20,164},{24,164},{29,160},{29,174}};g.DrawPolygon(&pen,pts,4);g.DrawArc(&pen,26.0f,159.0f,13.0f,16.0f,-60.0f,120.0f);
 }
 presentLayered(w,canvas,cw,ch,scale);
}
LRESULT CALLBACK volumeProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
 Panel* p=reinterpret_cast<Panel*>(GetWindowLongPtrW(w,GWLP_USERDATA));
 if(m==WM_NCCREATE){p=reinterpret_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
 if(!p)return DefWindowProcW(w,m,wp,lp);
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_ERASEBKGND)return 1;
 if(m==WM_NCCALCSIZE)return 0;
 if(m==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(w,&ps);EndPaint(w,&ps);renderVolume(p,w);return 0;}
 if(m==WM_LBUTTONDOWN||m==WM_MOUSEMOVE||m==WM_LBUTTONUP){
  if(m==WM_LBUTTONDOWN){Snapshot s;{std::lock_guard lock(p->mutex);s=p->snapshot;}if(!s.connected||s.volume<0||s.volume>10)return 0;p->volumeDragging=true;SetCapture(w);}
  if(!p->volumeDragging)return 0;Snapshot s;{std::lock_guard lock(p->mutex);s=p->snapshot;}int y=static_cast<short>(HIWORD(lp));int requested=std::clamp(int((150-y+5)/11.2),1,10);p->volumePreview=std::min(requested,std::min(10,s.volume+1));RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
  if(m==WM_LBUTTONUP){int target=p->volumePreview;p->volumeDragging=false;ReleaseCapture();if(s.connected)submitVolume(p,p->volumeIntent.select(s.volume,target,VolumeIntent::Clock::now()));RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}return 0;
 }
 if(m==WM_CAPTURECHANGED){p->volumeDragging=false;RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);return 0;}
 if(m==WM_CLOSE){ShowWindow(w,SW_HIDE);p->volumeOpen=false;return 0;}
 return DefWindowProcW(w,m,wp,lp);
}
void toggleVolume(Panel* p){
 if(p->volumeOpen){ShowWindow(p->volumePopup,SW_HIDE);p->volumeOpen=false;return;}
 if(!p->volumePopup){HINSTANCE instance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(p->hwnd,GWLP_HINSTANCE));static bool registered=false;if(!registered){WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=volumeProc;wc.lpszClassName=L"StationHubVolumePopup";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!RegisterClassW(&wc))return;registered=true;}p->volumePopup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,L"StationHubVolumePopup",L"Громкость Станции",WS_POPUP,0,0,58,184,p->hwnd,nullptr,instance,p);if(!p->volumePopup)return;const DWORD noBorder=0xfffffffe;DwmSetWindowAttribute(p->volumePopup,34,&noBorder,sizeof(noBorder));}
 RECT client{},area{};GetClientRect(p->hwnd,&client);RECT button=layout(width(client),height(client),p->skin).button[B_VOLUME];POINT top{button.left,button.top},bottom{button.right,button.bottom};ClientToScreen(p->hwnd,&top);ClientToScreen(p->hwnd,&bottom);button={top.x,top.y,bottom.x,bottom.y};MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromRect(&button,MONITOR_DEFAULTTONEAREST),&mi);area=mi.rcWork;int x=std::clamp(int((button.left+button.right)/2-29),int(area.left),int(area.right-58));int y=button.top-190;if(y<area.top)y=button.bottom+6;y=std::clamp(y,int(area.top),int(area.bottom-184));SetWindowPos(p->volumePopup,HWND_TOP, x,y,58,184,SWP_NOACTIVATE|SWP_SHOWWINDOW);p->volumeOpen=true;renderVolume(p,p->volumePopup);
}
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){Panel* p=reinterpret_cast<Panel*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){p=reinterpret_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->hwnd=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}if(!p)return DefWindowProcW(w,m,wp,lp);
 switch(m){
 // Both forms must remove the non-client frame. Returning DefWindowProc for the
 // simple form makes a layered update repeatedly shrink the window.
 case WM_NCCALCSIZE:return 0;
 case WM_GETMINMAXINFO:{auto mm=reinterpret_cast<MINMAXINFO*>(lp);mm->ptMinTrackSize.x=330;mm->ptMinTrackSize.y=110;return 0;}
 case WM_NCHITTEST:{POINT pt{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};ScreenToClient(w,&pt);RECT r{};GetClientRect(w,&r);int edge=6;bool left=pt.x<edge,right=pt.x>=r.right-edge,top=pt.y<edge,bottom=pt.y>=r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;return HTCLIENT;}
 case WM_MOVE:if(p->volumeOpen&&p->volumePopup){ShowWindow(p->volumePopup,SW_HIDE);p->volumeOpen=false;}break;
 case WM_SIZE:{int cw=LOWORD(lp),ch=HIWORD(lp);if(p->volumeOpen&&p->volumePopup){ShowWindow(p->volumePopup,SW_HIDE);p->volumeOpen=false;}if(cw>0&&ch>0){placeButtons(p,layout(cw,ch,p->skin));renderWidget(p);}return 0;}
 case WM_EXITSIZEMOVE:{RECT r{};GetClientRect(w,&r);auto path=widgetPrefs();if(!path.empty()){auto a=std::to_wstring(width(r)),b=std::to_wstring(height(r));WritePrivateProfileStringW(L"widget",L"width",a.c_str(),path.c_str());WritePrivateProfileStringW(L"widget",L"height",b.c_str(),path.c_str());}return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);EndPaint(w,&ps);renderWidget(p);return 0;}
 case WM_DRAWITEM:return TRUE; // off-screen accessibility buttons never draw pixels
 case WM_GETDLGCODE:return DLGC_WANTTAB|DLGC_WANTARROWS|DLGC_WANTCHARS;
 case WM_KEYDOWN:{
  if(wp==VK_TAB||wp==VK_LEFT||wp==VK_RIGHT){int direction=(wp==VK_LEFT||(wp==VK_TAB&&(GetKeyState(VK_SHIFT)&0x8000)))?-1:1;int pos=direction>0?-1:0;for(int j=0;j<9;j++)if(focusOrder[j]==p->focused){pos=j;break;}for(int n=0;n<9;n++){pos=(pos+direction+9)%9;int i=focusOrder[pos];if(p->buttons[i]&&IsWindowEnabled(p->buttons[i])){p->focused=i;break;}}InvalidateRect(w,nullptr,FALSE);return 0;}
  if((wp==VK_SPACE||wp==VK_RETURN)&&p->focused>=0&&p->buttons[p->focused]&&IsWindowEnabled(p->buttons[p->focused])){int id=buttonIds[p->focused];SendMessageW(w,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);return 0;}
  if(wp==VK_ESCAPE&&p->volumeOpen){toggleVolume(p);return 0;}break;
 }
 case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(w,nullptr,FALSE);break;
 case WM_COMMAND:{Snapshot s;{std::lock_guard lock(p->mutex);s=p->snapshot;}switch(LOWORD(wp)){
  case PREV:enqueue(p,"PREV");break;case PLAY:enqueue(p,s.playing?"PAUSE":"PLAY");break;case NEXT:enqueue(p,"NEXT");break;
  case LIKE:ratingAction(p,s.rating==L"LIKE"?"UNLIKE":"LIKE",s);break;case DISLIKE:ratingAction(p,s.rating==L"DISLIKE"?"UNDISLIKE":"DISLIKE",s);break;
  case VOLUME_BUTTON:toggleVolume(p);break;
  case MORE_BUTTON:{RECT area{};GetClientRect(w,&area);RECT button=layout(width(area),height(area),p->skin).button[B_MENU];POINT pt{button.left,button.bottom};ClientToScreen(w,&pt);HMENU menu=CreatePopupMenu();if(menu){AppendMenuW(menu,MF_STRING,1,L"Открыть настройки Station Hub");AppendMenuW(menu,MF_STRING,2,L"Закрыть виджет");SetForegroundWindow(w);int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,w,nullptr);DestroyMenu(menu);if(selected==1){ShowWindow(p->owner,SW_RESTORE);SetForegroundWindow(p->owner);}else if(selected==2){DestroyWindow(w);return 0;}}break;}
  case PIN:{p->pinned=!p->pinned;SetWindowPos(w,p->pinned?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowTextW(p->buttons[B_PIN],p->pinned?L"Открепить от верхнего слоя":L"Закрепить поверх окон");auto path=widgetPrefs();if(!path.empty())WritePrivateProfileStringW(L"widget",L"topmost",p->pinned?L"1":L"0",path.c_str());InvalidateRect(w,nullptr,FALSE);break;}
  case CLOSE:DestroyWindow(w);break;}return 0;}
 case WM_LBUTTONDOWN:{POINT pt{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};RECT bounds{};GetClientRect(w,&bounds);Layout l=layout(width(bounds),height(bounds),p->skin);for(int i=0;i<9;i++)if(PtInRect(&l.button[i],pt)&&p->buttons[i]&&IsWindowEnabled(p->buttons[i])){SetFocus(w);p->focused=i;p->pressed=i;SetCapture(w);InvalidateRect(w,nullptr,FALSE);return 0;}RECT seek=p->seekRect;InflateRect(&seek,0,8);Snapshot s;{std::lock_guard lock(p->mutex);s=p->snapshot;}if(PtInRect(&seek,pt)&&s.connected&&s.hasMedia&&s.duration>0&&!s.trackToken.empty()){p->seeking=true;p->seekDuration=s.duration;p->seekToken=s.trackToken;SetCapture(w);p->previewMs=std::clamp<long long>((static_cast<long long>(pt.x-p->seekRect.left)*s.duration)/std::max(1,width(p->seekRect)),0,s.duration);InvalidateRect(w,nullptr,FALSE);}else if(p->volumeOpen){toggleVolume(p);}else{ClientToScreen(w,&pt);ReleaseCapture();SendMessageW(w,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(pt.x,pt.y));}return 0;}
 case WM_MOUSEMOVE:{POINT pt{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};if(p->seeking){p->previewMs=std::clamp<long long>((static_cast<long long>(pt.x-p->seekRect.left)*p->seekDuration)/std::max(1,width(p->seekRect)),0,p->seekDuration);InvalidateRect(w,nullptr,FALSE);return 0;}RECT bounds{};GetClientRect(w,&bounds);Layout l=layout(width(bounds),height(bounds),p->skin);int hover=-1;for(int i=0;i<9;i++)if(PtInRect(&l.button[i],pt)&&p->buttons[i]&&IsWindowEnabled(p->buttons[i])){hover=i;break;}bool hoverSeek=PtInRect(&l.seek,pt)!=FALSE;if(hover!=p->hover||hoverSeek!=p->hoverSeek){p->hover=hover;p->hoverSeek=hoverSeek;InvalidateRect(w,nullptr,FALSE);}TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,w,0};TrackMouseEvent(&tracking);return 0;}
 case WM_MOUSELEAVE:p->hover=-1;p->hoverSeek=false;InvalidateRect(w,nullptr,FALSE);return 0;
 case WM_LBUTTONUP:{if(p->seeking){p->seeking=false;ReleaseCapture();enqueue(p,"SEEK "+std::to_string(p->previewMs)+" "+winrt::to_string(p->seekToken));InvalidateRect(w,nullptr,FALSE);return 0;}if(p->pressed>=0){int pressed=p->pressed;p->pressed=-1;ReleaseCapture();POINT pt{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};RECT bounds{};GetClientRect(w,&bounds);Layout l=layout(width(bounds),height(bounds),p->skin);if(PtInRect(&l.button[pressed],pt)&&p->buttons[pressed]&&IsWindowEnabled(p->buttons[pressed])){SendMessageW(w,WM_COMMAND,MAKEWPARAM(buttonIds[pressed],BN_CLICKED),0);return 0;}InvalidateRect(w,nullptr,FALSE);return 0;}break;}
 case WM_CAPTURECHANGED:if(p->seeking||p->pressed>=0){p->seeking=false;p->pressed=-1;InvalidateRect(w,nullptr,FALSE);}return 0;
 case UPDATE:{Snapshot s;{std::lock_guard lock(p->mutex);s=p->snapshot;}reconcileVolume(p,s);bool change=!p->haveDisplayed||p->displayed.playing!=s.playing||p->displayed.rating!=s.rating||p->displayed.ratingStatus!=s.ratingStatus||p->displayed.connected!=s.connected||p->displayed.hasMedia!=s.hasMedia;if(change)updateButtons(p,s);p->displayed=s;p->haveDisplayed=true;RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);if(p->volumeOpen&&p->volumePopup)InvalidateRect(p->volumePopup,nullptr,FALSE);return 0;}
 case ART:{auto bmp=reinterpret_cast<HBITMAP>(lp);if(static_cast<unsigned>(wp)!=p->artVersion.load()){if(bmp)DeleteObject(bmp);return 0;}if(p->bitmap)DeleteObject(p->bitmap);p->bitmap=bmp;p->tint=artTint(bmp);p->accent=artAccent(bmp);RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);for(HWND b:p->buttons)InvalidateRect(b,nullptr,FALSE);return 0;}
 case WM_CLOSE:DestroyWindow(w);return 0;
 case WM_DESTROY:if(p->volumePopup){DestroyWindow(p->volumePopup);p->volumePopup=nullptr;}p->stop=true;p->wake.notify_all();if(p->worker.joinable())p->worker.join();{MSG msg;while(PeekMessageW(&msg,w,ART,ART,PM_REMOVE))if(msg.lParam)DeleteObject(reinterpret_cast<HBITMAP>(msg.lParam));}if(p->bitmap)DeleteObject(p->bitmap);panel=nullptr;delete p;return 0;
 }
 return DefWindowProcW(w,m,wp,lp);
}
} // namespace

int stationWidgetSkin(){auto path=widgetPrefs();return path.empty()?0:std::clamp<int>(GetPrivateProfileIntW(L"widget",L"skin",0,path.c_str()),0,2);}
void setStationWidgetSkin(int skin){
 skin=std::clamp(skin,0,2);auto path=widgetPrefs();
 if(!path.empty()){auto value=std::to_wstring(skin);WritePrivateProfileStringW(L"widget",L"skin",value.c_str(),path.c_str());}
 if(panel){panel->skin=skin;if(panel->seeking||panel->pressed>=0){panel->seeking=false;panel->pressed=-1;ReleaseCapture();}if(panel->volumeOpen)toggleVolume(panel);RECT bounds{};GetClientRect(panel->hwnd,&bounds);placeButtons(panel,layout(width(bounds),height(bounds),skin));InvalidateRect(panel->hwnd,nullptr,FALSE);}
}

void showStationControl(HINSTANCE instance,HWND owner,const std::wstring& host,const std::wstring& key){
 if(panel){ShowWindow(panel->hwnd,SW_RESTORE);SetForegroundWindow(panel->hwnd);return;}
 IN_ADDR ip{};if(InetPtonW(AF_INET,host.c_str(),&ip)!=1||key.size()!=16||key.find_first_not_of(L"0123456789abcdef")!=std::wstring::npos){MessageBoxW(owner,L"Укажите адрес Станции и её ключ в главном окне.",L"Station Hub",MB_OK|MB_ICONWARNING);return;}
 static ULONG_PTR gdiplusToken=0;if(!gdiplusToken){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gdiplusToken,&input,nullptr);}
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 static bool registered=false;if(!registered){WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"StationHubStationControl";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!RegisterClassW(&wc))return;registered=true;}
 auto p=new Panel;p->owner=owner;p->host=winrt::to_string(host);p->key=winrt::to_string(key);p->skin=stationWidgetSkin();panel=p;
 auto prefs=widgetPrefs();int savedW=prefs.empty()?690:GetPrivateProfileIntW(L"widget",L"width",690,prefs.c_str());int savedH=prefs.empty()?230:GetPrivateProfileIntW(L"widget",L"height",230,prefs.c_str());savedW=std::clamp(savedW,330,1600);savedH=std::clamp(savedH,110,1000);
 HWND w=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_LAYERED,L"StationHubStationControl",L"Музыка Станции — Station Hub",WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,savedW,savedH,nullptr,nullptr,instance,p);if(!w){panel=nullptr;delete p;return;}
 // The alpha surface defines the edge; prevent Windows 11 drawing a system border.
 const DWORD noBorder=0xfffffffe;DwmSetWindowAttribute(w,34,&noBorder,sizeof(noBorder));
 const wchar_t* names[]={L"Назад",L"Пауза",L"Дальше",L"Нравится / снять лайк",L"Не нравится / снять дизлайк",L"Громкость Станции: повышение по одному делению",L"Дополнительные действия",L"Закрепить поверх окон",L"Закрыть"};
 p->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,w,nullptr,instance,nullptr);
 for(size_t i=0;i<p->buttons.size();++i){p->buttons[i]=CreateWindowExW(0,L"BUTTON",names[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,-100,-100,1,1,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(buttonIds[i])),instance,nullptr);if(p->tooltip){TOOLINFOW ti{};ti.cbSize=sizeof(ti);ti.uFlags=TTF_SUBCLASS;ti.hwnd=w;ti.uId=buttonIds[i];ti.lpszText=const_cast<LPWSTR>(names[i]);SendMessageW(p->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&ti));}}
 p->pinned=!prefs.empty()&&GetPrivateProfileIntW(L"widget",L"topmost",0,prefs.c_str())!=0;if(p->pinned){SetWindowPos(w,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowTextW(p->buttons[B_PIN],L"Открепить от верхнего слоя");}
 RECT area{};GetClientRect(w,&area);placeButtons(p,layout(width(area),height(area),p->skin));Snapshot empty;updateButtons(p,empty);
 p->worker=std::thread(worker,p);renderWidget(p);ShowWindow(w,SW_SHOW);UpdateWindow(w);
}
