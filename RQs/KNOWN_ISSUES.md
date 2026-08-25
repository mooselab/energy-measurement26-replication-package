# Known issues

Defects found while assembling this package. **None of them have been fixed.** The notebook cells
are shipped exactly as they were run, so the package reproduces the published numbers; correcting
any of these here would make the package disagree with the paper, which is worse than a documented
discrepancy.

Each entry records what the defect is, how it was verified, what it does to the numbers, and the
change that would correct it — so the decision to act stays with the authors.

Severity is about effect on published results, not about how wrong the code looks. Entries marked
*(pinned)* are neutralised by the versions in `requirements.txt` and need no action.

| ID | Issue | Affects | Severity |
|----|-------|---------|----------|
| [K1](#k1) | Bracket merge truncates sensor watts to integers | Table `tablecoverage` (RQ3), appendix power table | **High** |
| [K2](#k2) | "Overflow correction" is really a corrupt-record filter | E1 only, naming/interpretation | Low |
| [K3](#k3) | Clock-offset constant does not match its capture | E2 if enabled; E3 has an uncorrected offset | Medium |
| [K4](#k4) | Dead timestamp-unwrapping cell | E1, none (result discarded) | None |
| [K5](#k5) | Redundant `timestamp == 0` re-filter | E1, none (no-op) | None |
| [K6](#k6) | `power_correlation_r2.csv` is not used by the paper | none | None |
| [K7](#k7) | Superseded notebook mixes two experiments' metadata | supplementary notebook only | Low |
| [K8](#k8) | E1 and E3 share identical window boundaries | RQ1/RQ2 (E1), RQ3 (E3) | **Unresolved** |
| [K9](#k9) | Notebooks rely on imports leaking between cells | runnability, not values | Low |
| [K10](#k10) | `DataFrame.applymap` was removed in pandas 3.0 | runnability, not values | Low (pinned) |
| [K11](#k11) | The dead E1 cell also *blocks* the E1 notebooks on pandas 3.x | E1 runnability | Medium (pinned) |

---

<a name="k1"></a>
## K1 — The bracket merge truncates sensor watts to integers

**Where:** `merge_by_bracketing_average()` — E1 cell 15, E2 cell 12, E3 cell 9. Present in
`rq3_E1_coffeelake.ipynb`, `rq3_E2_arrowlake.ipynb`, `rq3_E3_microservice.ipynb`, and in the RQ1
notebooks for the power comparison.

**What it does.** When averaging the two bracketing sensor samples, every numeric column is cast to
`int64` first:

```python
for c in numeric_cols:
    df2_avg[c] = (left_df2[c].to_numpy(np.int64)
                  + right_df2[c].to_numpy(np.int64)) // 2
```

Sensor rails are float watts and most sit below 6 W, so this quantises each rail to whole watts.

**Verification.** Per-rail means over the E1 capture, raw versus after the cast:

| Rail | True (W) | Truncated (W) | Effect |
|---|---|---|---|
| B, D, H, J, K, O | 0.009 – 0.217 | 0.000 | **zeroed** |
| C | 0.995 | 0.371 | −62.7% |
| P, Q, R (DRAM) | 1.6 – 2.1 | 1.1 – 1.5 | −29 to −31% |
| A | 1.223 | 0.978 | −20.1% |
| I | 2.713 | 2.188 | −19.3% |
| U, V | 1.5 – 5.2 | 1.0 – 4.7 | −9 to −31% |
| Y, Z (CPU) | 8.4 – 10.1 | 7.9 – 9.6 | −5 to −6% |

Six of sixteen rails are zeroed outright.

**Effect on results.** RAPL's own columns are untouched — they live in `df1` and are passed through
— so the error is one-sided and shrinks only the ground truth. Running the RQ3 pipeline both ways
(PSU step excluded, `modifier.npy` being absent):

| | E1 instruction sets | E3 microservice |
|---|---|---|
| sensor DRAM energy | −36.7% | −22.6% |
| sensor whole-system energy | −10.4% | −7.2% |
| RAPL PKG energy | 0.0% | 0.0% |
| **apparent RAPL coverage** | **+6.9 pp** | **+5.9 pp** |

**Direction matters.** Correcting this *lowers* measured RAPL coverage, because the ground truth it
is compared against grows. The paper's RQ3 conclusion — that a substantial share of system energy is
unaccounted for by RAPL — would be **strengthened**, not weakened. Table `tablecoverage` would need
restating.

**Correction (not applied).** One line:

```python
df2_avg[c] = (left_df2[c].to_numpy(np.float64)
              + right_df2[c].to_numpy(np.float64)) / 2.0
```

**Scope note.** RQ1 and RQ2's published results do *not* pass through this merge. Two independent
alignment paths exist:

| Path | Feeds |
|---|---|
| Outer-join + 100 ms windowed integration | Figs. `fig:energy`, `fig:energy_normalized`; Tables `tableWilcox`, `tableskesd`, `tableepi`; Fig. `fig:skesd` |
| Bracket merge | Table `tablecoverage`; appendix `arrowPowerWilcoxon` |

This was confirmed by removing the merge from the RQ1 notebooks in a scratch build and re-deriving
the results: identical Wilcoxon W statistics, rank-biserial effect sizes matching to six decimal
places, and R² = 0.9979 as published. **So K1 does not touch RQ1 or RQ2.**

---

<a name="k2"></a>
## K2 — "Overflow correction" is really a corrupt-record filter (E1)

**Where:** E1 cells 3–4, `find_rollovers` / `drop_around_overflows`.

**Finding.** There is **no counter wraparound in any stream in this package** — not 32-bit, not
64-bit, in neither the Teensy nor the RAPL data of any of the three experiments. Run
`python verify_timestamp_overflow.py` to confirm.

What E1's Teensy stream actually contains is two consecutive corrupted records, indices
254,475–254,476 in `data11.bin`:

```
254,474  ts=       17,527,049,842,197   hex=00000ff0d5946e15
254,475  ts=6,148,932,039,329,985,986   hex=5555651c804835c2   <-- corrupt
254,476  ts=6,148,932,039,330,590,902   hex=5555651c805170b6   <-- corrupt
254,477  ts=       17,527,051,818,502   hex=00000ff0d5b29606
```

The upper bytes were overwritten with `0x5555651c` in place of `0x00000ff0`; the low 32 bits are
intact, and the step between the two corrupt records (604,916 ns) still matches the stream's
597,417 ns median. That is transport bit corruption — `0x55` is the alternating bit pattern typical
of a serial framing glitch — not a counter event.

**Effect on results.** None. The filter removes those two records plus a two-row margin, 5 rows of
282,360. That is the right outcome; only the name is misleading. E2 and E3 have no corrupted records.

**Correction (not applied).** Rename to reflect what it does. No numerical change.

---

<a name="k3"></a>
## K3 — The clock-offset constant does not match its capture

**Background — the offset is real in principle.** The two streams are stamped off different clocks:
`pollrapl.c` uses `CLOCK_MONOTONIC_RAW`; `tcpBounce.c`, the round-trip probe that synchronises the
Teensy, uses `CLOCK_MONOTONIC`. `CLOCK_MONOTONIC` is NTP-slewed and `CLOCK_MONOTONIC_RAW` is not, so
the two drift apart and a correction legitimately belongs in the pipeline.

**Where:** E2 cell 6, shipped verbatim:

```python
df_rapl_watt["timestamp"] = df_rapl_watt["timestamp"] - 8642644546100
```

**The problem.** For the E2 capture in this package, that constant (8,642.6 s) moves RAPL to
3.17–3.67 × 10¹² ns against a sensor span of 11.85–12.32 × 10¹² ns — **no overlap at all**. With it
applied, the bracket merge returns 0 rows and the outer join finds no RAPL samples in the workload
window; the notebook cannot produce results.

**Evidence the published run did not apply it.** Two independent measurements, both giving zero:

| Method | Result |
|---|---|
| Cross-correlate RAPL package power against sensor CPU power, ±30 s scan | peak at lag **0**, r = 0.9994 |
| Count bracket-merge matches inside its 500 µs tolerance, ±1 s scan | **438,245** of 499,993 RAPL samples (87.7%) at offset 0 |

The second is decisive: the original notebook's own stored output for that merge reports
**438,241 rows**. Reproducing that count at offset 0 shows the published run had no offset in effect.
Consistently, cell 6 prints the timestamp *before* mutating it, and its stored output shows the
un-offset value.

**What this package does about it.** Source cell 6 is the **one cell in the package that is not
verbatim**. It is replaced by a trim cell that applies no shift - none is needed - and instead gives
both raw streams a common origin by cutting each to start at the first workload boundary in
`runinfo.pkl`. With that in place `rq1_E2_arrowlake.ipynb` runs all 16 cells to completion.

The trim itself is close to inert, because the two streams only overlap for about 6 s before the
first workload begins. It removes 5,836 of 438,241 merged rows (1.3%), and moves every RQ3 domain
share by at most 0.09 pp:

| Domain share of `abs_total` | untrimmed | trimmed | delta |
|---|---|---|---|
| CPU | 90.74% | 90.83% | +0.09 pp |
| DRAM | 5.62% | 5.56% | −0.06 pp |
| DISK | 52.64% | 52.70% | +0.05 pp |
| NIC | 0.71% | 0.70% | −0.01 pp |
| RAPL PKG | 82.47% | 82.51% | +0.04 pp |

(Total integrated energy 0.018303 → 0.018257 kWh, −0.25%. Note the RQ3 cells integrate over the
whole merged frame without clipping to the workload window, which is why the trim touches them at
all; RQ1 and RQ2 clip to the workload span themselves and are unaffected.)

To restore the original behaviour exactly, replace the trim cell's body with the original line:
`df_rapl_watt["timestamp"] = df_rapl_watt["timestamp"] - 8642644546100` - the notebook will then
fail at the merge, as the source notebook does.

**Open question for the authors.** If a non-zero offset is intended for E2, the correct value needs
to be supplied — `8_642_644_546_100` is not reproducible against this data and may belong to a
different capture.

**Related, and unaddressed: E3 has a genuine uncorrected offset.** Cross-correlation for the
microservice capture peaks at **+0.46 s**, where r improves from 0.9632 to 0.9988. No offset is
applied to E3 anywhere. At the 100 ms integration windows RQ3 uses this is second-order, but it is a
real misalignment and worth checking before the E3 numbers are reused.

---

<a name="k4"></a>
## K4 — Dead timestamp-unwrapping cell (E1)

**Where:** E1 cell 3 defines `correct_timestamp_overflow` (adding multiples of `2**32 * 125 / 3`)
and assigns `corrected_df` from it. E1 cell 4 then reassigns `corrected_df` from the raw frame `df`,
discarding cell 3's result entirely.

**Effect on results.** None — the output is overwritten before use. Per K2 there is also no wrap in
the data for it to correct. Retained because it is part of the original notebook.

**But it is not harmless to run** — see K11.

---

<a name="k5"></a>
## K5 — Redundant `timestamp == 0` re-filter (E1)

**Where:** E1 cell 10 filters `timestamp != 0` on `df_rapl_watt`, after cell 7's cleaning has
already removed those rows from `df_rapl`.

**Effect on results.** None. E1 has zero such rows to begin with (E2 has 0, E3 has 1). A no-op.

---

<a name="k6"></a>
## K6 — `power_correlation_r2.csv` is not used by the paper

**Where:** E1 cell 20, E2 cell 19. Written to `outputs/*/power_correlation_r2.csv`.

**Values:** R² = 0.6994 (Coffee Lake), 0.1678 (Arrow Lake).

**Why it matters.** These do not appear anywhere in the manuscript, and are easily mistaken for the
R² in Table `tableWilcox` (0.9966 / 0.9979), which comes from the **energy** orthogonal regression
(E1 cell 22 / E2 cell 24), not from this file. A cell comment says it was added to answer a review
question about a correlation being NA; it was not adopted into the table.

The same applies to the per-sample power Wilcoxon backing appendix `arrowPowerWilcoxon`: it is
computed on bracket-merged data (K1), and per-sample RAPL "power" is dE/dt over a ~1 ms interval with
timing jitter, which is far noisier than the 100 ms windowed energy the rest of RQ1 uses. Its
reported MAE of 53–70 W against a CPU averaging ~25 W reflects that noise rather than a real power
discrepancy.

**Effect on results.** None on the Results section. The appendix table is affected by K1.

---

<a name="k7"></a>
## K7 — Superseded notebook mixes two experiments' metadata

**Where:** `rq1-rapl-accuracy/supplementary/rq1_E1_coffeelake_statistical_SUPERSEDED.ipynb`.

It reads the **Coffee Lake** sensor and RAPL data but loads the **Arrow Lake** `runinfo.pkl`, so its
per-workload windows do not correspond to the data being cut. It is included only to document the
provenance of some CSVs that shipped in the Coffee Lake folder, and is excluded from the replication
path. `rq1_E1_coffeelake.ipynb` is the correct Coffee Lake RQ1 notebook.

---

<a name="k8"></a>
## K8 — E1 and E3 share identical window boundaries

E1's hardcoded `workloads` / `segments` (cell 17) and E3's `segments` (cell 23) are byte-identical:

```
17398106102790, 17408115632950, 17418125344082, 17428134473623, 17438142474785,
17448150626666, 17458160825127, 17468168994269, 17478177946304, 17488185946880,
17498193747238
```

These are different machines running different workloads — a Coffee Lake instruction sweep and an
Arrow Lake microservice sweep — captured months apart. The same eleven nanosecond boundaries should
not apply to both. One list is most likely a copy-paste carried over from the other.

**This is unresolved.** It cannot be settled from the files in this package; it needs checking
against the original run logs. If E1's boundaries are the ones that drifted, E1's per-instruction
windows are cutting the wrong spans, which would affect RQ1 and RQ2 for Coffee Lake.

Note E3's boundaries are self-consistent with its own data (11 boundaries, 10 partitions of ~10 s,
matching the ten `w1_rate=*` wrk2 logs in `collection-code/wrk2-logs/`), which weakly favours E3's
list being the original.

---

<a name="k9"></a>
## K9 — Notebooks rely on imports leaking between cells

Several cells use `plt`, `pickle`, or `scipy` without importing them, relying on an earlier cell
having done so. Since this package selects a subset of cells, that can surface as a `NameError`
depending on which notebook you run.

In E2 specifically, `pickle` came from source cell 17 and `scipy` from source cell 18, neither of
which is in every selection. The replacement trim cell (see K3) imports both, so the E2 notebooks no
longer hit this. E1 and E3 are unaffected in practice but the same fragility exists.

**Effect on results.** None on values; it affects whether a notebook runs top to bottom. If you hit
a `NameError`, add the import at the top of the notebook:

```python
import pickle
import numpy as np
import pandas as pd
import scipy, scipy.stats
import matplotlib.pyplot as plt
```

---

<a name="k10"></a>
## K10 — `DataFrame.applymap` was removed in pandas 3.0

**Where:** E1 source cell 19 and E2 source cell 18, in the block that formats the Wilcoxon results
for LaTeX:

```python
df_descriptive_latexfriendly = df_descriptive_latexfriendly.applymap(
    lambda x: f"${x :.3f}$" if isinstance(x, (int, float)) else x)
```

`DataFrame.applymap` was deprecated in pandas 2.1 and removed in 3.0. On pandas 3.x these cells
raise `AttributeError: 'DataFrame' object has no attribute 'applymap'`.

**Effect on results.** None — it only formats an already-computed table for printing. But it stops
the notebook, and it does so *after* the statistics have been computed, which is easy to misread as
a failure of the analysis.

**Two ways round it, neither applied here:**

* Pin `pandas<3` (what `ENVIRONMENT.md` now specifies), or
* Substitute `.map(...)` for `.applymap(...)`, which is the direct replacement.

---

<a name="k11"></a>
## K11 — The dead E1 cell blocks the E1 notebooks on pandas 3.x

**Where:** E1 source cell 3, the `correct_timestamp_overflow` cell that K4 shows is dead code.

Being dead does not stop it from being *executed*. Inside it:

```python
OVERFLOW_CONST = 2**32 * 125 / 3          # 178,956,970,666.66666 - a float
corrected = result_df[time_col].astype("int64").copy()
...
offset += overflow_constant
corrected.iloc[i] += offset               # float written into an int64 Series
```

E1 has one flagged rollover, so the loop does fire and does assign a float into an `int64` column.
On pandas 3.0.3 that raises:

```
TypeError: Invalid value '17706008789168.668' for dtype 'int64'
```

The E1 notebooks therefore stop at their **second code cell**, before any analysis runs. Observed on
pandas 3.0.3; on the pinned `pandas<3` this path silently coerced instead, which is how the notebook
originally ran.

**Effect on results.** None, once you get past it — the cell's output is discarded by the very next
cell (K4). But as shipped on a modern pandas the E1 notebooks produce nothing at all.

**Ways round it, none applied here:**

* Install `pandas<3` as `ENVIRONMENT.md` specifies — the documented environment, and the one the
  original analysis ran in.
* Or skip E1 source cell 3 entirely. This is safe by inspection: the next cell reassigns
  `corrected_df` from the raw frame `df`, so nothing downstream can observe the difference.

---

## Reproducing these findings

`verify_timestamp_overflow.py` (in this directory) covers K2 and part of K3: it reports counter
wraps, out-of-order samples, duplicate timestamps and corrupted records for all six raw streams, and
needs only numpy. The remaining measurements — the K1 rail comparison, the K1 coverage deltas, and
the K3 cross-correlation and match-rate scans — were made with throwaway scripts against the data in
`data/`; the method for each is described in full above and each is a short script to rewrite.
