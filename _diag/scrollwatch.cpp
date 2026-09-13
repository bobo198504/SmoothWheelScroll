// scrollwatch -- measure where the MIDI editor's note area moves over time during one
// wheel notch, by correlating pixel rows between consecutive samples.
//
// The MIDI window reports no scrollbar position, so this is done purely from screen
// pixels: watch a narrow vertical strip of the note area and find the vertical shift
// that best matches the previous sample. It prints the shift after every sample, so
// a forward move followed by a backward move (the reported "rebound") is visible as a
// sign change in the displacement series.
//
// Read-only: it only BitBlt's from the screen.
//
// Usage: scrollwatch.exe <L> <T> <R> <B> <wheelX> <wheelY> [down|up] [samples] [ms]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_w, g_h;

static unsigned char *grab(int L, int T, int R, int B)
{
  g_w = R - L; g_h = B - T;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateCompatibleBitmap(screen, g_w, g_h);
  HGDIOBJ old = SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, g_w, g_h, screen, L, T, SRCCOPY);
  BITMAPINFO bi = {0};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = g_w;
  bi.bmiHeader.biHeight = -g_h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  unsigned char *buf = (unsigned char *)malloc(g_w * g_h * 4);
  GetDIBits(mem, bmp, 0, g_h, buf, &bi, DIB_RGB_COLORS);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  return buf;
}

// Row signature: sum of absolute channel values hashed per row. Robust to horizontal
// movement and only sensitive to vertical position.
static void rows(const unsigned char *px, double *out)
{
  for (int y = 0; y < g_h; ++y)
  {
    long long s = 0;
    const unsigned char *r = px + (size_t)y * g_w * 4;
    for (int x = 0; x < g_w; ++x)
      s += r[x * 4] + r[x * 4 + 1] + r[x * 4 + 2];
    out[y] = (double)s;
  }
}

static double sadRows(const double *a, const double *b, int shift, int n)
{
  double s = 0;
  int cnt = 0;
  for (int y = 0; y < n; ++y)
  {
    const int yy = y + shift;
    if (yy < 0 || yy >= n)
      continue;
    s += a[y] > b[yy] ? a[y] - b[yy] : b[yy] - a[y];
    ++cnt;
  }
  return cnt ? s / cnt : 1e18;
}

// Best integer vertical shift of "now" relative to "prev", searched in [-maxS,maxS].
static int bestShift(const double *prev, const double *now, int n, int maxS)
{
  int best = 0;
  double bv = 1e18;
  for (int s = -maxS; s <= maxS; ++s)
  {
    const double v = sadRows(prev, now, s, n);
    if (v < bv) { bv = v; best = s; }
  }
  return best;
}

int main(int argc, char **argv)
{
  if (argc < 7)
  {
    printf("usage: scrollwatch.exe L T R B wheelX wheelY [down|up] [samples] [msPerSample]\n");
    return 2;
  }
  const int L = atoi(argv[1]), T = atoi(argv[2]), R = atoi(argv[3]), B = atoi(argv[4]);
  const int wx = atoi(argv[5]), wy = atoi(argv[6]);
  const int down = (argc > 7 && strcmp(argv[7], "down") == 0);
  const int samples = (argc > 8) ? atoi(argv[8]) : 60;
  const int ms = (argc > 9) ? atoi(argv[9]) : 20;

  HWND m = FindWindowA("REAPERwnd", nullptr);
  if (!m) { printf("REAPERwnd not found\n"); return 2; }
  SetForegroundWindow(m);
  Sleep(300);

  unsigned char *prevPx = grab(L, T, R, B);
  double *prev = (double *)malloc(sizeof(double) * g_h);
  double *cur = (double *)malloc(sizeof(double) * g_h);
  rows(prevPx, prev);

  SetCursorPos(wx, wy);
  Sleep(150);

  INPUT in = {0};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = MOUSEEVENTF_WHEEL;
  in.mi.mouseData = down ? (DWORD)(-WHEEL_DELTA) : (DWORD)WHEEL_DELTA;
  SendInput(1, &in, sizeof(in));

  printf("watch %d,%d,%d,%d  wheel %d,%d %s  (shift >0 = content moved DOWN)\n", L, T, R, B,
         wx, wy, down ? "down" : "up");
  printf("  %5s  %6s  %8s\n", "t(ms)", "shift", "cumulative");
  int cum = 0, prevShift = 0, reversals = 0;
  for (int i = 0; i < samples; ++i)
  {
    Sleep(ms);
    unsigned char *px = grab(L, T, R, B);
    rows(px, cur);
    const int s = bestShift(prev, cur, g_h, 60);
    cum += s;
    if (s != 0 && prevShift != 0 && ((s > 0) != (prevShift > 0)))
      ++reversals;
    if (s != 0) prevShift = s;
    printf("  %5d  %6d  %8d%s\n", (i + 1) * ms, s, cum,
           (reversals && s != 0 && prevShift != 0 && ((s > 0) != (prevShift > 0))) ? "  <-- REVERSAL" : "");
    memcpy(prev, cur, sizeof(double) * g_h);
    free(px);
    free(prevPx);
    prevPx = nullptr;
  }
  printf("direction reversals: %d\n", reversals);
  return 0;
}
