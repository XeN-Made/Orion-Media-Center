#!/usr/bin/env python3
# ORION Media Center - Lists every text ORION translates, as docs/lang/template.lang.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Finds the English text in src/ handed to i18n::tr, trf, plural and marked
and writes it as a language file with empty translations: the template a
translator copies. With --check it writes nothing and fails when the template
is out of date, or when a language in assets/lang lacks a text or drops a
{0} placeholder.

    python3 tools/gen-lang.py            # rewrite docs/lang/template.lang
    python3 tools/gen-lang.py --check    # what make lint runs
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMPLATE = ROOT / "docs" / "lang" / "template.lang"
BS = chr(92)

CALL = re.compile(r"(?<![\w.>])((?:hui::)?i18n::)?(tr|trf|marked|plural)\s*\(")
ESCAPES = {"n": "\n", "r": "\r", "t": "\t", "0": "\0", '"': '"', BS: BS, "'": "'"}


def literal(text, at):
    """The C++ string literal (or adjacent literals) at text[at:], decoded,
    and where it ends; None when no literal starts there."""
    out = bytearray()
    found = False
    while True:
        while at < len(text) and text[at] in " \t\r\n":
            at += 1
        if at >= len(text) or text[at] != '"':
            break
        found = True
        at += 1
        while text[at] != '"':
            c = text[at]
            if c == BS:
                nxt = text[at + 1]
                hexed = re.match(r"x([0-9a-fA-F]{1,2})|u([0-9a-fA-F]{4})", text[at + 1:at + 6])
                if nxt in ESCAPES:
                    out += ESCAPES[nxt].encode()
                    at += 2
                elif hexed and hexed.group(1):
                    out.append(int(hexed.group(1), 16))
                    at += 1 + len(hexed.group(0))
                elif hexed:
                    out += chr(int(hexed.group(2), 16)).encode()
                    at += 6
                else:
                    raise SystemExit(f"gen-lang: an escape the tool does not read: {BS}{nxt}")
                continue
            out += c.encode()
            at += 1
        at += 1
    return (out.decode("utf-8", "replace"), at) if found else (None, at)


def skip_argument(text, at):
    """Past one call argument at text[at:], to just after its comma."""
    depth = 0
    while at < len(text):
        c = text[at]
        if c == '"':
            _, at = literal(text, at)
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return None
            depth -= 1
        elif c == "," and depth == 0:
            return at + 1
        at += 1
    return None


def strip_comments(text):
    # Comments mention tr("...") in their examples; drop them, keeping literals.
    out = []
    at = 0
    while at < len(text):
        if text.startswith("//", at):
            at = text.find("\n", at)
            at = len(text) if at < 0 else at
        elif text.startswith("/*", at):
            at = text.find("*/", at) + 2
        elif text[at] == '"':
            start = at
            _, at = literal(text, at)
            out.append(text[start:at])
        elif text[at] == "'":
            end = at + 1
            while text[end] != "'":
                end += 2 if text[end] == BS else 1
            out.append(text[at:end + 1])
            at = end + 1
        else:
            out.append(text[at])
            at += 1
    return "".join(out)


def keys():
    found = {}
    for path in sorted((ROOT / "src").rglob("*.[ch]pp")):
        text = path.read_text(encoding="utf-8")
        if "i18n" not in text:
            continue
        text = strip_comments(text)
        rel = path.relative_to(ROOT).as_posix()
        for match in CALL.finditer(text):
            at = match.end()
            if match.group(2) == "plural":
                at = skip_argument(text, at)
                if at is None:
                    continue
                for _ in range(2):
                    value, at = literal(text, at)
                    if value is None:
                        break
                    found.setdefault(value, rel)
                    while at < len(text) and text[at] in " \t\r\n,":
                        at += 1
                continue
            value, _ = literal(text, at)
            if value:
                found.setdefault(value, rel)
    # The definitions in i18n.hpp take text, not literals; nothing to drop.
    return found


def quote(text):
    return '"' + text.replace(BS, BS + BS).replace('"', BS + '"').replace("\n", BS + "n") + '"'


def unquote_line(line):
    """The two texts of a '"English" = "translation"' line, else None."""
    english, at = literal(line, 0)
    if english is None:
        return None
    rest = line[at:].lstrip()
    if not rest.startswith("="):
        return None
    translated, _ = literal(rest, 1)
    return english, translated or ""


def template(found):
    lines = [
        "# ORION Media Center - a language for ORION. Generated by tools/gen-lang.py.",
        "#",
        "# Copy this file to /data/orion/lang/<code>.lang, set the code and the name",
        "# (in the language itself), then write each translation between the second",
        "# pair of quotes. A text left empty shows in English. {0}, {1}... stand for",
        "# a name or a number: keep each one, anywhere in the sentence. \\n is a new",
        "# line, \\\" a quote. Settings, Library, Language picks the file up the next",
        "# time ORION opens.",
        "",
        "code = xx",
        "name = Language name",
        "author =",
        "",
    ]
    last = None
    for english, where in sorted(found.items(), key=lambda kv: (kv[1], kv[0].lower(), kv[0])):
        if where != last:
            if last is not None:
                lines.append("")
            lines.append("# " + where)
            last = where
        lines.append(quote(english) + ' = ""')
    return "\n".join(lines) + "\n"


PLACEHOLDER = re.compile(r"\{\d+\}")


def check_language(path, found):
    problems = []
    strings = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith('"'):
            pair = unquote_line(line)
            if pair is None:
                problems.append(f"{path.name}: cannot read {line[:60]}")
            else:
                strings[pair[0]] = pair[1]
    for english in found:
        if not strings.get(english):
            problems.append(f"{path.name}: no translation for {quote(english)}")
        elif sorted(PLACEHOLDER.findall(english)) != sorted(PLACEHOLDER.findall(strings[english])):
            problems.append(f"{path.name}: the placeholders differ in {quote(english)}")
    for english in strings:
        if english not in found:
            problems.append(f"{path.name}: {quote(english)} is not used any more")
    return problems


def main():
    found = keys()
    text = template(found)
    if "--check" not in sys.argv[1:]:
        TEMPLATE.parent.mkdir(parents=True, exist_ok=True)
        TEMPLATE.write_text(text, encoding="utf-8")
        print(f"gen-lang: {len(found)} texts in {TEMPLATE.relative_to(ROOT)}")
        return 0
    problems = []
    if not TEMPLATE.exists() or TEMPLATE.read_text(encoding="utf-8") != text:
        problems.append("docs/lang/template.lang is out of date: run python3 tools/gen-lang.py")
    for path in sorted((ROOT / "assets" / "lang").glob("*.lang")):
        problems += check_language(path, found)
    for problem in problems:
        print("gen-lang: " + problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
