// How many REPLAY CALLS (each one makes REAPER redraw the view) one gesture costs, per step size.
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kU=15.0, kDPU=8.0;

static long callsForStep(double stepUnits, int n, double gapMs, double finenessMs, double K){
  Params P; P.durationMs=finenessMs;
  Glide g; g.Reset();
  const double dt=0.001, gap=gapMs/1000.0;
  double accum=0, mt=0; int fed=0; long calls=0;
  for(double t=0;t<n*gap+finenessMs/1000.0+0.5;t+=dt){
    while(fed<n && mt<=t+1e-9){ g.Feed(kU*TravelFraction(fed==0?-1.0:gap,120,K),P); ++fed; mt+=gap; }
    if(g.Active()){
      accum += g.Tick(dt,P);
      const double m=floor(fabs(accum)/stepUnits+0.5);
      if(m>=1.0){ const double s=m*stepUnits; accum -= (accum<0?-s:s); ++calls; }
    }
  }
  return calls;
}
int main(){
  printf("一次手势的「重放调用次数」= REAPER 视图重绘次数。精细度 500ms，阻力 9。\n\n");
  printf("  %-16s %-8s","场景","步长");
  for(double st:{1.0, 2.0, 4.0, 8.0}) printf(" %10.0fdelta",st);
  printf("\n");
  struct C{double gap;int n;const char*nm;};
  C cs[]={{400,6,"极慢滚 6格@400ms"},{200,6,"慢滚  6格@200ms"},{120,10,"连滚 10格@120ms"},{60,10,"快滚 10格@60ms"}};
  for(auto&c:cs){
    printf("  %-16s %-8s",c.nm,"");
    for(double st:{1.0,2.0,4.0,8.0})
      printf(" %10ld", callsForStep(st/kDPU, c.n, c.gap, 500.0, 9.0));
    printf("\n");
  }
  printf("\n  1 delta 相对 8 delta 的倍数（慢滚最明显）:\n");
  for(auto&c:cs){
    double a=callsForStep(1.0/kDPU,c.n,c.gap,500.0,9.0), b=callsForStep(8.0/kDPU,c.n,c.gap,500.0,9.0);
    printf("    %-16s %5ld -> %5ld  = %.1fx\n", c.nm,(long)b,(long)a, b>0?a/b:0);
  }
  return 0;
}
