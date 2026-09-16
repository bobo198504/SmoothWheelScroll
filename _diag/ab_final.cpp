// FINAL: quantify the two mechanisms on the axes that decide it.
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kUnitsPerNotch = 15.0;
static double unitsOf(double d) { return d * kUnitsPerNotch / 120.0; }
static const double kDt = 0.0005;

static double B_Keep(double gapSec, double recv, double kin) {
  double sf = kin/120.0; if (sf<0) sf=0; if (sf>1) sf=1;
  if (sf>=1.0) return 1.0;
  if (gapSec<0) return sf;
  if (recv<=0) return sf;
  const double sr=(recv/120.0)*0.200; if(!(sr>0)) return 1.0;
  const double x=gapSec/sr, ff=0.050/0.200;
  if (x>=1.0) return sf; if (x<=ff) return 1.0;
  return sf+(1.0-sf)*((1.0-x)/(1.0-ff));
}
struct GlideB {
  static const int kMax=8192; double amt[kMax],age[kMax]; int n; double due;
  void Reset(){n=0;due=0;}
  void Feed(double a,double d){ if(a==0)return; if(d<=0){due+=a;return;} if(n<kMax){amt[n]=a;age[n]=0;++n;}else due+=a; }
  double Tick(double dt,double d){ if(dt<=0)return 0; double out=due; due=0; if(d<=0){n=0;return out;} int w=0;
    for(int i=0;i<n;++i){ double L=d-age[i]; double p=amt[i]; if(L>dt)p=amt[i]*(dt/L); out+=p; double k=amt[i]-p; age[i]+=dt;
      if(age[i]<d&&fabs(k)>1e-9){amt[w]=k;age[w]=age[i];++w;} } n=w; return out; }
  bool Active()const{return n>0||due!=0.0;}
};

// steady roll of n notches at gapMs; returns per-`bucketMs` series (units) into out[], returns count
static int seriesA(double R,double Dms,int n,double gapMs,double *out,int maxN,double bucketMs){
  anim3::Params P; P.durationMs=Dms; P.takePerSec=(R*1000.0)/8.0; P.snap=(1.0/8.0)*0.5;
  anim3::Glide g; g.Reset();
  double next=0; int fed=0; const double amt=unitsOf(120), T=n*gapMs/1000.0;
  double acc=0; int k=0; double bt=0;
  for(double t=0;t<T+Dms/1000.0+1.0;t+=kDt){
    while(fed<n&&next<=t+1e-12){g.Feed(amt,P);++fed;next+=gapMs/1000.0;}
    acc+=g.Tick(kDt,P); bt+=kDt;
    if(bt>=bucketMs/1000.0-1e-12){ if(k<maxN)out[k++]=acc; acc=0; bt=0; }
  }
  return k;
}
static int seriesB(double K,double Dms,int n,double gapMs,double *out,int maxN,double bucketMs){
  GlideB g; g.Reset();
  double next=0,lastT=-1; int fed=0; const double amt=unitsOf(120), T=n*gapMs/1000.0;
  double acc=0; int k=0; double bt=0;
  for(double t=0;t<T+Dms/1000.0+1.0;t+=kDt){
    while(fed<n&&next<=t+1e-12){ double gp=(lastT<0)?-1.0:(t-lastT); g.Feed(amt*B_Keep(gp,120,K),Dms/1000.0); lastT=t; ++fed; next+=gapMs/1000.0; }
    acc+=g.Tick(kDt,Dms/1000.0); bt+=kDt;
    if(bt>=bucketMs/1000.0-1e-12){ if(k<maxN)out[k++]=acc; acc=0; bt=0; }
  }
  return k;
}

static void stats(const double*s,int n,int skipHead,int skipTail,const char*tag){
  double mn=1e9,mx=-1e9,sum=0; int c=0;
  for(int i=skipHead;i<n-skipTail;++i){ double v=s[i]; if(v<mn)mn=v; if(v>mx)mx=v; sum+=v; ++c; }
  const double mean=c?sum/c:0;
  double var=0; for(int i=skipHead;i<n-skipTail;++i){double dd=s[i]-mean;var+=dd*dd;} var=c?var/c:0;
  const double cv=mean>0?sqrt(var)/mean:0;
  printf("   %-14s 每20ms交付: 最小 %.3f 最大 %.3f 均值 %.3f  波动CV %.2f  (峰谷比 %.2f)\n",
         tag, mn, mx, mean, cv, mn>0?mx/mn:0);
}

int main(){
  static double s[600];
  printf("=== 稳态连滚 20 格 @120ms，精细度 500ms，每 20ms 看一次交付 =========\n");
  printf("   （目标是'马达般的匀速'：CV 越小越平滑）\n\n");
  int n=seriesA(1.0,500,20,120.0,s,600,20.0); stats(s,n,15,45,"A 来不及吃");
  n=seriesB(12.0,500,20,120.0,s,600,20.0); stats(s,n,15,45,"B 角动量");

  printf("\n=== 同一段，换成慢速 20 格 @400ms（用户日常慢滚）=========\n\n");
  n=seriesA(1.0,500,20,400.0,s,600,20.0); stats(s,n,15,40,"A 来不及吃");
  n=seriesB(12.0,500,20,400.0,s,600,20.0); stats(s,n,15,40,"B 角动量");

  printf("\n=== 一台设备最细（1 delta/条）vs 普通（120/条）：总通过量的比 ===\n\n");
  // reuse runA logic via series sums
  for(int mech=0;mech<2;++mech){
    double lo=0, hi=0;
    for(int pass=0;pass<2;++pass){
      const double per = pass==0?120.0:1.0;
      const double total=1200, T=3.0, gap=T/(total/per), amt=unitsOf(per);
      double out=0;
      if(mech==0){ anim3::Params P;P.durationMs=500;P.takePerSec=(1.0*1000.0)/8.0;P.snap=(1.0/8.0)*0.5;
        anim3::Glide g;g.Reset(); double nx=0,fed=0;
        for(double t=0;t<T;t+=kDt){ while(fed<total/per&&nx<=t+1e-12){g.Feed(amt,P);++fed;nx+=gap;} out+=g.Tick(kDt,P);} 
        for(double t=0;t<8&&g.Active();t+=kDt) out+=g.Tick(kDt,P); }
      else { GlideB g;g.Reset(); double nx=0,lastT=-1,fed=0;
        for(double t=0;t<T;t+=kDt){ while(fed<total/per&&nx<=t+1e-12){ double gp=(lastT<0)?-1.0:(t-lastT); g.Feed(amt*B_Keep(gp,per,12.0),0.5); lastT=t; ++fed; nx+=gap;} out+=g.Tick(kDt,0.5);} 
        for(double t=0;t<8&&g.Active();t+=kDt) out+=g.Tick(kDt,0.5); }
      if(pass==0) hi=out; else lo=out;
    }
    printf("   %-14s 普通通过 %.3f | 最细通过 %.3f | 比值 %.2f  %s\n",
           mech==0?"A 来不及吃":"B 角动量", hi, lo, lo>0?hi/lo:0,
           (fabs(hi-lo)/hi<0.05)?"<= 基本一致":"<= 差很多（设备不通用）");
  }
  return 0;
}
