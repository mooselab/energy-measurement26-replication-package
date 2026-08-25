# Replication Package — Evaluating the Accuracy and Coverage of Intel RAPL

This package contains the data, collection code, and analysis notebooks behind the paper's three
research questions. It was assembled from three raw working folders by extracting **only** the
notebook cells that feed a figure, table, or number reported in the paper. Exploratory cells, dead
"CHOICE 1/2" branches, abandoned OpenHardwareMonitor/turbostat loaders, and duplicate helper
definitions were dropped.

Cells are extracted **verbatim** - the only edit is repointing the author's absolute paths to
relative ones. Every cell keeps a `metadata.provenance` record naming its original notebook and cell
index and a `verbatim` flag, so any result traces back to the working notebook it came from.

There is exactly **one** non-verbatim cell in the package: source cell 6 of the E2 notebook, which
applied a clock offset that this capture cannot survive. It is replaced by a trim cell, and is
flagged `verbatim: false`. See `KNOWN_ISSUES.md`, K3.

**Defects were found during assembly and deliberately left unfixed**, so that this package
reproduces the published numbers rather than quietly disagreeing with them. They are catalogued in
[`KNOWN_ISSUES.md`](KNOWN_ISSUES.md), with evidence, effect on the results, and the change that
would correct each one. Read it before reusing any number from here.

---

## 1. Experiments

| ID | System under study | Workload | Raw folder it came from |
|----|--------------------|----------|--------------------------|
| **E1** | Coffee Lake | Instruction-set sweep (int32/64/128, fp32/64, MMX, SSE, SSE2, AVX, AVX2) | `instructiontype_expanded` |
| **E2** | Arrow Lake | Instruction-set sweep (40 shuffled 10 s runs) | `pc2 instructiontype_expanded_withlogs` |
| **E3** | Arrow Lake | DeathStarBench Social Network microservices under a wrk2 request-rate sweep (400→4000 req/s, 10×10 s) | `pc2 mssrising 2` |

**How each experiment's workload windows are defined.** E2 ships a `runinfo.pkl` recording the
start/end nanosecond boundaries and command line of each of its 40 runs. E1 and E3 predate that
mechanism: their window boundaries are hardcoded in the notebooks — E1 as `workloads` / `segments`
in the Step 9 cell, E3 as `segments` in the Step 11 cell. Nothing is missing for either; this is
simply how those earlier runs were recorded.

Both measurement channels run concurrently in every experiment:

* **Intel RAPL** — PKG / DRAM / PSYS energy counters sampled at 1 kHz by `pollrapl.c`
  (`SCHED_FIFO`, priority 99). Each stored value is a per-sample energy *delta* in joules;
  timestamps are `CLOCK_MONOTONIC_RAW` nanoseconds.
* **FAMLEM / INA226 hardware sensors** — 16 per-rail power channels (A B C D H I J K O P Q R U V Y Z)
  streamed from a Teensy over TCP by `tcplogger.py`. This is the ground truth.

---

## 2. Layout

```
replication-package/
├── README.md                      ← you are here
├── requirements.txt               ← pinned Python dependencies (verified on 3.12.10)
├── ENVIRONMENT.md                 ← how to build the environment, binary formats
├── KNOWN_ISSUES.md                ← defects found during assembly, documented but NOT fixed
├── MISSING_INPUTS.md              ← the one input the paper used that is not in the source folders
├── setup_r_bridge.py              ← points rpy2 at your R install (RQ2 only)
├── .vscode/                       ← VS Code workspace settings (open THIS folder)
├── verify_timestamp_overflow.py   ← data-integrity check over all six raw streams
├── data/
│   ├── E1-coffeelake-instructions/   rapl/  teensy/
│   ├── E2-arrowlake-instructions/    rapl/  teensy/  runinfo.pkl
│   └── E3-arrowlake-microservice/    rapl/  teensy/
├── collection-code/          ← pollrapl.c, tcplogger.py, tcpBounce.c, orchestration, Teensy firmware
│   └── wrk2-logs/            ← the 10 wrk2 HdrHistogram reports for E3
├── rq1-rapl-accuracy/
├── rq2-instruction-type/
└── rq3-energy-coverage/
```

Each RQ directory holds one notebook per experiment plus an `outputs/` directory with the figures
and CSVs actually used in the manuscript. Notebook paths are relative to the notebook's own
directory, so run them **with the RQ directory as the working directory**.

---

## 3. RQ → artifact → notebook map

### RQ1 — How accurate is Intel RAPL at measuring CPU energy?

