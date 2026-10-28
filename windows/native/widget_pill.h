#pragma once
#include <gdiplus.h>
#include <algorithm>

// Draw a single capsule, with a clipped progress slice and a straight frontier.
// Keeping both shapes under the same clip prevents a narrow early-progress
// segment from protruding outside the capsule's rounded end.
inline void drawPillProgress(Gdiplus::Graphics& g,const Gdiplus::RectF& r,
                             Gdiplus::Brush& track,Gdiplus::Brush& progress,double ratio){
 using namespace Gdiplus;
 if(r.Width<=0||r.Height<=0)return;
 float d=std::min(r.Height,r.Width);
 GraphicsPath shape;
 shape.AddArc(r.X,r.Y,d,d,180.0f,90.0f);shape.AddArc(r.GetRight()-d,r.Y,d,d,270.0f,90.0f);
 shape.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0.0f,90.0f);shape.AddArc(r.X,r.GetBottom()-d,d,d,90.0f,90.0f);shape.CloseFigure();
 g.FillPath(&track,&shape);
 ratio=std::clamp(ratio,0.0,1.0);if(ratio<=0)return;
 auto saved=g.Save();g.SetClip(&shape);g.FillRectangle(&progress,r.X,r.Y,float(r.Width*ratio),r.Height);g.Restore(saved);
}
