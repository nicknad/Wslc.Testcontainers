#include "wslc/modules/elasticsearch.hpp"

#include "wslc/waiting/wait.hpp"

#include <chrono>
#include <utility>

namespace wslc::modules
{

ElasticsearchBuilder::ElasticsearchBuilder()
    : WslModuleBuilder<ElasticsearchBuilder>("docker.io/library/elasticsearch:9.5.3",
                                             ElasticsearchContainer::DefaultPort, "")
{
    // The Image is ~850 MB and the first start pays extraction plus JVM bootstrap, so the
    // per-wait default is raised; the startup budget derives from it (2 * timeout + 30s).
    WithWaitTimeout(std::chrono::minutes(5));
}

ElasticsearchContainer ElasticsearchBuilder::Build()
{
    return ElasticsearchContainer(BuildContainer());
}

WslContainerBuilder& ElasticsearchBuilder::Configure(WslContainerBuilder& builder)
{
    // The Image entrypoint strips the `eswrapper` command and forwards the remaining arguments to
    // elasticsearch. Settings travel as -E options because POSIX environment variable names cannot
    // contain the dots ES settings use.
    return builder
        .WithCommand("eswrapper", {"-Ediscovery.type=single-node", "-Expack.security.enabled=false",
                                   "-Expack.security.enrollment.enabled=false",
                                   // The container kernel reports vm.max_map_count=65530, below the 262144 the mmap
                                   // bootstrap check requires; niofs storage disables mmap and skips that check.
                                   "-Enode.store.allow_mmap=false"})
        .WithEnvironment("ES_JAVA_OPTS", "-Xms512m -Xmx512m")
        .WithWaitStrategy(wslc::waiting::ForWsl()
                              .WithTimeout(WaitTimeout())
                              .UntilHttpRequestSucceeds("/_cluster/health?wait_for_status=yellow&timeout=60s",
                                                        ElasticsearchContainer::DefaultPort));
}

ElasticsearchContainer::ElasticsearchContainer(WslContainer inner) : WslModuleContainer(std::move(inner)) {}

std::string ElasticsearchContainer::GetEndpoint() const
{
    return FormatHttpEndpoint(GetConnectEndpoint(DefaultPort));
}

} // namespace wslc::modules
