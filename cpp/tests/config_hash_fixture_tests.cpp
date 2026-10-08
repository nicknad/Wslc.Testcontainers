#include <gtest/gtest.h>

#include "internal/configuration.hpp"
#include "internal/util.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using nlohmann::json;
using wslc::ContainerNetworkMode;
using wslc::VhdAllocationType;
using wslc::internal::Configuration;
using wslc::internal::ToUtf16;
using wslc::internal::WslConfigHasher;
using wslc::internal::WslFileCopy;
using wslc::internal::WslPortMappingRecord;
using wslc::internal::WslScratchVolume;
using wslc::internal::WslVolumeMount;

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

const json& RequireProperty(const json& object, std::string_view key)
{
    if (!object.is_object())
    {
        throw std::runtime_error("expected a JSON object");
    }

    const auto it = object.find(key);
    if (it == object.end())
    {
        throw std::runtime_error("missing property '" + std::string(key) + "'");
    }

    return *it;
}

std::optional<std::string> ReadOptionalString(const json& object, std::string_view key)
{
    const json& value = RequireProperty(object, key);
    if (value.is_null())
    {
        return std::nullopt;
    }

    if (!value.is_string())
    {
        throw std::runtime_error("property '" + std::string(key) + "' is not a string");
    }

    return value.get_ref<const std::string&>();
}

std::optional<std::uint32_t> ReadOptionalUInt32(const json& object, std::string_view key)
{
    const json& value = RequireProperty(object, key);
    if (value.is_null())
    {
        return std::nullopt;
    }

    if (!value.is_number())
    {
        throw std::runtime_error("property '" + std::string(key) + "' is not a number");
    }

    try
    {
        return value.get<std::uint32_t>();
    }
    catch (const json::exception& error)
    {
        throw std::runtime_error("property '" + std::string(key) + "' is out of range: " + error.what());
    }
}

std::optional<bool> ReadOptionalBool(const json& object, std::string_view key)
{
    const json& value = RequireProperty(object, key);
    if (value.is_null())
    {
        return std::nullopt;
    }

    if (!value.is_boolean())
    {
        throw std::runtime_error("property '" + std::string(key) + "' is not a boolean");
    }

    return value.get<bool>();
}

std::filesystem::path ToPath(std::string_view value)
{
    return std::filesystem::path(ToUtf16(value));
}

Configuration ToConfiguration(const json& element)
{
    Configuration configuration;

    configuration.Image = ReadOptionalString(element, "image");
    configuration.Command = ReadOptionalString(element, "command");
    configuration.WorkingDirectory = ReadOptionalString(element, "workingDirectory");

    for (const json& argument : RequireProperty(element, "args"))
    {
        configuration.CommandArguments.push_back(argument.get_ref<const std::string&>());
    }

    for (const auto& [name, value] : RequireProperty(element, "env").items())
    {
        configuration.Environment.emplace(name, value.get_ref<const std::string&>());
    }

    for (const json& item : RequireProperty(element, "files"))
    {
        configuration.Files.push_back(WslFileCopy{ToPath(RequireProperty(item, "host").get_ref<const std::string&>()),
                                                  RequireProperty(item, "container").get_ref<const std::string&>()});
    }

    for (const json& item : RequireProperty(element, "volumes"))
    {
        configuration.Volumes.push_back(
            WslVolumeMount{ToPath(RequireProperty(item, "host").get_ref<const std::string&>()),
                           RequireProperty(item, "container").get_ref<const std::string&>(),
                           RequireProperty(item, "readOnly").get<bool>()});
    }

    for (const json& item : RequireProperty(element, "sessionVolumes"))
    {
        const std::string type = RequireProperty(item, "type").get_ref<const std::string&>();
        configuration.ScratchVolumes.push_back(WslScratchVolume{
            RequireProperty(item, "name").get_ref<const std::string&>(),
            RequireProperty(item, "container").get_ref<const std::string&>(),
            RequireProperty(item, "readOnly").get<bool>(), RequireProperty(item, "size").get<std::uint64_t>(),
            type == "fixed" ? VhdAllocationType::Fixed : VhdAllocationType::Dynamic});
    }

    for (const json& item : RequireProperty(element, "ports"))
    {
        configuration.PortMappings.push_back(
            WslPortMappingRecord{RequireProperty(item, "container").get<int>(), ReadOptionalString(item, "bind")});
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

    json document;
    try
    {
        document = json::parse(text);
    }
    catch (const json::exception& error)
    {
        throw std::runtime_error(std::string("fixture is not valid JSON: ") + error.what());
    }

    if (!document.is_array())
    {
        throw std::runtime_error("fixture is not a JSON array");
    }

    std::vector<GoldenVector> vectors;
    for (const json& element : document)
    {
        vectors.push_back(GoldenVector{RequireProperty(element, "name").get_ref<const std::string&>(),
                                       ToConfiguration(element),
                                       RequireProperty(element, "sha256").get_ref<const std::string&>()});
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
