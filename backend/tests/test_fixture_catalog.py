import pytest

from app.fixture_catalog import FixtureRequestError, get_fixture


def test_exact_fixture_lookup_returns_canonical_identity():
    fixture = get_fixture(" prometheus fixture works ", "pm-36-gm", None)
    assert fixture.manufacturer == "Prometheus Fixture Works"
    assert fixture.part_number == "PM-36-GM"
    assert fixture.source.uri == "fixture://prometheus/pm-36-gm/fixture-1"
    assert all(item.evidence_class == "synthetic_fixture" for item in fixture.parameters)
    assert fixture.source.document_hash == fixture.source_file_hash()


@pytest.mark.parametrize(
    ("manufacturer", "part_number"),
    [
        ("Other Company", "PM-36-GM"),
        ("Prometheus Fixture Works", "UNKNOWN"),
        ("", "PM-36-GM"),
    ],
)
def test_unknown_fixture_identity_fails_closed(manufacturer: str, part_number: str):
    with pytest.raises(FixtureRequestError) as error:
        get_fixture(manufacturer, part_number, None)
    assert error.value.code == "fixture_identity_not_found"


def test_fixture_mode_rejects_caller_supplied_source_url():
    with pytest.raises(FixtureRequestError) as error:
        get_fixture(
            "Prometheus Fixture Works",
            "PM-36-GM",
            "https://example.com/unrelated.pdf",
        )
    assert error.value.code == "fixture_source_url_not_allowed"
