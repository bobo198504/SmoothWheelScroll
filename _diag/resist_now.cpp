// 当前实现：阻力到底怎么算 —— 用插件自己的 src/anim3_core.h。
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kU=15.0;      // 一格的量（7-bit 单位）
static const double kDPU=8.0;     // 每单位 8 delta
static double takeSec(double R){ return (R*1000.0)/kDPU; }  // = R*125 单位/秒
static double snapNow(){ return (1.0/kDPU)*0.5; }

// 单格：喂 15 单位，看通过多少
static void single(double R,double D){
  Params P; P.durationMs=D; P.takePerSec=takeSec(R); P.snap=snapNow();
  Glide g; g.Reset(); g.Feed(kU,P);
  double dt=0.001,out=0,last=0;
  for(double t=0;t<20;t+=dt){double s=g.Tick(dt,P); if(s!=0)last=(t+dt)*1000; out+=s; if(!g.Active())break;}
  printf("   阻力 %.2f  精细度 %5.0fms -> 吃掉 %5.1f%%   停 %7.1fms\n",R,D,100.0*(1-out/kU),last);
}
// 持续连滚：每 gap 喂一格，共 n 格，看总共通过多少
static double roll(double R,double D,double gapMs,int n){
  Params P; P.durationMs=D; P.takePerSec=takeSec(R); P.snap=snapNow();
  Glide g; g.Reset();
  const double dt=0.001, gap=gapMs/1000.0;
  double out=0, mt=0; int fed=0;
  for(double t=0;t<n*gap+6.0;t+=dt){
    while(fed<n && mt<=t+1e-9){ g.Feed(kU,P); ++fed; mt+=gap; }
    out+=g.Tick(dt,P);
  }
  return out;
}
int main(){
  printf("=== 1. 单格：阻力吃多少（看出吃法）===\n");
  for(double D:{50.0,500.0}){ printf("  --- 精细度 %.0fms ---\n",D);
    for(double R:{0.0,0.5,1.0,2.0,5.0,10.0}) single(R,D); }
  printf("\n=== 2. 持续连滚：10 格，精细度 500ms，不同间隔下的通过率 ===\n");
  printf("   %-8s","阻力");
  for(double g:{400.0,200.0,100.0,50.0,25.0,12.0}) printf(" %7.0fms",g);
  printf("\n");
  for(double R:{0.5,1.0,2.0,5.0}){
    printf("   %-8.1f",R);
    for(double gx:{400.0,200.0,100.0,50.0,25.0,12.0})
      printf(" %6.1f%%",100.0*roll(R,500.0,gx,10)/(10*kU));
    printf("\n");
  }
  printf("\n   （每格 120 delta 进来；阻力 R = 每 1ms 吃 R 个 delta，即每秒吃 1000R 个）\n");
  printf("   阈值 = 120/R 毫秒每格：慢于它→吃穿，快于它→通过\n");
  for(double R:{0.5,1.0,2.0,5.0,10.0}) printf("     阻力 %.1f -> 阈值 %6.1f ms/格\n",R,120.0/R);
  return 0;
}
