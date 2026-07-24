# Limitations

## Data scope

The canonical evidence uses two Nasdaq BX sample days and AAPL. It does not
cover the primary Nasdaq book, multiple symbols, multiple years, corporate
actions, or diverse volatility regimes. A 31-day gap separates training and
evaluation samples. Raw archives are downloaded from Nasdaq but not committed
or redistributed.

The decoder length-validates every ITCH 5.0 type observed in these sessions but
semantically decodes only messages needed for book, trade, and session state.
Recognized administrative messages are counted and skipped.

## Execution

All fills are simulated against historical displayed messages. There is no
hidden liquidity, venue routing, auction participation, self-impact feedback,
empirically calibrated queue cancellation probability, or matching-engine
acknowledgement distribution. The adaptive size-sweep result is conditional on
one day, one venue, one symbol, one side, and the configured latency/fee values.
It must not be presented as live savings or expected profitability.
The canonical size sweep uses zero configured fees and latency; non-zero
latency stages and maker/taker fees are implemented and unit-tested but require
venue/date-specific calibration before they belong in the real-data headline.

## Research

The real research set has 153 passive child observations from one evaluation
day. Chronological and class gates make the reported experiment valid as a
small exploratory test, not as a production model study. The models do not
show predictive edge, and no alpha claim is made.

## Risk and operations

The risk engine is an in-process research component. It lacks durable
multi-account ledgers, authentication, reconciliation, regulatory reporting,
distributed failover, and production observability. Local nanosecond timings
are machine/compiler measurements, not production SLOs.

The dashboard reads prepared local artifacts only. It does not connect to a
venue, broker, or data entitlement service.
