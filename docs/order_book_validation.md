# Order-book and market-state validation

The book stores bid and ask maps of FIFO lists plus an order-ID locator table. Every reduce, erase, and replace verifies the locator's exact ID and quantity. Checksums are available at top-of-book, aggregate-depth, full-order-state, and FIFO-order granularity; `MarketState` adds instrument routing and symbol state.

The test suite runs two seeded 10,000-operation sequences against a reference model. It combines adds, cancels, executions, deletes, and replacements, checking active-order count, affected FIFO queues, and invariants after every mutation. Focused tests cover globally duplicate IDs across instruments, mismatched stock locates without mutation, directory/add symbol conflicts, strong replacement behavior, and deterministic two-instrument replay isolation.
