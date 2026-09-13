// click -- send a double-click at a screen point (to open a MIDI item in the editor,
// exactly as a user would). No REAPER state is touched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  if(argc<3){printf("usage: click.exe x y [dbl]\n");return 2;}
  int x=atoi(argv[1]), y=atoi(argv[2]);
  const bool dbl=(argc>3 && argv[3][0]=='d');
  HWND m=FindWindowA("REAPERwnd",nullptr); if(!m) return 2;
  SetForegroundWindow(m); Sleep(250);
  SetCursorPos(x,y); Sleep(120);
  INPUT in={0};
  for(int pass=0; pass<(dbl?2:1); ++pass){
    in.type=INPUT_MOUSE; in.mi.dwFlags=MOUSEEVENTF_LEFTDOWN; SendInput(1,&in,sizeof(in)); Sleep(30);
    in.mi.dwFlags=MOUSEEVENTF_LEFTUP; SendInput(1,&in,sizeof(in));
    if(dbl) Sleep(60);
  }
  printf("clicked %d,%d%s\n",x,y,dbl?" (double)":"");
  return 0;
}
