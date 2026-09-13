// accum -- is the list's wheel handling ACCUMULATING across messages, and is that
// stable? Sends repeated small deltas and records exactly when each row arrives.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
static DWORD g_pid=0; static HWND g_dlg=nullptr,g_list=nullptr; static char g_want[64]="Actions";
static BOOL CALLBACK et(HWND h,LPARAM){DWORD p=0;GetWindowThreadProcessId(h,&p);if(p!=g_pid)return TRUE;
 char c[64]={0},t[200]={0};GetClassNameA(h,c,sizeof(c));GetWindowTextA(h,t,sizeof(t));
 if(strcmp(c,"#32770")==0&&strstr(t,g_want)){g_dlg=h;return FALSE;}return TRUE;}
static HWND fc(HWND h){char c[64]={0};GetClassNameA(h,c,sizeof(c));
 if(strcmp(c,"SysListView32")==0&&IsWindowVisible(h)){RECT r={0};GetWindowRect(h,&r);
  if(r.right-r.left>100&&r.bottom-r.top>100)return h;}
 HWND k=nullptr;while((k=FindWindowExA(h,k,nullptr,nullptr))!=nullptr){HWND r=fc(k);if(r)return r;}return nullptr;}
static int sbp(HWND l){SCROLLINFO s={0};s.cbSize=sizeof(s);s.fMask=SIF_ALL;return GetScrollInfo(l,SB_VERT,&s)?s.nPos:-1;}
static void settle(HWND l){int last=-999999,same=0;for(int i=0;i<60;i++){Sleep(50);
 int p=sbp(l);if(p==last){if(++same>=3)return;}else{same=0;last=p;}}}
static void selftest(HWND l,const char*name,int d,int n,int ms){
 SendMessageA(l,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);settle(l);int b=sbp(l),prev=b;
 printf("\n%s (d=%d n=%d interval=%dms)\n",name,d,n,ms);
 for(int i=0;i<n;i++){SendMessageA(l,WM_MOUSEWHEEL,MAKEWPARAM(0,(short)-d),0);
  if(ms)Sleep(ms);int p=sbp(l);if(p!=prev){printf("   after msg %4d (total delta %6d): %+d\n",i+1,(i+1)*d,p-b);prev=p;}}
 settle(l);printf("   final %+d\n",sbp(l)-b);}
int main(int argc,char**argv){
 if(argc>1)strncpy(g_want,argv[1],sizeof(g_want)-1);
 HWND m=FindWindowA("REAPERwnd",nullptr);if(!m){printf("no reaper\n");return 2;}
 GetWindowThreadProcessId(m,&g_pid);EnumWindows(et,0);if(!g_dlg){printf("no dlg %s\n",g_want);return 2;}
 g_list=fc(g_dlg);if(!g_list){printf("no list\n");return 2;}
 printf("list=%p items=%d\n",(void*)g_list,(int)SendMessageA(g_list,LVM_GETITEMCOUNT,0,0));
 selftest(g_list,"delta 4 repeated (is there a per-notch floor?)",4,60,6);
 selftest(g_list,"delta 20 repeated",20,20,6);
 selftest(g_list,"delta 40 repeated",40,10,6);
 return 0;}
