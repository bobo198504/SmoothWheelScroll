// Per-100ms delivery profile for ONE notch at Fineness 500 ms, 1 ms frames.
// Shows why the pool's tail "stutters": it is front-loaded and crawls.
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <string>
using namespace anim3;
static const double kU = 15.0;
int main(){
  Params P; P.durationMs = 500.0;
#ifdef POOL
  P.takePerSec = 0.0; P.snap = 0.0;
  printf("== POOL  500ms: how much of the notch arrives in each 100 ms ==\n");
  Glide g; g.Reset(); g.Feed(kU);
#else
  printf("== WINDOW 500ms: how much of the notch arrives in each 100 ms ==\n");
  Glide g; g.Reset(); g.Feed(kU, P);
#endif
  const double dt=0.001; double bucket=0, tot=0; int seg=0;
  for(double t=0;t<20.0;t+=dt){
    double s=g.Tick(dt,P); bucket+=fabs(s); tot+=fabs(s);
    if(fabs(fmod(t+dt,0.1))<1e-9){
      if(seg<6) printf("   %4d-%4d ms : %7.4f units  %s\n", seg*100, (seg+1)*100, bucket,
                       bucket>0?std::string(bucket/0.03,'#').c_str():"");
      bucket=0; ++seg;
    }
    if(!g.Active())break;
  }
  printf("   total after 20 s = %.4f units\n", tot);
  return 0;
}
