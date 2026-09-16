// OUTPUT RATE vs SPEED, for the two eat rules, plus a smoothness measure.
//   A flat fraction (current): every message keeps the same share (90% eaten -> keeps 10%)
//   B fixed amount + angular momentum (original): a slow notch keeps slowMove deltas, rising to
//     the whole notch as the hand speeds up.
// "rate" = deltas/sec settled; "rough" = how lumpy the delivery is (CV of 20 ms buckets).
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
static const double kU=15.0, kDPN=120.0, kSlowGap=0.200, kFastGap=0.050;

static double KeepB(double gapSec,double receivedDeltas,double slowMove){
  double sf=slowMove/kDPN; if(sf<0)sf=0; if(sf>1)sf=1;
  if(sf>=1.0) return 1.0;
  if(gapSec<0) return sf;
  if(receivedDeltas<=0) return sf;
  const double sr=(receivedDeltas/kDPN)*kSlowGap; if(!(sr>0)) return 1.0;
  const double x=gapSec/sr, ff=kFastGap/kSlowGap;
  if(x>=1.0) return sf; if(x<=ff) return 1.0;
  return sf+(1.0-sf)*((1.0-x)/(1.0-ff));
}

// settled rate + roughness for one steady roll
static void Measure(int mode,double gapMs,double X,double eatFrac,double slowMove,
                    double *rate,double *rough){
  anim3::Params P; P.windowMs=X; anim3::Glide g; g.Reset();
  const double dt=0.001, gap=gapMs/1000.0; const int n=200;
  double next=0,lastFeed=-1,bucket=0,bt=0; int fed=0;
  double sum=0,sum2=0; int nb=0; bool started=false; double t0=0;
  for(double t=0;t<200.0;t+=dt){
    while(fed<n && next<=t+1e-12){
      double keep;
      if(mode==0) keep=1.0-eatFrac;
      else { double gp=(lastFeed<0)?-1.0:(t-lastFeed); keep=KeepB(gp,kDPN,slowMove); }
      lastFeed=t; P.eatFrac=1.0-keep; g.Feed(kU,P); ++fed; next+=gap;
    }
    double s=g.Tick(dt,P); bucket+=s*kDPN/kU; bt+=dt;
    if(!started && fed>=20){ started=true; t0=t; }
    if(started && t-t0>2.0){      // measure after startup
      if(bt>=0.020){ sum+=bucket; sum2+=bucket*bucket; ++nb; bucket=0; bt=0; }
    }
    if(fed>=n && !g.Active()) break;
  }
  const double mean=nb?sum/nb:0;
  const double var=nb?sum2/nb-mean*mean:0;
  *rate=mean*50.0;                       // per 20ms -> per second
  *rough=(mean>0)?std::sqrt(var>0?var:0)/mean:0;
}

int main(){
  const double X=150.0;
  printf("Fineness X=%.0f ms. 'rate' = deltas/s settled. 'rough' = delivery lumpiness (0=even).\n\n",X);
  printf("  A: flat fraction, keeps 10%% at EVERY speed\n");
  printf("  B: fixed slow amount (12 deltas) + angular momentum (rises to the whole notch)\n\n");
  printf("  %-10s %-12s | %-16s | %-16s\n","gap ms","speed n/s","A  rate    rough","B  rate    rough");
  const double gaps[]={300,200,150,100,80,60,50,40,30,25};
  for(int i=0;i<10;++i){
    double ra,rb,oa,ob;
    Measure(0,gaps[i],X,0.90,12.0,&ra,&oa);
    Measure(1,gaps[i],X,0.0 ,12.0,&rb,&ob);
    printf("  %-10.0f %-12.1f | %-8.1f %-7.2f | %-8.1f %-7.2f\n",
           gaps[i], 1000.0/gaps[i], ra, oa, rb, ob);
  }
  printf("\n  A's rate is proportional to speed, but its ROUGHNESS changes a lot: a slow roll is a\n");
  printf("  gappy trickle and only becomes even once the windows overlap. B rises smoothly and is\n");
  printf("  already even at slow speeds, because it keeps a fixed amount per notch.\n");
  return 0;
}
