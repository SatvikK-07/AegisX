# Assumptions

- Prices are signed fixed-point ticks; stored metadata records a scale of 10,000.
- Input framing is two-byte big-endian length followed by an ITCH body.
- Events use deterministic input order; same-timestamp messages are not reordered.
- Displayed depth is the only available liquidity. Hidden, auction, odd-lot, routing, and cross-venue effects are outside the book model.
- Passive cancellation queue reduction is a configurable fraction, not an empirically calibrated fill model.
- Fees, latency, scheduling parameters, and risk limits are scenario inputs.
- Research artifacts must be regenerated whenever source data, configuration, or code changes.
