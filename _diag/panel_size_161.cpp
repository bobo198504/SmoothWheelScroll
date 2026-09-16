// 1.6.1's opening size on THIS machine, computed from its own metrics (read out of versions/1.6.1).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
static HFONT UiFont(int *px){
  NONCLIENTMETRICSA ncm={0}; ncm.cbSize=sizeof(ncm); HFONT f=nullptr;
  if(SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,sizeof(ncm),&ncm,0)) f=CreateFontIndirectA(&ncm.lfMessageFont);
  if(!f) f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
  if(px){ HDC dc=GetDC(nullptr); HGDIOBJ o=SelectObject(dc,f); TEXTMETRICA tm={0}; GetTextMetricsA(dc,&tm);
          SelectObject(dc,o); ReleaseDC(nullptr,dc); *px=tm.tmHeight; }
  return f;
}
static int TextW(const char*t){ if(!t||!*t)return 0; HDC dc=GetDC(nullptr); HGDIOBJ o=SelectObject(dc,UiFont(nullptr));
  SIZE s={0}; GetTextExtentPoint32A(dc,t,(int)strlen(t),&s); SelectObject(dc,o); ReleaseDC(nullptr,dc); return s.cx; }
int main(){
  int fontH=16; UiFont(&fontH);
  const int pad=16, headH=fontH+12, groupH=fontH+6, barH=fontH+10, cardPad=9, gapY=0;
  const int rowH=(groupH+barH+cardPad*2)+gapY;
  const int curveH=130, curveGap=14, nsliders=5;
  const int height = pad + headH + rowH*nsliders + curveGap + curveH + pad;
  const int boxW=GetSystemMetrics(SM_CXMENUCHECK), gap=6;
  const char* cap="Enable smooth scrolling   (off = REAPER's native wheel)";
  const int width = pad*2 + boxW + gap + TextW(cap);
  printf("fontH=%d boxW=%d\n", fontH, boxW);
  printf("1.6.1 caption width = %d px\n", TextW(cap));
  printf("1.6.1 CLIENT size   = %d x %d\n", width, height);
  RECT wr={0,0,width,height};
  AdjustWindowRectEx(&wr, WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME, FALSE, WS_EX_TOOLWINDOW);
  printf("1.6.1 WINDOW size   = %ld x %ld\n", wr.right-wr.left, wr.bottom-wr.top);
  return 0;
}
