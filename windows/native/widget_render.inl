// The visible widget is one per-pixel-alpha surface. Native buttons live outside
// the client area for accessibility; drawing them separately caused white boxes
// and tearing during interactive resizing. Rendering at 2x makes curves smooth.
void drawWidgetButton(Gdiplus::Graphics& g,Panel* p,const Snapshot& s,int index,const RECT& r){
 using namespace Gdiplus;
 if(width(r)<=0||height(r)<=0)return;
 int id=buttonIds[index];float w=float(width(r)),h=float(height(r));
 auto state=g.Save();g.TranslateTransform(float(r.left),float(r.top));
 bool disabled=p->buttons[index]&&!IsWindowEnabled(p->buttons[index]);
 bool active=(id==LIKE&&s.rating==L"LIKE")||(id==DISLIKE&&s.rating==L"DISLIKE")||(id==PIN&&p->pinned);
 bool hover=p->hover==index||p->pressed==index;
 Color bg=id==PLAY?(hover?Color(255,255,241,53):Color(255,255,232,0)):active?Color(255,217,215,210):hover?Color(255,63,63,66):p->skin==2?Color(180,38,38,41):Color(230,36,36,38);
 bool flat=p->skin==1&&id!=PLAY&&id!=PIN&&id!=CLOSE;
 if(!flat||hover){if(flat)bg=Color(55,255,255,255);SolidBrush base(bg);g.FillEllipse(&base,1.0f,1.0f,w-2.0f,h-2.0f);}
 Color fg=disabled?Color(90,255,255,255):id==PLAY||(active&&!flat)?Color(255,25,25,20):Color(220,255,255,255);
 Pen pen(fg,std::max(1.5f,w/19.0f));pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);SolidBrush ink(fg);
 float cx=w/2,cy=h/2,unit=w/48;
 if(id==PLAY){if(s.playing){g.FillRectangle(&ink,cx-7*unit,cy-9*unit,5*unit,18*unit);g.FillRectangle(&ink,cx+2*unit,cy-9*unit,5*unit,18*unit);}else{PointF pts[3]={{cx-5*unit,cy-10*unit},{cx-5*unit,cy+10*unit},{cx+9*unit,cy}};g.FillPolygon(&ink,pts,3);}}
 else if(id==PREV||id==NEXT){float sign=id==NEXT?1.0f:-1.0f;PointF pts[3]={{cx-sign*5*unit,cy-8*unit},{cx-sign*5*unit,cy+8*unit},{cx+sign*5*unit,cy}};g.FillPolygon(&ink,pts,3);g.DrawLine(&pen,cx+sign*7*unit,cy-8*unit,cx+sign*7*unit,cy+8*unit);}
 else if(id==LIKE||id==DISLIKE){FontFamily family(L"Segoe UI Symbol");Font font(&family,w*.50f,FontStyleRegular,UnitPixel);StringFormat fmt;fmt.SetAlignment(StringAlignmentCenter);fmt.SetLineAlignment(StringAlignmentCenter);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.DrawString(id==LIKE?(active?L"♥":L"♡"):L"⊘",-1,&font,RectF(0,0,w,h),&fmt,&ink);}
 else if(id==CLOSE){g.DrawLine(&pen,cx-7*unit,cy-7*unit,cx+7*unit,cy+7*unit);g.DrawLine(&pen,cx+7*unit,cy-7*unit,cx-7*unit,cy+7*unit);}
 else if(id==PIN){g.DrawLine(&pen,cx-7*unit,cy-7*unit,cx+7*unit,cy-7*unit);g.DrawLine(&pen,cx-4*unit,cy-7*unit,cx-4*unit,cy+1*unit);g.DrawLine(&pen,cx+4*unit,cy-7*unit,cx+4*unit,cy+1*unit);g.DrawLine(&pen,cx-7*unit,cy+1*unit,cx+7*unit,cy+1*unit);g.DrawLine(&pen,cx,cy+1*unit,cx,cy+9*unit);}
 else if(id==MORE_BUTTON){for(int n=-1;n<=1;n++)g.FillEllipse(&ink,cx+n*7*unit-2*unit,cy-2*unit,4*unit,4*unit);}
 else if(id==VOLUME_BUTTON){PointF pts[4]={{cx-10*unit,cy-4*unit},{cx-5*unit,cy-4*unit},{cx+2*unit,cy-10*unit},{cx+2*unit,cy+10*unit}};g.DrawPolygon(&pen,pts,4);g.DrawArc(&pen,cx-2*unit,cy-8*unit,15*unit,16*unit,-60,120);}
 if((GetFocus()==p->hwnd&&p->focused==index)||(p->buttons[index]&&GetFocus()==p->buttons[index])){Pen focus(Color(190,255,255,255),1);g.DrawEllipse(&focus,2.0f,2.0f,w-4,h-4);}
 g.Restore(state);
}

