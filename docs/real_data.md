# Real data and provenance

## Official sources

The project downloads two complete Nasdaq BX TotalView-ITCH 5.0 BinaryFILE
samples from Nasdaq's public EMI directory. Data are fetched directly by the
user's local pipeline and are not redistributed through Git.

| Role | Session | Official archive | Bytes | Downloaded SHA-256 |
|---|---|---|---:|---|
| VWAP training | 2019-07-30 | `20190730.BX_ITCH_50.gz` | 391,242,214 | `a0a057010cc5172cfaf2d8a7b4d5f133557fa47b3a5837fcf985b6e5533e50eb` |
| Evaluation | 2019-08-30 | `20190830.BX_ITCH_50.gz` | 449,303,567 | `99b08ce5e0e860246e1d6716394513a06845ed632c8dc838b807d43f4c756fa0` |

Source directory:
`https://emi.nasdaq.com/ITCH/Nasdaq%20BX%20ITCH/`

Protocol specification:
`https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf`

## End-to-end validation

`prepare_real_itch.py` decompresses every byte, so Python's gzip reader checks
the archive footer and CRC. It then scans every BinaryFILE frame, discovers
AAPL from the Stock Directory message, and emits global system events plus
every message routed to AAPL's stock locate.

| Session | Full-feed frames scanned | AAPL frames emitted | Segment bytes | Segment SHA-256 |
|---|---:|---:|---:|---|
| 2019-07-30 | 28,734,686 | 198,850 | 5,849,380 | `078b588e73fac5f6ffaa9357d506dd7df03dad1288dfcd52a0503153d91bd03a` |
| 2019-08-30 | 33,966,990 | 319,100 | 9,421,809 | `e1c89702da15158c1c2b5514119c19dcdd05ddfb9ff1272c0d6f6a91f3a6638e` |

The strict C++ decoder processed the August segment as 314,151 decoded events,
4,949 recognized administrative skips, and zero unknown types. Replay metadata,
execution context, research metadata, and the attached provenance must all
carry the same segment SHA-256.

## Chronology

The July AAPL stream supplies the 13-interval regular-session execution-volume
profile. August is the later evaluation session. The profile records its
training and evaluation dates plus the July segment checksum; the execution
simulator rejects profiles with identical dates or a missing checksum.

## Storage policy

`data/raw/`, `data/processed/`, and `runs/` are ignored except for placeholder
files. This avoids silently publishing hundreds of megabytes of third-party
data and keeps a clean clone small. `configs/real_data.json` records the URLs,
expected sizes, dates, paths, venue, protocol, and symbol needed to reproduce
the run.

Synthetic fixtures still exist exclusively for deterministic unit tests,
sanitizers, and offline CI. They are not used in the canonical performance,
execution, or research results.
