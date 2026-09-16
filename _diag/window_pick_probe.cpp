// 窗口大小对比：EMA 窗口 N 取多少，"稳"与"跟手"兼顾最好？
//   稳：慢轮手抖时行程的相对波动（越小越好）
//   跟手：加速时行程涨到 60 delta 需要的格数（越小越好；理想最小）
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kDPN = 120.0;
struct Rng { unsigned s; double next(){ s=s*1103515245u+12345u; return ((s>>16)%1001)/1000.0; } };

static std::vector<double> 造间隔(int n,double g0,double g1,double jit){
  Rng r{777u}; std::vector<double> g(n);
  for(int i=0;i<n;i++){ double base=g0+(g1-g0)*i/(double)(n-1); g[i]=base+(r.next()*2-1)*jit; }
  return g;
}

static std::vector<double> 平滑(const std::vector<double>& gaps,double N){
  const int n=(int)gaps.size(); std::vector<double> e(n,0); double ema=-1;
  for(int i=0;i<n;i++){ double sec=(i==0)?gaps[0]/1000.0:gaps[i]/1000.0;
    if(i==0){ema=sec;} else if(N>0){ ema += (sec-ema)/N; sec=ema; }
    e[i]=sec; }
  return e;
}

// 慢轮抖动：固定间隔 200ms + 抖动，看行程波动
static void 抖动测试(double N,double* mean,double* relsd){
  Rng r{99u}; const int n=40; double sum=0,s2=0; int k=0;
  model::GapSmoother sm; sm.Reset();
  for(int i=0;i<n;i++){
    const double gap=(200.0+(r.next()*2-1)*30.0)/1000.0;
    const double g=sm.Feed(i==0?-1.0:gap);
    const double v=model::TravelScale(g,kDPN,1.0)*kDPN;
    if(i<3) continue; sum+=v; s2+=v*v; ++k;
  }
  (void)N;
  *mean=sum/k; *relsd=std::sqrt(s2/k-(sum/k)*(sum/k))/(sum/k);
}

static int 滞后(double N){
  const int n=34; const std::vector<double> gaps=造间隔(n,220,55,30);
  // 用临时改窗口不方便，这里直接内联一个参数化 EMA
  std::vector<double> v(n); double ema=-1;
  for(int i=0;i<n;i++){ double sec=(i==0)?-1.0:gaps[i]/1000.0;
    if(i>0){ if(ema<0)ema=sec; else ema+=(sec-ema)/N; sec=ema; }
    v[i]=model::TravelScale(sec,kDPN,1.0)*kDPN; }
  for(int i=0;i<n;i++) if(v[i]>=60.0) return i+1;
  return n;
}

int main(){
  printf("窗口  |  慢区均值  慢区相对波动  |  到60delta第几格\n");
  printf("------+-------------------------+-----------------\n");
  for(double N : {1.0,2.0,3.0,4.0,6.0}){
    double mean=0,rs=0; 抖动测试(N,&mean,&rs);
    printf("  %-4.0f |  %7.1f   %8.2f      |   %d\n",N,mean,rs,滞后(N));
  }
  printf("\n(N=1 相当于几乎不平滑；看哪一行两者都合适)\n");
  return 0;
}
