# Prometheus RFC 8785 conformance corpus

This directory is the shared Python/C++ byte-identity corpus for Program 01A.
Successful cases point to a raw JSON input, exact RFC 8785 bytes, and a SHA-256
object identity. Failure cases name the stable Prometheus error code expected in
both implementations.

Ordinary inputs are checked-in byte files. Inputs that are deliberately invalid
UTF-8 or exceed a configured resource limit use a deterministic `hex` or
`generated` recipe in `manifest.json`. This avoids treating malformed bytes as
repository text and avoids checking in redundant multi-megabyte files. A recipe
is part of the corpus contract: both language harnesses must construct the same
bytes from it, and no corpus case may be skipped.

The `rfc-strings`, `rfc-numbers`, and `utf16-property-order` cases are drawn from
RFC 8785 Sections 3.2.2 and 3.2.3. The smallest-subnormal, largest-finite,
low fixed-notation threshold, and scientific-notation cases use values from
RFC 8785 Appendix B. The safe-integer bounds follow the interoperability
recommendation in Appendix B. The high fixed-notation case stays within that
safe domain so its canonical spelling remains acceptable to the strict parser.

Prometheus adds fail-closed policies beyond base RFC 8785 serialization:

- negative zero is rejected instead of collapsing to `0`;
- integer tokens outside the interoperable safe-integer range are rejected;
- overflow, nonzero underflow, NaN, and infinities are rejected;
- duplicate decoded keys, invalid UTF-8, BOM-prefixed input, and lone UTF-16
  surrogates are rejected;
- raw bytes, nesting, nodes, object members, array elements, and individual
  UTF-8 strings are bounded.

NFC and NFD are intentionally not normalized. Their distinct canonical bytes and
hashes are part of the contract.
