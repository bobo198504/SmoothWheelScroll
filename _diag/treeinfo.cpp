#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
static DWORD g_pid=0;
static void walk(HWND h){
  char c[64]={0};GetClassNameA(h,c,sizeof(c));
  if((strcmp(c,"SysListView32")==0||strcmp(c,"SysTreeView32")==0)&&IsWindowVisible(h)){
    RECT r={0};GetWindowRect(h,&r);
    if(r.right-r.left>=80&&r.bottom-r.top>=80){
      SCROLLINFO si={0};si.cbSize=sizeof(si);si.fMask=SIF_ALL;
      GetScrollInfo(h,SB_VERT,&si);
      int cnt = strcmp(c,"SysTreeView32")==0?(int)SendMessageA(h,TVM_GETCOUNT,0,0)
                                            :(int)SendMessageA(h,LVM_GETITEMCOUNT,0,0);
      int vis = strcmp(c,"SysTreeView32")==0?(int)SendMessageA(h,TVM_GETVISIBLECOUNT,0,0)
                                            :(int)SendMessageA(h,LVM_GETCOUNTPERPAGE,0,0);
      printf("%-14s %-26s rect=%d,%d,%d,%d items=%5d vis=%4d scroll: nMin=%d nMax=%d nPage=%d pos=%d canScroll=%d\n",
        c, "?", r.left,r.top,r.right,r.bottom, cnt, vis,
        si.nMin,si.nMax,(int)si.nPage,si.nPos,
        (si.nMax-(int)si.nPage+1>0)?1:0);
    }
  }
  HWND k=nullptr;while((k=FindWindowExA(h,k,nullptr,nullptr))!=nullptr)walk(k);
}
static BOOL CALLBACK et(HWND h,LPARAM){DWORD p=0;GetWindowThreadProcessId(h,&p);if(p==g_pid)walk(h);return TRUE;}
int main(){HWND m=FindWindowA("REAPERwnd",nullptr);if(!m)return 2;
 GetWindowThreadProcessId(m,&g_pid);EnumWindows(et,0);return 0;}
