// ORION Media Center - WS-Discovery's messages: how Windows finds the PCs around it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows 10 and 11 often answer no NetBIOS at all, but every PC with
// network discovery on answers a WS-Discovery probe for computers (UDP
// multicast to 239.255.255.250:3702), and gives its name to a metadata
// request over HTTP; File Explorer's Network view is built this way, and
// Samba's wsdd answers the same. This is the text of those messages and the
// reading of the answers; the sockets are the caller's (platform/ps5/smb.cpp).
//
//   send wsd_probe(id) to 239.255.255.250:3702
//   for each answer: if (wsd_is_computer(text)) { address = its sender;
//       POST wsd_get(wsd_endpoint(text), id2, host) to wsd_xaddr(text) }
//   name = wsd_computer_name(reply body)

#pragma once

#include <string>
#include <string_view>

namespace hui
{

constexpr const char *kWsdAddress = "239.255.255.250";
constexpr int kWsdPort = 3702;

// A message id: "urn:uuid:" and a random version-4 UUID made from `seed`.
std::string wsd_message_id(unsigned long long seed);

// A probe for computers (Windows' pub:Computer type).
std::string wsd_probe(const std::string &message_id);

// The answer is a ProbeMatch for a computer.
bool wsd_is_computer(std::string_view answer);

// Its metadata address ("http://192.168.1.20:5357/<uuid>"), the first
// http:// one of its XAddrs; empty when it gives none.
std::string wsd_xaddr(std::string_view answer);

// Its endpoint ("urn:uuid:..."), which the metadata request is sent to.
std::string wsd_endpoint(std::string_view answer);

// The whole HTTP request for a machine's metadata at `xaddr`.
std::string wsd_get(const std::string &xaddr, const std::string &endpoint,
                    const std::string &message_id);

// The computer's name from the metadata ("MEDIA-PC" from
// "MEDIA-PC/Workgroup:WORKGROUP"); empty when it has none.
std::string wsd_computer_name(std::string_view metadata);

} // namespace hui
