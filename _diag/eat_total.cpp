// Measure the EATEN amount directly: at each 1 ms frame, pool before vs after the brake.
// It must equal rate * (time the brake ran), and equal 0 with no rate.
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
static const double kU=15.0,kDPN=120.0,kDPU=8.0;

// Replicates Tick's brake JUST to measure it (same formula as the header), so we can sum it.

static void Run(double gapMs,double rate,double X,int n,const char*tag){
  // We drive the REAL model, and separately track the pool by summing what we fed and what came out:
  //   eaten = fed - out - still_in_flight
  model::Params P;P.windowMs=X;P.eatRatePerMs=rate/kDPU;P.keepFrac=0.10;
  model::Axis g;g.Reset();
  const double dt=0.001,gap=gapMs/1000.0;
  double next=0,fed=0,out=0;int f=0;double brakeMs=0;
  for(double t=0;t<400.0;t+=dt){
    while(f<n&&next<=t+1e-12){g.Feed(kU,P);fed+=kDPN;++f;next+=gap;}
    out+=g.Tick(dt,P);
    brakeMs+=1.0;                       // one frame = 1 ms of brake
    if(f>=n&&!g.Active())break;
  }
  out*=kDPU;
  const double eaten=fed-out;           // everything fed that did not come out (nothing left in flight at the end)
  const double want = rate*brakeMs;     // the rate * the ms the brake ran
  printf("  %-26s fed %.0f  out %.1f  eaten %.1f   rate*ms = %.1f   ratio %.4f%s\n",
         tag,fed,out,eaten,want, eaten/want, std::fabs(eaten/want-1)<0.02?"  (linear)":"");
}
int main(){
  printf("eaten must equal rate * (ms the brake ran). X=150 ms.\n\n");
  Run(25.0,0.0,150.0,400,"no rate");
  Run(25.0,1.0,150.0,400,"rate 1 d/ms");
  Run(25.0,2.0,150.0,400,"rate 2 d/ms");
  Run(25.0,4.0,150.0,400,"rate 4 d/ms");
  Run(100.0,2.0,150.0,100,"slow roll, rate 2");
  return 0;
}
