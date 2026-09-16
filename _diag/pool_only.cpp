#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static const double kU = 15.0;
int main(){
  printf("== POOL (one total, paid out per frame at dt/d) ==\n");
  printf("   promise: motion lasts exactly the setting, total 15.0000\n\n");
  for (double D : {10.0,50.0,100.0,300.0,500.0}) {
    Params P; P.durationMs=D; P.takePerSec=0.0; P.snap=0.0;
    Glide g; g.Reset(); g.Feed(kU);
    double dt=0.001,total=0,last=0,maxStep=0; int calls=0;
    for(double t=0;t<20.0;t+=dt){ double s=g.Tick(dt,P);
      if(s!=0.0){total+=s;last=(t+dt)*1000.0;++calls;if(fabs(s)>maxStep)maxStep=fabs(s);}
      if(!g.Active())break; }
    printf("   Fineness %5.0f ms -> motion over at %7.1f ms   total %8.4f   %5d calls   max step %.4f\n",D,last,total,calls,maxStep);
  }
  return 0;
}
