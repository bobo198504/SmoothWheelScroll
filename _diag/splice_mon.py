# Splice the new DrawMonitor (from _diag/newmon.inc) into src/smooth_wheel_scroll.cpp,
# replacing the existing DrawMonitor function body.
import io

SRC = 'src/smooth_wheel_scroll.cpp'
INC = '_diag/newmon.inc'

s = io.open(SRC, encoding='utf-8').read()
new = io.open(INC, encoding='utf-8').read()

start = s.index('static void DrawMonitor(HDC dc)')
end = s.index('static void PaintPanel(HWND h)')
old = s[start:end]
if not old.rstrip().endswith('}'):
    raise SystemExit('unexpected end of DrawMonitor: ' + repr(old[-60:]))

s = s[:start] + new + '\n' + s[end:]
io.open(SRC, 'w', encoding='utf-8', newline='').write(s)
print('spliced: old %d bytes -> new %d bytes' % (len(old), len(new)))
