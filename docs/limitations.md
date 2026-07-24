# Limitations

AegisX supports a selected ITCH 5.0 subset and skips several recognized framed messages. It does not claim full exchange-protocol coverage. Its fixtures are synthetic, tiny, and unsuitable for capacity, latency, alpha, execution-quality, or financial-performance conclusions.

The execution model has no hidden liquidity, venue selection, empirically
calibrated market impact, or calibrated queue-position/cancellation parameters.
It timestamps an exchange acknowledgement but does not simulate venue-specific
matching-engine acknowledgement queues. Passive repricing is represented in the
decision vocabulary but an automatic cancel/reprice loop is not claimed. The
risk engine is not a production risk service: it has no durable ledger, account
aggregation, reconciliation, authentication, or regulatory workflow. The
dashboard only reads prepared local files and does not validate data licensing.

The bundled fixtures are too small for a defensible out-of-sample machine
learning score. The research runner deliberately refuses one-class and
undersized training periods.

The committed CI workflow defines hosted checks. A local pass is not evidence
that hosted CI is green; the exact hosted run must be checked after each push.
