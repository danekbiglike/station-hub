#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>

// Fill an anti-aliased rounded path with the artwork itself. A graphics clip
// is binary on some GDI+ drivers and leaves visibly jagged image corners.
inline bool drawRoundedArtwork(Gdiplus::Graphics& g,HBITMAP source,Gdiplus::RectF r,float radius){
 using namespace Gdiplus;
 if(!source||r.Width<=0||r.Height<=0)return false;
 Bitmap art(source,nullptr);if(art.GetLastStatus()!=Ok||art.GetWidth()==0||art.GetHeight()==0)return false;
 TextureBrush texture(&art,WrapModeClamp);if(texture.GetLastStatus()!=Ok)return false;
 Matrix transform(r.Width/art.GetWidth(),0,0,r.Height/art.GetHeight(),r.X,r.Y);
 if(texture.SetTransform(&transform)!=Ok)return false;
 radius=std::min(radius,std::min(r.Width,r.Height)/2);float d=radius*2;
 GraphicsPath path;path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
 path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();
 return g.FillPath(&texture,&path)==Ok;
}
