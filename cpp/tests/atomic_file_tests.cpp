#include <gtest/gtest.h>

#include <windows.h>

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using wslc::WslProcessException;
using wslc::internal::BestEffortDeleteFile;
using wslc::internal::CommitFileReplace;
using wslc::internal::CreateAdjacentTempFile;
using wslc::internal::EnsureReplaceableDestination;
using wslc::internal::IsReparsePoint;
using wslc::internal::RandomHex;

namespace
{

std::filesystem::path MakeTempDirectory()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("wslc-atomic-" + RandomHex(8));
    std::filesystem::create_directories(path);
    return path;
}

void WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST(AtomicFile, CommitReplacesTheExistingDestination)
{
    const std::filesystem::path directory = MakeTempDirectory();
    const std::filesystem::path destination = directory / "dest.txt";
    WriteFile(destination, "old");

    const std::filesystem::path temp = CreateAdjacentTempFile(destination);
    WriteFile(temp, "new");
    CommitFileReplace(temp, destination);

    EXPECT_EQ(ReadFile(destination), "new");
    EXPECT_FALSE(std::filesystem::exists(temp));

    std::filesystem::remove_all(directory);
}

TEST(AtomicFile, DiscardLeavesTheDestinationUntouched)
{
    const std::filesystem::path directory = MakeTempDirectory();
    const std::filesystem::path destination = directory / "dest.txt";
    WriteFile(destination, "old");

    const std::filesystem::path temp = CreateAdjacentTempFile(destination);
    WriteFile(temp, "partial");
    BestEffortDeleteFile(temp);

    EXPECT_FALSE(std::filesystem::exists(temp));
    EXPECT_EQ(ReadFile(destination), "old");

    std::filesystem::remove_all(directory);
}

TEST(AtomicFile, EnsureReplaceableRejectsDirectories)
{
    const std::filesystem::path directory = MakeTempDirectory();
    EXPECT_THROW(EnsureReplaceableDestination(directory), WslProcessException);
    std::filesystem::remove_all(directory);
}

TEST(AtomicFile, EnsureReplaceableAllowsMissingAndRegularFiles)
{
    const std::filesystem::path directory = MakeTempDirectory();
    EXPECT_NO_THROW(EnsureReplaceableDestination(directory / "missing.txt"));

    const std::filesystem::path regular = directory / "regular.txt";
    WriteFile(regular, "data");
    EXPECT_NO_THROW(EnsureReplaceableDestination(regular));

    std::filesystem::remove_all(directory);
}

TEST(AtomicFile, EnsureReplaceableRejectsReparsePoints)
{
    const std::filesystem::path directory = MakeTempDirectory();
    const std::filesystem::path target = directory / "target.txt";
    WriteFile(target, "payload");
    const std::filesystem::path link = directory / "link.txt";

    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), 0) == 0)
    {
        const DWORD error = GetLastError();
        std::filesystem::remove_all(directory);
        GTEST_SKIP() << "Symbolic link creation is not available: " << error;
    }

    EXPECT_THROW(EnsureReplaceableDestination(link), WslProcessException);
    EXPECT_EQ(ReadFile(target), "payload");
    std::filesystem::remove_all(directory);
}

TEST(AtomicFile, IsReparsePointDetectsLinks)
{
    const std::filesystem::path directory = MakeTempDirectory();
    const std::filesystem::path target = directory / "target.txt";
    WriteFile(target, "payload");
    const std::filesystem::path link = directory / "link.txt";

    EXPECT_FALSE(IsReparsePoint(target));
    EXPECT_FALSE(IsReparsePoint(directory / "missing.txt"));

    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), 0) == 0)
    {
        const DWORD error = GetLastError();
        std::filesystem::remove_all(directory);
        GTEST_SKIP() << "Symbolic link creation is not available: " << error;
    }

    EXPECT_TRUE(IsReparsePoint(link));
    std::filesystem::remove_all(directory);
}
