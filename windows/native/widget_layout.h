#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>

enum ButtonIndex { B_PREV, B_PLAY, B_NEXT, B_LIKE, B_DISLIKE, B_VOLUME, B_MENU, B_PIN, B_CLOSE };
struct Layout {
 RECT cover{}, title{}, artist{}, time{}, seek{}, status{};
 std::array<RECT,9> button{};
 bool wide{};
};
inline RECT rect(int x,int y,int w,int h){return RECT{x,y,x+std::max(0,w),y+std::max(0,h)};}
inline int width(const RECT& r){return r.right-r.left;}
inline int height(const RECT& r){return r.bottom-r.top;}

// Each row has a reserved vertical band. The compact layout hides the clock
// rather than drawing it underneath the controls when the window is short.
inline Layout layout(int w,int h,int skin=0){
 Layout l; l.wide=w>=h*2||h<220;
 int topButton=std::clamp(h/7,24,32), gap=std::clamp(h/35,3,8);
 constexpr int order[]={B_MENU,B_DISLIKE,B_PREV,B_PLAY,B_NEXT,B_LIKE,B_VOLUME};
 l.button[B_CLOSE]=rect(w-topButton-9,8,topButton,topButton);
 l.button[B_PIN]=rect(w-2*topButton-gap-9,8,topButton,topButton);
 if(l.wide){
  // Keep the entire player together in a large window rather than spreading
  // the artwork, controls and timeline across unrelated empty space.
  int groupWidth=std::min(w-24,820),groupX=(w-groupWidth)/2;
  int side=std::clamp(h-24,56,std::min(340,std::max(56,groupWidth*42/100)));
  int coverX=groupX, coverY=(h-side)/2;
  l.cover=rect(coverX,coverY,side,side);
  int x=coverX+side+12, right=groupX+groupWidth, avail=std::max(0,right-x);
  int textRight=coverY<l.button[B_PIN].bottom?std::min(right,int(l.button[B_PIN].left)-8):right;
  textRight=std::max(x+45,textRight);
  int textY=coverY;
  l.title=rect(x,textY,textRight-x,std::clamp(side/7,19,30));
  l.artist=rect(x,l.title.bottom+2,textRight-x,std::clamp(side/9,16,23));
  int bs=std::clamp(side/4,24,40),play=std::clamp(side/3,30,50),space=std::clamp(w/90,3,9);
  int total=6*bs+play+6*space;
  if(total>avail){bs=std::max(20,(avail-6*space)*4/29);play=std::max(25,bs*5/4);total=6*bs+play+6*space;}
  int start=x+std::max(0,(avail-total)/2),cy=l.cover.bottom-play/2;
  for(int i:order){int size=i==B_PLAY?play:bs;l.button[i]=rect(start,cy-size/2,size,size);start+=size+space;}
  bool pill=skin==2&&side>=110;int seekHeight=pill?(side>=150?32:24):12;
  l.seek=rect(x,l.cover.bottom-play-seekHeight-3,avail,seekHeight);
  if(side>=150&&!pill)l.time=rect(x,l.seek.top-15,avail,14);
  l.status=pill?l.seek:l.time;
 }else{
  int bs=std::clamp(w/10,28,44),play=std::clamp(w/8,36,52),space=std::clamp(w/45,3,8);
  int controlsTop=h-10-play,seekY=controlsTop-18;
  int timeY=seekY-(h>=260?18:0),artistY=timeY-5-18;
  int titleH=h>=250?26:20,titleY=artistY-2-titleH;
  int coverStart=topButton+12,available=std::max(1,titleY-coverStart-8);
  int side=std::min({w-50,available,270});
  int extra=available-side,shift=extra/2;
  int coverY=titleY-8-side-shift;
  l.cover=rect((w-side)/2,coverY,side,side);
  int content=std::min(w-36,520),left=(w-content)/2;
  l.title=rect(left,titleY-shift,content,titleH);l.artist=rect(left,artistY-shift,content,18);
  bool pill=skin==2&&h>=260;int pillHeight=pill&&h>=300?32:24;
  if(h>=260&&!pill)l.time=rect(left,timeY-shift,content,14);
  l.seek=rect(left+9,seekY-shift-(pill?pillHeight-12:0),content-18,pill?pillHeight:12);
  l.status=pill?l.seek:l.time;
  int total=6*bs+play+6*space;
  if(total>w-24){bs=std::max(20,(w-24-6*space)*4/29);play=std::max(25,bs*5/4);total=6*bs+play+6*space;}
  int start=(w-total)/2,cy=controlsTop-shift+play/2;
  for(int i:order){int size=i==B_PLAY?play:bs;l.button[i]=rect(start,cy-size/2,size,size);start+=size+space;}
  // Centre the full stack as one group, while reserving the top corner for
  // the window buttons at the smallest sizes.
  int offset=std::max(topButton+8-int(l.cover.top),(h-int(l.button[B_PLAY].bottom)-int(l.cover.top))/2);
  auto moveY=[offset](RECT& r){if(width(r)>0&&height(r)>0){r.top+=offset;r.bottom+=offset;}};
  moveY(l.cover);moveY(l.title);moveY(l.artist);moveY(l.time);moveY(l.status);moveY(l.seek);
  for(int i:order)moveY(l.button[i]);
  if(skin==2&&h>=420){
   int headingHeight=std::clamp(h/9,36,64),seekHeight=32;
   int coverSide=std::min({230,w-80,h-headingHeight-12-16-seekHeight-8-play-16});
   int groupHeight=headingHeight+12+coverSide+16+seekHeight+8+play;
   int y=std::max(8,(h-groupHeight)/2);
   int artistWidth=std::max(0,w-2*(topButton*2+gap+16));
   l.title=rect((w-artistWidth)/2,y,artistWidth,headingHeight);l.artist={};
   y+=headingHeight+12;l.cover=rect((w-coverSide)/2,y,coverSide,coverSide);y+=coverSide+16;
   l.seek=rect(left+9,y,content-18,seekHeight);l.status=l.seek;l.time={};y+=seekHeight+8;
   for(int i:order){int size=height(l.button[i]);l.button[i].top=y+(play-size)/2;l.button[i].bottom=l.button[i].top+size;}
  }else if(skin==2&&h>=380&&l.cover.top>=40){
   // The vertical music-player view puts the artist above the cover and the
   // track title inside the seek capsule, leaving room for the window buttons.
   int artistWidth=std::max(0,w-2*(topButton*2+gap+16));
   int artistTop=std::max(8,int(l.cover.top)-36);
   l.title=rect((w-artistWidth)/2,artistTop,artistWidth,int(l.cover.top)-artistTop-4);
   l.artist={};
  }
 }
 if(skin==2&&height(l.seek)>12){
  // Keep the user's left-to-right order while matching the music player:
  // two side actions flank the capsule and the three transport buttons sit
  // beneath it. At very small sizes the original single row remains usable.
  RECT full=l.seek;int rowGap=std::clamp(w/180,2,6);
  int size=std::min(height(full),std::max(14,(width(full)-68-4*rowGap)/4));
  int capsuleLeft=full.left+2*(size+rowGap),capsuleRight=full.right-2*(size+rowGap);
  if(capsuleRight>capsuleLeft){
   int top=full.top+(height(full)-size)/2;
   l.button[B_MENU]=rect(full.left,top,size,size);
   l.button[B_DISLIKE]=rect(full.left+size+rowGap,top,size,size);
   l.button[B_LIKE]=rect(capsuleRight+rowGap,top,size,size);
   l.button[B_VOLUME]=rect(capsuleRight+2*rowGap+size,top,size,size);
   l.seek=rect(capsuleLeft,full.top,capsuleRight-capsuleLeft,height(full));l.status=l.seek;
   int centre=(full.left+full.right)/2,play=width(l.button[B_PLAY]);
   int transport=std::min(size,height(l.button[B_PREV])),cy=(l.button[B_PLAY].top+l.button[B_PLAY].bottom)/2;
   l.button[B_PLAY].left=centre-play/2;l.button[B_PLAY].right=l.button[B_PLAY].left+play;
   l.button[B_PREV]=rect(l.button[B_PLAY].left-rowGap-transport,cy-transport/2,transport,transport);
   l.button[B_NEXT]=rect(l.button[B_PLAY].right+rowGap,cy-transport/2,transport,transport);
  }
 }
 return l;
}
