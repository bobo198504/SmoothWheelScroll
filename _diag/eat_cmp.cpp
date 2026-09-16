// SAME slow roll, two eat rules. Which one stutters?
//   A = current: keep is a FLAT fraction (90% eaten -> keep 10%, at every speed)
//   B = original TravelFraction: keep RISES with speed (slow -> 10%, fast -> 100%)
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
static const double kU=15.0, kDPN=120.0;
static const double kSlowGap=0.200, kFastGap=0.050;

static double KeepB(double gapSec, double receivedDeltas, double slowMoveDeltas){
  double sf = slowMoveDeltas/kDPN; if(sf<0)sf=0; if(sf>1)sf=1;
  if(sf>=1.0) return 1.0;
  if(gapSec<0) return sf;
  if(receivedDeltas<=0) return sf;
  const double slowRef=(receivedDeltas/kDPN)*kSlowGap;
  if(!(slowRef>0)) return 1.0;
  const double x=gapSec/slowRef, ff=kFastGap/kSlowGap;
  if(x>=1.0) return sf;
  if(x<=ff) return 1.0;
  return sf+(1.0-sf)*((1.0-x)/(1.0-ff));
}

// run n notches at gapMs; mode A eatFrac flat, mode B TravelFraction(slowMove=12)
static void Run(const char*tag,int mode,int n,double gapMs,double X,double eatFrac,double slowMove){
  anim3::Params P; P.windowMs=X;
  anim3::Glide g; g.Reset();
  const double dt=0.001, gap=gapMs/1000.0;
  double next=0,out=0,win=0,lastFeed=-1;
  int fed=0;
  printf("  %-28s per 50 ms: ", tag);
  for(double t=0;t<80.0;t+=dt){
    while(fed<n && next<=t+1e-12){
      double keep;
      if(mode==0) keep = 1.0-eatFrac;
      else { const double gp=(lastFeed<0)?-1.0:(t-lastFeed); keep=KeepB(gp,kDPN,slowMove); }
      lastFeed=t;
      P.eatFrac = 1.0-keep;   // the model's eat
      g.Feed(kU,P);
      ++fed; next+=gap;
    }
    out+=g.Tick(dt,P);
    if(t-win>=0.05){ printf("%5.1f", out*kDPN/kU); out=0; win=t; }
    if(fed>=n && !g.Active()) break;
  }
  printf("\n");
}
int main(){
  printf("20 notches, X=150 ms. 'per 50 ms' is the deltas handed over in each 50 ms window.\n\n");
  printf("SLOW roll (200 ms per notch = 5 notches/s):\n");
  Run("A flat 90% (keep 10%)", 0, 20, 200.0, 150.0, 0.90, 12.0);
  Run("B original (keep 10%->100%)", 1, 20, 200.0, 150.0, 0.0, 12.0);
  printf("\nMEDIUM roll (80 ms per notch = 12.5 notches/s):\n");
  Run("A flat 90% (keep 10%)", 0, 20, 80.0, 150.0, 0.90, 12.0);
  Run("B original", 1, 20, 80.0, 150.0, 0.0, 12.0);
  printf("\nFAST roll (40 ms per notch = 25 notches/s):\n");
  Run("A flat 90% (keep 10%)", 0, 20, 40.0, 150.0, 0.90, 12.0);
  Run("B original", 1, 20, 40.0, 150.0, 0.0, 12.0);
  printf("\n  A keeps the same 10%% at every speed, so a slow roll gives a tiny, GAPPY trickle and\n");
  printf("  only becomes continuous as the windows start to overlap -- the 'sudden'.\n");
  printf("  B raises the keep as the hand speeds up, so the motion BUILDS smoothly instead.\n");
  return 0;
}
