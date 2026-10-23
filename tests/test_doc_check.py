"""tools/doc_check.py: license headers and comments on functions declared in headers.

MIT (see LICENSE).
"""

from pathlib import Path

import pytest

from tools.doc_check import ROOT, check_file, check_header_functions, check_license, main


def names(problems: list) -> list[str]:
    """The function names reported in a list of problems."""
    return [p.message.split(":")[0] for p in problems]


@pytest.mark.parametrize(
    "head",
    [
        "/* x.c -- thing. MIT (see LICENSE). */",
        "# Copyright (C) 2021 someone",
        '"""Tool.\n\nlicense"""',
    ],
)
def test_license_found_in_the_header(head):
    """A license stated as MIT, a Copyright line or the word license in the first lines is
    accepted, in C or Python headers.
    """
    assert check_license(Path("x"), head + "\nint a;\n") == []


def test_missing_license_is_reported():
    """A file without a license notice is reported at line 1."""
    (p,) = check_license(Path("x.c"), "/* x.c -- thing */\nint a;\n")
    assert p.line == 1 and "license" in p.message


def test_license_after_the_header_lines_does_not_count():
    """A notice buried far below the header does not count: readers look at the top of the file."""
    text = "/* x */\n" + "\n" * 40 + "/* MIT */\n"
    assert check_license(Path("x.c"), text)


def test_undocumented_declaration():
    """A function declared in a header without a comment is reported by name."""
    text = "/* h.h -- MIT */\n\nint documented_not(int a);\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["documented_not"]


def test_comment_covers_a_group_until_a_blank_line():
    """One comment documents the declarations right under it, but not past a blank line."""
    text = "/* group: both covered */\nint a(void);\nint b(void);\n\nint c(void);\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["c"]


def test_externs_and_forward_structs_stay_in_the_group():
    """Extern variables and forward struct declarations inside a group do not end it (libc_shim.h
    relies on this).
    """
    text = "/* group */\nstruct s;\nextern int g;\nint a(struct s *);\n"
    assert check_header_functions(Path("h.h"), text) == []


def test_typedef_ends_the_group():
    """A comment on a typedef documents the type, not the functions after it."""
    text = "/* the type */\ntypedef struct { int n; } set;\nint set_has(const set *s);\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["set_has"]


def test_multiline_comment_and_declaration():
    """Multi-line comments and declarations split over several lines are recognised, as are //
    comments.
    """
    text = (
        "/* one\n * two */\n"
        "int long_one(int a,\n             int b);\n"
        "// line comment\n"
        "void d(void);\n"
    )
    assert check_header_functions(Path("h.h"), text) == []


def test_struct_members_are_not_declarations():
    """Function-pointer members of a struct are not file-scope declarations."""
    text = "/* api */\ntypedef struct {\n  void *(*fn)(void *self);\n} api;\n"
    assert check_header_functions(Path("h.h"), text) == []


def test_static_inline_definition_needs_a_comment():
    """Static inline functions defined in a header need a comment like declared ones."""
    text = "\nstatic inline int f(int x) {\n  return x;\n}\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["f"]


def test_only_source_headers_get_the_function_rule(tmp_path):
    """Headers outside source/ (test helpers) are not held to the API comment rule."""
    h = tmp_path / "tests" / "x.h"
    h.parent.mkdir()
    h.write_text("/* MIT */\nint f(void);\n")
    assert check_file(h) == []


def test_vendored_headers_only_need_a_license(tmp_path):
    """A header under third_party/ keeps its upstream documentation: undocumented functions
    are fine there, a missing license is still reported.
    """
    h = tmp_path / "source" / "android" / "third_party" / "lib.h"
    h.parent.mkdir(parents=True)
    h.write_text("/* lib.h -- vendored. MIT. */\nint lib_f(void);\n")
    assert check_file(h) == []
    h.write_text("/* lib.h */\nint lib_f(void);\n")
    assert [p.message for p in check_file(h)] != []


def test_main_reports_and_fails(tmp_path, capsys):
    """The command line prints each problem and exits 1."""
    bad = tmp_path / "bad.py"
    bad.write_text('"""No notice."""\n')
    assert main([str(bad)]) == 1
    assert "no license" in capsys.readouterr().out


def test_the_repository_follows_the_rules(capsys):
    """The whole repository passes the check, so make test fails as soon as a file loses its
    license or an API its comment.
    """
    assert main([]) == 0, capsys.readouterr().out
    assert ROOT.is_dir()


def test_c_tests_need_a_description(tmp_path):
    """C test functions in tests/c/ need a comment saying which case they check; helpers,
    prototypes and the RUN() calls do not.
    """
    t = tmp_path / "tests" / "c" / "test_x.c"
    t.parent.mkdir(parents=True)
    t.write_text(
        "/* sources: */\n/* Host unit test. MIT (see LICENSE). */\n"
        "static int helper(void) { return 1; }\n"
        "/* the documented case */\nstatic void test_ok(void) {}\n"
        "static void test_bare(void) {}\n"
        "static void test_proto(void);\n"
        "int main(void) { RUN(test_ok); return 0; }\n"
    )
    assert [p.message for p in check_file(t)] == ["test_bare: test without a description"]
