#!/usr/bin/env bash
# Compare the CURRENT model against the frozen 1.0.0 baseline numbers.
# The baseline is the accepted feel; any drift must be intentional and explained.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

cat > "$ROOT/build/_baseline.cpp" <<'CPP'
#include "../src/anim_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim;
static const double START=15,ACCEL=3.6,REL=100,U=15.0;   // frozen 1.0.0 params
static void Run(double gap,int N,double *tot,double *pk){
  Params P;P.startPct=START;P.accelPct=ACCEL;P.releaseMs=REL;
  P.onsetRatio=0.8;P.movingOnsetMs=0.0;   // 1.0.0 的渐入设置
  Glide g;double dt=0.0156,t=0,next=0;int fired=0;double p=0;
  for(int i=0;i<40000000;++i){
    if(fired<N&&t+1e-12>=next){g.Kick(U,+1.0,989,t,P);fired++;next+=gap;}
    g.Tick(dt,P);t+=dt;double v=std::fabs(g.Velocity());if(v>p)p=v;
    if(fired>=N&&!g.Active())break;if(t>gap*N+20)break;
  }
  *tot=g.Total();*pk=p;
}
int main(){
  struct C{double gap;int N;const char*n;double bt,bp;};
  C cs[]={
    {0.060,1,"single",1.89,17.94},
    {0.250,10,"10@250",47.85,201.14},
    {0.120,10,"10@120",55.78,220.14},
    {0.060,10,"10@60",121.50,417.27},
    {0.033,10,"10@33",239.93,1021.88},
  };
  int bad=0;
  printf("  %-8s %10s %10s %7s | %9s %9s\n","op","total","base","diff","peak","base");
  for(auto&c:cs){
    double tot,pk;Run(c.gap,c.N,&tot,&pk);
    double d=tot-c.bt;
    bool ok=std::fabs(d)<0.02 && std::fabs(pk-c.bp)<0.5;
    if(!ok)++bad;
    printf("  %-8s %10.2f %10.2f %+7.2f | %9.2f %9.2f  %s\n",
           c.n,tot,c.bt,d,pk,c.bp,ok?"OK":"!! DRIFT");
  }
  printf("\n%s\n", bad? "!! 与 1.0.0 基准不一致，需确认是有意改动。"
                      : "一致：与 1.0.0 基准相同。");
  return bad?1:0;
}
CPP
g++ -std=c++17 -O2 -o "$ROOT/build/_baseline.exe" "$ROOT/build/_baseline.cpp"
"$ROOT/build/_baseline.exe"
rm -f "$ROOT/build/_baseline.cpp" "$ROOT/build/_baseline.exe"
