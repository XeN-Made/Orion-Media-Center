// ps5-homebrew-ui - A small JSON reader for param.json and ORION's own files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/json.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace hui::json
{

namespace
{

constexpr int kMaxDepth = 64;

const Value &null_value()
{
    static const Value kNull;
    return kNull;
}

void append_utf8(std::string &out, std::uint32_t code)
{
    if (code < 0x80)
    {
        out += static_cast<char>(code);
    }
    else if (code < 0x800)
    {
        out += static_cast<char>(0xc0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3f));
    }
    else if (code < 0x10000)
    {
        out += static_cast<char>(0xe0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (code & 0x3f));
    }
    else
    {
        out += static_cast<char>(0xf0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (code & 0x3f));
    }
}

} // namespace

const Value &Value::operator[](std::string_view key) const
{
    if (type_ == Type::object)
    {
        for (const auto &member : members_)
        {
            if (member.first == key)
                return member.second;
        }
    }
    return null_value();
}

const Value &Value::operator[](std::size_t index) const
{
    if (type_ == Type::array && index < elements_.size())
        return elements_[index];
    return null_value();
}

std::size_t Value::size() const
{
    if (type_ == Type::array)
        return elements_.size();
    if (type_ == Type::object)
        return members_.size();
    return 0;
}

std::string Value::string_or(std::string_view fallback) const
{
    return type_ == Type::string ? string_ : std::string(fallback);
}

double Value::number_or(double fallback) const
{
    return type_ == Type::number ? number_ : fallback;
}

bool Value::bool_or(bool fallback) const
{
    return type_ == Type::boolean ? boolean_ : fallback;
}

// Recursive descent over the text; every failure records the byte offset.
class Parser
{
  public:
    explicit Parser(std::string_view text) : text_(text)
    {
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xef &&
            static_cast<unsigned char>(text_[1]) == 0xbb &&
            static_cast<unsigned char>(text_[2]) == 0xbf)
            at_ = 3;
    }

    bool document(Value *out)
    {
        skip_space();
        if (!value(out, 0))
            return false;
        skip_space();
        return at_ == text_.size() || fail("text after the value");
    }

    const std::string &error() const
    {
        return error_;
    }

  private:
    bool fail(const char *what)
    {
        if (error_.empty())
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "%s at byte %zu", what, at_);
            error_ = buffer;
        }
        return false;
    }

    void skip_space()
    {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }

    bool literal(std::string_view word)
    {
        if (text_.substr(at_, word.size()) != word)
            return fail("unknown literal");
        at_ += word.size();
        return true;
    }

    bool value(Value *out, int depth)
    {
        if (depth > kMaxDepth)
            return fail("nesting too deep");
        if (at_ >= text_.size())
            return fail("unexpected end");
        switch (text_[at_])
        {
        case '{':
            return object(out, depth);
        case '[':
            return array(out, depth);
        case '"':
            out->type_ = Type::string;
            return string(&out->string_);
        case 't':
            out->type_ = Type::boolean;
            out->boolean_ = true;
            return literal("true");
        case 'f':
            out->type_ = Type::boolean;
            out->boolean_ = false;
            return literal("false");
        case 'n':
            out->type_ = Type::null;
            return literal("null");
        default:
            return number(out);
        }
    }

    bool object(Value *out, int depth)
    {
        out->type_ = Type::object;
        ++at_; // '{'
        skip_space();
        if (at_ < text_.size() && text_[at_] == '}')
        {
            ++at_;
            return true;
        }
        for (;;)
        {
            skip_space();
            if (at_ >= text_.size() || text_[at_] != '"')
                return fail("expected a key");
            std::string key;
            if (!string(&key))
                return false;
            skip_space();
            if (at_ >= text_.size() || text_[at_] != ':')
                return fail("expected ':'");
            ++at_;
            skip_space();
            out->members_.emplace_back(std::move(key), Value());
            if (!value(&out->members_.back().second, depth + 1))
                return false;
            skip_space();
            if (at_ < text_.size() && text_[at_] == ',')
            {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == '}')
            {
                ++at_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool array(Value *out, int depth)
    {
        out->type_ = Type::array;
        ++at_; // '['
        skip_space();
        if (at_ < text_.size() && text_[at_] == ']')
        {
            ++at_;
            return true;
        }
        for (;;)
        {
            skip_space();
            out->elements_.emplace_back();
            if (!value(&out->elements_.back(), depth + 1))
                return false;
            skip_space();
            if (at_ < text_.size() && text_[at_] == ',')
            {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == ']')
            {
                ++at_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool hex4(std::uint32_t *code)
    {
        if (at_ + 4 > text_.size())
            return fail("short \\u escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text_[at_++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return fail("bad \\u escape");
        }
        *code = value;
        return true;
    }

    bool string(std::string *out)
    {
        ++at_; // '"'
        for (;;)
        {
            if (at_ >= text_.size())
                return fail("unterminated string");
            const char c = text_[at_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in string");
            if (c != '\\')
            {
                *out += c;
                continue;
            }
            if (at_ >= text_.size())
                return fail("unterminated escape");
            const char escape = text_[at_++];
            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                *out += escape;
                break;
            case 'b':
                *out += '\b';
                break;
            case 'f':
                *out += '\f';
                break;
            case 'n':
                *out += '\n';
                break;
            case 'r':
                *out += '\r';
                break;
            case 't':
                *out += '\t';
                break;
            case 'u':
            {
                std::uint32_t code = 0;
                if (!hex4(&code))
                    return false;
                // A high surrogate must be followed by its low half.
                if (code >= 0xd800 && code <= 0xdbff)
                {
                    std::uint32_t low = 0;
                    if (text_.substr(at_, 2) != "\\u")
                        return fail("lone surrogate");
                    at_ += 2;
                    if (!hex4(&low))
                        return false;
                    if (low < 0xdc00 || low > 0xdfff)
                        return fail("bad surrogate pair");
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                }
                else if (code >= 0xdc00 && code <= 0xdfff)
                {
                    return fail("lone surrogate");
                }
                append_utf8(*out, code);
                break;
            }
            default:
                return fail("unknown escape");
            }
        }
    }

    bool number(Value *out)
    {
        const std::size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-')
            ++at_;
        const auto digits = [&]()
        {
            const std::size_t from = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9')
                ++at_;
            return at_ > from;
        };
        if (!digits())
            return fail("unexpected character");
        if (at_ < text_.size() && text_[at_] == '.')
        {
            ++at_;
            if (!digits())
                return fail("bad number");
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E'))
        {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-'))
                ++at_;
            if (!digits())
                return fail("bad number");
        }
        // strtod needs a terminated buffer; a number is short.
        const std::string token(text_.substr(start, at_ - start));
        out->type_ = Type::number;
        out->number_ = std::strtod(token.c_str(), nullptr);
        return true;
    }

    std::string_view text_;
    std::size_t at_ = 0;
    std::string error_;
};

std::string quote(std::string_view text)
{
    std::string out = "\"";
    for (char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(c));
                out += escape;
            }
            else
            {
                out += c;
            }
        }
    }
    return out + "\"";
}

bool parse(std::string_view text, Value *out, std::string *error)
{
    Parser parser(text);
    Value result;
    if (!parser.document(&result))
    {
        *out = Value();
        if (error != nullptr)
            *error = parser.error();
        return false;
    }
    *out = std::move(result);
    return true;
}

} // namespace hui::json
