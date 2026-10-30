// ORION Media Center - Tests: a stored RAR5 archive planned from its headers, and what it hands to
// libarchive. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "core/rar5.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using hui::Rar5File;
using hui::Rar5Plan;

const std::string kSignature("Rar!\x1a\x07\x01\x00", 8);

std::string vint(std::uint64_t value)
{
    std::string out;
    do
    {
        unsigned char byte = value & 0x7f;
        value >>= 7;
        if (value != 0)
            byte |= 0x80;
        out += static_cast<char>(byte);
    } while (value != 0);
    return out;
}

std::string le32(std::uint32_t value)
{
    std::string out;
    for (int i = 0; i < 4; ++i)
        out += static_cast<char>((value >> (8 * i)) & 0xff);
    return out;
}

// One block as the technote lays it out, its data after it.
std::string block(std::uint64_t type, std::uint64_t flags, const std::string &fields,
                  const std::string &extra = {}, const std::string &data = {})
{
    if (!extra.empty())
        flags |= 0x01;
    if (!data.empty())
        flags |= 0x02;
    std::string header = vint(type) + vint(flags);
    if (!extra.empty())
        header += vint(extra.size());
    if (!data.empty())
        header += vint(data.size());
    header += fields + extra;
    const std::string sized = vint(header.size()) + header;
    return le32(hui::save::crc32(sized)) + sized + data;
}

std::string main_block(bool volume = false)
{
    return block(1, 0, vint(volume ? 0x01 : 0x00));
}

// A file block: stored unless `method` says otherwise.
std::string file_block(const std::string &name, const std::string &data, std::uint64_t flags = 0,
                       int method = 0, const std::string &extra = {}, bool directory = false)
{
    const std::string fields = vint(directory ? 0x01 : 0x00) + vint(data.size()) + vint(0x20) +
                               vint(static_cast<std::uint64_t>(method) << 7) + vint(0) +
                               vint(name.size()) + name;
    return block(2, flags, fields, extra, data);
}

std::string end_block(bool more = false)
{
    return block(5, 0, vint(more ? 0x01 : 0x00));
}

struct Volumes
{
    std::vector<std::string> parts;

    Rar5Plan plan(std::vector<Rar5File> *files, std::string *why) const
    {
        return hui::plan_rar5(
            parts.size(),
            [this](std::size_t volume, std::uint64_t offset, std::size_t size, std::string *out)
            {
                const std::string &part = parts.at(volume);
                *out = offset >= part.size() ? std::string()
                                             : part.substr(static_cast<std::size_t>(offset), size);
                return true;
            },
            files, why);
    }
    std::string bytes(const hui::Rar5Piece &piece) const
    {
        return parts.at(piece.volume)
            .substr(static_cast<std::size_t>(piece.offset), static_cast<std::size_t>(piece.size));
    }
};

TEST(Rar5, AStoredArchiveIsPlannedFileByFile)
{
    Volumes archive;
    archive.parts.push_back(
        kSignature + main_block() + file_block("PPSA02632-app", "", 0, 0, {}, true) +
        file_block("PPSA02632-app/eboot.bin", "EBOOT BYTES") +
        block(3, 0, vint(0) + vint(0) + vint(0) + vint(0) + vint(0) + vint(3) + "CMT", {},
              "a comment") +
        file_block("PPSA02632-app/sce_sys/param.json", "{}") + end_block());
    std::vector<Rar5File> files;
    std::string why;
    ASSERT_EQ(archive.plan(&files, &why), Rar5Plan::stored) << why;
    ASSERT_EQ(files.size(), 3u);
    EXPECT_TRUE(files[0].directory);
    EXPECT_EQ(files[0].name, "PPSA02632-app");
    EXPECT_TRUE(files[0].pieces.empty());
    EXPECT_EQ(files[1].name, "PPSA02632-app/eboot.bin");
    ASSERT_EQ(files[1].pieces.size(), 1u);
    EXPECT_EQ(archive.bytes(files[1].pieces[0]), "EBOOT BYTES");
    EXPECT_EQ(files[1].size, 11u);
    EXPECT_EQ(archive.bytes(files[2].pieces[0]), "{}");
}