| Paper artifact | Notebook | Source cells |
|---|---|---|
| Fig. `fig:energy` (`twinenergy.pdf`) | `rq1-rapl-accuracy/rq1_E1_coffeelake.ipynb` + `rq1_E2_arrowlake.ipynb` | E1 c23 / E2 c27 |
| Fig. `fig:energy_normalized` (`twinenergy_normalized.pdf`) | same two notebooks | E1 c24 / E2 c28 |
| Table `tableWilcox` (`wilcoxonAggregate.tex`) | both | E1 c19+c25 / E2 c18+c30 |
| Appendix Table `arrowEnergyWilcoxon` | `rq1_E2_arrowlake.ipynb` | E2 c30 |
| Appendix Table `arrowPowerWilcoxon` | `rq1_E2_arrowlake.ipynb` | E2 c18 |
| Reported normalized-difference % and TLS slope/intercept | both | E1 c22 / E2 c24 |

Pipeline: parse both streams → clean RAPL → derive power → outer-join on timestamp → clip to the
workload span → trapezoid-integrate both channels over matched 100 ms windows → per-instruction-set
Wilcoxon signed-rank with rank-biserial effect size, plus orthogonal (total least squares)
regression of RAPL against the sensor.

### RQ2 — Does instruction type affect RAPL accuracy?

| Paper artifact | Notebook | Source cells |
|---|---|---|
| Fig. `fig:skesd` (`twinskesdplot.pdf`) | `rq2-instruction-type/rq2_twin_skesd.ipynb` | twin c4 |
| Table `tableskesd` (`sk_esd_summary_results.csv`) | `rq2_twin_skesd.ipynb` | twin c3, c5 |
| Per-CPU SK-ESD rankings | `rq2_E1_coffeelake_skesd.ipynb`, `rq2_E2_arrowlake_skesd.ipynb` | E1 c27 / E2 c33 |
| Kruskal-Wallis + Dunn post-hoc CSVs | same two | E1 c26 / E2 c25 |

Run order matters: the two per-CPU notebooks each emit an SK-ESD sample matrix
(`skesd_sample_coffee.pkl`, `skesd_sample_pc2.pkl`) that the twin notebook consumes. Both pickles
are also pre-staged in `rq2-instruction-type/inputs/`, so the twin notebook can be run on its own.

> Table `tableepi` (per-instruction underestimation, GInst/s throughput) cannot be regenerated from
> this package — see `MISSING_INPUTS.md`.

### RQ3 — How much whole-system energy does Intel RAPL capture?

| Paper artifact | Notebook | Source cells |
|---|---|---|
| Table `tablecoverage`, instruction-workload column | `rq3-energy-coverage/rq3_E1_coffeelake.ipynb`, `rq3_E2_arrowlake.ipynb` | E1 c36–c41 / E2 c34–c38 |
| Table `tablecoverage`, microservice column | `rq3_E3_microservice.ipynb` | E3 c17–c22 |
| `pie_instructions.png`, `pie_rapl_instructions.png`, legends | all three | E1 c41 / E3 c22 |

Pipeline: bracket-merge the two streams → sum rails into subsystems → apply the measured
PSU-efficiency surface to recover true AC input power → trapezoid-integrate each domain to kWh →
express RAPL PKG+DRAM as a share of the whole-system total.

Rail-to-subsystem mapping used throughout:

| Subsystem | Channels |
|---|---|
| CPU (`cpu_total`) | Y + Z + U + V |
| DRAM | P + Q + R + A + C |
| NIC | I |
| SATA | U + V |
| Motherboard | H + I + J + K + O + P + Q + R |
| Whole system (`abs_total`) | all 16 channels |
| PSU minor rails (efficiency lookup) | I, J, A, B, C, D, V |
| PSU major rails (efficiency lookup) | H, K, O, P, Q, R, U, Y, Z |

This mapping is hardcoded in the analysis cells rather than declared in one place; it is the most
fragile assumption in the pipeline and should be checked against the Teensy wiring
(`collection-code/teensyEnergy.zip`, `outputs/*/legend.png`) before reuse.

---

## 4. Suggested run order

1. `rq1-rapl-accuracy/rq1_E2_arrowlake.ipynb` — the most complete path; verifies the parsers, the
   merge, and the RQ1 statistics end to end.
2. `rq2-instruction-type/rq2_E2_arrowlake_skesd.ipynb` and `rq2_E1_coffeelake_skesd.ipynb` — emit
   the SK-ESD sample pickles.
3. `rq2-instruction-type/rq2_twin_skesd.ipynb` — cross-CPU ranking, Fig. `fig:skesd` and
   Table `tableskesd`.
4. `rq1-rapl-accuracy/rq1_E1_coffeelake.ipynb` — self-contained; no extra inputs needed.
5. `rq3-energy-coverage/*` — requires `modifier.npy` (see `MISSING_INPUTS.md`).

RQ1 and RQ2 are fully reproducible from what is in this package. RQ3 stops at the PSU-efficiency
step until `modifier.npy` is restored.

### Before you run anything

```bash
uv venv --python 3.12.10 && uv pip install -r requirements.txt
# or:  python -m venv .venv && .venv/Scripts/activate && pip install -r requirements.txt

.venv/Scripts/python.exe setup_r_bridge.py     # RQ2 only; needs R installed
```

`requirements.txt` pins the versions this package was verified against. **`pandas<3` is a hard
requirement** - on 3.x the notebooks stop for two unrelated reasons (K10, K11), neither of which
affects any result. See `ENVIRONMENT.md`.

