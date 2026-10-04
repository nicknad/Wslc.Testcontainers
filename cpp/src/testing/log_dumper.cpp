#include "wslc/testing/log_dumper.hpp"

#include "wslc/exceptions.hpp"

namespace wslc::testing
{

void LogDumper::Dump(LogStream& Logs, const std::function<void(const std::string&)>& write_line, int maxLines,
                     std::stop_token token)
{
    if (!write_line)
    {
        throw WslcException("The write callback must not be null.");
    }

    if (maxLines <= 0)
    {
        throw WslcException("maxLines must be positive.");
    }

    int count = 0;
    while (count < maxLines)
    {
        const auto line = Logs.Next(token);
        if (!line)
        {
            break;
        }

        write_line(line->ToString());
        count++;
    }
}

} // namespace wslc::testing