void presentLayered(HWND hwnd,Gdiplus::Bitmap& canvas,int w,int h,int scale){
 using namespace Gdiplus;
 RECT windowRect{};GetWindowRect(hwnd,&windowRect);
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 HDC screen=GetDC(nullptr),memory=screen?CreateCompatibleDC(screen):nullptr;void* bits=nullptr;HBITMAP dib=screen?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
 if(!screen||!memory||!dib||!bits){if(dib)DeleteObject(dib);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);return;}
 HGDIOBJ old=SelectObject(memory,dib);
 {Bitmap output(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));Graphics g(&output);g.SetCompositingMode(CompositingModeSourceCopy);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);g.DrawImage(&canvas,Rect(0,0,w,h),0,0,w*scale,h*scale,UnitPixel);}
 POINT origin{windowRect.left,windowRect.top},source{0,0};SIZE size{w,h};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
 UpdateLayeredWindow(hwnd,screen,&origin,&size,memory,&source,0,&blend,ULW_ALPHA);
 SelectObject(memory,old);DeleteObject(dib);DeleteDC(memory);ReleaseDC(nullptr,screen);
}
// A soft glow contains no high-frequency detail. Render it once at low
// resolution when the palette changes; scaling this layer is much cheaper than
// recomputing full-window path gradients for every hover and animation frame.
Gdiplus::Bitmap* cachedGlow(Panel* p){
 using namespace Gdiplus;
 COLORREF color=p->skin==2?p->accent:p->tint;
 if(p->glowCache&&p->glowSkin==p->skin&&p->glowColor==color)return p->glowCache.get();
 constexpr int gw=160,gh=100;
 auto glow=std::make_unique<Bitmap>(gw,gh,PixelFormat32bppPARGB);
 if(glow->GetLastStatus()!=Ok)return nullptr;
 {Graphics g(glow.get());g.Clear(Color(0,0,0,0));g.SetSmoothingMode(SmoothingModeAntiAlias);
  GraphicsPath shape;shape.AddEllipse(-gw*.35f,-gh*.7f,gw*1.15f,gh*2.1f);
  PathGradientBrush light(&shape);light.SetCenterColor(Color(p->skin==2?135:100,GetRValue(color),GetGValue(color),GetBValue(color)));Color edge(0,0,0,0);INT count=1;light.SetSurroundColors(&edge,&count);g.FillPath(&light,&shape);
  if(p->skin==2){GraphicsPath warm;warm.AddEllipse(gw*.18f,-gh*.65f,gw*.95f,gh*1.55f);PathGradientBrush halo(&warm);halo.SetCenterColor(Color(85,210,146,8));count=1;halo.SetSurroundColors(&edge,&count);g.FillPath(&halo,&warm);}
 }
 p->glowSkin=p->skin;p->glowColor=color;p->glowCache=std::move(glow);return p->glowCache.get();
}
Gdiplus::Bitmap* cachedBackground(Panel* p,int w,int h){
 using namespace Gdiplus;
 if(p->backgroundCache&&p->backgroundWidth==w&&p->backgroundHeight==h&&p->backgroundSkin==p->skin&&p->backgroundTint==p->tint&&p->backgroundAccent==p->accent)return p->backgroundCache.get();
 constexpr int scale=2;auto canvas=std::make_unique<Bitmap>(w*scale,h*scale,PixelFormat32bppPARGB);if(canvas->GetLastStatus()!=Ok)return nullptr;
 {Graphics g(canvas.get());g.Clear(Color(0,0,0,0));g.ScaleTransform(float(scale),float(scale));g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
  auto vivid=[](BYTE value){return static_cast<BYTE>(std::min(120,int(value*1.35)));};
  Color a=p->skin==2?Color(255,12,12,15):p->skin==1?Color(255,vivid(GetRValue(p->tint)),vivid(GetGValue(p->tint)),vivid(GetBValue(p->tint))):Color(255,GetRValue(p->tint),GetGValue(p->tint),GetBValue(p->tint));
  LinearGradientBrush gradient(Rect(0,0,w,h),a,p->skin==2?Color(255,8,8,10):p->skin==1?Color(255,24,24,25):Color(255,21,22,24),LinearGradientModeForwardDiagonal);
  roundFill(g,gradient,RectF(0,0,float(w),float(h)),12);
  if(p->skin==1||p->skin==2){
   auto saved=g.Save();GraphicsPath clipPath;float d=24.0f;
   clipPath.AddArc(0.0f,0.0f,d,d,180.0f,90.0f);clipPath.AddArc(float(w)-d,0.0f,d,d,270.0f,90.0f);clipPath.AddArc(float(w)-d,float(h)-d,d,d,0.0f,90.0f);clipPath.AddArc(0.0f,float(h)-d,d,d,90.0f,90.0f);clipPath.CloseFigure();g.SetClip(&clipPath);
   if(auto glow=cachedGlow(p)){g.SetInterpolationMode(InterpolationModeBilinear);g.DrawImage(glow,Rect(0,0,w,h),0,0,160,100,UnitPixel);}g.Restore(saved);
  }
 }
 p->backgroundWidth=w;p->backgroundHeight=h;p->backgroundSkin=p->skin;p->backgroundTint=p->tint;p->backgroundAccent=p->accent;p->backgroundCache=std::move(canvas);return p->backgroundCache.get();
}
void renderWidget(Panel* p){
 using namespace Gdiplus;
 if(p->rendering)return;p->rendering=true;
 struct RenderGuard{bool& flag;~RenderGuard(){flag=false;}} guard{p->rendering};
 RECT bounds{};GetClientRect(p->hwnd,&bounds);
 int w=width(bounds),h=height(bounds);if(w<=0||h<=0||w>4096||h>4096)return;
 Snapshot s;{std::lock_guard lock(p->mutex);s=p->snapshot;}Layout l=layout(w,h,p->skin);
 constexpr int scale=2;
 Bitmap canvas(w*scale,h*scale,PixelFormat32bppPARGB);if(canvas.GetLastStatus()!=Ok)return;
 {
  Graphics g(&canvas);g.Clear(Color(0,0,0,0));if(auto background=cachedBackground(p,w,h))g.DrawImage(background,0,0);g.ScaleTransform(float(scale),float(scale));
  g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  if(!drawRoundedArtwork(g,p->bitmap,RectF(float(l.cover.left),float(l.cover.top),float(width(l.cover)),float(height(l.cover))),8)){
   SolidBrush empty(Color(120,255,255,255));roundFill(g,empty,RectF(float(l.cover.left),float(l.cover.top),float(width(l.cover)),float(height(l.cover))),8);label(g,L"♪",l.cover,35,Color(210,255,255,255),false,true);
  }
  std::wstring title=!s.connected?L"Нет подключения":s.hasMedia?(s.title.empty()?L"Музыка Станции":s.title):L"Музыка не играет";
  bool artistHeading=p->skin==2&&s.hasMedia;
  const std::wstring& heading=artistHeading&&!s.artist.empty()?s.artist:title;
  label(g,heading,l.title,float(std::clamp(height(l.title)-4,14,artistHeading?48:22)),Color(255,255,255,255),true,!l.wide);
  if(!artistHeading)label(g,s.artist,l.artist,float(std::clamp(height(l.artist)-4,12,17)),Color(195,255,255,255),false,!l.wide);
  long long position=p->seeking?p->previewMs:s.position;double ratio=position>=0&&s.duration>0?std::clamp(double(position)/s.duration,0.0,1.0):0.0;
  SolidBrush track(Color(90,255,255,255)),fill(Color(255,255,232,0));float sy=float(l.seek.top+height(l.seek)/2);
  bool pill=p->skin==2&&height(l.seek)>12;
  if(s.hasMedia&&(s.duration>0||pill)){
   float thickness=pill?float(height(l.seek)):p->skin==1?8.0f:4.0f;
   if(pill){track.SetColor(Color(68,255,255,255));fill.SetColor(Color(70,255,255,255));}
   float left=float(l.seek.left),top=sy-thickness/2,barWidth=float(width(l.seek));
   RectF bar(left,top,barWidth,thickness);
   if(pill)drawPillProgress(g,bar,track,fill,ratio);
   else{roundFill(g,track,bar,thickness/2);if(ratio>0)roundFill(g,fill,RectF(left,top,float(barWidth*ratio),thickness),thickness/2);}
  }
  if(p->seeking){SolidBrush knob(Color(255,255,255,255));g.FillEllipse(&knob,float(l.seek.left+width(l.seek)*ratio)-5,sy-5,10.0f,10.0f);}
  if((!s.connected||!s.notice.empty())&&!(pill&&s.connected&&(p->hoverSeek||p->seeking))){std::wstring msg=!s.connected?L"Нет подключения к Станции":s.notice;label(g,msg,l.status,11,Color(200,255,255,255),false,true);}
  else if(s.hasMedia&&(height(l.time)>0||pill)){std::wstring text=pill&&!p->hoverSeek&&!p->seeking?title:clockText(position)+L" / "+clockText(s.duration);label(g,text,pill?l.seek:l.time,pill?14.0f:11.0f,Color(pill?240:175,255,255,255),pill,true);}
  for(size_t i=0;i<l.button.size();i++)drawWidgetButton(g,p,s,static_cast<int>(i),l.button[i]);
 }
 presentLayered(p->hwnd,canvas,w,h,scale);
}
