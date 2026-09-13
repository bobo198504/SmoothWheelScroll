// filmstrip -- capture a screen region repeatedly during a wheel notch and write the
// samples to a raw file (header: w,h,count, then RGB24 frames back to back). A node
// script turns that into a PNG laid out side by side, so a forward-then-backward
// ("rebound") motion is directly visible as the pattern shifting one way then back.
//
// It also prints, per sample, the BEST vertical shift of the content relative to the
// FIRST sample (absolute displacement, no error accumulation).
//
// Read-only: it only BitBlt's from the screen.
//
// Usage: filmstrip.exe L T R B wheelX wheelY [down|up] [frames] [ms] [mods] out.raw
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_w, g_h;

static unsigned char *grab(int L, int T, int R, int B)
{
  g_w = R - L; g_h = B - T;
  HDC sc = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(sc);
  HBITMAP bm = CreateCompatibleBitmap(sc, g_w, g_h);
  HGDIOBJ o = SelectObject(mem, bm);
  BitBlt(mem, 0, 0, g_w, g_h, sc, L, T, SRCCOPY);
  BITMAPINFO bi = {0};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = g_w;
  bi.bmiHeader.biHeight = -g_h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  unsigned char *pb = (unsigned char *)malloc((size_t)g_w * g_h * 4);
  GetDIBits(mem, bm, 0, g_h, pb, &bi, DIB_RGB_COLORS);
  SelectObject(mem, o); DeleteObject(bm); DeleteDC(mem); ReleaseDC(nullptr, sc);
  return pb;
}

// Row signature: total luminance per row. Vertical position only.
static void sig(const unsigned char *pb, double *out)
{
  for (int y = 0; y < g_h; ++y)
  {
    long long s = 0;
    const unsigned char *p = pb + (size_t)y * g_w * 4;
    for (int x = 0; x < g_w; ++x)
      s += (p[x*4]*299 + p[x*4+1]*587 + p[x*4+2]*114) / 1000;
    out[y] = (double)s;
  }
}

static double sad(const double *a, const double *b, int shift)
{
  double s = 0; int n = 0;
  for (int y = 0; y < g_h; ++y)
  {
    const int yy = y + shift;
    if (yy < 0 || yy >= g_h) continue;
    const double d = a[y] - b[yy];
    s += d < 0 ? -d : d;
    ++n;
  }
  return n ? s / n : 1e18;
}

int main(int argc, char **argv)
{
  if (argc < 12)
  {
    printf("usage: filmstrip.exe L T R B wheelX wheelY [down|up] frames ms mods out.raw\n");
    return 2;
  }
  const int L = atoi(argv[1]), T = atoi(argv[2]), R = atoi(argv[3]), B = atoi(argv[4]);
  const int wx = atoi(argv[5]), wy = atoi(argv[6]);
  const int down = (strcmp(argv[7], "down") == 0);
  const int frames = atoi(argv[8]);
  const int ms = atoi(argv[9]);
  const char *mods = argv[10];
  const char *outp = argv[11];
  const int notchEvery = (argc > 12) ? atoi(argv[12]) : 0;

  HWND m = FindWindowA("REAPERwnd", nullptr);
  if (!m) { printf("no reaper\n"); return 2; }
  SetForegroundWindow(m);
  Sleep(300);

  unsigned char **shots = (unsigned char **)malloc(sizeof(void *) * frames);
  double **sigs = (double **)malloc(sizeof(void *) * frames);

  shots[0] = grab(L, T, R, B);
  sigs[0] = (double *)malloc(sizeof(double) * g_h);
  sig(shots[0], sigs[0]);

  SetCursorPos(wx, wy);
  Sleep(150);
  WORD k1 = 0, k2 = 0;
  if (strchr(mods, 'c')) k1 = VK_CONTROL;
  if (strchr(mods, 'a')) k2 = VK_MENU;
  if (strchr(mods, 's')) k2 = VK_SHIFT;
  INPUT ki = {0};
  ki.type = INPUT_KEYBOARD;
  if (k1) { ki.ki.wVk = k1; SendInput(1, &ki, sizeof(ki)); }
  if (k2) { ki.ki.wVk = k2; SendInput(1, &ki, sizeof(ki)); }
  Sleep(40);
  INPUT in = {0};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = MOUSEEVENTF_WHEEL;
  in.mi.mouseData = down ? (DWORD)(-WHEEL_DELTA) : (DWORD)WHEEL_DELTA;
  SendInput(1, &in, sizeof(in));
  if (k2) { ki.ki.wVk = k2; ki.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &ki, sizeof(ki)); }
  if (k1) { ki.ki.wVk = k1; ki.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &ki, sizeof(ki)); }

  printf("region %d,%d,%d,%d  wheel %d,%d %s  frames=%d every %dms\n", L, T, R, B, wx, wy,
         down ? "down" : "up", frames, ms);
  printf("  %6s %8s   (shift vs frame 0; sign = direction of motion)\n", "t(ms)", "shift");

  for (int i = 1; i < frames; ++i)
  {
    Sleep(ms);
    // Optionally keep the roll going, so a multi-notch roll can be captured.
    if (notchEvery > 0 && (i % notchEvery) == 0)
    {
      if (k1) { ki.ki.wVk = k1; ki.ki.dwFlags = 0; SendInput(1, &ki, sizeof(ki)); }
      if (k2) { ki.ki.wVk = k2; ki.ki.dwFlags = 0; SendInput(1, &ki, sizeof(ki)); }
      SendInput(1, &in, sizeof(in));
      if (k2) { ki.ki.wVk = k2; ki.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &ki, sizeof(ki)); }
      if (k1) { ki.ki.wVk = k1; ki.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &ki, sizeof(ki)); }
    }
    shots[i] = grab(L, T, R, B);
    sigs[i] = (double *)malloc(sizeof(double) * g_h);
    sig(shots[i], sigs[i]);
    int best = 0; double bv = 1e18;
    for (int s = -160; s <= 160; ++s)
    {
      const double v = sad(sigs[0], sigs[i], s);
      if (v < bv) { bv = v; best = s; }
    }
    printf("  %6d %8d\n", i * ms, best);
  }

  FILE *f = fopen(outp, "wb");
  if (f)
  {
    int hdr[3] = {g_w, g_h, frames};
    fwrite(hdr, sizeof(int), 3, f);
    for (int i = 0; i < frames; ++i)
    {
      // write as RGB24
      for (int y = 0; y < g_h; ++y)
        for (int x = 0; x < g_w; ++x)
        {
          const unsigned char *p = shots[i] + ((size_t)y * g_w + x) * 4;
          fputc(p[2], f); fputc(p[1], f); fputc(p[0], f);
        }
    }
    fclose(f);
    printf("wrote %s (%dx%d, %d frames)\n", outp, g_w, g_h, frames);
  }
  return 0;
}
