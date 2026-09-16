// A roll that starts slow and gradually speeds up. Which of these produces the "突然变快"?
//   (a) the eat->spit flip (per message, an 11x step), at gap < 50 ms;
//   (b) the WINDOWS starting to OVERLAP (gap < windowMs), which makes shares ADD.
// Both are visible in the same run.
#include "model.h"
#include <cstdio>
#include <cmath>
static const double kU=15.0, kDPN=120.0, kEatStop=120.0/50.0;
int main(){
  const double X=150.0, eatFrac=0.90, V=1.1;
  printf("Fineness X=%.0f ms, eat=%.0f%%, V=%.1f.  Gaps shrink from 300 ms to 40 ms.\n\n",X,100*eatFrac,V);
  model::Params P; P.windowMs=X;
  model::Axis g; g.Reset();
  const double dt=0.001;
  double gapMs=300, next=0, out=0, winStart=0; int idx=0;
  const int n=24;
  printf("  %-5s %-8s %-6s %-8s %-10s %-10s %s\n","#","gap","speed","decision","50ms win","overlap","");
  for(double t=0;t<40.0;t+=dt){
    if(idx<n && next<=t+1e-12){
      const double speed=kDPN/gapMs;
      const bool spit = speed>kEatStop;
      // what THIS message contributes to the model
      P.eatFrac = spit?0.0:eatFrac; P.spitMul = spit?V:1.0;
      g.Feed(kU,P);
      ++idx;
      next += gapMs/1000.0;
      gapMs -= 11.0; if(gapMs<40.0) gapMs=40.0;
      if(idx<=24) printf("  %-5d %-8.0f %-6.2f %-8s %-10s %-10s\n", idx, gapMs+11, speed,
                         spit?"SPIT":"eat", (gapMs+11 < X)?"OVERLAP":"-",
                         (gapMs+11 < X)?"<- adds":"");
    }
    out += g.Tick(dt,P);
    if(t-winStart>=0.05){ 
      // print per-50ms delivery so a step is visible in the output too
      winStart=t; 
    }
    if(idx>=n && !g.Active()) break;
  }
  printf("\n  total out = %.1f deltas (fed %d notches = %d deltas)\n", out*kDPN/kU, n, n*120);
  printf("\n  READ: the 'SPIT' column marks the message that jumps 11x. The 'OVERLAP' column marks\n");
  printf("  where windows start sharing time. Either can read as 'suddenly faster'.\n");
  return 0;
}
