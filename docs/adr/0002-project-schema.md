# ADR-0002: portable versioned project schema

Status: accepted. Projects persist as versioned JSON manifests plus content-addressed artifacts, never serialized Qt, Open Cascade, MuJoCo, or Python objects. Writes will use temporary-file plus atomic replacement and retain a recoverable previous manifest. Unknown future fields are preserved by migration tooling.
