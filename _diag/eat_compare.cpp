// 池子版「阻力 R」实际吃掉多少（单格，不同精细度），用于对照窗口版的「动能」。
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kU = 15.0, kDeltasPerUnit = 8.0;
int main(){
  printf("池子版：单格（15 单位）被吃掉的比例\n\n");
  printf("   %-8s","阻力R");
  for(double D:{10.0,50.0,100.0,300.0,500.0}) printf(" %6.0fms",D);
  printf("\n");
  for(double R:{1.0,3.0,10.0}){
    printf("   %-8.0f",R);
    for(double D:{10.0,50.0,100.0,300.0,500.0}){
      Params P; P.durationMs=D; P.takePerSec=(R*100.0)/kDeltasPerUnit; P.snap=0.0;
      Glide g; g.Reset(); g.Feed(kU);
      double dt=0.001,out=0;
      for(double t=0;t<10.0;t+=dt){ out+=g.Tick(dt,P); if(!g.Active())break; }
      printf(" %6.1f%%", 100.0*(1.0-out/kU));
    }
    printf("\n");
  }
  printf("\n窗口版「动能 K」= 慢转（gap>=200ms）一格保留的比例：\n\n");
  for(double K:{1.0,12.0,64.0,120.0})
    printf("   动能 %3.0f -> 慢转保留 %5.1f%%（走 %.0f 个 delta）\n", K, 100.0*K/120.0, K);
  return 0;
}
