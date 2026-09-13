// rolltest -- how far does the plugin move a list for a roll of N notches,
// compared with REAPER's own flat 3-rows-per-notch?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
static DWORD g_pid=0;static HWND g_list=nullptr;
static void findL(HWND h){ if(g_list)return;char c[64]={0};GetClassNameA(h,c,sizeof(c));
 if(strcmp(c,"SysListView32")==0&&IsWindowVisible(h)){RECT r={0};GetWindowRect(h,&r);
  if(r.right-r.left>400&&r.bottom-r.top>200){g_list=h;return;}}
 HWND k=nullptr;while((k=FindWindowExA(h,k,nullptr,nullptr))!=nullptr)findL(k);}
static BOOL CALLBACK et(HWND h,LPARAM){DWORD p=0;GetWindowThreadProcessId(h,&p);
 if(p!=g_pid)return TRUE;char c[64]={0},t[200]={0};GetClassNameA(h,c,sizeof(c));GetWindowTextA(h,t,sizeof(t));
 if(strcmp(c,"#32770")==0&&strcmp(t,"Actions")==0)findL(h);return TRUE;}
static int sbp(){SCROLLINFO s={0};s.cbSize=sizeof(s);s.fMask=SIF_ALL;return GetScrollInfo(g_list,SB_VERT,&s)?s.nPos:-1;}
static void settle(){int last=-999999,same=0;for(int i=0;i<80;i++){Sleep(50);int p=sbp();
 if(p==last){if(++same>=3)return;}else{same=0;last=p;}}}
static void roll(int n,const char*name){
 SendMessageA(g_list,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);settle();
 int b=sbp();RECT r={0};GetWindowRect(g_list,&r);
 SetForegroundWindow(GetAncestor(g_list,GA_ROOT));Sleep(150);
 SetCursorPos((r.left+r.right)/2,(r.top+r.bottom)/2);Sleep(120);
 for(int i=0;i<n;i++){INPUT in={0};in.type=INPUT_MOUSE;in.mi.dwFlags=MOUSEEVENTF_WHEEL;
  in.mi.mouseData=(DWORD)(-120);SendInput(1,&in,sizeof(in));Sleep(120);}
 settle();
 int m=sbp()-b;
 printf("  %-14s %2d notches -> %3d rows (native %3d, %+.0f%%)\n",name,n,m,n*3,100.0*(m-n*3.0)/(n*3.0));
}
int main(){
 HWND m=FindWindowA("REAPERwnd",nullptr);if(!m){printf("no reaper\n");return 2;}
 GetWindowThreadProcessId(m,&g_pid);EnumWindows(et,0);
 if(!g_list){printf("no Actions list\n");return 2;}
 roll(1,"1 notch");roll(3,"3 notches");roll(5,"5 notches");roll(10,"10 notches");
 return 0;}
