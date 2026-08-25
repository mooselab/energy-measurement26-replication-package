# Raw measurement data

Each experiment folder holds the two concurrent capture streams. See `../ENVIRONMENT.md`
for the binary layouts, and run `../verify_timestamp_overflow.py` for an integrity check.

| Folder | System | Workload | Streams |
|---|---|---|---|
| `E1-coffeelake-instructions` | Coffee Lake | instruction-set sweep | `rapl/*.bin`, `teensy/data*.bin` + `metadata` |
| `E2-arrowlake-instructions`  | Arrow Lake  | instruction-set sweep | `rapl/*.bin`, `teensy/data*.bin` + `metadata`, `runinfo.pkl` |

RQ3 (the DeathStarBench microservice experiment) is not part of this RQ-organized structure yet;
its data currently lives in the sibling `microservice/` folder and will be migrated later.

`psu-efficiency/` is where `modifier.npy` belongs; it is not included - see `../MISSING_INPUTS.md`.
