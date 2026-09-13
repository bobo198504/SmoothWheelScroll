// watch -- send ONE real wheel notch while the plugin drives a list, and sample the
// list's scroll position every ~8ms to see the rows arrive over time.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
static DWORD g_pid=0;static HWND g_list=nullptr;
static void findL(HWND h){ if(g_list)return; char c[64]={0};GetClassNameA(h,c,sizeof(c));
 if(strcmp(c,"SysListView32")==0&&IsWindowVisible(h)){RECT r={0};GetWindowRect(h,&r);
  if(r.right-r.left>400&&r.bottom-r.top>200){g_list=h;return;}}
 HWND k=nullptr;while((k=FindWindowExA(h,k,nullptr,nullptr))!=nullptr)findL(k);}
static BOOL CALLBACK et(HWND h,LPARAM){DWORD p=0;GetWindowThreadProcessId(h,&p);
 if(p!=g_pid)return TRUE;char c[64]={0},t[200]={0};GetClassNameA(h,c,sizeof(c));GetWindowTextA(h,t,sizeof(t));
 if(strcmp(c,"#32770")==0&&strcmp(t,"Actions")==0)findL(h);return TRUE;}
static int sbp(HWND l){SCROLLINFO s={0};s.cbSize=sizeof(s);s.fMask=SIF_ALL;return GetScrollInfo(l,SB_VERT,&s)?s.nPos:-1;}
int main(int argc,char**argv){
 HWND m=FindWindowA("REAPERwnd",nullptr);if(!m){printf("no reaper\n");return 2;}
 GetWindowThreadProcessId(m,&g_pid);EnumWindows(et,0);
 if(!g_list){printf("no Actions list\n");return 2;}
 RECT r={0};GetWindowRect(g_list,&r);
 SendMessageA(g_list,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
 Sleep(600);
 int b=sbp(g_list);printf("list=%p base=%d  sending 1 notch...\n",(void*)g_list,b);
 SetForegroundWindow(GetAncestor(g_list,GA_ROOT));Sleep(200);
 SetCursorPos((r.left+r.right)/2,(r.top+r.bottom)/2);Sleep(150);
 INPUT in={0};in.type=INPUT_MOUSE;in.mi.dwFlags=MOUSEEVENTF_WHEEL;
 in.mi.mouseData=(DWORD)(-120);   // wheel down = list moves down
 SendInput(1,&in,sizeof(in));
 int prev=b;
 for(int i=0;i<120;i++){Sleep(8);int p=sbp(g_list);if(p!=prev){printf("  t=%4dms  pos %+d\n",i*8,p-b);prev=p;}}
 printf("final %+d\n",sbp(g_list)-b);
 return 0;}
