using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.Elasticsearch;

/// <summary>
/// Testcontainers-style builder for Elasticsearch. Encapsulates the image, single-node settings
/// and readiness waits so tests don't memorize them. Defaults run an unsecured single node with a
/// 512 MB heap and niofs storage, suitable for a trusted test dependency.
/// </summary>
public sealed class ElasticsearchBuilder : WslModuleBuilder<ElasticsearchBuilder>
{
    /// <summary>Initializes the builder with the default Elasticsearch image and readiness preset.</summary>
    public ElasticsearchBuilder()
        : base(
            "docker.io/library/elasticsearch:9.5.3",
            ElasticsearchContainer.DefaultPort,
            string.Empty)
    {
        // The image is ~850 MB and the first start pays extraction plus JVM bootstrap, so the
        // per-wait default is raised; the startup budget derives from it (2 * timeout + 30s).
        WithWaitTimeout(TimeSpan.FromMinutes(5));
    }

    /// <summary>Builds the container. The container is not started.</summary>
    public ElasticsearchContainer Build() => new(BuildContainer());

    // Elasticsearch's startup log lines go through its own logging pipeline; readiness is the
    // cluster-health HTTP check added in Configure instead of a log message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            // The image entrypoint strips the `eswrapper` command and forwards the remaining
            // arguments to elasticsearch. Settings travel as -E options because POSIX environment
            // variable names cannot contain the dots ES settings use.
            .WithCommand(
                "eswrapper",
                "-Ediscovery.type=single-node",
                "-Expack.security.enabled=false",
                "-Expack.security.enrollment.enabled=false",
                // The container kernel reports vm.max_map_count=65530, below the 262144 the mmap
                // bootstrap check requires; niofs storage disables mmap and skips that check.
                "-Enode.store.allow_mmap=false")
            .WithEnvironment("ES_JAVA_OPTS", "-Xms512m -Xmx512m")
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds(
                        "/_cluster/health?wait_for_status=yellow&timeout=60s",
                        ElasticsearchContainer.DefaultPort));
}
