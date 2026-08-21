# Bounded 2024 aluminum evidence for the YUBI bracket trial

This note records candidate evidence, not an assigned YUBI material model. The
only project-specific material statement found for `BRACKET_GRIPPER` is
`A2024`. No reviewed source in this evidence set establishes the delivered
temper, product form, thickness, elastic constants, or an allowable stress for
that part. Prometheus must therefore keep structural execution blocked until an
engineer either supplies applicable material records or explicitly marks a
bounded material model as an assumption.

The machine-readable record is
[`fixtures/evidence/aluminum-2024-candidates-v1.json`](../../fixtures/evidence/aluminum-2024-candidates-v1.json).

## Candidate claims

| Candidate | What the source supports | What it does not support |
| --- | --- | --- |
| Toyota YUBI BOM | BOM item 34 identifies `BRACKET_GRIPPER` as `A2024`. | Temper, product form, thickness, elastic properties, strength, and delivered-material identity. |
| Kaiser Aluminum technical data | For the datasheet's 2024 sheet/coil/plate scope, the T4/T351 row lists a typical modulus of elasticity of `73.1 GPa`. | YUBI applicability, a Poisson ratio, a material certificate, or a stress requirement. |
| MIL-HDBK-5J Revision J | Table 3.2.3.0(d) lists tensile modulus `E = 10.7 × 10^3 ksi` and Poisson ratio `0.33` for bare 2024 sheet and plate, all tempers, at thickness at least `0.250 in`. Table 3.2.3.0(b1) includes T351 plate in that product scope. | YUBI applicability, delivered-material identity, current certification authority, or an automatic stress requirement. |

## Source identities and locators

### Toyota YUBI BOM

The project source is the Toyota `yubi-hw` repository at commit
`e8334ff04945ccf56c0576a56f6fab74b63daaa2`. In
[`YUBI Gripper_DYNAMIXEL_BOM.csv`](https://raw.githubusercontent.com/Toyota/yubi-hw/e8334ff04945ccf56c0576a56f6fab74b63daaa2/docs/BOM/YUBI%20Gripper_DYNAMIXEL_BOM.csv),
physical line 37 is BOM item 34 and places `A2024` in the material column for
`BRACKET_GRIPPER`.

The repository blob is Git object
`18ce2e51d939f298f359569a30a398e551d9652e`. Its exact LF bytes hash to
`sha256:a16691d0f83c5dac275fd89f0336842f131f4ea441a12004bd08cb0977e28562`.
The existing Windows-first trial pinned the CRLF checkout representation,
`sha256:5251dad9b3f3c3deb26aeb0d7b887c6418c90593389f95b12df73ab694c7922b`.
Those hashes identify different newline representations of the same pinned Git
blob; they are not interchangeable artifact identities.

### Kaiser Aluminum technical data

Kaiser Aluminum's two-page
[`Sheet Coil & Plate Alloy 2024: Technical Data`](https://online.kaiseraluminum.com/depot/PublicProductInformation/Document/1012/Kaiser_Aluminum_2024_Sheet_Coil_and_Plate.pdf)
is marked `Rev. 05/06`. On page 1, the Typical Mechanical Properties table
lists `73.1 GPa` in the modulus-of-elasticity column for the `T4, T351` row. The
table is explicitly typical producer data. It does not report Poisson's ratio.

### MIL-HDBK-5J Revision J

The official [DLA ASSIST record](https://quicksearch.dla.mil/qsDocDetails.aspx?ident_number=53876)
identifies Revision J, dated 31 January 2003, and marks the handbook canceled
as of 24 March 2006. The record points readers to Notice 2 for replacement
information. A public copy used for table inspection was obtained through the
[Abbott Aerospace technical-library entry](https://www.abbottaerospace.com/downloads/mil-hdbk-5j-metallic-materials-and-elements-for-aerospace-vehicle-structures/);
the retrieved 74,073,423-byte PDF hashes to
`sha256:c581072c5638c97e0e2cc1ce7d17cb04886478a9890cbac63ced7b6c9c0789e7`.

The relevant source locations are:

- Table 3.2.3.0(b1), printed page 3-71: bare 2024 plate in T351 is listed from
  `0.250 in` through `4.000 in` across the stated thickness bands.
- Table 3.2.3.0(d), printed page 3-74 and PDF page 376: bare 2024 sheet and
  plate, all tempers, at thickness `>= 0.250 in` has `E = 10.7` in units of
  `10^3 ksi` and `mu = 0.33`.
- Section 1.4.4.1 defines `E` as modulus of elasticity from the initial tensile
  stress-strain slope. Table 3.2.3.0(d) does not assign a material direction to
  this value. It must not be described as a transverse modulus.

MIL-HDBK-5J is retained here as bounded historical reference data. Its canceled
status and the existence of a successor do not make its numbers project-specific
or currently authoritative for the YUBI bracket.

## Unit derivation

The [NIST Guide to the SI, Appendix B.9](https://www.nist.gov/pml/special-publication-811/nist-guide-si-appendix-b-conversion-factors/nist-guide-si-appendix-b9)
lists the pound-force-per-square-inch to pascal conversion. Its
[footnotes](https://www.nist.gov/pml/special-publication-811/nist-guide-si-footnotes)
give the exact conventional pound-force basis, and NIST's
[FiPy unit table](https://pages.nist.gov/fipy/en/4.0/generated/fipy.tools.dimensions.physicalField.html)
provides a high-precision check. Using that basis:

```text
10.7 Msi × 1,000,000 psi/Msi × 6,894.757293168 Pa/psi
  = 73,773,903,036.8976 Pa
```

The evidence contract stores `73,773,903,036.9 Pa` and displays `73.8 GPa`.
The stored precision records a deterministic conversion; it does not imply that
the handbook input is known to comparable measurement precision.

## Required review before use

Prometheus may present these records as candidates. It may not merge them into
an unqualified statement such as “the YUBI bracket is 2024-T351 plate.” To use a
numeric candidate, the reviewer must choose one of two explicit states:

- `known`: supported by project-specific material or manufacturing evidence
  that identifies the supplied bracket's temper, form, and applicable range;
- `assumed`: a hypothetical model whose reports and findings remain labeled
  with the assumption.

The review must also provide displacement and/or stress limits with independent
bases. No yield strength, von Mises limit, factor of safety, or pass threshold is
copied from these candidate records. Without those decisions, the correct
result is `indeterminate`, not `pass`.
