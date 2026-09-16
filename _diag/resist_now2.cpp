#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;
static double slowMove(double R){ double v=kNotchDeltas*(1.0-R/10.0); return v<1.0?1.0:v; }
int main(){
  printf("  %-8s %-14s %-10s %s\n","阻力","慢转走(delta)","占一格","说明");
  struct P{double R;const char*n;};
  P ps[]={{10.0,"<- 你现在存的"}, {9.0,"<- 默认/§51验收值"}, {8.0,""}, {5.0,""}, {0.0,""}};
  for(auto&p:ps){
    double d=slowMove(p.R);
    printf("  %-8.1f %-14.1f %-10.1f%% %s\n",p.R,d,100.0*d/120.0,p.n);
  }
  return 0;
}
