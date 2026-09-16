// Same source-level inputs, two cores: the last version's pool vs the window. takePerSec is
// computed identically (R deltas per millisecond), snap identical.
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kU=15.0, kDPU=8.0;
static void one(double R,double D){
  Params P; P.durationMs=D; P.takePerSec=(R*1000.0)/kDPU; P.snap=(1.0/kDPU)*0.5;
  Glide g; g.Reset();
#ifdef POOL
  g.Feed(kU);
#else
  g.Feed(kU,P);
#endif
  double dt=0.001,out=0,last=0;
  for(double t=0;t<60;t+=dt){double s=g.Tick(dt,P); if(s!=0)last=(t+dt)*1000; out+=s; if(!g.Active())break;}
  printf("   精细度 %5.0f ms, 阻力 %.1f -> 停 %8.1f ms   通过 %5.1f%%\n",D,R,last,100.0*out/kU);
}
int main(){
#ifdef POOL
  printf("### 最后那版（池子）单格：\n");
#else
  printf("### 本次（窗口，池子已去）单格：\n");
#endif
  printf("  -- 阻力=0（=「阻力没加」）:\n");
  for(double D:{100.0,300.0,500.0}) one(0.0,D);
  printf("  -- 阻力=1（默认）:\n");
  for(double D:{300.0,500.0}) one(1.0,D);
  return 0;
}
