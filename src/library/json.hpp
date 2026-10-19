// ps5-homebrew-ui - A small JSON reader for param.json and ORION's own files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hui::json
{

enum class Type : unsigned char
{
    null,
    boolean,
    number,
    string,
    array,
    object,
};

// One parsed value. Objects keep their keys in file order; lookups are linear,
// which is fine for files of a few dozen keys.
//
//   json::Value root;
//   if (json::parse(text, &root))
//       std::string name = root["localizedParameters"]["en-US"]["titleName"].string_or("");
class Value
{
  public:
    Type type() const
    {
        return type_;
    }
    bool is_object() const
    {
        return type_ == Type::object;
    }
    bool is_array() const
    {
        return type_ == Type::array;
    }
    bool is_string() const
    {
        return type_ == Type::string;
    }
    bool is_number() const
    {
        return type_ == Type::number;
    }

    // A missing key or index yields a shared null value, so lookups chain.
    const Value &operator[](std::string_view key) const;
    const Value &operator[](std::size_t index) const;
    std::size_t size() const; // elements of an array, keys of an object

    std::string string_or(std::string_view fallback) const;
    double number_or(double fallback) const;
    bool bool_or(bool fallback) const;

    const std::vector<std::pair<std::string, Value>> &members() const
    {
        return members_;
    }
    const std::vector<Value> &elements() const
    {
        return elements_;
    }

  private:
    friend class Parser;
    Type type_ = Type::null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> elements_;
    std::vector<std::pair<std::string, Value>> members_;
};

// text as a JSON string literal, quotes included: what a writer needs.
std::string quote(std::string_view text);

// Parses a whole document (RFC 8259; a UTF-8 byte order mark is skipped).
// Nesting deeper than 64 levels is refused. On failure *out is left null and
// *error, when given, says where.
bool parse(std::string_view text, Value *out, std::string *error = nullptr);

} // namespace hui::json
