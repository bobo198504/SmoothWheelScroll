// onewheel -- set a control to a known state, send ONE wheel notch, report.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
static DWORD g_pid=0; static HWND g_dlg=nullptr; static char g_want[64];
static BOOL CALLBACK et(HWND h,LPARAM){
  DWORD p=0; GetWindowThreadProcessId(h,&p); if(p!=g_pid) return TRUE;
  char c[64]={0},t[160]={0}; GetClassNameA(h,c,sizeof(c)); GetWindowTextA(h,t,sizeof(t));
  if(strcmp(c,"#32770")==0){ char lt[160],lw[64]; size_t i;
    for(i=0;t[i]&&i<159;i++) lt[i]=(char)tolower((unsigned char)t[i]); lt[i]=0;
    for(i=0;g_want[i]&&i<63;i++) lw[i]=(char)tolower((unsigned char)g_want[i]); lw[i]=0;
    if(strstr(lt,lw)){g_dlg=h;printf("dialog \"%s\"\n",t);return FALSE;} }
  return TRUE; }
static HWND g_combo=nullptr;
static void findC(HWND h){ char c[64]={0}; GetClassNameA(h,c,sizeof(c));
  if(!g_combo && strcmp(c,"ComboBox")==0 && (int)SendMessageA(h,CB_GETCOUNT,0,0)>1){
    RECT r={0}; GetWindowRect(h,&r); if(IsWindowVisible(h)&&r.bottom-r.top>14) g_combo=h; }
  HWND k=nullptr; while((k=FindWindowExA(h,k,nullptr,nullptr))!=nullptr) findC(k); }
int main(int argc,char**argv){
  int up = (argc>2 && strcmp(argv[2],"up")==0);
  strncpy(g_want, argc>1?argv[1]:"Preferences", sizeof(g_want)-1);
  HWND m=FindWindowA("REAPERwnd",nullptr); if(!m){printf("no reaper\n");return 2;}
  GetWindowThreadProcessId(m,&g_pid); EnumWindows(et,0); if(!g_dlg){printf("no dlg\n");return 2;}
  findC(g_dlg); if(!g_combo){printf("no combo\n");return 2;}
  RECT r={0}; GetWindowRect(g_combo,&r);
  SendMessageA(g_combo,CB_SETCURSEL,0,0); Sleep(100);
  int before=(int)SendMessageA(g_combo,CB_GETCURSEL,0,0);
  SetCursorPos((r.left+r.right)/2,(r.top+r.bottom)/2); Sleep(150);
  // make sure the dialog is foreground/active
  SetForegroundWindow(g_dlg); Sleep(200);
  INPUT in={0}; in.type=INPUT_MOUSE; in.mi.dwFlags=MOUSEEVENTF_WHEEL;
  in.mi.mouseData= up? (DWORD)WHEEL_DELTA : (DWORD)(-WHEEL_DELTA);
  SendInput(1,&in,sizeof(in)); Sleep(300);
  int after=(int)SendMessageA(g_combo,CB_GETCURSEL,0,0);
  printf("combo rect=%d,%d,%d,%d items=%d  dir=%s  sel %d -> %d  %s\n",
    r.left,r.top,r.right,r.bottom,(int)SendMessageA(g_combo,CB_GETCOUNT,0,0),
    up?"up":"down",before,after, before!=after?"*** VALUE EDITED ***":"(no change)");
  return 0; }
