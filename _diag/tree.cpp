// tree -- does SysTreeView32 accumulate fine wheel deltas the same way a list does?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
static DWORD g_pid=0; static HWND g_dlg=nullptr,g_tree=nullptr; static char g_want[64]="Preferences";
static BOOL CALLBACK et(HWND h,LPARAM){DWORD p=0;GetWindowThreadProcessId(h,&p);if(p!=g_pid)return TRUE;
 char c[64]={0},t[200]={0};GetClassNameA(h,c,sizeof(c));GetWindowTextA(h,t,sizeof(t));
 if(strcmp(c,"#32770")==0&&strstr(t,g_want)){g_dlg=h;return FALSE;}return TRUE;}
static HWND fc(HWND h){char c[64]={0};GetClassNameA(h,c,sizeof(c));
 if(strcmp(c,"SysTreeView32")==0&&IsWindowVisible(h)){RECT r={0};GetWindowRect(h,&r);
  if(r.right-r.left>60&&r.bottom-r.top>60)return h;}
 HWND k=nullptr;while((k=FindWindowExA(h,k,nullptr,nullptr))!=nullptr){HWND r=fc(k);if(r)return r;}return nullptr;}
// Top visible node index: walk items from the root and find the first whose rect is
// at/below the client top.
static int topNode(HWND t){
  HTREEITEM root=(HTREEITEM)SendMessageA(t,TVM_GETNEXTITEM,TVGN_ROOT,0);
  int idx=0; int count=(int)SendMessageA(t,TVM_GETCOUNT,0,0);
  // Use TVM_GETITEMRECT on successive items reached by TVGN_NEXT is not a reliable
  // top index; instead use the scroll position of the tree's scrollbar.
  (void)root;
  return count>0? (int)SendMessageA(t,TVM_GETSCROLLTIME,0,0) : -1;
}
static int sbp(HWND h){SCROLLINFO s={0};s.cbSize=sizeof(s);s.fMask=SIF_ALL;return GetScrollInfo(h,SB_VERT,&s)?s.nPos:-1;}
static void settle(HWND h){int last=-999999,same=0;for(int i=0;i<60;i++){Sleep(50);
 int p=sbp(h);if(p==last){if(++same>=3)return;}else{same=0;last=p;}}}
int main(int argc,char**argv){
 if(argc>1)strncpy(g_want,argv[1],sizeof(g_want)-1);
 HWND m=FindWindowA("REAPERwnd",nullptr);if(!m){printf("no reaper\n");return 2;}
 GetWindowThreadProcessId(m,&g_pid);EnumWindows(et,0);if(!g_dlg){printf("no dlg %s\n",g_want);return 2;}
 g_tree=fc(g_dlg);if(!g_tree){printf("no tree in %s\n",g_want);return 2;}
 RECT r={0};GetWindowRect(g_tree,&r);
 printf("tree=%p rect=%d,%d,%d,%d\n",(void*)g_tree,r.left,r.top,r.right,r.bottom);
 printf("scrollbar: min=%d max=%d page=%d pos=%d\n",
   GetScrollInfo(g_tree,SB_VERT,({static SCROLLINFO s;s.cbSize=sizeof(s);s.fMask=SIF_ALL;GetScrollInfo(g_tree,SB_VERT,&s);&s;}))?0:0,0,0,0);
 printf("\n--- small repeated deltas (accumulation test) ---\n");
 SendMessageA(g_tree,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);settle(g_tree);
 int b=sbp(g_tree),prev=b;
 for(int i=0;i<200;i++){
   SendMessageA(g_tree,WM_MOUSEWHEEL,MAKEWPARAM(0,(short)-4),0);
   Sleep(6);
   int p=sbp(g_tree);
   if(p!=prev){printf("   msg %3d (delta %4d): pos %+d\n",i+1,(i+1)*4,p-b);prev=p;}
 }
 settle(g_tree);
 printf("   final %+d\n",sbp(g_tree)-b);
 printf("\n--- one whole notch ---\n");
 SendMessageA(g_tree,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);settle(g_tree);
 b=sbp(g_tree);
 SendMessageA(g_tree,WM_MOUSEWHEEL,MAKEWPARAM(0,(short)-120),0);settle(g_tree);
 printf("   delta 120 -> %+d\n",sbp(g_tree)-b);
 return 0;}
