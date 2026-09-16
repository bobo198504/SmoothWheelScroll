// FOLLOW-UP: (1) is A's device-dependence just the `snap` floor?  (2) a clean pause timeline.
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kUnitsPerNotch = 15.0;
static double unitsOf(double d) { return d * kUnitsPerNotch / 120.0; }
static const double kDt = 0.0005;

// ---- B (momentum), faithful §51 ----
static double B_Keep(double gapSec, double recv, double kin) {
  double sf = kin / 120.0; if (sf < 0) sf = 0; if (sf > 1) sf = 1;
  if (sf >= 1.0) return 1.0;
  if (gapSec < 0) return sf;
  if (recv <= 0) return sf;
  const double slowRef = (recv / 120.0) * 0.200;
  if (!(slowRef > 0)) return 1.0;
  const double x = gapSec / slowRef;
  const double ff = 0.050 / 0.200;
  if (x >= 1.0) return sf;
  if (x <= ff) return 1.0;
  return sf + (1.0 - sf) * ((1.0 - x) / (1.0 - ff));
}
struct GlideB {
  static const int kMax = 8192; double amt[kMax], age[kMax]; int n; double due;
  void Reset(){n=0;due=0;}
  void Feed(double a,double d){ if(a==0)return; if(d<=0){due+=a;return;} if(n<kMax){amt[n]=a;age[n]=0;++n;}else due+=a; }
  double Tick(double dt,double d){
    if(dt<=0)return 0; double out=due; due=0; if(d<=0){n=0;return out;} int w=0;
    for(int i=0;i<n;++i){ double left=d-age[i]; double pay=amt[i]; if(left>dt)pay=amt[i]*(dt/left);
      out+=pay; double keep=amt[i]-pay; age[i]+=dt; if(age[i]<d&&fabs(keep)>1e-9){amt[w]=keep;age[w]=age[i];++w;} }
    n=w; return out; }
  bool Active()const{return n>0||due!=0.0;}
};

// ---- A with a settable snap ----
static double runA(double R, double Dms, double total, double Tsec, double per, double snapDeltas) {
  anim3::Params P; P.durationMs=Dms; P.takePerSec=(R*1000.0)/8.0; P.snap=snapDeltas/8.0;
  anim3::Glide g; g.Reset();
  const double d=Dms/1000.0, gap=Tsec/(total/per), amt=unitsOf(per);
  double out=0,fed=0,next=0;
  for(double t=0;t<Tsec;t+=kDt){ while(next<=t+1e-12){g.Feed(amt,P);fed+=amt;next+=gap;} out+=g.Tick(kDt,P); }
  for(double t=0;t<8.0&&g.Active();t+=kDt) out+=g.Tick(kDt,P);
  return 100.0*(1.0-out/fed);
}

int main(){
  printf("=== 1. A 的设备相关性：是 snap 造成的吗？(手速 400 delta/s, 精细度 500ms) ===\n\n");
  printf("   %-22s %14s %14s\n","设备","A snap=0.5delta","A snap=0");
  const double devs[]={120,60,20,8,4,1};
  const char* nm[]={"120 (普通鼠标)","60","20 (无级鼠标)","8","4","1 (最细)"};
  for(int i=0;i<6;++i){
    printf("   %-22s %13.1f%% %13.1f%%\n", nm[i],
           runA(1.0,500,1200,3.0,devs[i],0.5), runA(1.0,500,1200,3.0,devs[i],0.0));
  }

  printf("\n=== 2. 顿挫时间线：连滚中卡 200ms，看输出（每 50ms 一段）===\n\n");
  // 10 events at 120 ms (0..1.08s), pause 200 ms (1.08..1.28), 10 more at 120 ms.
  const double times0[20] = {0,.12,.24,.36,.48,.60,.72,.84,.96,1.08,
                             1.28,1.40,1.52,1.64,1.76,1.88,2.00,2.12,2.24,2.36};
  for(int mech=0; mech<2; ++mech){
    anim3::Glide ga; ga.Reset();
    anim3::Params P; P.durationMs=500; P.takePerSec=(1.0*1000.0)/8.0; P.snap=(1.0/8.0)*0.5;
    GlideB gb; gb.Reset();
    double lastT=-1; int fed=0; const double amt=unitsOf(120);
    printf("   %s 的输出(每 50ms 交付的单位):\n     ", mech==0?"A 来不及吃":"B 角动量  ");
    for(double t0=0; t0<2.5; t0+=0.05){
      double bucket=0;
      for(double tt=t0; tt<t0+0.05; tt+=kDt){
        while(fed<20 && times0[fed]<=tt+1e-12){
          if(mech==0) ga.Feed(amt,P);
          else { double gp=(lastT<0)?-1.0:(times0[fed]-lastT); gb.Feed(amt*B_Keep(gp,120,12),0.5); lastT=times0[fed]; }
          ++fed;
        }
        bucket += (mech==0)? ga.Tick(kDt,P) : gb.Tick(kDt,0.5);
      }
      printf("%6.2f", bucket);
    }
    printf("\n     (t=0..2.5s，每格 6 字符；卡顿在 1.08~1.28s，即第 22~26 个格子)\n\n");
  }

  printf("=== 3. 一次孤立慢转，用户中途松手（一条消息后无后续）===\n\n");
  for(int mech=0;mech<2;++mech){
    if(mech==0){
      anim3::Params P; P.durationMs=500; P.takePerSec=(1.0*1000.0)/8.0; P.snap=(1.0/8.0)*0.5;
      anim3::Glide g; g.Reset(); g.Feed(unitsOf(120),P);
      double out=0; for(double t=0;t<3.0&&g.Active();t+=kDt) out+=g.Tick(kDt,P);
      printf("   A：付 %6.4f 单位（收到 %.4f）\n", out, unitsOf(120));
    } else {
      GlideB g; g.Reset(); g.Feed(unitsOf(120)*B_Keep(-1.0,120,12),0.5);
      double out=0; for(double t=0;t<3.0&&g.Active();t+=kDt) out+=g.Tick(kDt,0.5);
      printf("   B：付 %6.4f 单位（收到 %.4f）\n", out, unitsOf(120));
    }
  }
  return 0;
}
