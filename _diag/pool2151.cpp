#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kU=15.0, kDPU=8.0;
static double one(double R,double D){
  Params P; P.durationMs=D; P.takePerSec=(R*1000.0)/kDPU; P.snap=(1.0/kDPU)*0.5;
  Glide g; g.Reset(); g.Feed(kU, 0.2, P);   // first message: elapsed = maxElapsed
  double dt=0.001,out=0; for(double t=0;t<20;t+=dt){out+=g.Tick(dt,P); if(!g.Active())break;}
  return out;
}
static double stop(double R,double D){
  Params P; P.durationMs=D; P.takePerSec=(R*1000.0)/kDPU; P.snap=(1.0/kDPU)*0.5;
  Glide g; g.Reset(); g.Feed(kU,0.2,P);
  double dt=0.001,last=0; for(double t=0;t<20;t+=dt){ if(g.Tick(dt,P)!=0.0)last=(t+dt)*1000; if(!g.Active())break;}
  return last;
}
int main(){
  printf("=== 21:51 POOL 版：单格(15单位)、各阻力、各精细度 ===\n\n");
  printf("   %-8s","阻力"); for(double D:{50.0,100.0,300.0,500.0}) printf(" %14.0fms",D); printf("\n");
  for(double R:{0.0,0.2,1.0,3.0,10.0}){
    printf("   %-8.1f",R);
    for(double D:{50.0,100.0,300.0,500.0}) printf("  %5.1f%%/%7.0fms", 100.0*(1.0-one(R,D)/kU), stop(R,D));
    printf("\n");
  }
  printf("\n   （每格 = 通过% / 停止时刻）\n");
  printf("\n=== 快滚：10格，不同间隔，阻力=1，精细度=500 ===\n\n");
  for(double gap:{400.0,200.0,100.0,50.0,20.0}){
    Params P; P.durationMs=500.0; P.takePerSec=(1.0*1000.0)/kDPU; P.snap=(1.0/kDPU)*0.5;
    Glide g; g.Reset(); double dt=0.001,out=0,mt=0; int fed=0; const int n=10; double prev=0;
    for(double t=0;t<n*gap/1000.0+3.0;t+=dt){
      while(fed<n && mt<=t+1e-9){ double el=(fed==0)?0.2:(mt-prev); g.Feed(kU,el,P); prev=mt; ++fed; mt+=gap/1000.0; }
      out+=g.Tick(dt,P);
    }
    printf("   间隔 %5.0fms -> 通过 %6.2f / 150.00 (%.1f%%), 吃掉 %.1f%%\n", gap, out, 100.0*out/(10*kU), 100.0*(1.0-out/(10*kU)));
  }
  return 0;
}
