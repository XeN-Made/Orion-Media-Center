// ORION Media Center - ShadowMountPlus's HTTP API: the games it knows, asked directly.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/library.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hui::orion
{

// ShadowMountPlus (1.7 and later) answers on 127.0.0.1:10101, plain HTTP
// with JSON, for apps on the console itself. Its game list is everything it
// mounts or found installed: folders, disc images on exFAT drives and
// anywhere else it looks, installed packages. ORION asks it, so a title the
// player sees in ShadowMountPlus is in ORION too, with no FTP server needed.
inline constexpr int kShadowMountPort = 10101;

// One request to 127.0.0.1:<port>: POST with a JSON body, or GET when json
// is empty. *body is the answer's body. False with *error when nothing
// answered or the answer is not 200.
bool shadowmount_request(int port, std::string_view path, std::string_view json, std::string *body,
                         std::string *error, std::size_t limit = 8u << 20);

// An HTTP/1.x answer as it came off the socket: its status and body
// (a chunked one put back together). False when it is not one.
bool parse_http_answer(std::string_view raw, int *status, std::string *body);

// The games of /api/v1/games, as titles: installed ones with a title ID,
// PS4 or PS5, named as the home screen names them. Their category is not in
// the list (a game unless told otherwise).
bool parse_shadowmount_games(std::string_view json, std::vector<library::Title> *titles);

} // namespace hui::orion
