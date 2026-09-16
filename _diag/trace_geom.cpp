// Validate the monitor's grid geometry WITHOUT REAPER: replicate the two measurement functions the
// panel uses (MonitorTextWidth / MonitorCols / MonitorBlockHeight) and print the layout for a range
// of panel widths. Uses the real font, so the numbers are the real ones.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HFONT UiFont() {
  NONCLIENTMETRICSA ncm = {0};
  ncm.cbSize = sizeof(ncm);
  HFONT f = nullptr;
  if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    f = CreateFontIndirectA(&ncm.lfMessageFont);
  return f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static int MonitorTextWidth(const char *text) {
  if (!text || !*text) return 0;
  HDC dc = GetDC(nullptr);
  HGDIOBJ oldFont = SelectObject(dc, UiFont());
  SIZE sz = {0};
  GetTextExtentPoint32A(dc, text, (int)strlen(text), &sz);
  SelectObject(dc, oldFont);
  ReleaseDC(nullptr, dc);
  return sz.cx;
}

static const int kTraceLineH = 15;
static const int kTracePadX = 8;

static int MonitorCols(int blockW) {
  const int cellW = MonitorTextWidth("-0000.0") + 12;
  if (cellW <= 0) return 1;
  int cols = (blockW - kTracePadX * 2) / cellW;
  return cols < 1 ? 1 : cols;
}
static int MonitorBlockHeight(int blockW) {
  const int count = 50;
  const int cols = MonitorCols(blockW);
  const int rows = (count + cols - 1) / cols;
  const int lines = 5 + rows;
  return 5 + lines * kTraceLineH + kTraceLineH;
}

int main() {
  const int cellW = MonitorTextWidth("-0000.0") + 12;
  printf("cell width = %d px (for \"-0000.0\"), line height = %d px\n\n", cellW, kTraceLineH);
  printf("  %-8s %-6s %-6s %-8s %-10s\n", "blockW", "cols", "rows", "height", "50 shown as");
  const int widths[] = {260, 300, 360, 420, 480, 600, 720, 900, 1200, 1600};
  for (int i = 0; i < 10; ++i) {
    const int W = widths[i];
    const int cols = MonitorCols(W);
    const int rows = (50 + cols - 1) / cols;
    char note[64];
    snprintf(note, sizeof(note), "%dx%d grid", cols, rows);
    printf("  %-8d %-6d %-6d %-8d %-10s\n", W, cols, rows, MonitorBlockHeight(W), note);
  }
  printf("\n(rows >= 1 always; on a narrow panel the grid grows TALLER, which is the "
         "\"display vertically\" case)\n");
  return 0;
}
