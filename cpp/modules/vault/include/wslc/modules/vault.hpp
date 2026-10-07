#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class VaultContainer;

/// <summary>
/// Testcontainers-style builder for HashiCorp Vault. Encapsulates the Image, dev-mode command,
/// port and readiness waits so tests don't memorize them. The default runs Vault in dev mode
/// with a fixed root token; override the root token when a test needs its own. Dev mode stores
/// everything in memory and is for tests only.
/// </summary>
class VaultBuilder : public WslModuleBuilder<VaultBuilder>
{
public:
    /// <summary>Initializes the builder with the default Vault Image and readiness preset.</summary>
    VaultBuilder();

    /// <summary>Sets the dev-mode root token. Defaults to "root".</summary>
    VaultBuilder& WithRootToken(std::string rootToken);

    /// <summary>Builds the container with the configured root token. The container is not started.</summary>
    VaultContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // Vault's startup output has no stable single readiness line, so readiness is the
    // /v1/sys/health HTTP check added in Configure instead of a log message.
    int ReadyMessageOccurrences() const override { return 0; }

private:
    std::string m_rootToken = "root";
};

/// <summary>
/// Typed Vault container: renders the HTTP address and root token for a Vault client
/// configuration.
/// </summary>
class VaultContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Vault's HTTP API listens on by default (8200).</summary>
    static constexpr int DefaultPort = 8200;

    VaultContainer(const VaultContainer&) = delete;
    VaultContainer& operator=(const VaultContainer&) = delete;
    VaultContainer(VaultContainer&&) noexcept = default;
    VaultContainer& operator=(VaultContainer&&) noexcept = default;

    /// <summary>Renders the Vault server address (e.g. http://127.0.0.1:49153) for VAULT_ADDR.</summary>
    std::string GetAddress() const;

    /// <summary>The dev-mode root token clients must authenticate with.</summary>
    std::string RootToken() const { return m_rootToken; }

private:
    friend class VaultBuilder;
    VaultContainer(WslContainer inner, std::string rootToken);

    std::string m_rootToken;
};

} // namespace wslc::modules
