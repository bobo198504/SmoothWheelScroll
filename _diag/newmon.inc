static void DrawMonitor(HDC dc)
{
  if (!g_hasMonitor)
    return;
  const RECT &r = g_monRect;
  if (r.right - r.left < 40 || r.bottom - r.top < 30)
    return;

  if (g_cardBrush)
    FillRect(dc, &r, g_cardBrush);
  if (g_lineBrush)
    FrameRect(dc, &r, g_lineBrush);

  // Nothing may spill out of the frame: this is a table of numbers, and a wrapped row that overflowed
  // would draw over the panel below it.
  const int saved = SaveDC(dc);
  IntersectClipRect(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);

  HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
  const int oldBk = SetBkMode(dc, TRANSPARENT);
  const int padX = 8, lineH = 15;
  const int x0 = r.left + padX;
  int y = r.top + 5;
  char b[512];

  // Heading, in the quiet colour: a label, not data.
  SetTextColor(dc, g_theme.sub);
  TextOutA(dc, x0, y, "Model trace: captured in  ->  sent out  ->  eaten", 48);
  y += lineH;

  // ---- 1. CAPTURED (in) ----------------------------------------------------------------------
  // The deltas as they came off the wheel, and the gap between the last two messages.
  {
    SetTextColor(dc, g_theme.sub);
    const char *lab = "captured deltas:";
    TextOutA(dc, x0, y, lab, (int)strlen(lab));
    const int lw = MeasureTextWidth(lab);
    SetTextColor(dc, g_theme.text);
    if (!g_trace.seen)
    {
      TextOutA(dc, x0 + lw + 10, y, "(roll the wheel)", 16);
    }
    else
    {
      int n = 0;
      for (int i = PanelTrace::kIn - g_trace.inN; i < PanelTrace::kIn; ++i)
        n += _snprintf(b + n, sizeof(b) - n, "%d  ", g_trace.inDelta[i]);
      TextOutA(dc, x0 + lw + 10, y, b, n);
    }
    y += lineH;
    SetTextColor(dc, g_theme.sub);
    if (g_trace.seen)
      _snprintf(b, sizeof(b), "gap between last two: %.1f ms", g_trace.gapMs);
    else
      _snprintf(b, sizeof(b), "gap between last two: -");
    TextOutA(dc, x0, y, b, (int)strlen(b));
    y += lineH;
  }

  // ---- 2. OUTPUT -----------------------------------------------------------------------------
  // What actually reached REAPER, newest last. Drawn in as many columns as the panel is wide enough
  // for; when the panel is narrow the grid grows TALLER instead (the "display vertically" case) --
  // the block sits inside the panel's scroll area, so a tall grid is safe.
  {
    SetTextColor(dc, g_theme.sub);
    _snprintf(b, sizeof(b), "output deltas (last %d):", PanelTrace::kOut);
    TextOutA(dc, x0, y, b, (int)strlen(b));
    y += lineH;

    const int count = (g_trace.outCount < PanelTrace::kOut) ? (int)g_trace.outCount
                                                            : PanelTrace::kOut;
    if (count <= 0)
    {
      SetTextColor(dc, g_theme.text);
      TextOutA(dc, x0, y, "(nothing sent yet)", 18);
      y += lineH;
    }
    else
    {
      // A cell is wide enough for the widest value the ring can hold, so columns line up.
      const int cellW = MeasureTextWidth("-0000.0") + 12;
      int cols = (cellW > 0) ? ((r.right - padX) - x0) / cellW : 1;
      if (cols < 1)
        cols = 1;
      const int first = (g_trace.outCount < PanelTrace::kOut)
                            ? 0
                            : (int)(g_trace.outCount % PanelTrace::kOut);
      SetTextColor(dc, g_theme.text);
      for (int k = 0; k < count; ++k)
      {
        const double v = g_trace.outDelta[(first + k) % PanelTrace::kOut];
        _snprintf(b, sizeof(b), "%.2f", v);
        const int cx = x0 + (k % cols) * cellW;
        const int cy = y + (k / cols) * lineH;
        if (cy > r.bottom - 3) // ran out of the block: stop rather than spill
          break;
        TextOutA(dc, cx, cy, b, (int)strlen(b));
      }
      const int rows = (count + cols - 1) / cols;
      y += rows * lineH;
    }
  }

  // ---- 3. EATEN ------------------------------------------------------------------------------
  // How many deltas the resistance kept back, one figure per 10 ms window, oldest .. newest.
  {
    SetTextColor(dc, g_theme.sub);
    const char *lab = "eaten (deltas per 10 ms):";
    TextOutA(dc, x0, y, lab, (int)strlen(lab));
    const int lw = MeasureTextWidth(lab);
    SetTextColor(dc, g_theme.text);
    if (!g_trace.eatSeen)
    {
      TextOutA(dc, x0 + lw + 10, y, "(nothing eaten yet)", 19);
    }
    else
    {
      int n = 0;
      for (int i = 0; i < PanelTrace::kEat; ++i)
        n += _snprintf(b + n, sizeof(b) - n, "%.2f  ", g_trace.eatWin[i]);
      TextOutA(dc, x0 + lw + 10, y, b, n);
    }
    y += lineH;
  }

  SetBkMode(dc, oldBk);
  SelectObject(dc, oldFont);
  RestoreDC(dc, saved);
}
