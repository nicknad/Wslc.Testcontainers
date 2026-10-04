#pragma once

#include <cstdlib>
#include <string>

namespace wslc::test
{

/// <summary>True when WSLC_RUN_INTEGRATION=1 (real WSL runtime tests).</summary>
bool IntegrationEnabled();

} // namespace wslc::test

/// <summary>Skips the current GoogleTest test unless real-runtime tests are enabled.</summary>
#define WSLC_SKIP_UNLESS_INTEGRATION()                                                                                 \
    if (!::wslc::test::IntegrationEnabled())                                                                           \
    GTEST_SKIP() << "Set WSLC_RUN_INTEGRATION=1 to run WSL container integration tests."
