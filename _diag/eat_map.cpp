// Verify the closed form with the REAL model, and test the remap that reaches 90%.
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
static const double kU=15.0,kDPN=120.0,kDPU=8.0;

// one message, rate expressed as a multiple y of (A/X): r_deltas_per_ms = (A/X)*y
static double OneEaten(double perDeltas,double X,double y){
  const double r = (perDeltas/X)*y;
  model::Params P;P.windowMs=X;P.eatRatePerMs=r/kDPU;
  model::Axis g;g.Reset();g.Feed(perDeltas/kDPU,P);
  const double dt=0.001;double out=0;
  for(double t=0;t<20.0;t+=dt){out+=g.Tick(dt,P);if(!g.Active())break;}
  out*=kDPU; return (perDeltas-out)/perDeltas;
}
static double Closed(double y){ return y*(1.0-std::exp(-1.0/y)); }
// inverse: y such that Closed(y)=f
static double Inv(double f){ double lo=1e-6,hi=200.0; for(int i=0;i<100;++i){double m=0.5*(lo+hi); if(Closed(m)<f)lo=m;else hi=m;} return 0.5*(lo+hi); }

int main(){
  printf("== 1. the closed form matches the real model ==\n");
  printf("  %-8s %-14s %-14s\n","y","closed form","model");
  for(double y:{0.5,1.0,2.0,4.0,8.0})
    printf("  %-8.1f %-14.4f %-14.4f\n", y, Closed(y), OneEaten(120.0,150.0,y));

  printf("\n== 2. REMAP: make the slider mean 'eaten fraction of a slow turn' ==\n");
  printf("  %-8s %-14s %-14s %-14s\n","slider %","rate x (y)","closed","model");
  for(double Y:{30.0,60.0,80.0,90.0}){
    const double y=Inv(Y/100.0);
    printf("  %-8.0f %-14.3f %-14.4f %-14.4f\n", Y, y, Closed(y), OneEaten(120.0,150.0,y));
  }

  printf("\n== 3. and the fast-roll behaviour stays sane with the remap (Y=90 -> rate x4.66) ==\n");
  {
    // steady roll, rate = 4.66*(A/X); measure eaten fraction vs speed
    printf("  %-14s %-12s\n","gap per notch","eaten %");
    for(double gap : {400.0,200.0,150.0,100.0,50.0,25.0}){
      const double A=120.0, X=150.0, y=4.6613, r=(A/X)*y;
      model::Params P;P.windowMs=X;P.eatRatePerMs=r/kDPU;
      model::Axis g;g.Reset();
      const double dt=0.001,gp=gap/1000.0; const int n=200;
      double next=0,fed=0,out=0;int f=0;
      for(double t=0;t<400.0;t+=dt){
        while(f<n&&next<=t+1e-12){g.Feed(kU,P);fed+=kDPN;++f;next+=gp;}
        out+=g.Tick(dt,P); if(f>=n&&!g.Active())break;
      }
      out*=kDPU;
      printf("  %-14.0f %-12.1f\n", gap, 100.0*(fed-out)/fed);
    }
  }
  return 0;
}
