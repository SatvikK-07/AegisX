# Upgrade record

AegisX moved from generated market scenarios to a real-data-first pipeline.

Completed changes:

- official Nasdaq EMI acquisition with resumable parallel ranges;
- two complete Nasdaq BX ITCH 5.0 sessions with gzip integrity and SHA-256
  provenance;
- streaming full-feed scan and AAPL extraction;
- expanded modern ITCH administrative definitions;
- strict real-feed replay with zero unknown message types;
- July training / August evaluation separation for VWAP;
- real-session execution size sweep with urgency cancel/replace;
- separate multi-parent real research-label simulation;
- research checksum, minimum-row, partition-size, and class-balance gates;
- real benchmark artifacts and dashboard provenance/performance views;
- manual hosted real-data validation workflow that does not commit raw data.

Synthetic fixtures remain intentionally for fast offline correctness, edge
cases, sanitizers, and standard CI. They no longer support headline performance
or execution claims.

Future work remains real rather than cosmetic: more dates, symbols, and venues;
primary Nasdaq-book validation; empirically calibrated queue and impact models;
multi-day research with uncertainty estimates; and durable production risk
infrastructure.
