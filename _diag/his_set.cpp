#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kDPU=8.0;
// 用户调好：Fineness 500ms, Resistance 1.00
static double run(int n,double gapMs){
  Params P; P.durationMs=500.0; P.takePerSec=(1.0*1000.0)/kDPU; P.snap=(1.0/kDPU)*0.5;
  Glide g; g.Reset();
  const double dt=0.001, gap=gapMs/1000.0;
  double msgTime=0,out=0; int fed=0;
  for(double t=0;t<n*gap+4.0;t+=dt){
    if(fed<n&&msgTime<=t+1e-9){g.Feed(15.0);++fed;msgTime+=gap;}
    if(fed>=n&&!g.Active())break;
    out+=g.Tick(dt,P);
  }
  return out*kDPU; // deltas
}
int main(void){
  printf("你的设置：Fineness 500ms + Resistance 1.00\n");
  printf("慢滚几格才留得下？（每格 120 delta）\n\n");
  printf("%-14s %-14s %s\n","每格间隔","留(delta)","留下比例");
  double gs[]={1000,700,500,300,200,150,100,50,20};
  for(int i=0;i<9;i++){
    double o=run(5,gs[i]);
    printf("%6.0f ms      %8.1f      %5.1f%%\n", gs[i], o, 100*o/(120.0*5));
  }
  printf("\n单格独转（间隔 5s）：%.1f delta（%.1f%%）\n", run(1,5000.0), 100*run(1,5000.0)/120);
  return 0;
}
