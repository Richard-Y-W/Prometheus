# ADR-0003: JSON/OpenAPI service transport

Status: accepted. V1 uses versioned JSON Schema and OpenAPI over HTTPS; localhost HTTP is development-only. Contracts remain transport-independent so Protobuf/gRPC can be introduced without changing domain meaning.
