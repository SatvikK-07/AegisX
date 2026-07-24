# ITCH decoder validation

The table-driven decoder validates exact body sizes, including the type byte.
It decodes:

`S(12)`, `R(39)`, `H(25)`, `A(36)`, `F(40)`, `E(31)`, `C(36)`,
`X(23)`, `D(19)`, `U(35)`, `P(44)`, `Q(40)`, and `B(19)`.

It recognizes, length-checks, counts, and safely skips administrative messages:

`Y(20)`, `L(26)`, `V(35)`, `W(12)`, `K(28)`, `J(35)`, `h(21)`,
`I(50)`, and `N(20)`.

Known types with wrong lengths and truncated frames are errors. Unknown types
fail in strict mode. Permissive mode skips only an already framed unknown body
and records its type and count.

## Real-feed evidence

Two complete official Nasdaq BX archives were scanned: 62,701,676 framed
messages in total. The strict C++ decoder replayed the extracted AAPL streams
with zero unknown types and no framing or length failures.

The August stream recorded:

- 319,100 framed messages;
- 314,151 decoded events;
- 4,949 recognized administrative skips;
- 9,421,809 bytes processed;
- segment SHA-256
  `e1c89702da15158c1c2b5514119c19dcdd05ddfb9ff1272c0d6f6a91f3a6638e`.

## Deterministic test evidence

`data/fixtures/itch_reference_segment.itch` is generated independently by a
specification-layout encoder. Tests spot-check decoded fields, exact lengths,
strict/permissive behavior, truncation, all newly recognized administrative
types, and streaming callbacks. The fixture exists for fast correctness tests;
official-feed validation comes from the two external sessions above.
