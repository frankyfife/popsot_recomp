"""Run retail with a pad script and capture frame traces (host-resolution dumps) at given seconds.

    python tools/fx-capture.py <name> <fx-scale> <s1,s2,...> ["RECOMP_X='v'; ..."]

Writes logs/fx-<name>/d_flip<n>_after.bmp (the finished frame) and one BMP per
smaller render target (RECOMP_FRAME_TRACE + RECOMP_HW_GLOW_DUMP), using the
retail pad timeline of VALIDATION.md. <fx-scale> other than 1 sets
RECOMP_HW_FX_SCALE. The options in bin/resot-options.ini apply.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(r"D:\Projekte\rePOP_ff")
name, fx = sys.argv[1], sys.argv[2]
times = [float(t) for t in sys.argv[3].split(",")]
extra = sys.argv[4] if len(sys.argv) > 4 else ""
out = ROOT / "logs" / ("fx-" + name)
out.mkdir(parents=True, exist_ok=True)
flag = out / "trace.flag"
if flag.exists():
    flag.unlink()
pad = ("15000:start:400,25000:start:400,35000:a:400,45000:start:400,60000:a:400,"
       "75000:a:400,90000:start:400,100000:a:400,110000:a:400")
sets = "RECOMP_PAD_SCRIPT='%s'; RECOMP_FRAME_TRACE='%s'; RECOMP_HW_GLOW_DUMP='%s'" % (pad, flag, out / "d")
if fx != "1":
    sets += "; RECOMP_HW_FX_SCALE='%s'" % fx
if extra:
    sets += "; " + extra
script = (r"Set-Location 'D:\Projekte\rePOP_ff'; & '.\tools\run.ps1' -Target xbox-retail -Seconds %d -Windowed -Set @{ %s }"
          % (int(max(times)) + 8, sets))
p = subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script])
t0 = time.time()
for t in times:
    while time.time() - t0 < t:
        time.sleep(0.2)
    flag.write_text("1")
    time.sleep(2.5)
    try:
        flag.unlink()
    except FileNotFoundError:
        pass
p.wait()
print(name, "done;", len(list(out.glob("*.bmp"))), "dumps in", out)