`python verify_timestamp_overflow.py` re-checks every raw stream for counter wraps, out-of-order
samples, duplicate timestamps and corrupted records. A few seconds, numpy only.

### What each notebook does when run top to bottom

Verified on Python 3.12.10 with the pinned requirements, R 4.6.1 with `ScottKnottESD` 2.0.3, and
`modifier.npy` absent:

| Notebook | Cells run | Outcome |
|---|---|---|
| `rq1_E2_arrowlake.ipynb` | 16/16 | **completes** - reproduces Table `tableWilcox` R² = 0.9979 |
| `rq1_E1_coffeelake.ipynb` | 17/17 | **completes** - reproduces Table `tableWilcox` R² = 0.9966 |
| `rq2_E2_arrowlake_skesd.ipynb` | 11/11 | **completes** - writes `skesd_sample_pc2.pkl` |
| `rq2_E1_coffeelake_skesd.ipynb` | 13/13 | **completes** - writes `skesd_sample_coffee.pkl` |
| `rq2_twin_skesd.ipynb` | 4/4 | **completes** - `sk_esd_summary_results.csv` byte-identical to published |
| `rq3_E2_arrowlake.ipynb` | 7/12 | stops at the PSU step - `modifier.npy` missing |
| `rq3_E1_coffeelake.ipynb` | 9/15 | stops at the PSU step |
| `rq3_E3_microservice.ipynb` | 12/20 | stops at the PSU step |

**Five of eight notebooks run end to end and reproduce the published numbers exactly** - both RQ1
statistics tables and the whole of Table `tableskesd`, all 20 rows and every column. The only
remaining blocker is the missing `modifier.npy` (see `MISSING_INPUTS.md`), which is an absent input
rather than a defect in the notebooks.

RQ2 needs R, and **RStudio alone is not enough** - it is only an IDE and does not include the R
runtime. On Windows also launch from PowerShell or cmd rather than Git Bash, or rpy2 fails at
import. `ENVIRONMENT.md` explains both. Install R with `ScottKnottESD` to clear the RQ2
rows; see `MISSING_INPUTS.md` for the RQ3 rows.

Before running anything, `python verify_timestamp_overflow.py` re-checks every raw stream for
counter wraps, out-of-order samples, duplicate timestamps, and corrupted records, and prints the
sampling rate and span of each. It takes a few seconds and needs only numpy.

---

## 5. Notes on the extracted notebooks

* **Cells are verbatim, with one flagged exception.** The only change made to any other cell's
  source is repointing the author's absolute paths (`C:/Users/andyh/Downloads/serialreceive/...`) to
  the relative paths in this package. No bug was fixed and no analysis was altered. This is
  deliberate: the package's first job is to reproduce the published numbers, and a package that
  silently disagrees with the paper is worse than one with documented defects. Every extracted cell
  was machine-checked against its source and differs only by those path substitutions.
* **The one exception: E2 source cell 6.** As written it applies a clock offset that leaves the two
  streams with no overlap, so the notebook cannot produce any result at all (K3). It is replaced by
  a trim cell that applies no shift and instead cuts both raw streams to start at the first workload
  boundary from `runinfo.pkl`. The replacement is marked `verbatim: false` in its provenance and
  carries a comment explaining itself. Measured effect on RQ3's domain shares: at most 0.09 pp, since
  it removes only 1.3% of merged rows; RQ1 and RQ2 clip to the workload span themselves and are
  unaffected. K3 gives the numbers and the line to restore for the original behaviour.
* **Read `KNOWN_ISSUES.md` before reusing any number.** Nine defects were found during assembly and
  left in place. One of them (K1, integer truncation in the bracket merge) materially affects
  Table `tablecoverage`; one (K3, the E2 clock-offset constant) will stop the E2 notebooks producing
  results if you run every cell. The rest have no effect on published values.
* **Stored outputs are kept.** Cells carry the figures and printed values from the original run, so
  results are inspectable without re-executing. Those stored outputs may print the original author's
  absolute paths; only the cell *source* was repointed.
* **Two structural choices, neither of which changes a value:**
  * `rq3_E3_microservice.ipynb` places the `segments` cell (source cell 23) before the clipping cell
    that consumes it (source cell 16). The source notebook has them the other way round and relied on
    out-of-order execution. Contents are unchanged.
  * `rq2_twin_skesd.ipynb` omits source cell 2, a superseded duplicate of the SK-ESD call that the
    author kept "just in case". Cell 3 is the version that produced the published table.
* **`rq1-rapl-accuracy/supplementary/` holds a superseded notebook**, included only to document the
  provenance of some Coffee Lake CSVs. It is not part of the replication path - see K7.
* **Naming quirk:** the Arrow Lake RQ1 figure files are named `meteorenergy*` and their in-plot
  titles say "Meteorlake". The system under study is Arrow Lake (E2); the filenames were never
  updated.
