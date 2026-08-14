import hashlib
import json
from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FIXTURE_PATH = REPOSITORY_ROOT / "fixtures" / "evidence" / "pm-36-gm.synthetic.json"
SUPPORTED_FIXTURE_IDS = ["prometheus-fixture-works/PM-36-GM"]


def normalize_identity(value: str) -> str:
    return "".join(character.lower() for character in value if character.isalnum())


class FixtureRequestError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def fixture_error_detail(error: FixtureRequestError) -> dict[str, Any]:
    return {
        "code": error.code,
        "message": str(error),
        "provider": "fixture",
        "supported_fixture_ids": list(SUPPORTED_FIXTURE_IDS),
    }


@dataclass(frozen=True)
class FixtureSource:
    uri: str
    title: str
    revision: str
    content_type: str
    rights_status: str
    document_hash: str


@dataclass(frozen=True)
class FixtureParameter:
    name: str
    quantity: str
    dimension: str
    value: dict[str, Any]
    unit: str
    original_value: str
    original_unit: str
    validity_conditions: tuple[str, ...]
    evidence_class: str = "synthetic_fixture"


@dataclass(frozen=True)
class ComponentFixture:
    fixture_id: str
    source_path: Path
    manufacturer: str
    part_number: str
    revision: str
    family: str
    component_class: str
    source: FixtureSource
    parameters: tuple[FixtureParameter, ...]
    supported_recipes: tuple[str, ...]
    missing_information: tuple[dict[str, str], ...]
    limitations: tuple[str, ...]

    def source_file_hash(self) -> str:
        digest = hashlib.sha256(self.source_path.read_bytes()).hexdigest()
        return f"sha256:{digest}"


def _require_exact_keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ValueError(f"invalid {label} keys; missing={missing}, extra={extra}")


def _number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _validate_engineering_value(value: dict[str, Any], name: str) -> None:
    kind = value.get("kind")
    if kind == "scalar":
        _require_exact_keys(value, {"kind", "value"}, f"{name} scalar value")
        if not _number(value["value"]):
            raise ValueError(f"{name} scalar value must be numeric")
        return
    if kind == "range":
        _require_exact_keys(value, {"kind", "minimum", "maximum"}, f"{name} range")
        if not _number(value["minimum"]) or not _number(value["maximum"]):
            raise ValueError(f"{name} range bounds must be numeric")
        if value["minimum"] > value["maximum"]:
            raise ValueError(f"{name} range minimum exceeds maximum")
        return
    if kind == "enumeration":
        _require_exact_keys(value, {"kind", "values"}, f"{name} enumeration")
        if not isinstance(value["values"], list) or not value["values"]:
            raise ValueError(f"{name} enumeration must contain values")
        return
    if kind == "curve":
        _require_exact_keys(
            value,
            {
                "kind",
                "independent_quantity",
                "independent_unit",
                "interpolation",
                "points",
            },
            f"{name} curve",
        )
        points = value["points"]
        if not isinstance(points, list) or len(points) < 2:
            raise ValueError(f"{name} curve must contain at least two points")
        previous_x: float | None = None
        for point in points:
            if set(point) != {"x", "y"} or not _number(point["x"]) or not _number(point["y"]):
                raise ValueError(f"{name} curve points must contain numeric x and y")
            if previous_x is not None and point["x"] <= previous_x:
                raise ValueError(f"{name} curve x values must increase")
            previous_x = point["x"]
        return
    if kind == "unknown":
        _require_exact_keys(value, {"kind", "reason"}, f"{name} unknown value")
        if not isinstance(value["reason"], str) or not value["reason"].strip():
            raise ValueError(f"{name} unknown value requires a reason")
        return
    raise ValueError(f"{name} has unsupported value kind {kind!r}")


def _load_fixture(path: Path) -> ComponentFixture:
    payload = json.loads(path.read_text(encoding="utf-8"))
    _require_exact_keys(
        payload,
        {
            "schema_version",
            "fixture_id",
            "manufacturer",
            "part_number",
            "revision",
            "family",
            "component_class",
            "source",
            "parameters",
            "supported_recipes",
            "missing_information",
            "limitations",
        },
        "fixture",
    )
    if payload["schema_version"] != "1.0.0":
        raise ValueError("unsupported fixture schema version")
    source_payload = payload["source"]
    _require_exact_keys(
        source_payload,
        {"uri", "title", "revision", "content_type", "rights_status"},
        "fixture source",
    )
    document_hash = f"sha256:{hashlib.sha256(path.read_bytes()).hexdigest()}"
    source = FixtureSource(document_hash=document_hash, **source_payload)
    parameter_keys = {
        "name",
        "quantity",
        "dimension",
        "value",
        "unit",
        "original_value",
        "original_unit",
        "validity_conditions",
    }
    parameters = []
    names: set[str] = set()
    for raw_parameter in payload["parameters"]:
        _require_exact_keys(raw_parameter, parameter_keys, "fixture parameter")
        name = raw_parameter["name"]
        if name in names:
            raise ValueError(f"duplicate fixture parameter {name}")
        names.add(name)
        _validate_engineering_value(raw_parameter["value"], name)
        parameters.append(
            FixtureParameter(
                name=name,
                quantity=raw_parameter["quantity"],
                dimension=raw_parameter["dimension"],
                value=deepcopy(raw_parameter["value"]),
                unit=raw_parameter["unit"],
                original_value=raw_parameter["original_value"],
                original_unit=raw_parameter["original_unit"],
                validity_conditions=tuple(raw_parameter["validity_conditions"]),
            )
        )
    return ComponentFixture(
        fixture_id=payload["fixture_id"],
        source_path=path,
        manufacturer=payload["manufacturer"],
        part_number=payload["part_number"],
        revision=payload["revision"],
        family=payload["family"],
        component_class=payload["component_class"],
        source=source,
        parameters=tuple(parameters),
        supported_recipes=tuple(payload["supported_recipes"]),
        missing_information=tuple(deepcopy(payload["missing_information"])),
        limitations=tuple(payload["limitations"]),
    )


_FIXTURE = _load_fixture(FIXTURE_PATH)
FIXTURES = {
    (
        normalize_identity(_FIXTURE.manufacturer),
        normalize_identity(_FIXTURE.part_number),
    ): _FIXTURE
}


def get_fixture(
    manufacturer: str,
    part_number: str,
    source_url: str | None,
) -> ComponentFixture:
    if source_url is not None:
        raise FixtureRequestError(
            "fixture_source_url_not_allowed",
            "Fixture mode cannot attribute synthetic values to a caller-supplied URL.",
        )
    key = (normalize_identity(manufacturer), normalize_identity(part_number))
    fixture = FIXTURES.get(key)
    if fixture is None:
        raise FixtureRequestError(
            "fixture_identity_not_found",
            "The offline fixture provider supports only Prometheus Fixture Works / PM-36-GM.",
        )
    if fixture.source.document_hash != fixture.source_file_hash():
        raise FixtureRequestError(
            "fixture_integrity_failure",
            "The checked-in synthetic fixture no longer matches its loaded content hash.",
        )
    return deepcopy(fixture)
