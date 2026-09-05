import numpy as np
import pytest

from src.face_database import FaceDatabase, normalize

THRESHOLD = 0.40
MARGIN = 0.05


def vector(*values):
    """Build a 128-length vector whose first entries are the given values."""
    result = np.zeros(128, dtype=np.float32)
    for index, value in enumerate(values):
        result[index] = value
    return result


@pytest.fixture
def database():
    return FaceDatabase(THRESHOLD, MARGIN)


def test_empty_database_returns_unknown(database):
    result = database.match(vector(1.0))
    assert result.name is None
    assert result.score == 0.0
    assert database.is_empty()


def test_exact_match_is_accepted(database):
    database.add("Alice", [vector(1.0, 0.0)])
    database.add("Bob", [vector(0.0, 1.0)])

    result = database.match(vector(1.0, 0.0))

    assert result.name == "Alice"
    assert result.score == pytest.approx(1.0, abs=1e-5)


def test_score_below_threshold_is_unknown_but_reports_the_candidate(database):
    database.add("Alice", [vector(1.0, 0.0)])
    database.add("Bob", [vector(0.0, 1.0)])

    # Equally 0.30 similar to both, so neither clears 0.40.
    result = database.match(vector(1.0, 1.0, 3.0))

    assert result.name is None
    assert result.best_name in ("Alice", "Bob")
    assert result.score == pytest.approx(1.0 / np.sqrt(11.0), abs=1e-5)


def test_ambiguous_pair_is_rejected_by_the_margin_rule(database):
    database.add("Alice", [vector(1.0, 0.0)])
    database.add("Bob", [vector(0.0, 1.0)])

    # 0.707 against both: over the threshold, but no margin between them.
    result = database.match(vector(1.0, 1.0))

    assert result.name is None
    assert result.score == pytest.approx(0.7071, abs=1e-3)
    assert result.runner_up_score == pytest.approx(0.7071, abs=1e-3)


def test_a_person_scores_as_their_best_sample(database):
    # Alice enrolled twice, looking quite different each time.
    database.add("Alice", [vector(1.0, 0.0), vector(0.0, 1.0)])
    database.add("Bob", [vector(0.0, 0.0, 1.0)])

    result = database.match(vector(0.0, 1.0))

    assert result.name == "Alice"
    assert result.score == pytest.approx(1.0, abs=1e-5)


def test_people_lists_names_and_sample_counts(database):
    database.add("Alice", [vector(1.0), vector(1.0, 0.1)])
    database.add("Bob", [vector(0.0, 1.0)])

    people = {person.name: person.samples for person in database.people()}

    assert people == {"Alice": 2, "Bob": 1}


def test_adding_an_existing_name_appends_samples(database):
    database.add("Alice", [vector(1.0)])
    database.add("Alice", [vector(0.0, 1.0)])

    assert database.people()[0].samples == 2


def test_save_and_load_round_trip(tmp_path):
    original = FaceDatabase(THRESHOLD, MARGIN)
    original.add("Alice", [vector(1.0, 0.0)])
    original.add("Bob", [vector(0.0, 1.0)])
    original.save(str(tmp_path))

    restored = FaceDatabase.load(str(tmp_path), THRESHOLD, MARGIN)

    assert [p.name for p in restored.people()] == ["Alice", "Bob"]
    assert restored.match(vector(1.0, 0.0)).name == "Alice"
    assert restored.people()[0].enrolled_at != ""


def test_loading_a_missing_database_gives_an_empty_one(tmp_path):
    restored = FaceDatabase.load(str(tmp_path / "nothing"), THRESHOLD, MARGIN)
    assert restored.is_empty()


def test_normalize_makes_unit_length():
    assert np.linalg.norm(normalize(vector(3.0, 4.0))) == pytest.approx(1.0, abs=1e-6)


def test_normalize_leaves_a_zero_vector_alone():
    assert np.linalg.norm(normalize(np.zeros(128, dtype=np.float32))) == 0.0
