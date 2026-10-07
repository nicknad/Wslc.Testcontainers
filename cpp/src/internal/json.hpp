#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wslc::internal::json
{

/// <summary>
/// Minimal immutable JSON value for inspect payloads and instance metadata only.
/// Deliberately not a general parser: Ports.HostPort extraction and the metadata store are the
/// only consumers. Do not extend it (e.g. with new number formats) without port-mapping and
/// metadata round-trip tests; prefer a tested library if a third use case appears.
/// </summary>
struct Value
{
    enum class Kind
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    Kind Kind = Kind::Null;
    bool Boolean = false;
    double Number = 0;
    std::string String;
    std::vector<Value> Array;
    std::vector<std::pair<std::string, Value>> Object;

    bool IsNull() const noexcept { return Kind == Kind::Null; }
    bool IsBoolean() const noexcept { return Kind == Kind::Boolean; }
    bool IsNumber() const noexcept { return Kind == Kind::Number; }
    bool IsString() const noexcept { return Kind == Kind::String; }
    bool IsArray() const noexcept { return Kind == Kind::Array; }
    bool IsObject() const noexcept { return Kind == Kind::Object; }

    /// <summary>Looks up an Object property; nullptr when absent or not an Object.</summary>
    const Value* Find(std::string_view key) const;
};

/// <summary>
/// Parses a JSON document; nullopt on syntax errors. Callers treat nullopt as "not yet
/// available" (e.g. transient inspect payloads during port assignment) rather than failing.
/// </summary>
std::optional<Value> Parse(std::string_view Text);

/// <summary>Escapes a string for embedding in a JSON document.</summary>
std::string Escape(std::string_view Text);

} // namespace wslc::internal::json
