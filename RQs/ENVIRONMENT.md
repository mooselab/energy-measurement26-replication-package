# Environment

## Verified configuration

The package was last executed end to end on:

| | |
|---|---|
| Python | **3.12.10** (CPython, Windows x86_64) |
| pandas | 2.3.3 |
| numpy | 2.5.2 |
| scipy | 1.18.0 |
| matplotlib | 3.11.1 |
| seaborn | 0.13.2 |
| scikit-posthocs | 0.14.0 |
| jupyter | 1.1.1 |
| rpy2 | 3.6.7 |
| R | **4.6.1** |
| ScottKnottESD | 2.0.3 |
| effsize / ggplot2 / svglite | 0.8.1 / 4.0.3 / 2.2.2 |

Under this configuration **five of the eight notebooks run to completion** and reproduce the
published results exactly:

* Both RQ1 notebooks — Wilcoxon *W* identical, rank-biserial effect sizes agreeing to seven
  significant figures, and orthogonal-regression R² of **0.9979** (Arrow Lake) and **0.9966**
  (Coffee Lake), the values in Table `tableWilcox`.
* All three RQ2 notebooks — `sk_esd_summary_results.csv` regenerates **byte-identical** to the
  published file, all 20 rows and every column: ranking, mean, confidence interval and Cliff's
  delta. That is Table `tableskesd` reproduced end to end.

The three RQ3 notebooks stop at the PSU step because `modifier.npy` is absent
(`MISSING_INPUTS.md`); that is a missing input, not a defect.

## Building the environment

Two steps. The second is only needed for the RQ2 notebooks, which call R.

```bash
# 1. Python environment
python -m venv .venv
.venv/Scripts/activate            # Windows
# source .venv/bin/activate       # macOS / Linux
pip install -r requirements.txt

# 2. Point rpy2 at your R installation (RQ2 only)
python setup_r_bridge.py
```

