// whichlist -- for every list/tree control in REAPER, report: its class, the class of
// its top-level ancestor, and whether a wheel over it produces a REAPER ACTION
// (which would mean REAPER does not scroll it natively) or no action (native scroll).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
static DWORD g_pid=0;
struct Ctl{HWND h;char cls[32];char root[32];RECT r;};
static Ctl g_c[80];static int g_n=0;
static void walk(HWND h){
  char c[64]={0};GetClassNameA(h,c,sizeof(c));
  if((strcmp(c,"SysListView32")==0||strcmp(c,"SysTreeView32")==0)&&IsWindowVisible(h)){
    RECT r={0};GetWindowRect(h,&r);
    if(r.right-r.left>=80&&r.bottom-r.top>=80&&g_n<80){
      HWND root=GetAncestor(h,GA_ROOT);
      g_c[g_n].h=h;strncpy(g_c[g_n].cls,c,sizeof(g_c[g_n].cls)-1);
      g_c[g_n].root[0]=0; if(root)GetClassNameA(root,g_c[g_n].root,sizeof(g_c[g_n].root));
      g_c[g_n].r=r;++g_n;}
  }
  HWND k=nullptr;while((k=FindWindowExA(h,k,nullptr,nullptr))!=nullptr)walk(k);
}
static BOOL CALLBACK et(HWND h,LPARAM){DWORD p=0;GetWindowThreadProcessId(h,&p);
 if(p==g_pid)walk(h);return TRUE;}
int main(void){
  HWND m=FindWindowA("REAPERwnd",nullptr);if(!m)return 2;
  GetWindowThreadProcessId(m,&g_pid);
  EnumWindows(et,0);
  printf("%-3s %-16s %-22s %-26s %s\n","#","class","top-level ancestor","rect","native notch move");
  for(int i=0;i<g_n;i++){
    // Park at top, send one whole notch, see if the list itself scrolls.
    SendMessageA(g_c[i].h,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
    Sleep(120);
    SCROLLINFO s={0};s.cbSize=sizeof(s);s.fMask=SIF_ALL;
    GetScrollInfo(g_c[i].h,SB_VERT,&s);int b=s.nPos;
    SendMessageA(g_c[i].h,WM_MOUSEWHEEL,MAKEWPARAM(0,(short)-120),0);
    Sleep(200);
    GetScrollInfo(g_c[i].h,SB_VERT,&s);int a=s.nPos;
    char rr[32];snprintf(rr,sizeof(rr),"%d,%d,%d,%d",g_c[i].r.left,g_c[i].r.top,g_c[i].r.right,g_c[i].r.bottom);
    printf("%-3d %-16s %-22s %-26s %+d\n",i,g_c[i].cls,g_c[i].root,rr,a-b);
  }
  return 0;}