TEST(Rar5, AFileSplitAcrossVolumesIsOneFileInPieces)
{
    Volumes archive;
    archive.parts.push_back(kSignature + main_block(true) + file_block("game/a.bin", "first ") +
                            file_block("game/big.bin", "HALF ONE ", 0x10) + end_block(true));
    archive.parts.push_back(kSignature + main_block(true) +
                            file_block("game/big.bin", "HALF TWO", 0x08) + end_block());
    std::vector<Rar5File> files;
    std::string why;
    ASSERT_EQ(archive.plan(&files, &why), Rar5Plan::stored) << why;
    ASSERT_EQ(files.size(), 2u);
    ASSERT_EQ(files[1].pieces.size(), 2u);
    EXPECT_EQ(files[1].pieces[0].volume, 0u);
    EXPECT_EQ(files[1].pieces[1].volume, 1u);
    EXPECT_EQ(archive.bytes(files[1].pieces[0]) + archive.bytes(files[1].pieces[1]),
              "HALF ONE HALF TWO");
    EXPECT_EQ(files[1].size, 17u);
}

TEST(Rar5, AMissingVolumeIsDamage)
{
    Volumes archive;
    archive.parts.push_back(kSignature + main_block(true) +
                            file_block("game/big.bin", "HALF ONE ", 0x10) + end_block(true));
    std::vector<Rar5File> files;
    std::string why;
    EXPECT_EQ(archive.plan(&files, &why), Rar5Plan::damaged);
    EXPECT_EQ(why, "a volume is missing");
}

TEST(Rar5, ACompressedOrEncryptedFileGoesToLibarchive)
{
    std::vector<Rar5File> files;
    std::string why;
    Volumes compressed;
    compressed.parts.push_back(kSignature + main_block() + file_block("a.bin", "x") +
                               file_block("b.bin", "packed", 0, 3) + end_block());
    EXPECT_EQ(compressed.plan(&files, &why), Rar5Plan::unpack);
    EXPECT_EQ(why, "b.bin is compressed");

    Volumes encrypted;
    const std::string record = vint(0x01) + std::string(8, '\0');
    encrypted.parts.push_back(kSignature + main_block() +
                              file_block("a.bin", "secret", 0, 0, vint(record.size()) + record) +
                              end_block());
    EXPECT_EQ(encrypted.plan(&files, &why), Rar5Plan::unpack);

    Volumes headers;
    headers.parts.push_back(kSignature + block(4, 0, vint(0) + vint(0) + vint(15)));
    EXPECT_EQ(headers.plan(&files, &why), Rar5Plan::unpack);
}

TEST(Rar5, LinksAreLeftOut)
{
    Volumes archive;
    const std::string record = vint(0x05) + vint(1) + vint(0) + vint(4) + "a.bin";
    archive.parts.push_back(kSignature + main_block() + file_block("a.bin", "x") +
                            file_block("link.bin", "", 0, 0, vint(record.size()) + record) +
                            end_block());
    std::vector<Rar5File> files;
    std::string why;
    ASSERT_EQ(archive.plan(&files, &why), Rar5Plan::stored) << why;
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].name, "a.bin");
}

TEST(Rar5, OtherArchivesAndDamageAreSaidSo)
{
    std::vector<Rar5File> files;
    std::string why;
    Volumes rar4;
    rar4.parts.push_back(std::string("Rar!\x1a\x07\x00", 7) + std::string(32, '\0'));
    EXPECT_EQ(rar4.plan(&files, &why), Rar5Plan::not_rar5);

    Volumes zip;
    zip.parts.push_back("PK\x03\x04 and the rest of a zip");
    EXPECT_EQ(zip.plan(&files, &why), Rar5Plan::not_rar5);

    Volumes bad;
    std::string bytes = kSignature + main_block() + file_block("a.bin", "x") + end_block();
    bytes[kSignature.size() + 6] ^= 0x40; // inside the main block's header
    bad.parts.push_back(bytes);
    EXPECT_EQ(bad.plan(&files, &why), Rar5Plan::damaged);

    Volumes cut;
    const std::string whole = kSignature + main_block() + file_block("game/a-long-name.bin", "x");
    cut.parts.push_back(whole.substr(0, whole.size() - 8));
    EXPECT_EQ(cut.plan(&files, &why), Rar5Plan::damaged);

    Volumes none;
    EXPECT_EQ(none.plan(&files, &why), Rar5Plan::unreadable);
}

TEST(Rar5, AHeaderLongerThanTheFirstReadIsReadWhole)
{
    Volumes archive;
    const std::string name = "game/" + std::string(6000, 'n') + ".bin";
    archive.parts.push_back(kSignature + main_block() + file_block(name, "data") + end_block());
    std::vector<Rar5File> files;
    std::string why;
    ASSERT_EQ(archive.plan(&files, &why), Rar5Plan::stored) << why;
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].name, name);
    EXPECT_EQ(archive.bytes(files[0].pieces[0]), "data");
}

} // namespace