Or with [uv](https://docs.astral.sh/uv/):

```bash
uv venv --python 3.12.10
uv pip install -r requirements.txt
.venv/Scripts/python.exe setup_r_bridge.py
```

`requirements.txt` pins the exact versions in the table above.

`setup_r_bridge.py` finds R (via `R_HOME`, then `Rscript`, then the Windows registry, then the usual
install locations), writes a small `.pth` into the environment's site-packages that sets `R_HOME` and
puts R's `bin/x64` on `PATH`, and then verifies by starting R and loading `ScottKnottESD` in a fresh
interpreter. It prints what it found, so a failure tells you whether R is missing or just its
packages. Re-run it after rebuilding the environment or upgrading R.

A `.pth` is used rather than shell exports or an editor `.env` because it takes effect inside the
interpreter, so Jupyter kernels, terminals and plain `python` all behave the same with no per-tool
configuration. Setting `R_HOME` alone is not enough on Windows: R loads `stats.dll` and friends
through its own `LoadLibrary` calls, which read the process `PATH` and ignore
`os.add_dll_directory()`. Without the `PATH` entry you get

```
RRuntimeError: unable to load shared object '.../library/stats/libs/x64/stats.dll'
```

With both steps done, `python -c "import rpy2.robjects"` works from anywhere in the environment.

### Running the notebooks in VS Code

Open **this folder** (`replication-package`) as the workspace, then install the two recommended
extensions VS Code offers (`ms-python.python`, `ms-toolsai.jupyter`). `.vscode/settings.json` is
committed and does the rest.

**Build the venv from a Python in a shared location** - `C:\Program Files\Python312` or an
equivalent - not from a per-user or tool-managed interpreter that other processes may not be able to
reach. If VS Code rejects the venv with

```
Selected file is not a valid Python interpreter: ...\.venv\Scripts\python.exe
```

the usual cause is that the venv's *base* interpreter is unreachable, not the venv itself. Check
with:

```bash
.venv/Scripts/python.exe -c "import sys,os; print(sys._base_executable, os.path.exists(sys._base_executable))"
```

and compare against `.venv/pyvenv.cfg`. If `home`/`executable` point somewhere VS Code cannot see,
delete `.venv` and recreate it from an interpreter that it can.

If VS Code reports that the interpreter cannot be resolved, set it once by hand:
**Ctrl+Shift+P → "Python: Select Interpreter" → `.venv\Scripts\python.exe`**. Note that
`python.defaultInterpreterPath` is consulted *only* when no interpreter has yet been chosen for the
workspace, so a previously stored choice takes precedence over it. The path in that setting is
deliberately relative rather than using `${workspaceFolder}`, which does not expand reliably in that
setting and cannot be resolved at all in a multi-root workspace.

One setting in there is load-bearing:

```jsonc
"jupyter.notebookFileRoot": "${fileDirname}"
```

Every data path in the notebooks is relative to the notebook's own directory - `"../data/E2-arrowlake-instructions/rapl"`
and so on - because that is how they were originally run. The kernel must therefore start in the
notebook's folder rather than the workspace root. Without it the first parser cell fails with
`FileNotFoundError`.

A kernel named **Python 3.12 (RAPL replication)** is registered inside `.venv` itself
(`.venv/share/jupyter/kernels/rapl-repro`), so nothing is added to your global Jupyter kernel list.
VS Code also discovers `.venv` directly, so either selection works.

The R bridge needs no VS Code configuration: `setup_r_bridge.py` installs a `.pth` that takes effect
inside the interpreter, so notebook kernels pick up `R_HOME` automatically.

**Verified** by executing all eight notebooks through that kernelspec with the working directory set
to each notebook's own folder - the same thing VS Code does. Five complete; the three RQ3 notebooks
stop at the missing `modifier.npy`.

> The notebooks ship with the stored outputs of the published run, which are evidence of it. Running
> a notebook in VS Code and saving will overwrite them. Use *Clear All Outputs* deliberately, or
> discard the change, if you want the shipped versions preserved.

| Package | Used for |
|---|---|
| `numpy`, `pandas` | binary parsing, the bracket-merge, all frame handling |
| `scipy` | `trapezoid`, `wilcoxon`, `kruskal`, `mannwhitneyu`, `odr` (total least squares), `find_peaks` |
| `scikit-posthocs` | Dunn post-hoc pairwise comparison (RQ2) |
| `matplotlib`, `seaborn` | all figures |
| `rpy2` | bridge to R for Scott-Knott ESD (RQ2 only) |

`struct`, `glob`, `os`, `pickle`, `math` are standard library.
`verify_timestamp_overflow.py` needs only `numpy`.

## Why the pandas bound matters

**`pandas<3` is a hard requirement, not a preference.** On pandas 3.x two unrelated failures stop
the notebooks. Both are runnability problems; neither affects any result:

* `DataFrame.applymap` was removed in 3.0 and is called by two cells while formatting a LaTeX table.
  Raises `AttributeError` *after* the statistics have been computed (`KNOWN_ISSUES.md`, K10).
* E1 source cell 3 writes a float into an `int64` column. 3.x rejects this outright, stopping the E1
  notebooks at their **second code cell**, before any analysis (K11). On pandas 2.x it is a
  `FutureWarning` and the value is coerced, which is how the original analysis ran.

The interpreter version is not itself the constraint — the pandas major version is. Python 3.12.10 is
specified because it is what the pinned set was verified against.

## R — required for RQ2 only

The Scott-Knott ESD analysis runs in R through `rpy2`. **RQ1 and RQ3 are pure Python**, and even
within RQ2 the Kruskal-Wallis and Dunn steps run without R — only the SK-ESD cells need it.

```r
install.packages(c("ScottKnottESD", "effsize", "ggplot2", "svglite"))
```

| Package | Used for |
|---|---|
| `ScottKnottESD` | the non-parametric SK-ESD clustering and ranking |
| `effsize` | Cliff's delta within the SK-ESD summary |
| `ggplot2` | the ranking plots (Fig. `fig:skesd`) |
| `svglite` | SVG export of those plots |

`setup_r_bridge.py` (see *Building the environment*) handles the wiring. The rest of this section is
background for when it reports a problem.

`rpy2` needs `R_HOME` set before the Python process starts; without it `import rpy2.robjects` fails
with `ValueError: r_home is None`. To set it by hand instead of using the script:

```bash
export R_HOME=/usr/lib/R                  # POSIX
set R_HOME=C:\Program Files\R\R-4.6.1     # Windows
```

Diagnose with `python -m rpy2.situation`.

Note that **RStudio is not R.** RStudio is only an IDE and does not bundle the R runtime; installing
it alone leaves `rpy2` failing with `r_home is None`. On Windows, `winget install RProject.R`
installs R itself.

R packages must be on `.libPaths()` as R sees it. If you install them to a non-default library,
either move them to R's default user library or set `R_LIBS_USER`; `Rscript -e ".libPaths()"` shows
where R is actually looking.

### Windows: do not launch from Git Bash

Even with the bridge configured, `rpy2` fails at import under Git Bash / MSYS:

```
IndexError: list index out of range   (rpy2/situation/__init__.py, _get_r_cmd_config)
```

`rpy2` runs `R CMD config --ldflags` at import. With a POSIX `sh` on `PATH`, R dispatches that to
`bin/config.sh`, which needs `make`; without it the command emits only `make: command not found` on
stderr and nothing on stdout, and rpy2 indexes an empty list. Use **PowerShell or cmd** instead.

This is an rpy2/Windows interaction, not a problem with the notebooks. The pre-computed SK-ESD
pickles in `rq2-instruction-type/inputs/` also mean the twin notebook needs only R, not a prior run
of the per-CPU notebooks.

## Data collection (not needed to re-run the analysis)

`collection-code/` contains the acquisition side, for reference and for anyone repeating the
measurement rather than the analysis:

* `pollrapl.c` — 1 kHz `SCHED_FIFO` RAPL sampler. Needs MSR access; see
  `capability sudo setcap.txt` for the required capabilities.
* `tcplogger.py` — receives the Teensy stream over TCP and writes `teensy/data*.bin`.
* `tcpBounce.c` — round-trip latency probe used for clock synchronization.
* `teensyEnergy.zip` — Teensy firmware.
* `automation.ipynb` — experiment orchestration.
* `validate_rapl.ipynb` — sanity-check notebook for the RAPL binary parser.
* `export LUA PATH usr share.txt` — the wrk2/Lua invocation used to drive the E3 microservice
  workload against DeathStarBench Social Network.
* `wrk2-logs/` — the ten HdrHistogram latency reports, one per request-rate step of E3.

## Binary formats

**RAPL** (`data/*/rapl/*.bin`) — four contiguous arrays per file, not interleaved:

```
[ N x int64 timestamp_ns ][ N x float32 pkg_J ][ N x float32 dram_J ][ N x float32 psys_J ]
```

N = 10,000 samples per file (200,000 bytes = 10 s at 1 kHz). Values are per-sample energy
*deltas* in joules, already differenced and unit-scaled by `pollrapl.c`, with 32-bit counter
wraparound handled. Timestamps are `CLOCK_MONOTONIC_RAW` nanoseconds.

Known data-quality issues, handled by the cleaning cell in every notebook: spurious
`262143.968750` spikes in `energy_pkg` (pruned at `> 200000`), rare `timestamp == 0` rows, and
duplicate timestamps. PSYS is unreliable or all-zero on Coffee Lake and is not used in the paper's
coverage analysis.

Each RAPL stream also contains a handful of decreasing timestamp steps of almost exactly
−10,000,000,000 ns — one file's worth of samples (7 in E1, 7 in E2, 11 in E3). These are not
counter wraps. The bracket-merge sorts by timestamp before use, so they are absorbed, but raw
file order should not be trusted without sorting. Run `verify_timestamp_overflow.py` to see them.

**Teensy** (`data/*/teensy/data*.bin`) — fixed 92-byte records:

```
[ 4-byte header (discarded) ][ 8-byte uint64 timestamp_ns ][ 16 x (1-byte channel id + 4-byte float32 watts) ]
```

`teensy/metadata` is the 920-byte binary header the Teensy emits at stream start. Teensy sampling
is faster than RAPL (~1674 Hz on E1, ~1942 Hz on E3), which the bracket-merge relies on.

E1's Teensy stream contains 2 corrupted records with clobbered upper timestamp bytes; the E1
notebooks drop them. E2 and E3 are clean. See `KNOWN_ISSUES.md`, K2.

> Legacy note: older Teensy captures encoded timestamps as 24 MHz timer ticks rather than
> nanoseconds. All three experiments in this package use nanoseconds. If a derived duration ever
> looks off by a factor of ~24e6/1e9, that is the cause.
