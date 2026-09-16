#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kU=15.0,kDPU=8.0;
static void one(double R,double D){
  Params P; P.durationMs=D; P.takePerSec=(R*1000.0)/kDPU; P.snap=(1.0/kDPU)*0.5;
  Glide g; g.Reset(); g.Feed(kU);
  double dt=0.001,out=0,last=0; for(double t=0;t<40;t+=dt){double s=g.Tick(dt,P); if(s!=0)last=(t+dt)*1000; out+=s; if(!g.Active())break;}
  printf("  R=%-5.2f D=%5.0f -> 通过 %5.1f%%  停 %7.0fms\n", R,D,100.0*out/kU,last);
}
int main(){
  printf("=== 21:51 POOL 真身（Tick 吃）单格 ===\n");
  for(double D:{50.0,500.0}) for(double R:{0.0,0.2,1.0,3.0}) one(R,D);
  return 0;
}
