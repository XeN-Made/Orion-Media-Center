// ORION Media Center - WS-Discovery's messages: how Windows finds the PCs around it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/ws_discovery.hpp"

#include <cstdint>
#include <cstdio>

namespace hui
{

namespace
{

constexpr const char *kEnvelope =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
    "xmlns:wsd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
    "xmlns:pub=\"http://schemas.microsoft.com/windows/pub/2005/07\">";

// The text of the first element whose local name is `name`, whatever its
// prefix ("<wsd:Types>", "<d:Types>", "<Types>"), with its attributes.
std::string_view element(std::string_view xml, std::string_view name)
{
    for (std::size_t at = xml.find('<'); at != std::string_view::npos; at = xml.find('<', at + 1))
    {
        std::size_t start = at + 1;
        if (start < xml.size() && (xml[start] == '/' || xml[start] == '?' || xml[start] == '!'))
            continue;
        std::size_t end = start;
        while (end < xml.size() && xml[end] != '>' && xml[end] != ' ' && xml[end] != '/')
            ++end;
        const std::string_view full = xml.substr(start, end - start);
        std::string_view tag = full;
        const std::size_t colon = tag.find(':');
        if (colon != std::string_view::npos)
            tag = tag.substr(colon + 1);
        if (tag != name)
            continue;
        const std::size_t open_end = xml.find('>', end);
        if (open_end == std::string_view::npos || xml[open_end - 1] == '/')
            return {};
        // Its own closing tag, not the first of the elements inside it.
        const std::size_t close = xml.find("</" + std::string(full) + ">", open_end);
        if (close == std::string_view::npos)
            return {};
        return xml.substr(open_end + 1, close - open_end - 1);
    }
    return {};
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r' ||
                             text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' ||
                             text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

} // namespace

std::string wsd_message_id(unsigned long long seed)
{
    // SplitMix64: two well-mixed words from one seed.
    const auto mix = [&seed]()
    {
        std::uint64_t z = (seed += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    };
    const std::uint64_t high = mix();
    const std::uint64_t low = mix();
    char text[64];
    std::snprintf(text, sizeof(text), "urn:uuid:%08x-%04x-4%03x-%04x-%012llx",
                  static_cast<unsigned>(high >> 32), static_cast<unsigned>((high >> 16) & 0xffff),
                  static_cast<unsigned>(high & 0x0fff),
                  static_cast<unsigned>(0x8000 | ((low >> 48) & 0x3fff)),
                  static_cast<unsigned long long>(low & 0xffffffffffffull));
    return text;
}

std::string wsd_probe(const std::string &message_id)
{
    return std::string(kEnvelope) +
           "<soap:Header>"
           "<wsa:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
           "<wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>"
           "<wsa:MessageID>" +
           message_id +
           "</wsa:MessageID>"
           "</soap:Header>"
           "<soap:Body><wsd:Probe><wsd:Types>pub:Computer</wsd:Types></wsd:Probe></soap:Body>"
           "</soap:Envelope>";
}

bool wsd_is_computer(std::string_view answer)
{
    if (element(answer, "ProbeMatch").empty())
        return false;
    // "wsdp:Device pub:Computer": the type's local name, whatever its prefix.
    const std::string_view types = element(answer, "Types");
    for (std::size_t at = 0; at < types.size();)
    {
        std::size_t end = types.find(' ', at);
        if (end == std::string_view::npos)
            end = types.size();
        std::string_view type = types.substr(at, end - at);
        const std::size_t colon = type.find(':');
        if (colon != std::string_view::npos)
            type = type.substr(colon + 1);
        if (type == "Computer")
            return true;
        at = end + 1;
    }
    return false;
}

std::string wsd_xaddr(std::string_view answer)
{
    const std::string_view addresses = element(answer, "XAddrs");
    for (std::size_t at = 0; at < addresses.size();)
    {
        std::size_t end = addresses.find(' ', at);
        if (end == std::string_view::npos)
            end = addresses.size();
        const std::string_view one = trimmed(addresses.substr(at, end - at));
        // IPv4 only: the console has no IPv6 to reach "http://[fe80::...]".
        if (one.rfind("http://", 0) == 0 && one.find('[') == std::string_view::npos)
            return std::string(one);
        at = end + 1;
    }
    return {};
}

std::string wsd_endpoint(std::string_view answer)
{
    return std::string(trimmed(element(element(answer, "EndpointReference"), "Address")));
}

std::string wsd_get(const std::string &xaddr, const std::string &endpoint,
                    const std::string &message_id)
{
    // "http://host:port/path": the Host header and the path.
    const std::string rest = xaddr.substr(xaddr.rfind("http://", 0) == 0 ? 7 : 0);
    const std::size_t slash = rest.find('/');
    const std::string host = rest.substr(0, slash);
    const std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
    const std::string body =
        std::string(kEnvelope) +
        "<soap:Header>"
        "<wsa:To>" +
        endpoint +
        "</wsa:To>"
        "<wsa:Action>http://schemas.xmlsoap.org/ws/2004/09/transfer/Get</wsa:Action>"
        "<wsa:MessageID>" +
        message_id +
        "</wsa:MessageID>"
        "<wsa:ReplyTo><wsa:Address>"
        "http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous"
        "</wsa:Address></wsa:ReplyTo>"
        "</soap:Header><soap:Body/></soap:Envelope>";
    return "POST " + path + " HTTP/1.1\r\nHost: " + host +
           "\r\nContent-Type: application/soap+xml\r\nUser-Agent: wsd\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

std::string wsd_computer_name(std::string_view metadata)
{
    // "MEDIA-PC/Workgroup:WORKGROUP" or "MEDIA-PC/Domain:corp.example".
    const std::string_view computer = trimmed(element(metadata, "Computer"));
    return std::string(computer.substr(0, computer.find('/')));
}

} // namespace hui
