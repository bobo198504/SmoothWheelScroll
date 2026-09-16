// eat = 90%, V = 1.0 (no acceleration). A SLOW, steady roll. What does the output look like over
// time? The suspect: with 90% eaten, each notch contributes only 10%, so a SLOW roll's output is
// tiny and SPIKY (it arrives in 12-delta lumps per notch), and a slightly faster roll stops looking
// spiky -- "slow, then suddenly it moves".
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
static const double kU=15.0, kDPN=120.0;
// One steady roll: n notches at gapMs, eatFrac, X. Prints the per-50ms output.
static void Run(const char *what, int n, double gapMs, double eatFrac, double X){
  model::Params P; P.windowMs=X; P.eatFrac=eatFrac; P.spitMul=1.0;
  model::Axis g; g.Reset();
  const double dt=0.001, gap=gapMs/1000.0;
  double next=0, out=0; int fed=0; double win=0;
  printf("  %-22s (%.0f%% eaten, X=%.0f ms, %d notches @%.0f ms)\n", what, 100*eatFrac, X, n, gapMs);
  printf("     per 50 ms: ");
  for(double t=0;t<60.0;t+=dt){
    while(fed<n && next<=t+1e-12){ g.Feed(kU,P); ++fed; next+=gap; }
    out += g.Tick(dt,P);
    if(t-win>=0.05){ printf("%6.1f", out*kDPN/kU); out=0; win=t; }
    if(fed>=n && !g.Active()) break;
  }
  printf("\n\n");
}
int main(){
  printf("V = 1.0 (no acceleration). Each notch hands over only 120*(1-eat).\n\n");
  Run("eat 0% (for reference)", 20, 200.0, 0.0, 150.0);
  Run("eat 90%, slow", 20, 200.0, 0.90, 150.0);
  Run("eat 90%, medium", 20, 100.0, 0.90, 150.0);
  Run("eat 90%, fast", 20, 40.0, 0.90, 150.0);
  printf("  With 90%% eaten a notch is worth 12 deltas. At 200 ms/notch that is 60 deltas/s, so a\n");
  printf("  50 ms window sees ~3 deltas; the lumps are the notches arriving. As the hand speeds up\n");
  printf("  the SAME 60 deltas/s window fills much faster, so it reads as 'suddenly moving'.\n");
  return 0;
}
