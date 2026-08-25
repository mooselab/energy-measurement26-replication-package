#!/usr/bin/env python3
"""Point this environment's rpy2 at your R installation.

Only needed for the RQ2 Scott-Knott ESD notebooks. RQ1 and RQ3 are pure Python.

Run it once after creating the virtualenv:

    .venv/Scripts/python.exe setup_r_bridge.py        # Windows
    .venv/bin/python setup_r_bridge.py                # POSIX

It locates R, then writes a small `.pth` file into this environment's
site-packages that sets `R_HOME` and, on Windows, registers R's `bin/x64` as a
DLL search directory. A `.pth` is used rather than a VS Code `.env` because it
takes effect inside the interpreter itself, so it applies identically to Jupyter
kernels, integrated terminals and plain `python` - no editor-specific setting and
no `PATH` juggling.

Setting `R_HOME` alone is not sufficient on Windows: R's own DLLs
(`stats.dll` and friends) fail to load unless `bin/x64` is on the DLL search
path, which surfaces as

    RRuntimeError: unable to load shared object '.../stats/libs/x64/stats.dll'

Re-run this after rebuilding the virtualenv, or after upgrading R.
"""
import os
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

PTH_NAME = "_rpy2_r_home.pth"


def find_r_home() -> Path | None:
    """Locate R, preferring what the system already points at."""
    if os.environ.get("R_HOME"):
        p = Path(os.environ["R_HOME"])
        if p.is_dir():
            return p

    exe = shutil.which("R") or shutil.which("Rscript")
    if exe:
        try:
            out = subprocess.run(
                [str(Path(exe).with_name("Rscript")), "-e", "cat(R.home())"],
                capture_output=True, text=True, timeout=60,
            )
            cand = Path(out.stdout.strip())
            if cand.is_dir():
                return cand
        except Exception:
            pass

    if sys.platform == "win32":
        try:
            import winreg
            for root in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
                for key in (r"SOFTWARE\R-core\R", r"SOFTWARE\WOW6432Node\R-core\R"):
                    try:
                        with winreg.OpenKey(root, key) as k:
                            cand = Path(winreg.QueryValueEx(k, "InstallPath")[0])
                            if cand.is_dir():
                                return cand
                    except OSError:
                        continue
        except ImportError:
            pass
        roots = [Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "R"]
        versions = [d for r in roots if r.is_dir() for d in r.iterdir() if d.is_dir()]
        if versions:
            return sorted(versions, key=lambda d: d.name)[-1]

    for cand in (Path("/usr/lib/R"), Path("/usr/local/lib/R"),
                 Path("/Library/Frameworks/R.framework/Resources")):
        if cand.is_dir():
            return cand
    return None


def main() -> int:
    site = Path(sysconfig.get_paths()["purelib"])
    if not site.is_dir():
        print("cannot locate site-packages for %s" % sys.executable)
        return 1

    r_home = find_r_home()
    if r_home is None:
        print("R not found.\n"
              "  Install R itself - RStudio is only an IDE and does not include it:\n"
              "    Windows : winget install RProject.R\n"
              "    Debian  : sudo apt install r-base\n"
              "    macOS   : brew install r\n"
              "  Then re-run this script. RQ1 and RQ3 work without R.")
        return 1

    dll_dir = r_home / "bin" / "x64"
    if not dll_dir.is_dir():
        dll_dir = r_home / "bin"

    # A .pth line beginning with "import " is executed at interpreter startup.
    #
    # The DLL directory must go on the process PATH, not through
    # os.add_dll_directory(): R loads stats.dll and friends with its own
    # LoadLibrary calls, which consult PATH and ignore Python's DLL directory
    # list. Assigning to os.environ writes through to the process environment,
    # so this is visible to R.
    line = (
        "import os; "
        "os.environ.setdefault('R_HOME', r'%s'); "
        "os.environ.__setitem__('PATH', r'%s' + os.pathsep + os.environ.get('PATH',''))"
        " if r'%s' not in os.environ.get('PATH','') else None"
        % (r_home, dll_dir, dll_dir)
    )
    pth = site / PTH_NAME
    pth.write_text(line + "\n", encoding="utf-8")

    print("R_HOME        : %s" % r_home)
    print("DLL directory : %s" % dll_dir)
    print("wrote         : %s" % pth)

    # Verify in a *fresh* interpreter so the .pth is actually exercised.
    check = subprocess.run(
        [sys.executable, "-c",
         "import rpy2.robjects as ro;"
         "from rpy2.robjects.packages import importr;"
         "importr('ScottKnottESD');"
         "print('R:', ro.r('R.version.string')[0]);"
         "print('ScottKnottESD:', ro.r('as.character(packageVersion(\"ScottKnottESD\"))')[0])"],
        capture_output=True, text=True,
    )
    if check.returncode == 0:
        print("\nverified in a fresh interpreter:")
        for ln in check.stdout.strip().splitlines():
            print("  " + ln)
        print("\nRQ2 notebooks are ready.")
        return 0

    print("\nrpy2 still cannot start R:")
    tail = [ln for ln in check.stderr.strip().splitlines() if ln.strip()][-4:]
    for ln in tail:
        print("  " + ln)
    if "there is no package called" in check.stderr or "ScottKnottESD" in check.stderr:
        print("\nR starts but the analysis packages are missing. Install them with:\n"
              '  Rscript -e "install.packages(c(\'ScottKnottESD\',\'effsize\','
              "'ggplot2','svglite'))\"")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
