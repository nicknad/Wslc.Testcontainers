#include <gtest/gtest.h>

#include "internal/pipe_reader.hpp"
#include "wslc/exceptions.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <stop_token>
#include <string>
#include <thread>

using wslc::OperationCanceledException;
using wslc::internal::IoHandle;
using wslc::internal::ReadPipeAvailable;

namespace
{

struct PipePair
{
    IoHandle read;
    IoHandle write;
};

PipePair CreatePipePair()
{
    HANDLE read = INVALID_HANDLE_VALUE;
    HANDLE write = INVALID_HANDLE_VALUE;
    EXPECT_NE(CreatePipe(&read, &write, nullptr, 0), 0) << "CreatePipe failed: " << GetLastError();
    return PipePair{IoHandle(read), IoHandle(write)};
}

} // namespace

TEST(CopyCancel, PreCancelledTokenThrowsWithoutBlocking)
{
    PipePair pipe = CreatePipePair();
    std::stop_source source;
    source.request_stop();

    std::array<char, 64> buffer{};
    const auto started = std::chrono::steady_clock::now();
    EXPECT_THROW(ReadPipeAvailable(pipe.read, buffer, source.get_token()), OperationCanceledException);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}

TEST(CopyCancel, ReturnsWrittenDataIntactAndReportsEndOfFile)
{
    PipePair pipe = CreatePipePair();
    const std::string payload = "wslc-copy-cancel-payload";

    DWORD written = 0;
    ASSERT_NE(WriteFile(pipe.write.get(), payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr), 0);
    ASSERT_EQ(written, payload.size());
    pipe.write.reset();

    std::array<char, 64> buffer{};
    const std::size_t read = ReadPipeAvailable(pipe.read, buffer, std::stop_token{});
    ASSERT_EQ(read, payload.size());
    EXPECT_EQ(std::string(buffer.data(), read), payload);

    // The closed write end surfaces as end-of-file instead of blocking.
    EXPECT_EQ(ReadPipeAvailable(pipe.read, buffer, std::stop_token{}), 0u);
}

TEST(CopyCancel, CancellationInterruptsAnIdleRead)
{
    PipePair pipe = CreatePipePair();
    std::stop_source source;
    std::array<char, 64> buffer{};

    std::jthread canceller(
        [&source]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            source.request_stop();
        });

    const auto started = std::chrono::steady_clock::now();
    EXPECT_THROW(ReadPipeAvailable(pipe.read, buffer, source.get_token()), OperationCanceledException);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}
