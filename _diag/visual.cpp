// visual -- objective proof that wheeling a point MOVES something: hash the pixels of
// a screen rectangle before and after a real wheel notch. Read-only (BitBlt).
// Usage: visual.exe <watchL> <watchT> <watchR> <watchB> <wheelX> <wheelY> [up|down]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD hashRect(int L,int T,int R,int B){
  HDC screen=GetDC(nullptr); HDC mem=CreateCompatibleDC(screen);
  int w=R-L,h=B-T;
  HBITMAP bmp=CreateCompatibleBitmap(screen,w,h);
  HGDIOBJ old=SelectObject(mem,bmp);
  BitBlt(mem,0,0,w,h,screen,L,T,SRCCOPY);
  BITMAPINFO bi={0}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h; bi.bmiHeader.biPlanes=1;
  bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
  int stride=w*4; unsigned char *buf=(unsigned char*)malloc(stride*h);
  GetDIBits(mem,bmp,0,h,buf,&bi,DIB_RGB_COLORS);
  unsigned long hh=2166136261u;
  for(int i=0;i<stride*h;i++){ hh^=buf[i]; hh*=16777619u; }
  free(buf); SelectObject(mem,old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr,screen);
  return hh;
}

int main(int argc,char**argv){
  if(argc<7){printf("usage: visual.exe L T R B wheelX wheelY [up|down]\n");return 2;}
  int L=atoi(argv[1]),T=atoi(argv[2]),R=atoi(argv[3]),B=atoi(argv[4]);
  int wx=atoi(argv[5]),wy=atoi(argv[6]);
  int down = (argc>7 && strcmp(argv[7],"down")==0);
  HWND main=FindWindowA("REAPERwnd",nullptr);
  if(!main){printf("no reaper\n");return 2;}
  SetForegroundWindow(main); Sleep(300);
  Sleep(300);
  unsigned long before=hashRect(L,T,R,B);
  SetCursorPos(wx,wy); Sleep(150);
  INPUT in={0}; in.type=INPUT_MOUSE; in.mi.dwFlags=MOUSEEVENTF_WHEEL;
  in.mi.mouseData = down? (DWORD)(-WHEEL_DELTA) : (DWORD)WHEEL_DELTA;
  SendInput(1,&in,sizeof(in));
  Sleep(900);
  unsigned long after=hashRect(L,T,R,B);
  printf("wheel at (%d,%d) dir=%s -> watched area %d,%d,%d,%d : %s\n",
         wx,wy,down?"down":"up",L,T,R,B, before!=after?"MOVED (hash changed)":"no change");
  return 0;
}
