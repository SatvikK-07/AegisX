# Final upgrade requirement audit

Status values are **pass**, **partial**, or **not claimed**. A partial item is
kept visible instead of being relabelled as complete.

| Area | Status | Evidence |
|---|---|---|
| Offline C++20 build, strict warnings | pass | CMake and CI matrix |
| Reference ITCH fixture and checksum | pass | fixture CTest and `itch_validation.md` |
| Multi-instrument FIFO replay | pass | deterministic and randomized native tests |
| Parent-order conservation | pass | explicit state invariant plus transition/terminal tests |
| Immutable arrival benchmark | pass | arrival/latency native test |
| Historical/visible/shadow state separation | pass | execution implementation and shadow tests |
| Aggressive depth walk, limits, no double consumption | pass | level walk and shadow accounting |
| Passive FIFO queue model | pass | shared-flow conservation and queue-history tests |
| Passive repricing policy | partial | action type exists; no automatic reprice loop is claimed |
| Market-data/decision/transmission/ack latency | pass | explicit timestamps and delayed visible replay |
| Per-share and bps maker/taker fees | pass | fill-level fee accounting |
| Distinct TWAP/VWAP/POV/adaptive policies | pass | separate schedule/volume/decision paths |
| Full execution cost report | pass | shortfall ticks/bps/currency, market VWAP, spread, opportunity, adverse selection, impact proxy, fees, schedule deviation |
| Position, P&L, reservations, limits | pass | risk unit tests and audit demo |
| Parent and open-order limits | pass | execution integration and risk checks |
| Gross/net/concentration including reservations | pass | risk regression test |
| Kill-switch lifecycle/audit log | pass | risk audit artifact |
| Historical/parametric VaR, ES, component and liquidity risk | pass | Python risk helpers/tests |
| Named portfolio stress scenarios | pass | Python stress report/tests |
| Leakage-safe research pipeline | pass | purged split, feature guard, model/economic tests |
| Credible real-feed ML score | not claimed | bundled fixtures are intentionally insufficient |
| Six execution regimes/four strategies | pass | execution benchmark artifact |
| One-million-check risk benchmark | pass | `PERFORMANCE.md` |
| Read-only dashboard | pass | execution trace, risk audit/stress, research/provenance tabs |
| GCC/Clang, sanitizer, format, tidy, Python CI | pass when hosted run is green | `.github/workflows/ci.yml` |
| Live venue connectivity, persistence, HA | not claimed | explicitly outside research-platform scope |

## Correctness audit

- Quantity conservation is asserted after submissions, fills, cancellations,
  risk rejections, and terminal cleanup.
- Passive external flow is a shared quantity consumed once in own-order FIFO.
- Reservations enter projected position, gross/net exposure, open exposure, and
  order count and are released only on fill completion or effective cancel.
- Same-price historical adds restore shadow liquidity; a market walk cannot
  consume one displayed unit twice.
- Buy adverse selection is positive when the future midpoint rises; sell
  adverse selection is positive when it falls.

## Simplicity audit

The upgrade adds no broker, exchange adapter, container, message bus, database,
or distributed service. Configuration remains versioned JSON and all generated
run data remains outside source control.
