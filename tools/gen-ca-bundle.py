#!/usr/bin/env python3
# ORION Media Center - Builds the certificate list into the app from assets/certs/cacert.pem.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes src/platform/ps5/ca_bundle.cpp: every certificate of
assets/certs/cacert.pem as one string literal, so ORION verifies HTTPS
servers even when the file did not reach the console (some upload tools
skip text files). Run it after updating cacert.pem.

usage: tools/gen-ca-bundle.py
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "certs" / "cacert.pem"
TARGET = ROOT / "src" / "platform" / "ps5" / "ca_bundle.cpp"

pem = SOURCE.read_text(encoding="ascii")
blocks = re.findall(r"-----BEGIN CERTIFICATE-----\n.*?-----END CERTIFICATE-----\n", pem, re.S)
if not blocks:
    raise SystemExit(f"{SOURCE}: no certificates")
for block in blocks:
    # C++ compilers need only take literals of 4095 characters.
    if len(block) > 4000:
        raise SystemExit("a certificate is too long for one literal")

out = [
    "// ORION Media Center - The HTTPS certificate list, built in (tools/gen-ca-bundle.py writes this).",
    "// Copyright (C) 2026 BlackBearReloaded",
    "// SPDX-License-Identifier: GPL-3.0-or-later",
    "//",
    "// Generated from assets/certs/cacert.pem (Mozilla's list, as Ubuntu's",
    "// ca-certificates ships it): do not edit by hand.",
    "",
    '#include "platform/ps5/ca_bundle.hpp"',
    "",
    "#include <iterator>",
    "",
    "namespace hui::sys",
    "{",
    "",
    "namespace",
    "{",
    "",
    "// One literal per certificate.",
    "constexpr const char *kCertificates[] = {",
]
for block in blocks:
    lines = block.splitlines()
    for index, line in enumerate(lines):
        end = "," if index == len(lines) - 1 else ""
        out.append(f'    "{line}\\n"{end}')
out += [
    "};",
    "",
    "} // namespace",
    "",
    "const std::string &ca_bundle()",
    "{",
    "    static const std::string bundle = []",
    "    {",
    "        std::string all;",
    "        for (const char *certificate : kCertificates)",
    "            all += certificate;",
    "        return all;",
    "    }();",
    "    return bundle;",
    "}",
    "",
    "std::size_t ca_bundle_count()",
    "{",
    "    return std::size(kCertificates);",
    "}",
    "",
    "} // namespace hui::sys",
    "",
]
TARGET.write_text("\n".join(out), encoding="ascii")
print(f"{TARGET.relative_to(ROOT)}: {len(blocks)} certificates")
