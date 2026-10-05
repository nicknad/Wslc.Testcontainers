#include "wslc/exec.hpp"

#include "wslc/exceptions.hpp"

namespace wslc
{

namespace
{

std::string truncate(const std::string& value, std::size_t max_chars = 4096)
{
    if (value.size() <= max_chars)
    {
        return value;
    }

    return value.substr(0, max_chars) + "\n... (truncated, " + std::to_string(value.size() - max_chars) +
           " chars omitted)";
}

} // namespace

const ExecResult& ExecResult::EnsureSuccess() const
{
    if (!Succeeded())
    {
        throw WslProcessException("Command failed with exit code " + std::to_string(ExitCode) + ".\nstdout:\n" +
                                  truncate(Stdout) + "\nstderr:\n" + truncate(Stderr));
    }

    return *this;
}

} // namespace wslc
