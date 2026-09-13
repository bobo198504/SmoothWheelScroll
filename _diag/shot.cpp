// shot -- write one screen region to a raw RGB24 file (w,h header then pixels).
// Used to compare before/after states. Read-only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  if(argc<6){printf("usage: shot.exe L T R B out.raw\n");return 2;}
  int L=atoi(argv[1]),T=atoi(argv[2]),R=atoi(argv[3]),B=atoi(argv[4]);
  int w=R-L,h=B-T;
  HDC sc=GetDC(nullptr); HDC mem=CreateCompatibleDC(sc);
  HBITMAP bm=CreateCompatibleBitmap(sc,w,h); HGDIOBJ o=SelectObject(mem,bm);
  BitBlt(mem,0,0,w,h,sc,L,T,SRCCOPY);
  BITMAPINFO bi={0}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h; bi.bmiHeader.biPlanes=1;
  bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
  unsigned char*pb=(unsigned char*)malloc((size_t)w*h*4);
  GetDIBits(mem,bm,0,h,pb,&bi,DIB_RGB_COLORS);
  FILE*f=fopen(argv[5],"wb"); if(!f)return 3;
  int hdr[2]={w,h}; fwrite(hdr,sizeof(int),2,f);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){const unsigned char*p=pb+((size_t)y*w+x)*4;
    fputc(p[2],f);fputc(p[1],f);fputc(p[0],f);}
  fclose(f);
  printf("wrote %s (%dx%d)\n",argv[5],w,h);
  return 0;
}
