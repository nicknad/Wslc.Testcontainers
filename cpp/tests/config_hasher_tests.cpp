#include <gtest/gtest.h>

#include "internal/configuration.hpp"

#include <optional>
#include <string>
#include <vector>

using wslc::ContainerNetworkMode;
using wslc::VhdAllocationType;
using wslc::internal::Configuration;
using wslc::internal::WslConfigHasher;
using wslc::internal::WslFileCopy;
using wslc::internal::WslPortMappingRecord;
using wslc::internal::WslScratchVolume;
using wslc::internal::WslVolumeMount;

TEST(ConfigHasher, IdenticalConfigurationsProduceIdenticalHashes)
{
    Configuration first;
    first.Image = "alpine:latest";
    first.Command = "sleep";
    first.CommandArguments = {"infinity"};
    first.Environment = {{"A", "1"}, {"B", "2"}};
    first.PortMappings = {WslPortMappingRecord{8080, std::nullopt}, WslPortMappingRecord{5432, std::nullopt}};

    Configuration second = first;
    second.Environment = {{"B", "2"}, {"A", "1"}};
    second.PortMappings = {WslPortMappingRecord{5432, std::nullopt}, WslPortMappingRecord{8080, std::nullopt}};

    EXPECT_EQ(WslConfigHasher::Compute(first), WslConfigHasher::Compute(second));
}

TEST(ConfigHasher, EnvironmentValuesChangeTheHash)
{
    Configuration first;
    first.Image = "alpine:latest";
    Configuration second = first;
    second.Environment = {{"A", "1"}};
    Configuration third = second;
    third.Environment = {{"A", "2"}};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(second));
    EXPECT_NE(WslConfigHasher::Compute(second), WslConfigHasher::Compute(third));
}

TEST(ConfigHasher, CommandArgumentOrderIsSignificant)
{
    Configuration first;
    first.Image = "alpine";
    first.Command = "sh";
    first.CommandArguments = {"-c", "one"};
    Configuration second = first;
    second.CommandArguments = {"one", "-c"};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(second));
}

TEST(ConfigHasher, BindAddressChangesTheHash)
{
    Configuration first;
    first.Image = "alpine";
    first.PortMappings = {WslPortMappingRecord{8080, std::nullopt}};
    Configuration bound = first;
    bound.PortMappings = {WslPortMappingRecord{8080, "127.0.0.1"}};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(bound));
}

TEST(ConfigHasher, ResourceNetworkingAndVolumeSettingsChangeTheHash)
{
    Configuration first;
    first.Image = "alpine";
    Configuration cpu = first;
    cpu.CpuCount = 2;
    Configuration memory = first;
    memory.MemoryMb = 2048;
    Configuration netmode = first;
    netmode.NetworkingMode = ContainerNetworkMode::Isolated;
    Configuration named = first;
    named.ScratchVolumes = {WslScratchVolume{"data", "/data", false, 100, VhdAllocationType::Dynamic}};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(cpu));
    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(memory));
    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(netmode));
    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(named));
}

TEST(ConfigHasher, ScratchVolumeOrderDoesNotChangeTheHash)
{
    Configuration first;
    first.Image = "alpine";
    first.ScratchVolumes = {WslScratchVolume{"a", "/a", false, 100, VhdAllocationType::Dynamic},
                            WslScratchVolume{"b", "/b", false, 200, VhdAllocationType::Dynamic}};
    Configuration reordered = first;
    reordered.ScratchVolumes = {first.ScratchVolumes[1], first.ScratchVolumes[0]};

    EXPECT_EQ(WslConfigHasher::Compute(first), WslConfigHasher::Compute(reordered));
}

TEST(ConfigHasher, VolumeReadOnlyFlagChangesTheHash)
{
    Configuration first;
    first.Image = "alpine";
    first.Volumes = {WslVolumeMount{"C:\\data", "/data", false}};
    Configuration second = first;
    second.Volumes = {WslVolumeMount{"C:\\data", "/data", true}};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(second));
}

TEST(ConfigHasher, DelimiterCharactersCannotCollide)
{
    Configuration first;
    first.Image = "alpine";
    first.Environment = {{"A", "x\nenv:B=y"}};
    Configuration second;
    second.Image = "alpine";
    second.Environment = {{"A", "x"}, {"B", "y"}};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(second));
}

TEST(ConfigHasher, ArgumentSeparatorCharactersCannotCollide)
{
    Configuration first;
    first.Image = "alpine";
    first.Command = "sh";
    first.CommandArguments = {"a\x1f"
                              "b"};
    Configuration second = first;
    second.CommandArguments = {"a", "b"};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(second));
}

TEST(ConfigHasher, NullAndEmptyValuesHashDifferently)
{
    Configuration first;
    first.Image = "alpine";
    first.WorkingDirectory = std::nullopt;
    Configuration second = first;
    second.WorkingDirectory = "";

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(second));
}

TEST(ConfigHasher, FilePathsAndDestinationsChangeTheHash)
{
    Configuration first;
    first.Image = "alpine";
    first.Files = {WslFileCopy{"C:\\one.txt", "/tmp/file.txt"}};
    Configuration otherSource = first;
    otherSource.Files = {WslFileCopy{"C:\\two.txt", "/tmp/file.txt"}};
    Configuration otherDestination = first;
    otherDestination.Files = {WslFileCopy{"C:\\one.txt", "/tmp/other.txt"}};

    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(otherSource));
    EXPECT_NE(WslConfigHasher::Compute(first), WslConfigHasher::Compute(otherDestination));
}
