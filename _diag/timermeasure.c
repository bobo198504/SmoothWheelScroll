/* Read-only measurement: what interval do the two timer kinds ACTUALLY give on this
 * machine, for the periods the plugin uses? Nothing here touches REAPER.
 *
 *   gcc -O2 -o timermeasure.exe timermeasure.c -lwinmm
 *
 * Phase 1: timeBeginPeriod(1) + SetTimer(nullptr,0,5,Proc)  -- the shipped kFastTimer=false path.
 * Phase 2: timeBeginPeriod(1) + timeSetEvent(4,1,Proc,TIME_PERIODIC) -- the 240Hz path.
 *
 * A plain Win32 message loop, so this is exactly what the plugin sees (no REAPER involved).
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>

static LARGE_INTEGER g_freq;
static long long g_last;
static long long g_gaps[4000];
static int g_n;
static int g_target;

static long long usec(void)
{
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return (long long)((t.QuadPart * 1000000LL) / g_freq.QuadPart);
}

static void note(void)
{
  long long t = usec();
  if (g_last && g_n < g_target)
    g_gaps[g_n++] = t - g_last;
  g_last = t;
}

static void CALLBACK ThreadProc(HWND, UINT, UINT_PTR, DWORD) { note(); }
static void CALLBACK MmProc(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) { note(); }

static int cmp(const void *a, const void *b)
{
  long long x = *(const long long *)a, y = *(const long long *)b;
  return (x > y) - (x < y);
}

/* Pump without ever blocking: GetMessage would sit forever if a callback kind posts
 * nothing retrievable, which would hang the harness rather than report "no samples". */
static void pump(void)
{
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  Sleep(1);
}

static void report(const char *label, int wantMs, DWORD elapsedMs)
{
  if (g_n < 2) { printf("%-34s : no samples\n", label); return; }
  long long *g = (long long *)malloc(sizeof(long long) * g_n);
  memcpy(g, g_gaps, sizeof(long long) * g_n);
  qsort(g, g_n, sizeof(long long), cmp);
  long long sum = 0, mx = g[g_n - 1];
  for (int i = 0; i < g_n; ++i) sum += g[i];
  printf("%-34s : n=%4d  mean=%6.2f ms  median=%6.2f ms  p95=%6.2f ms  max=%6.2f ms\n",
         label, g_n, (double)sum / g_n / 1000.0, (double)g[g_n / 2] / 1000.0,
         (double)g[(int)(g_n * 0.95)] / 1000.0, (double)mx / 1000.0);
  printf("%-34s   -> effective rate %.1f Hz (requested %d ms = %.1f Hz), elapsed %lu ms\n",
         "", 1000000.0 / ((double)sum / g_n), wantMs, 1000.0 / wantMs,
         (unsigned long)elapsedMs);
  free(g);
}

int main(void)
{
  QueryPerformanceFrequency(&g_freq);
  printf("QPC freq = %lld\n\n", (long long)g_freq.QuadPart);
  timeBeginPeriod(1);

  /* ---- Phase 1: shipped path, plain thread timer ---- */
  memset(g_gaps, 0, sizeof(g_gaps));
  g_n = 0; g_last = 0; g_target = 200;
  UINT_PTR t1 = SetTimer(NULL, 0, 5, ThreadProc);
  DWORD t0 = GetTickCount();
  MSG msg;
  while (g_n < g_target && GetTickCount() - t0 < 6000)
  {
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    Sleep(1);
  }
  report("SetTimer 5ms (shipped kFastTimer=0)", 5, GetTickCount() - t0);
  KillTimer(NULL, t1);

  /* ---- Phase 2: 240Hz path, multimedia timer ---- */
  memset(g_gaps, 0, sizeof(g_gaps));
  g_n = 0; g_last = 0; g_target = 400;
  HANDLE mm = (HANDLE)(UINT_PTR)timeSetEvent(4, 1, MmProc, 0, TIME_PERIODIC);
  t0 = GetTickCount();
  while (g_n < g_target && GetTickCount() - t0 < 6000)
  {
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    Sleep(1);
  }
  report("timeSetEvent 4ms (kFastTimer=1, 240Hz)", 4, GetTickCount() - t0);
  if (mm) timeKillEvent((UINT)(UINT_PTR)mm);

  /* ---- Phase 3: 1ms multimedia (what AGENTS.md 7 claims is shipped) ---- */
  memset(g_gaps, 0, sizeof(g_gaps));
  g_n = 0; g_last = 0; g_target = 900;
  mm = (HANDLE)(UINT_PTR)timeSetEvent(1, 1, MmProc, 0, TIME_PERIODIC);
  t0 = GetTickCount();
  while (g_n < g_target && GetTickCount() - t0 < 6000)
  {
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    Sleep(1);
  }
  report("timeSetEvent 1ms (AGENTS.md 7 claim)", 1, GetTickCount() - t0);
  if (mm) timeKillEvent((UINT)(UINT_PTR)mm);

  timeEndPeriod(1);
  return 0;
}
