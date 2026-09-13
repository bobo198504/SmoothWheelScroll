// rowtrack -- track the vertical position of note rows in the MIDI editor by finding,
// for each sample, the row whose luminance-weighted centroid is highest, and printing
// its y. A forward-then-backward sequence is the reported rebound.
//
// More robust than whole-frame correlation when the note area is mostly empty: it
// keys on actual note pixels instead of the (mostly black) background.
//
// Read-only (BitBlt only).
//
// Usage: rowtrack.exe L T R B wheelX wheelY [down|up] [samples] [ms]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_w, g_h;
static unsigned char *grab(int L,int T,int R,int B){
  g_w=R-L; g_h=B-T;
  HDC sc=GetDC(nullptr); HDC mem=CreateCompatibleDC(sc);
  HBITMAP bm=CreateCompatibleBitmap(sc,g_w,g_h); HGDIOBJ o=SelectObject(mem,bm);
  BitBlt(mem,0,0,g_w,g_h,sc,L,T,SRCCOPY);
  BITMAPINFO bi={0};
  bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=g_w;
  bi.bmiHeader.biHeight=-g_h; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
  bi.bmiHeader.biCompression=BI_RGB;
  unsigned char*pb=(unsigned char*)malloc(g_w*g_h*4);
  GetDIBits(mem,bm,0,g_h,pb,&bi,DIB_RGB_COLORS);
  SelectObject(mem,o); DeleteObject(bm); DeleteDC(mem); ReleaseDC(nullptr,sc);
  return pb;
}
// Note rows are BRIGHT on a dark background. Return the luminance-weighted centroid
// of the rows that exceed a threshold, plus how many such rows there are.
static double noteCentroid(const unsigned char*px,int*countOut){
  double *lum=(double*)malloc(sizeof(double)*g_h);
  double mx=0;
  for(int y=0;y<g_h;y++){ long long s=0;
    for(int x=0;x<g_w;x++){ const unsigned char*p=px+((size_t)y*g_w+x)*4;
      s+=(p[0]*299+p[1]*587+p[2]*114)/1000; }
    lum[y]=(double)s/g_w; if(lum[y]>mx)mx=lum[y]; }
  const double thr = mx*0.35 > 24 ? mx*0.35 : 24;
  double sum=0, wsum=0; int n=0;
  for(int y=0;y<g_h;y++) if(lum[y]>thr){ sum+=lum[y]; wsum+=lum[y]*y; n++; }
  free(lum);
  *countOut=n;
  return n? wsum/sum : -1;
}
int main(int argc,char**argv){
  int L=atoi(argv[1]),T=atoi(argv[2]),R=atoi(argv[3]),B=atoi(argv[4]);
  int wx=atoi(argv[5]),wy=atoi(argv[6]);
  const int down=(argc>7&&strcmp(argv[7],"down")==0);
  const char *mod = argc>10?argv[10]:"";
  const int samples=argc>8?atoi(argv[8]):50;
  const int ms=argc>9?atoi(argv[9]):15;
  HWND m=FindWindowA("REAPERwnd",nullptr); if(!m){printf("no reaper\n");return 2;}
  SetForegroundWindow(m); Sleep(300);
  unsigned char*px=grab(L,T,R,B); int n=0;
  double c0=noteCentroid(px,&n); free(px);
  printf("region %d,%d,%d,%d  wheel %d,%d %s\n",L,T,R,B,wx,wy,down?"down":"up");
  printf("baseline note centroid y=%.1f (rows above threshold: %d)\n",c0,n);
  SetCursorPos(wx,wy); Sleep(150);
  // Optional modifiers, so the vertical-scroll gesture in the MIDI editor (which is
  // Ctrl+Alt+wheel over the notes) can be reproduced.
  WORD k1=0,k2=0;
  if(strchr(mod,'c')) k1=VK_CONTROL;
  if(strchr(mod,'a')) k2=VK_MENU;
  if(strchr(mod,'s')) k2=VK_SHIFT;
  INPUT kin={0}; kin.type=INPUT_KEYBOARD;
  if(k1){kin.ki.wVk=k1;SendInput(1,&kin,sizeof(kin));}
  if(k2){kin.ki.wVk=k2;SendInput(1,&kin,sizeof(kin));}
  Sleep(50);
  INPUT in={0}; in.type=INPUT_MOUSE; in.mi.dwFlags=MOUSEEVENTF_WHEEL;
  in.mi.mouseData = down?(DWORD)(-WHEEL_DELTA):(DWORD)WHEEL_DELTA;
  SendInput(1,&in,sizeof(in));
  Sleep(50);
  if(k2){kin.ki.wVk=k2;kin.ki.dwFlags=KEYEVENTF_KEYUP;SendInput(1,&kin,sizeof(kin));}
  if(k1){kin.ki.wVk=k1;kin.ki.dwFlags=KEYEVENTF_KEYUP;SendInput(1,&kin,sizeof(kin));}
  printf("  %6s %10s %10s\n","t(ms)","centroid","delta");
  double prev=c0; int reversals=0; double dirPrev=0;
  for(int i=0;i<samples;i++){
    Sleep(ms);
    px=grab(L,T,R,B); int k=0; double c=noteCentroid(px,&k); free(px);
    const double d=c-prev;
    const double dir = d>0.5?1:(d<-0.5?-1:0);
    if(dir!=0 && dirPrev!=0 && dir!=dirPrev) ++reversals;
    if(dir!=0) dirPrev=dir;
    printf("  %6d %10.1f %10.1f%s\n",(i+1)*ms,c,d,dir?"":"  (steady)");
    prev=c;
  }
  printf("direction reversals: %d\n",reversals);
  return 0;
}
