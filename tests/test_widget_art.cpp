#include "../windows/native/widget_art.h"
#include "../windows/native/widget_pill.h"
#include <cassert>
#include <cstdio>
int main(){
 using namespace Gdiplus;
 GdiplusStartupInput startup;ULONG_PTR token{};assert(GdiplusStartup(&token,&startup,nullptr)==Ok);
 {
  Bitmap source(8,8,PixelFormat32bppARGB);{Graphics g(&source);g.Clear(Color(255,240,20,30));}
  HBITMAP bitmap{};assert(source.GetHBITMAP(Color(255,0,0,0),&bitmap)==Ok);
  Bitmap canvas(100,100,PixelFormat32bppARGB);
  {Graphics g(&canvas);g.Clear(Color(0,0,0,0));g.SetSmoothingMode(SmoothingModeAntiAlias);assert(drawRoundedArtwork(g,bitmap,RectF(10,10,80,80),12));}
  Color center,outside,corner;assert(canvas.GetPixel(50,50,&center)==Ok);assert(canvas.GetPixel(2,2,&outside)==Ok);assert(canvas.GetPixel(10,10,&corner)==Ok);
  assert(center.GetR()>200&&center.GetG()<80&&center.GetB()<80&&center.GetA()>240);
  assert(outside.GetA()==0&&corner.GetA()<200);
  DeleteObject(bitmap);
 }
 {
  Bitmap canvas(240,60,PixelFormat32bppARGB);Graphics g(&canvas);g.Clear(Color(0,0,0,0));g.SetSmoothingMode(SmoothingModeAntiAlias);
  SolidBrush track(Color(255,30,30,30)),progress(Color(255,240,20,20));
  drawPillProgress(g,RectF(10,10,200,32),track,progress,0.06);
  Color corner,inside,unfilled;assert(canvas.GetPixel(15,11,&corner)==Ok);assert(canvas.GetPixel(15,26,&inside)==Ok);assert(canvas.GetPixel(40,26,&unfilled)==Ok);
  assert(corner.GetA()<100&&inside.GetR()>180&&unfilled.GetR()<60);
 }
 GdiplusShutdown(token);puts("Artwork and capsule clipping checks passed.");
}
