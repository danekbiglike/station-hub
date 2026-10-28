#include "../windows/native/widget_layout.h"
#include <cstdio>
#include <vector>

static bool overlaps(const RECT& a,const RECT& b){
 return width(a)>0&&height(a)>0&&width(b)>0&&height(b)>0&&
        a.left<b.right&&b.left<a.right&&a.top<b.bottom&&b.top<a.bottom;
}
static bool validTransport(const Layout& l,int skin){
 bool capsuleRow=skin==2&&height(l.seek)>12;
 bool orderOk=capsuleRow?
  l.button[B_MENU].right<=l.button[B_DISLIKE].left&&l.button[B_DISLIKE].right<=l.seek.left&&l.seek.right<=l.button[B_LIKE].left&&l.button[B_LIKE].right<=l.button[B_VOLUME].left:
  l.button[B_MENU].right<=l.button[B_DISLIKE].left&&l.button[B_DISLIKE].right<=l.button[B_PREV].left&&l.button[B_NEXT].right<=l.button[B_LIKE].left&&l.button[B_LIKE].right<=l.button[B_VOLUME].left;
 return l.seek.bottom<=l.button[B_PLAY].top&&orderOk&&l.button[B_PREV].right<=l.button[B_PLAY].left&&l.button[B_PLAY].right<=l.button[B_NEXT].left&&std::abs(int(l.button[B_MENU].left+l.button[B_VOLUME].right)-int(l.button[B_PLAY].left+l.button[B_PLAY].right))<=2;
}
int main(){
 // The artwork and text begin on the same line in the wide player.
 for(auto [w,h]:std::vector<std::pair<int,int>>{{487,202},{600,200},{900,300}}){
  Layout l=layout(w,h);
  if(!l.wide||l.title.top!=l.cover.top||std::abs(l.cover.top-(h-l.cover.bottom))>1){
   std::printf("Uneven wide layout at %dx%d\n",w,h);return 1;
  }
 }
 for(auto [w,h]:std::vector<std::pair<int,int>>{{890,430},{760,270},{1600,400}}){
  Layout l=layout(w,h);
  if(!validTransport(l,0)){
   std::printf("Incorrect transport order at %dx%d\n",w,h);return 1;
  }
  if(!l.wide||width(l.cover)>340||l.seek.right-l.cover.left>820||std::abs(l.cover.left-(w-l.seek.right))>1){
   std::printf("Unbalanced wide layout at %dx%d\n",w,h);return 1;
  }
 }
 {Layout l=layout(875,518);if(l.wide||width(l.seek)>520||std::abs(l.cover.left-(875-l.cover.right))>1){puts("Unbalanced medium layout");return 1;}}
 for(int skin=0;skin<3;skin++)for(int w=330;w<=1600;w++)for(int h=110;h<=1000;h++){
  Layout l=layout(w,h,skin);
  if(!validTransport(l,skin)){
   std::printf("Incorrect transport order at %dx%d, skin %d\n",w,h,skin);return 1;
  }
  if(l.button[B_DISLIKE].left-l.button[B_MENU].right!=l.button[B_VOLUME].left-l.button[B_LIKE].right){
   std::printf("Unequal outer button gaps at %dx%d, skin %d\n",w,h,skin);return 1;
  }
  std::vector<RECT> boxes{l.cover,l.title,l.artist,l.time,l.seek};
  for(auto b:l.button)boxes.push_back(b);
  for(size_t i=0;i<boxes.size();i++){
   const RECT& r=boxes[i];
   if(width(r)>0&&height(r)>0&&(r.left<0||r.top<0||r.right>w||r.bottom>h)){
    std::printf("Out of bounds at %dx%d, skin %d, item %zu\n",w,h,skin,i);return 1;
   }
   for(size_t j=i+1;j<boxes.size();j++)if(overlaps(r,boxes[j])){
    std::printf("Overlap at %dx%d, skin %d, items %zu and %zu\n",w,h,skin,i,j);return 1;
   }
  }
 }
 puts("Layout bounds and overlap checks passed.");
}
