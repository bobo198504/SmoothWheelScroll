#include "anim3_core.h"
#include <cstdio>
#include <cmath>
using namespace anim3;
static const double kDPU=8.0, kStep=1.0/kDPU;
int main(void){
  // 单格，Fineness 500, Resistance 1.00 —— 打印每一次投递的时刻
  Params P; P.durationMs=500.0; P.takePerSec=(1.0*1000.0)/kDPU; P.snap=(1.0/kDPU)*0.5;
  Glide g; g.Reset(); g.Feed(15.0);
  double accum=0,out=0,prev=-1; int calls=0;
  printf("单格独转（Fineness 500, Resistance 1.00）：每次投递的时刻（ms）\n\n");
  for(double t=0;t<3.0;t+=0.001){
    const double st=g.Tick(0.001,P);
    accum+=st;
    const double m=floor(fabs(accum)/kStep+0.5);
    if(m>=1.0){
      const double s=m*kStep; accum-=(accum<0?-s:s); out+=s*kDPU; ++calls;
      if(calls<=12) printf("  第%2d次: t=%6.0f ms   累计 %6.1f delta%s\n", calls, t*1000, out,
        (prev>=0&&t*1000-prev>50)?"   <-- 前面静默了这么久":""); 
      prev=t*1000;
    }
    if(!g.Active())break;
  }
  printf("\n  共 %d 次投递，总 %0.1f delta\n", calls, out);
  printf("  (原生鼠标：1 次投递，120 delta)\n");
  return 0;
}
