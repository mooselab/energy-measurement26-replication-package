# Missing inputs

One input the published analysis depended on is **not present** in any of the three source folders
this package was assembled from, and one reported table has no source data here. Both are flagged
inline in the notebooks that need them.

---

## 1. `data/psu-efficiency/modifier.npy` — the PSU efficiency surface

**Needed by:** all three RQ3 notebooks (`rq3_E1_coffeelake.ipynb`, `rq3_E2_arrowlake.ipynb`,
`rq3_E3_microservice.ipynb`).

**Original location:** `C:/Users/andyh/Downloads/serialreceive/adjustmentFactor/modifier.npy`

**What it is:** a 2-D lookup array indexed by (minor-rail total, major-rail total) that returns the
PSU's conversion efficiency at that load split. The RQ3 cells use it as:

```python
modifier_array = np.load("../data/psu-efficiency/modifier.npy")
MAX_X_VAL = 900   # major-rail watts at the right edge
MAX_Y_VAL = 160   # minor-rail watts at the top edge
# note: row 0 is the TOP of the surface, so the y index is inverted
iy = max_y - (minor_rails_total / MAX_Y_VAL * max_y).astype(int)
ix =         (major_rails_total / MAX_X_VAL * max_x).astype(int)
psu_multiplier     = modifier_array[iy, ix]
psu_watt           = abs_total * (1 - psu_multiplier)
psu_adjusted_total = abs_total + psu_watt
```

**Consequence:** without it the RQ3 notebooks run up to the PSU cell and then fail. Everything
downstream — the `psu_adjusted_total` denominator, the PSU slice of the coverage pies, and the
11.8%–16% PSU share and 37.1%/53.1% unaccounted-energy figures in Table `tablecoverage` — cannot be
regenerated. The per-domain sensor-vs-RAPL comparison *before* the PSU correction is unaffected.

**To restore:** drop the original `modifier.npy` into `data/psu-efficiency/`. No path edits needed.

---

## 2. Not missing, but not derivable here: Table `tableepi`

Table `tableepi` (RQ2, "RAPL energy underestimation per instruction, ArrowLake") reports a
**Throughput (GInst/s)** column. Instruction-retired counts are not recorded in `runinfo.pkl`, in
the RAPL binaries, or in the Teensy stream, and no performance-counter log is present in any of the
three source folders. The **Bias (J)** column *is* derivable from `rq2_E2_arrowlake_skesd.ipynb`;
the per-instruction normalization is not.

To make this table reproducible, the instruction-retired counts from the workload harness
(`combine_instruction`) need to be added to the package.

---

## Not an issue: E1 has no `runinfo.pkl`

The Coffee Lake experiment (E1) predates the `runinfo.pkl` run-metadata mechanism, so no such file
exists or is needed. Its 10 instruction-set window boundaries are hardcoded as `workloads` and
`segments` in the Step 9 cell of `rq1_E1_coffeelake.ipynb`, `rq2_E1_coffeelake_skesd.ipynb`, and
`rq3_E1_coffeelake.ipynb`. Only the Arrow Lake instruction experiment (E2) ships a `runinfo.pkl`.

**Both E1 and E2 are fully reproducible for RQ1 and RQ2 from what is in this package.**

## Not an issue: E1 needs no timestamp overflow correction

Verified against the raw binaries with `verify_timestamp_overflow.py`: there is no counter wrap in
any Teensy or RAPL stream in any experiment. E1's "overflow correction" cells are really a
corrupt-record filter that removes 2 damaged samples out of 282,360. See `KNOWN_ISSUES.md`, K2.
