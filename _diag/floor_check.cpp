// Instrument (c): total in, total out, the time, and the implied eaten rate.
#include "model.h"
#include <cstdio>
#include <cmath>
static const double kU=15.0,kDPN=120.0,kDPU=8.0;
int main(){
  const double gapMs=25.0, rate=4.0, keep=0.10, X=150.0; const int n=300;
  model::Params P;P.windowMs=X;P.eatRatePerMs=rate/kDPU;P.keepFrac=keep;
  model::Axis g;g.Reset();
  const double dt=0.001,gap=gapMs/1000.0;
  double next=0,in=0,out=0,outSettled=0;int fed=0;double tEnd=0,t0=-1;
  for(double t=0;t<400.0;t+=dt){
    while(fed<n&&next<=t+1e-12){g.Feed(kU,P);in+=kDPN;++fed;next+=gap;}
    const double ss=g.Tick(dt,P); out+=ss; if(t0<0&&fed>=50){t0=t;outSettled=0;} if(t0>=0)outSettled+=ss; tEnd=t;
    if(fed>=n&&!g.Active())break;
  }
  out*=kDPU; outSettled*=kDPU;
  const double feedSec=n*gap;                 // how long the wheel was fed
  const double totalSec=feedSec;              // its own duration
  printf("fed %d notches @%.0f ms = %.1f deltas over %.2f s  -> %.0f deltas/s in\n",
         n,gapMs,in,feedSec,in/feedSec);
  printf("out = %.1f deltas over the roll; motion ended at %.2f s (roll ended %.2f s)\n",
         out,tEnd,feedSec);
  printf("implied out rate over the FEED time = %.0f deltas/s\n", out/feedSec);
  printf("rate wants %.0f deltas/s; in - eaten = %.0f - %.0f = %.0f\n",
         rate*1000.0, in/feedSec, rate*1000.0, in/feedSec-rate*1000.0);
  printf("\n  ratio out/(in-eaten*T) = %.3f\n", out/(in-rate*1000.0*feedSec));
  return 0;
}
