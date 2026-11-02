// ORION Media Center - Tests: WS-Discovery's messages and the reading of the answers.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/ws_discovery.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{

// A Windows PC's answer, shaped as Windows sends it (prefixes and all).
constexpr const char *kWindowsMatch =
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
    "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
    "xmlns:wsd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
    "xmlns:wsdp=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\" "
    "xmlns:pub=\"http://schemas.microsoft.com/windows/pub/2005/07\">"
    "<soap:Header><wsa:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:To>"
    "<wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</wsa:Action>"
    "</soap:Header><soap:Body><wsd:ProbeMatches><wsd:ProbeMatch>"
    "<wsa:EndpointReference><wsa:Address>urn:uuid:5f1c7a2e-0000-4000-8000-00155d000001"
    "</wsa:Address></wsa:EndpointReference>"
    "<wsd:Types>wsdp:Device pub:Computer</wsd:Types>"
    "<wsd:XAddrs>http://[fe80::1%12]:5357/5f1c7a2e-0000-4000-8000-00155d000001/ "
    "http://192.168.1.20:5357/5f1c7a2e-0000-4000-8000-00155d000001/</wsd:XAddrs>"
    "<wsd:MetadataVersion>2</wsd:MetadataVersion>"
    "</wsd:ProbeMatch></wsd:ProbeMatches></soap:Body></soap:Envelope>";

TEST(WsDiscovery, ProbesForComputers)
{
    const std::string id = hui::wsd_message_id(42);
    EXPECT_EQ(id.size(), 45u);
    EXPECT_EQ(id.rfind("urn:uuid:", 0), 0u);
    EXPECT_EQ(id[23], '4'); // version 4
    EXPECT_NE(hui::wsd_message_id(43), id);
    const std::string probe = hui::wsd_probe(id);
    EXPECT_NE(probe.find("<wsd:Types>pub:Computer</wsd:Types>"), std::string::npos);
    EXPECT_NE(probe.find("<wsa:MessageID>" + id + "</wsa:MessageID>"), std::string::npos);
    EXPECT_NE(probe.find("/discovery/Probe</wsa:Action>"), std::string::npos);
}

TEST(WsDiscovery, ReadsAComputersAnswer)
{
    EXPECT_TRUE(hui::wsd_is_computer(kWindowsMatch));
    EXPECT_EQ(hui::wsd_xaddr(kWindowsMatch),
              "http://192.168.1.20:5357/5f1c7a2e-0000-4000-8000-00155d000001/");
    EXPECT_EQ(hui::wsd_endpoint(kWindowsMatch), "urn:uuid:5f1c7a2e-0000-4000-8000-00155d000001");
}

// A printer answers probes too; it is no computer. Nor is a probe.
TEST(WsDiscovery, LeavesOutWhatIsNoComputer)
{
    std::string printer = kWindowsMatch;
    printer.replace(printer.find("wsdp:Device pub:Computer"), 24, "wsdp:Device wprt:PrintDevice");
    EXPECT_FALSE(hui::wsd_is_computer(printer));
    EXPECT_FALSE(hui::wsd_is_computer(hui::wsd_probe(hui::wsd_message_id(1))));
    EXPECT_FALSE(hui::wsd_is_computer(""));
    // Other prefixes, as Samba's wsdd writes them.
    EXPECT_TRUE(hui::wsd_is_computer("<d:ProbeMatch><d:Types>dp:Device x:Computer</d:Types>"
                                     "</d:ProbeMatch>"));
}

TEST(WsDiscovery, AsksForTheName)
{
    const std::string get =
        hui::wsd_get("http://192.168.1.20:5357/abc/", "urn:uuid:abc", "urn:uuid:def");
    EXPECT_EQ(get.rfind("POST /abc/ HTTP/1.1\r\nHost: 192.168.1.20:5357\r\n", 0), 0u);
    const std::size_t body = get.find("\r\n\r\n") + 4;
    EXPECT_NE(get.find("Content-Length: " + std::to_string(get.size() - body)), std::string::npos);
    EXPECT_NE(get.find("<wsa:To>urn:uuid:abc</wsa:To>"), std::string::npos);
    EXPECT_NE(get.find("/transfer/Get</wsa:Action>"), std::string::npos);

    EXPECT_EQ(hui::wsd_computer_name("<wsx:Metadata><pub:Computer>MEDIA-PC/Workgroup:WORKGROUP"
                                     "</pub:Computer></wsx:Metadata>"),
              "MEDIA-PC");
    EXPECT_EQ(hui::wsd_computer_name("<Computer> NAS </Computer>"), "NAS");
    EXPECT_EQ(hui::wsd_computer_name("<pub:Computer/>"), "");
    EXPECT_EQ(hui::wsd_computer_name("nothing"), "");
}

} // namespace
