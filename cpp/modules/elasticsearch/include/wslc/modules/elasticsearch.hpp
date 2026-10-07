#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class ElasticsearchContainer;

/// <summary>
/// Testcontainers-style builder for Elasticsearch. Encapsulates the Image, single-node settings
/// and readiness waits so tests don't memorize them. Defaults run an unsecured single node with a
/// 512 MB heap and niofs storage, suitable for a trusted test dependency.
/// </summary>
class ElasticsearchBuilder : public WslModuleBuilder<ElasticsearchBuilder>
{
public:
    /// <summary>Initializes the builder with the default Elasticsearch Image and readiness preset.</summary>
    ElasticsearchBuilder();

    /// <summary>Builds the container. The container is not started.</summary>
    ElasticsearchContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // Elasticsearch's startup log lines go through its own logging pipeline; readiness is the
    // cluster-health HTTP check added in Configure instead of a log message.
    int ReadyMessageOccurrences() const override { return 0; }
};

/// <summary>
/// Typed Elasticsearch container: renders the HTTP endpoint for a client or REST call.
/// </summary>
class ElasticsearchContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port the Elasticsearch HTTP API listens on by default (9200).</summary>
    static constexpr int DefaultPort = 9200;

    ElasticsearchContainer(const ElasticsearchContainer&) = delete;
    ElasticsearchContainer& operator=(const ElasticsearchContainer&) = delete;
    ElasticsearchContainer(ElasticsearchContainer&&) noexcept = default;
    ElasticsearchContainer& operator=(ElasticsearchContainer&&) noexcept = default;

    /// <summary>Renders the HTTP endpoint (e.g. http://127.0.0.1:49153) for a client or REST call.</summary>
    std::string GetEndpoint() const;

private:
    friend class ElasticsearchBuilder;
    explicit ElasticsearchContainer(WslContainer inner);
};

} // namespace wslc::modules
