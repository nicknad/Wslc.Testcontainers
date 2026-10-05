#include <gtest/gtest.h>

#include "internal/configuration.hpp"
#include "internal/json.hpp"
#include "internal/util.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using wslc::ContainerNetworkMode;
using wslc::VhdAllocationType;
using wslc::internal::Configuration;
using wslc::internal::ToUtf16;
using wslc::internal::WslConfigHasher;
using wslc::internal::WslFileCopy;
using wslc::internal::WslPortMappingRecord;
using wslc::internal::WslScratchVolume;
using wslc::internal::WslVolumeMount;
using wslc::internal::json::Value;

#ifndef WSLC_REPO_ROOT
#error "WSLC_REPO_ROOT must be defined by the build to locate tests/fixtures."
#endif

namespace
{

struct GoldenVector
{
    std::string Name;
    Configuration Config;
    std::string Sha256;
};

const Value& RequireProperty(const Value& object, std::string_view key)
{
    const Value* value = object.Find(key);
    if (value == nullptr)
    {
        throw std::runtime_error("missing property '" + std::string(key) + "'");
    }

    return *value;
}

std::optional<std::string> ReadOptionalString(const Value& object, std::string_view key)
{
    const Value& value = RequireProperty(object, key);
    if (value.IsNull())
    {
        return std::nullopt;
    }

    if (!value.IsString())
    {
        throw std::runtime_error("property '" + std::string(key) + "' is not a string");
    }

    return value.String;
}

std::optional<std::uint32_t> ReadOptionalUInt32(const Value& object, std::string_view key)
{
    const Value& value = RequireProperty(object, key);
    if (value.IsNull())
    {
        return std::nullopt;
    }

    if (!value.IsNumber())
    {
        throw std::runtime_error("property '" + std::string(key) + "' is not a number");
    }

    return static_cast<std::uint32_t>(value.Number);
}

std::optional<bool> ReadOptionalBool(const Value& object, std::string_view key)
{
    const Value& value = RequireProperty(object, key);
    if (value.IsNull())
    {
        return std::nullopt;
    }

    if (!value.IsBoolean())
    {
        throw std::runtime_error("property '" + std::string(key) + "' is not a boolean");
    }

    return value.Boolean;
}

std::filesystem::path ToPath(std::string_view value)
{
    return std::filesystem::path(ToUtf16(value));
}

Configuration ToConfiguration(const Value& element)
{
    Configuration configuration;

    configuration.Image = ReadOptionalString(element, "image");
    configuration.Command = ReadOptionalString(element, "command");
    configuration.WorkingDirectory = ReadOptionalString(element, "workingDirectory");

    for (const Value& argument : RequireProperty(element, "args").Array)
    {
        configuration.CommandArguments.push_back(argument.String);
    }

    for (const auto& pair : RequireProperty(element, "env").Object)
    {
        configuration.Environment.emplace(pair.first, pair.second.String);
    }

    for (const Value& item : RequireProperty(element, "files").Array)
    {
        configuration.Files.push_back(
            WslFileCopy{ToPath(RequireProperty(item, "host").String), RequireProperty(item, "container").String});
    }

    for (const Value& item : RequireProperty(element, "volumes").Array)
    {
        configuration.Volumes.push_back(WslVolumeMount{ToPath(RequireProperty(item, "host").String),
                                                       RequireProperty(item, "container").String,
                                                       RequireProperty(item, "readOnly").Boolean});
    }

    for (const Value& item : RequireProperty(element, "sessionVolumes").Array)
    {
        const std::string type = RequireProperty(item, "type").String;
        configuration.ScratchVolumes.push_back(WslScratchVolume{
            RequireProperty(item, "name").String, RequireProperty(item, "container").String,
            RequireProperty(item, "readOnly").Boolean, static_cast<std::uint64_t>(RequireProperty(item, "size").Number),
            type == "fixed" ? VhdAllocationType::Fixed : VhdAllocationType::Dynamic});
    }

    for (const Value& item : RequireProperty(element, "ports").Array)
    {
        configuration.PortMappings.push_back(WslPortMappingRecord{
            static_cast<int>(RequireProperty(item, "container").Number), ReadOptionalString(item, "bind")});
    }

    configuration.NetworkingMode = [&]() -> std::optional<ContainerNetworkMode>
    {
        const std::optional<std::string> mode = ReadOptionalString(element, "networkingMode");
        if (!mode)
        {
            return std::nullopt;
        }

        if (*mode == "bridged")
        {
            return ContainerNetworkMode::Bridged;
        }

        if (*mode == "none")
        {
            return ContainerNetworkMode::Isolated;
        }

        throw std::runtime_error("unknown networkingMode '" + *mode + "'");
    }();

    configuration.CpuCount = ReadOptionalUInt32(element, "cpu");
    configuration.MemoryMb = ReadOptionalUInt32(element, "memoryMB");
    configuration.Reuse = ReadOptionalBool(element, "reuse");

    return configuration;
}

std::vector<GoldenVector> LoadGoldenVectors()
{
    const std::filesystem::path fixture =
        std::filesystem::path(WSLC_REPO_ROOT) / "tests" / "fixtures" / "config_hash_vectors.json";
    std::ifstream stream(fixture, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("cannot open fixture");
    }

    std::string text;
    stream.seekg(0, std::ios::end);
    text.resize(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0, std::ios::beg);
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));

    const std::optional<Value> document = wslc::internal::json::Parse(text);
    if (!document || !document->IsArray())
    {
        throw std::runtime_error("fixture is not a JSON array");
    }

    std::vector<GoldenVector> vectors;
    for (const Value& element : document->Array)
    {
        vectors.push_back(GoldenVector{RequireProperty(element, "name").String, ToConfiguration(element),
                                       RequireProperty(element, "sha256").String});
    }

    return vectors;
}

} // namespace

TEST(ConfigHashFixture, SharedGoldenVectorsMatch)
{
    std::vector<GoldenVector> vectors;
    try
    {
        vectors = LoadGoldenVectors();
    }
    catch (const std::exception& error)
    {
        FAIL() << "failed to load golden vectors: " << error.what();
    }

    ASSERT_FALSE(vectors.empty());
    for (const GoldenVector& vector : vectors)
    {
        EXPECT_EQ(WslConfigHasher::Compute(vector.Config), vector.Sha256)
            << "golden vector '" << vector.Name << "' diverged";
    }
}

TEST(ConfigHashFixture, ReuseFlagDoesNotChangeTheHash)
{
    const std::vector<GoldenVector> vectors = LoadGoldenVectors();
    std::map<std::string, Configuration> by_name;
    for (const GoldenVector& vector : vectors)
    {
        by_name.emplace(vector.Name, vector.Config);
    }

    // Reuse is a session policy switch, not configuration metadata: the two vectors differ only
    // in their reuse flag and are expected to share one identity.
    ASSERT_EQ(by_name.count("reuse-true"), 1u);
    ASSERT_EQ(by_name.count("reuse-false"), 1u);
    EXPECT_EQ(WslConfigHasher::Compute(by_name.at("reuse-true")), WslConfigHasher::Compute(by_name.at("reuse-false")));
}
