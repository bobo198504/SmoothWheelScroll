// 单格、阻力0、Fineness 500ms：真实模型 + 真实投递步长，量出尾巴到底多长
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kDPU=8.0, kStep=1.0; // 整单位投递（主视图现在是整单位）
int main(void){
  for(double ease : {500.0, 300.0, 100.0}){
    Params P; P.durationMs=ease; P.takePerSec=0.0; P.snap=(1.0/kDPU)*0.5;
    Glide g; g.Reset(); g.Feed(15.0);   // 一格 = 15 单位
    double accum=0,out=0,last=-1; int calls=0;
    double t90=-1,t99=-1,tEnd=-1;
    for(double t=0;t<20.0;t+=0.001){
      accum += g.Tick(0.001,P);
      const double m=floor(fabs(accum)/kStep+0.5);
      if(m>=1.0){ const double s=m*kStep; accum-=(accum<0?-s:s); out+=s; ++calls; last=t;
        if(t90<0&&out>=15*0.90)t90=t; if(t99<0&&out>=15*0.99)t99=t; }
      if(!g.Active()){ tEnd=t; break; }
    }
    printf("Fineness %3.0fms: 送完 %5.2f/15 单位, %3d 次, 90%%@%5.0fms 99%%@%5.0fms 停@%5.0fms\n",
           ease,out,calls,t90*1000,t99*1000,tEnd*1000);
  }
  printf("\n注：'停' = 欠账衰减到半个 delta 以下（snap）。\n");
  return 0;
}
