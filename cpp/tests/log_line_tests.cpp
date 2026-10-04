#include <gtest/gtest.h>

#include "wslc/log_line.hpp"

#include <chrono>
#include <string>

using wslc::LogLine;
using wslc::LogSource;

TEST(LogLine, UnknownSourceFallsBackToSystem)
{
    const LogLine line{static_cast<LogSource>(3), "x", std::chrono::system_clock::now()};

    const std::string rendered = line.ToString();

    EXPECT_NE(rendered.find("system"), std::string::npos);
}
