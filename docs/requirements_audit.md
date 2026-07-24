# Final requirement audit

Status values are **pass**, **partial**, and **not claimed**. Partial and
negative results remain visible.

| Area | Status | Evidence |
|---|---|---|
| Official external ITCH data | pass | two complete Nasdaq-hosted BX archives, URLs, sizes, gzip integrity, SHA-256 |
| Chronologically separate profile/evaluation sessions | pass | July training and August evaluation |
| Strict real-feed decoder validation | pass | 62.7M frames scanned; zero unknown types in both AAPL streams |
| Multi-instrument FIFO engine | pass | native deterministic/randomized tests; real canonical run is AAPL-only |
| Full ITCH 5.0 semantic coverage | partial | all observed types are length-validated; selected administrative types are intentionally skipped |
| Parent-order conservation | pass | explicit invariant on submission/fill/cancel/terminal transitions |
| Historical/visible/shadow state separation | pass | implementation and regression tests |
| Aggressive depth walk/no double consumption | pass | level walk and shadow accounting |
| Passive FIFO queue model | pass | shared-flow conservation and queue-history tests |
| Passive urgency cancel/replace | pass | opt-in real-run policy and native test |
| General dynamic repricing | partial | no venue-calibrated continuous reprice model |
| Four execution policies | pass | TWAP, earlier-session-profile VWAP, instrument POV, adaptive |
| Full execution cost report | pass | shortfall, VWAP, spread, opportunity, adverse selection, impact proxy, fees, schedule |
| Real execution experiment | pass | five 100%-completed August size comparisons |
| Market-regime generalization | not claimed | one venue/symbol/session/side cannot establish regime robustness |
| Position/P&L/reservation limits | pass | native tests and audit artifact |
| Gross/net/concentration/open-order limits | pass | projected exposure includes reservations |
| Kill-switch lifecycle/audit | pass | risk artifacts and tests |
| VaR/ES/component/liquidity/stress analytics | pass | Python helpers and tests |
| Leakage-safe real-data research | pass | checksum gates, purged chronology, sample/class minimums |
| Predictive ML edge | not claimed | real test metrics do not beat baselines convincingly |
| Read-only dashboard | pass | provenance, replay, execution, risk, research, performance |
| GCC/Clang/sanitizer/format/tidy/Python CI | pass when current hosted run is green | `ci.yml` |
| Full real-data hosted validation | manual | `real-data-validation.yml`; raw archives are not committed |
| Live connectivity, persistence, HA | not claimed | research-platform scope |

## Correctness invariants

- Target quantity equals filled plus open plus remaining-unsubmitted plus
  terminal-unfilled quantity.
- Passive external flow is consumed once across own children in FIFO order.
- Same-price historical adds reconcile shadow liquidity; displayed units cannot
  be consumed twice.
- Risk reservations enter projected position, gross/net/symbol/open exposure,
  and order counts.
- Replay, execution, research, and provenance checksums must identify the same
  real input segment.
- Buy adverse selection is positive when the post-fill midpoint rises; sell
  adverse selection is positive when it falls.

Synthetic fixtures remain only for deterministic unit/CI checks and are not
used as external-market evidence.
