"""tools/doc_check.py: license headers and comments on functions declared in headers.

MIT (see LICENSE).
"""

from pathlib import Path

import pytest

from tools.doc_check import ROOT, check_file, check_header_functions, check_license, main


def names(problems: list) -> list[str]:
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
    assert check_license(Path("x"), head + "\nint a;\n") == []


def test_missing_license_is_reported():
    (p,) = check_license(Path("x.c"), "/* x.c -- thing */\nint a;\n")
    assert p.line == 1 and "license" in p.message


def test_license_after_the_header_lines_does_not_count():
    text = "/* x */\n" + "\n" * 40 + "/* MIT */\n"
    assert check_license(Path("x.c"), text)


def test_undocumented_declaration():
    text = "/* h.h -- MIT */\n\nint documented_not(int a);\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["documented_not"]


def test_comment_covers_a_group_until_a_blank_line():
    text = "/* group: both covered */\nint a(void);\nint b(void);\n\nint c(void);\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["c"]


def test_externs_and_forward_structs_stay_in_the_group():
    text = "/* group */\nstruct s;\nextern int g;\nint a(struct s *);\n"
    assert check_header_functions(Path("h.h"), text) == []


def test_typedef_ends_the_group():
    text = "/* the type */\ntypedef struct { int n; } set;\nint set_has(const set *s);\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["set_has"]


def test_multiline_comment_and_declaration():
    text = (
        "/* one\n * two */\n"
        "int long_one(int a,\n             int b);\n"
        "// line comment\n"
        "void d(void);\n"
    )
    assert check_header_functions(Path("h.h"), text) == []


def test_struct_members_are_not_declarations():
    text = "/* api */\ntypedef struct {\n  void *(*fn)(void *self);\n} api;\n"
    assert check_header_functions(Path("h.h"), text) == []


def test_static_inline_definition_needs_a_comment():
    text = "\nstatic inline int f(int x) {\n  return x;\n}\n"
    assert names(check_header_functions(Path("h.h"), text)) == ["f"]


def test_only_source_headers_get_the_function_rule(tmp_path):
    h = tmp_path / "tests" / "x.h"
    h.parent.mkdir()
    h.write_text("/* MIT */\nint f(void);\n")
    assert check_file(h) == []


def test_main_reports_and_fails(tmp_path, capsys):
    bad = tmp_path / "bad.py"
    bad.write_text('"""No notice."""\n')
    assert main([str(bad)]) == 1
    assert "no license" in capsys.readouterr().out


def test_the_repository_follows_the_rules(capsys):
    assert main([]) == 0, capsys.readouterr().out
    assert ROOT.is_dir()
