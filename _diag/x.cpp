#include "anim3_core.h"
#include <cstdio>
using namespace anim3;
int main(){
  printf("真代码 EatFraction 的结果（notched 120/message, kinetic=12）:\n");
  const double sp[]={150,300,600,900,1200,2400,4800};
  for(int i=0;i<7;i++){
    const double rate=sp[i], gap=120.0/rate;
    printf("  %6.0f delta/s (1 notch / %4.0f ms): eats %5.1f%%\n", rate, gap*1000,
           100*EatFraction(gap,120.0,12.0));
  }
}
