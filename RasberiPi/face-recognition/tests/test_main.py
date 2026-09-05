from src.main import parse_command


def test_a_plain_command_letter():
    assert parse_command(b"e\n") == "e"


def test_surrounding_whitespace_is_ignored():
    assert parse_command(b"  e  \n") == "e"


def test_uppercase_is_accepted():
    assert parse_command(b"Q\n") == "q"


def test_only_the_first_letter_matters():
    assert parse_command(b"enroll\n") == "e"


def test_an_empty_line_is_not_a_command():
    assert parse_command(b"\n") is None
    assert parse_command(b"   \n") is None
    assert parse_command(b"") is None


def test_the_e_key_in_a_cyrillic_layout_still_enrolls():
    # Physical E on a Ukrainian/Russian layout produces U+0443 CYRILLIC U.
    assert parse_command("у\n".encode("utf-8")) == "e"
    assert parse_command("у\n".encode("cp1251")) == "e"


def test_the_q_key_in_a_cyrillic_layout_still_quits():
    assert parse_command("й\n".encode("utf-8")) == "q"
    assert parse_command("й\n".encode("cp1251")) == "q"


def test_a_cyrillic_e_that_looks_like_a_latin_e_enrolls():
    assert parse_command("е\n".encode("utf-8")) == "e"


def test_undecodable_bytes_do_not_raise():
    # This lone byte is what crashed the app: a Cyrillic character sent from a
    # Windows console in an encoding that is not valid UTF-8.
    assert parse_command(b"\xd1") != "CRASH"
    assert parse_command(b"\xff\xfe\x00") != "CRASH"


def test_unknown_letters_pass_through_and_simply_match_nothing():
    assert parse_command(b"z\n") == "z"
