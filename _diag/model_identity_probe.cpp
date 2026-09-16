// 模型一致性探针：当前 core 的每帧输出，与"已批准的窗口模型"逐帧对比。
//
// 已批准模型（见 _hist/model3_approved 的 Tick）每帧付给窗口：
//     付 = 窗口剩余量 × (dt / 窗口总时长)      —— 每帧一个"固定"份额
// 当前 core 付的是：
//     付 = 窗口剩余量 × (dt / 窗口剩余时间)    —— 按剩余时间比例
//
// 两者应当**逐帧完全一致**：因为"剩余量"和"剩余时间"同步缩小，比值恒定。
// 这个探针照抄已批准版的逐帧算法（不是重新推导），所以结论可信。
//
// 中文说明，结论用数字说话。
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double 单格单位 = 15.0;   // 一格 = 15 个 7 位单位
static const double 每格delta = 120.0;
static const double 每单位delta = 8.0;

// ── 当前 core：喂一格，逐帧记录输出，直到排空 ──
static std::vector<double> 当前模型(double X毫秒)
{
  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = X毫秒;
  P.eatFrac = 0.0;
  P.spitMul = 1.0;
  g.Feed(单格单位, P);

  std::vector<double> 帧;
  const double dt = 0.001;                 // 1ms 一帧
  const int 最大帧数 = (int)(X毫秒 / 1.0) + 5; // 窗口走完 X 毫秒就结束，留 5 帧余量
  for (int i = 0; i < 最大帧数; ++i)
  {
    const double 出 = g.Tick(dt, P) * 每单位delta;
    帧.push_back(出);
    if (!g.Active())
      break;
  }
  return 帧;
}

// ── 已批准模型：照抄 _hist/model3_approved/anim3_core.h 的 Tick ──
//   ⚠️ 关键：已批准版里 `amt_[i]`（窗口量）**从不递减**，它只累加 `age_[i]`。
//   每帧付：窗口量 × (dt / 时长)，用"剩余比例"封顶最后一帧。
//   总共 d/dt 帧，恰好付完。
static std::vector<double> 已批准模型(double X毫秒)
{
  const double 时长 = X毫秒 / 1000.0;
  const double dt = 0.001;
  const double 窗口量 = 单格单位;   // 模型单位；注意：**不递减**（照抄源码）
  double 窗龄 = 0.0;

  std::vector<double> 帧;
  while (窗龄 < 时长 && 帧.size() < 100000)
  {
    const double 份额 = dt / 时长;             // 固定份额
    const double 余 = 1.0 - 窗龄 / 时长;       // 窗口还剩多少比例
    const double 付比 = (份额 < 余) ? 份额 : 余;
    帧.push_back(窗口量 * 付比 * 每单位delta); // 窗口量不减
    窗龄 += dt;
  }
  return 帧;
}

int main()
{
  printf("逐帧对比：当前 core  vs  已批准窗口模型   （1ms 一帧，单格 120 delta）\n\n");
  bool 全部一致 = true;
  for (double X : {100.0, 150.0, 300.0})
  {
    const std::vector<double> A = 当前模型(X);
    const std::vector<double> B = 已批准模型(X);

    double 最大差 = 0.0, 总A = 0.0, 总B = 0.0;
    const int n = (int)((A.size() < B.size()) ? A.size() : B.size());
    for (int i = 0; i < n; ++i)
    {
      最大差 = std::fmax(最大差, std::fabs(A[i] - B[i]));
      总A += A[i];
      总B += B[i];
    }
    for (int i = n; i < (int)A.size(); ++i) 总A += A[i];
    for (int i = n; i < (int)B.size(); ++i) 总B += B[i];

    const bool 一致 = (最大差 < 1e-9) && (std::fabs(总A - 总B) < 1e-9);
    if (!一致)
      全部一致 = false;

    printf("X=%.0f ms：当前 %d 帧，已批准 %d 帧\n", X, (int)A.size(), (int)B.size());
    printf("   逐帧最大差 = %.12f delta\n", 最大差);
    printf("   总量：当前 %.6f  |  已批准 %.6f  |  差 %.6f\n", 总A, 总B, 总A - 总B);
    printf("   判定：%s\n\n", 一致 ? "逐帧一致 —— 模型的时间线没有被改动" : "不一致 —— 模型确实变了");
  }
  printf("%s\n", 全部一致 ? "结论：模型时间线与已批准的窗口模型完全相同。"
                          : "结论：模型时间线已被改动，需要回退。");
  return 全部一致 ? 0 : 1;
}
