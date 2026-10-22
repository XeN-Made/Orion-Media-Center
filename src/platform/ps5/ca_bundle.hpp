// ORION Media Center - The HTTPS certificate list, built into the app.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

namespace hui::sys
{

// assets/certs/cacert.pem as it was when the app was built (PEM, every
// certificate), for curl (CURLOPT_CAINFO_BLOB). Built in because the file
// can be missing on the console: some upload tools skip text files, and
// without it no HTTPS request (box art, updates) can be verified.
// tools/gen-ca-bundle.py writes ca_bundle.cpp from the file.
const std::string &ca_bundle();
std::size_t ca_bundle_count();

} // namespace hui::sys
