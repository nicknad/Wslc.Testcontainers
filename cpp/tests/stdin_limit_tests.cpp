#include <gtest/gtest.h>

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <string>

using wslc::WslProcessException;
using wslc::internal::c_maxStandardInputBytes;
using wslc::internal::ValidateStandardInputSize;

TEST(StandardInputLimit, AcceptsTheLimit)
{
    EXPECT_NO_THROW(ValidateStandardInputSize(c_maxStandardInputBytes));
}

TEST(StandardInputLimit, RejectsOneByteOverTheLimit)
{
    try
    {
        ValidateStandardInputSize(c_maxStandardInputBytes + 1);
        FAIL() << "Expected wslc::WslProcessException";
    }
    catch (const WslProcessException& exception)
    {
        EXPECT_NE(std::string(exception.what()).find("64 MiB"), std::string::npos);
    }
}
