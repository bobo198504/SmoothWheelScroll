// VERIFY the shipped build (window + angular-momentum resistance), using the plugin's OWN
// src/anim3_core.h and the plugin's own slider->deltas mapping (SlowMoveDeltas, resistance/10).
//
//   g++ -std=c++17 -O2 -I../src -o am_verify.exe am_verify.cpp
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;

static const double kU = 15.0;              // 7-bit units per notch
static double unitsOf(double d) { return d * kU / anim3::kNotchDeltas; }
static double slowMove(double R) { double v = anim3::kNotchDeltas * (1.0 - R/10.0); return v<1.0?1.0:v; }
static const double kDt = 0.0005;

// One gesture: totalDeltas of hand motion over Tsec, events of `per` deltas. Fineness Dms.
struct Out { double fed, delivered; };
static Out run(double R, double Dms, double totalDeltas, double Tsec, double per) {
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double slow = slowMove(R);
  const double gap = Tsec / (totalDeltas/per);
  const double amt = unitsOf(per);
  double out=0, fed=0, next=0, lastT=-1;
  for (double t=0; t<Tsec; t+=kDt) {
    while (next <= t+1e-12) {
      const double gp = (lastT<0) ? -1.0 : (t-lastT);
      const double keep = TravelFraction(gp, per, slow);   // note: per = this message's deltas
      g.Feed(amt*keep, P);
      fed += amt; lastT = t; next += gap;
    }
    out += g.Tick(kDt, P);
  }
  for (double t=0; t<8.0 && g.Active(); t+=kDt) out += g.Tick(kDt, P);
  return {fed, out};
}
static double eatenPct(const Out&o){ return 100.0*(1.0-o.delivered/o.fed); }

int main(){
  printf("A. 设备无关性（阻力 9.0 = 默认，精细度 500ms）\n");
  printf("   同一段手部运动：3 秒内 1200 delta = 400 delta/s\n\n");
  printf("   %-30s %10s %12s %10s\n","设备（每条 delta）","吃掉","通过(单位)","相对普通");
  double base=0;
  const double devs[]={120,60,20,8,4,1};
  const char*nm[]={"普通鼠标 1 格=120","60/条","无级鼠标 20/条","8/条","4/条","1/条（最细）"};
  for(int i=0;i<6;++i){
    Out o=run(9.0,500,1200,3.0,devs[i]);
    if(i==0) base=o.delivered;
    printf("   %-30s %9.1f%% %12.4f %9.2fx\n",nm[i],eatenPct(o),o.delivered,base>0?o.delivered/base:0);
  }
  printf("   -> 各档通过量应当一致（比值≈1.00）\n");

  printf("\nB. 快慢响应（普通鼠标 120/条，阻力 9.0，精细度 500ms）\n\n");
  printf("   %-26s %10s %12s\n","手速","吃掉","通过(单位)");
  for(double sp:{60.0,120.0,300.0,600.0,1200.0,2400.0}){
    Out o=run(9.0,500,sp*2.0,2.0,120);
    char lab[64]; snprintf(lab,sizeof(lab),"%.0f delta/s (%.1f 格/s)",sp,sp/120.0);
    printf("   %-26s %9.1f%% %12.4f\n",lab,eatenPct(o),o.delivered);
  }

  printf("\nC. 滑块刻度 -> 慢转一格走多远（应单调，且 9.0→12, 10.0→1）\n\n");
  printf("   %-8s %-16s %-16s\n","阻力","慢转走(delta)","慢转保留");
  for(double R:{0.0,2.5,5.0,7.5,9.0,9.9,10.0}){
    printf("   %-8.1f %-16.1f %.1f%%\n",R,slowMove(R),100.0*slowMove(R)/120.0);
  }

  printf("\nD. 窗口精确性（阻力 0 = 不吃；精细度=设定值）\n\n");
  for(double D:{50.0,100.0,300.0,500.0}){
    // R=0 => slowMove returns 120 => keep=1 always
    Params P; P.durationMs=D; Glide g; g.Reset(); g.Feed(kU,P);
    double dt=0.001,out=0,last=0; int calls=0;
    for(double t=0;t<10.0;t+=dt){ double s=g.Tick(dt,P); if(s!=0){out+=s;last=(t+dt)*1000;++calls;} if(!g.Active())break; }
    printf("   精细度 %5.0f ms -> 停 %7.1f ms  通过 %7.4f (%.2f%%)  %4d 次\n",
           D,last,out,100.0*out/kU,calls);
  }

  printf("\nE. 守恒：会不会多给？\n\n");
  double worst=0;
  for(double R:{0.0,5.0,9.0,10.0}) for(double per:{120.0,20.0,1.0}) for(double sp:{60.0,600.0,2400.0}){
    Out o=run(R,500,sp*2.0,2.0,per); if(o.delivered/o.fed>worst) worst=o.delivered/o.fed;
  }
  printf("   最大 交付/收到 = %.4f  %s\n",worst,worst<=1.0001?"OK（永不多给）":"超发！");
  return 0;
}
