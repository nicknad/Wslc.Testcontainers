#include "internal/json.hpp"

#include <cstdlib>
#include <format>

namespace wslc::internal::json
{

namespace
{

constexpr int MaxDepth = 64;

struct Parser
{
    std::string_view Text;
    std::size_t position = 0;

    bool AtEnd() const { return position >= Text.size(); }

    char Peek() const { return AtEnd() ? '\0' : Text[position]; }

    bool Consume(char expected)
    {
        if (Peek() != expected)
        {
            return false;
        }

        position++;
        return true;
    }

    void SkipWhitespace()
    {
        while (!AtEnd())
        {
            const char character = Text[position];
            if (character == ' ' || character == '\t' || character == '\r' || character == '\n')
            {
                position++;
                continue;
            }

            break;
        }
    }

    bool MatchLiteral(std::string_view literal)
    {
        if (Text.substr(position, literal.size()) != literal)
        {
            return false;
        }

        position += literal.size();
        return true;
    }

    bool ParseHex4(unsigned& value)
    {
        if (position + 4 > Text.size())
        {
            return false;
        }

        value = 0;
        for (int i = 0; i < 4; i++)
        {
            const char character = Text[position + static_cast<std::size_t>(i)];
            value <<= 4;
            if (character >= '0' && character <= '9')
            {
                value |= static_cast<unsigned>(character - '0');
            }
            else if (character >= 'a' && character <= 'f')
            {
                value |= static_cast<unsigned>(character - 'a' + 10);
            }
            else if (character >= 'A' && character <= 'F')
            {
                value |= static_cast<unsigned>(character - 'A' + 10);
            }
            else
            {
                return false;
            }
        }

        position += 4;
        return true;
    }

    static void AppendUtf8(std::string& target, unsigned code_point)
    {
        if (code_point <= 0x7F)
        {
            target.push_back(static_cast<char>(code_point));
        }
        else if (code_point <= 0x7FF)
        {
            target.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
        else if (code_point <= 0xFFFF)
        {
            target.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            target.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
        else
        {
            target.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            target.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            target.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }

    bool ParseString(std::string& result)
    {
        if (!Consume('"'))
        {
            return false;
        }

        result.clear();
        while (!AtEnd())
        {
            const char character = Text[position++];
            if (character == '"')
            {
                return true;
            }

            if (character != '\\')
            {
                result.push_back(character);
                continue;
            }

            if (AtEnd())
            {
                return false;
            }

            const char Escape = Text[position++];
            switch (Escape)
            {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
            {
                unsigned code_point = 0;
                if (!ParseHex4(code_point))
                {
                    return false;
                }

                if (code_point >= 0xD800 && code_point <= 0xDBFF)
                {
                    // High surrogate: must be followed by a low surrogate.
                    if (position + 6 > Text.size() || Text[position] != '\\' || Text[position + 1] != 'u')
                    {
                        return false;
                    }

                    position += 2;
                    unsigned low = 0;
                    if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
                    {
                        return false;
                    }

                    code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                }
                else if (code_point >= 0xDC00 && code_point <= 0xDFFF)
                {
                    return false;
                }

                AppendUtf8(result, code_point);
                break;
            }
            default:
                return false;
            }
        }

        return false;
    }

    bool ParseValue(Value& value, int depth)
    {
        if (depth > MaxDepth)
        {
            return false;
        }

        SkipWhitespace();
        if (AtEnd())
        {
            return false;
        }

        const char character = Peek();
        if (character == '{')
        {
            return ParseObject(value, depth);
        }

        if (character == '[')
        {
            return ParseArray(value, depth);
        }

        if (character == '"')
        {
            value.Kind = Value::Kind::String;
            return ParseString(value.String);
        }

        if (character == 't')
        {
            if (!MatchLiteral("true"))
            {
                return false;
            }

            value.Kind = Value::Kind::Boolean;
            value.Boolean = true;
            return true;
        }

        if (character == 'f')
        {
            if (!MatchLiteral("false"))
            {
                return false;
            }

            value.Kind = Value::Kind::Boolean;
            value.Boolean = false;
            return true;
        }

        if (character == 'n')
        {
            if (!MatchLiteral("null"))
            {
                return false;
            }

            value.Kind = Value::Kind::Null;
            return true;
        }

        return ParseNumber(value);
    }

    bool ParseNumber(Value& value)
    {
        const std::size_t Start = position;
        if (Peek() == '-')
        {
            position++;
        }

        while (!AtEnd())
        {
            const char character = Peek();
            if ((character >= '0' && character <= '9') || character == '.' || character == 'e' || character == 'E' ||
                character == '+' || character == '-')
            {
                position++;
                continue;
            }

            break;
        }

        if (position == Start)
        {
            return false;
        }

        const std::string token(Text.substr(Start, position - Start));
        char* end = nullptr;
        const double Number = std::strtod(token.c_str(), &end);
        if (*end != '\0')
        {
            return false;
        }

        value.Kind = Value::Kind::Number;
        value.Number = Number;
        return true;
    }

    bool ParseObject(Value& value, int depth)
    {
        if (!Consume('{'))
        {
            return false;
        }

        value.Kind = Value::Kind::Object;
        value.Object.clear();
        SkipWhitespace();
        if (Consume('}'))
        {
            return true;
        }

        for (;;)
        {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key))
            {
                return false;
            }

            SkipWhitespace();
            if (!Consume(':'))
            {
                return false;
            }

            Value item;
            if (!ParseValue(item, depth + 1))
            {
                return false;
            }

            value.Object.emplace_back(std::move(key), std::move(item));
            SkipWhitespace();
            if (Consume(','))
            {
                continue;
            }

            return Consume('}');
        }
    }

    bool ParseArray(Value& value, int depth)
    {
        if (!Consume('['))
        {
            return false;
        }

        value.Kind = Value::Kind::Array;
        value.Array.clear();
        SkipWhitespace();
        if (Consume(']'))
        {
            return true;
        }

        for (;;)
        {
            Value item;
            if (!ParseValue(item, depth + 1))
            {
                return false;
            }

            value.Array.push_back(std::move(item));
            SkipWhitespace();
            if (Consume(','))
            {
                continue;
            }

            return Consume(']');
        }
    }
};

} // namespace

const Value* Value::Find(std::string_view key) const
{
    if (Kind != Kind::Object)
    {
        return nullptr;
    }

    for (const auto& pair : Object)
    {
        if (pair.first == key)
        {
            return &pair.second;
        }
    }

    return nullptr;
}

std::optional<Value> Parse(std::string_view Text)
{
    Parser parser{Text};
    Value value;
    if (!parser.ParseValue(value, 0))
    {
        return std::nullopt;
    }

    parser.SkipWhitespace();
    if (!parser.AtEnd())
    {
        return std::nullopt;
    }

    return value;
}

std::string Escape(std::string_view Text)
{
    std::string result;
    result.reserve(Text.size() + 8);
    for (const char character : Text)
    {
        switch (character)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20)
            {
                result += std::format("\\u{:04x}", static_cast<unsigned char>(character));
            }
            else
            {
                result.push_back(character);
            }

            break;
        }
    }

    return result;
}

} // namespace wslc::internal::json
