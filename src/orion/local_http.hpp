// ps5-homebrew-ui - A small HTTP client for services on the console itself (127.0.0.1).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace hui::orion
{

// What came back: the status code and the body (chunked bodies put together).
struct HttpReply
{
    int status = 0;
    std::string body;
};

// Reads a whole HTTP/1.x response as the server sent it before closing.
// False when it is not one.
bool parse_http_reply(std::string_view raw, HttpReply *reply);

// POSTs a JSON body to http://127.0.0.1:<port><path> and waits for the whole
// reply (ShadowMountPlus's API: orion/remover.hpp). Plain HTTP over the
// loopback only: nothing leaves the console. False with *error set when the
// service did not answer or the answer was not HTTP.
bool post_local_json(int port, const std::string &path, const std::string &json, HttpReply *reply,
                     std::string *error);

} // namespace hui::orion
