#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
static DWORD g_pid=0;static HWND g_dlg=nullptr,g_list=nullptr;static char g_want[64]="Actions";
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
int main(int argc,char**argv){
 if(argc>1)strncpy(g_want,argv[1],sizeof(g_want)-1);
 HWND m=FindWindowA("REAPERwnd",nullptr);if(!m)return 2;
 GetWindowThreadProcessId(m,&g_pid);EnumWindows(et,0);if(!g_dlg)return 2;
 g_list=fc(g_dlg);if(!g_list){printf("no list\n");return 2;}
 printf("per-message delta -> total delta needed for ONE row (3 rows at 360 total = 40/row)\n");
 for(int d=1;d<=8;d++){
   SendMessageA(g_list,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);settle(g_list);
   int b=sbp(g_list),prev=b,first=-1;
   for(int i=0;i<400;i++){
     SendMessageA(g_list,WM_MOUSEWHEEL,MAKEWPARAM(0,(short)-d),0);
     Sleep(3);
     int p=sbp(g_list);
     if(p!=prev){ if(first<0)first=(i+1)*d; prev=p; }
     if(p-b>=3)break;
   }
   int tot=0;while(tot<400){SendMessageA(g_list,WM_MOUSEWHEEL,MAKEWPARAM(0,(short)-d),0);tot++;}
   settle(g_list);
   printf("  delta %d : first row after total %4d  | 400 msgs (total %4d) -> %+d rows\n",
          d, first, 400*d, sbp(g_list)-b);
 }
 return 0;}
