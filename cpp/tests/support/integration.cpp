#include "support/integration.hpp"

namespace wslc::test
{

/// <summary>True when WSLC_RUN_INTEGRATION=1 (real WSL runtime tests).</summary>
bool IntegrationEnabled()
{
    char* value = nullptr;
    std::size_t length = 0;
    _dupenv_s(&value, &length, "WSLC_RUN_INTEGRATION");
    const bool enabled = value != nullptr && std::string(value) == "1";
    free(value);
    return enabled;
}

} // namespace wslc::test
