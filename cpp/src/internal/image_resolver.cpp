#include "internal/image_resolver.hpp"

#include "internal/api.hpp"
#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <optional>
#include <string>

namespace wslc::internal
{

namespace
{

const char* ProgressStatusName(WslcImageProgressStatus status)
{
    switch (status)
    {
    case WSLC_IMAGE_PROGRESS_STATUS_PULLING:
        return "Pulling fs layer";
    case WSLC_IMAGE_PROGRESS_STATUS_WAITING:
        return "Waiting";
    case WSLC_IMAGE_PROGRESS_STATUS_DOWNLOADING:
        return "Downloading";
    case WSLC_IMAGE_PROGRESS_STATUS_VERIFYING:
        return "Verifying Checksum";
    case WSLC_IMAGE_PROGRESS_STATUS_EXTRACTING:
        return "Extracting";
    case WSLC_IMAGE_PROGRESS_STATUS_COMPLETE:
        return "Pull Complete";
    default:
        return "Unknown";
    }
}

HRESULT CALLBACK PullProgressCallback(const WslcImageProgressMessage* progress, PVOID context)
{
    if (progress == nullptr || context == nullptr)
    {
        return S_OK;
    }

    const auto* Publish = static_cast<std::function<void(LogLine)>*>(context);
    try
    {
        (*Publish)(LogLine::Diagnostic(std::string("pull ") + ProgressStatusName(progress->status) + " " +
                                       std::to_string(progress->detail.currentBytes) + "/" +
                                       std::to_string(progress->detail.totalBytes)));
    }
    catch (...)
    {
        // Diagnostics must never tear down the native pull.
    }

    return S_OK;
}

HRESULT CALLBACK ImportProgressCallback(const WslcImageProgressMessage* progress, PVOID context)
{
    if (progress == nullptr || context == nullptr)
    {
        return S_OK;
    }

    const auto* Publish = static_cast<std::function<void(LogLine)>*>(context);
    try
    {
        (*Publish)(LogLine::Diagnostic(std::string("import ") + ProgressStatusName(progress->status)));
    }
    catch (...)
    {
        // Diagnostics must never tear down the native import.
    }

    return S_OK;
}

struct ImageReference
{
    std::string repository;
    std::string tag;
};

std::optional<ImageReference> TryParseReference(std::string_view reference)
{
    if (IsBlank(reference) || reference.contains('@'))
    {
        return std::nullopt;
    }

    std::string remaining(reference);
    std::string tag = "latest";
    const std::size_t last_slash = remaining.rfind('/');
    const std::size_t last_colon = remaining.rfind(':');
    if (last_colon != std::string::npos && (last_slash == std::string::npos || last_colon > last_slash))
    {
        tag = remaining.substr(last_colon + 1);
        remaining = remaining.substr(0, last_colon);
        if (tag.empty())
        {
            return std::nullopt;
        }
    }

    // The first segment is a registry only when it looks like a Host: it contains a dot or
    // port, or is localhost. Otherwise it is a namespace on Docker Hub.
    std::string repository;
    const std::size_t first_slash = remaining.find('/');
    if (first_slash == std::string::npos)
    {
        repository = "docker.io/library/" + remaining;
    }
    else
    {
        const std::string first_segment = remaining.substr(0, first_slash);
        const bool has_registry =
            first_segment.contains('.') || first_segment.contains(':') || EqualsIgnoreCase(first_segment, "localhost");
        repository = has_registry ? remaining : "docker.io/" + remaining;
    }

    if (repository.empty())
    {
        return std::nullopt;
    }

    return ImageReference{std::move(repository), std::move(tag)};
}

} // namespace

ImageResolver::ImageResolver(WslcSession session, const Configuration& configuration,
                             std::function<void(LogLine)> Publish)
    : m_session(session), m_configuration(configuration), m_publish(std::move(Publish))
{
}

std::string ImageResolver::Resolve(const std::string& fallback_image_name, std::stop_token token)
{
    EnsureComInitialized();
    if (m_configuration.Image)
    {
        const std::string& Image = *m_configuration.Image;
        if (!ImageExists(Image, token))
        {
            m_publish(LogLine::Diagnostic("pulling image '" + Image + "'"));
            WslcPullImageOptions options{};
            options.uri = Image.c_str();
            options.progressCallback = &PullProgressCallback;
            options.progressCallbackContext = &m_publish;
            options.registryAuth = nullptr;
            PWSTR error = nullptr;
            const HRESULT result = WslcPullSessionImage(m_session, &options, &error);
            check(result, ErrorKind::Provisioning, "Failed to pull image '" + Image + "'", &error);
        }

        return Image;
    }

    const std::string TarballImageName = m_configuration.TarballImageName.value_or(fallback_image_name);
    const std::filesystem::path TarballPath = m_configuration.TarballPath.value_or(std::filesystem::path());
    m_publish(
        LogLine::Diagnostic("importing image '" + TarballImageName + "' from '" + ToUtf8(TarballPath.wstring()) + "'"));
    WslcImportImageOptions options{};
    options.progressCallback = &ImportProgressCallback;
    options.progressCallbackContext = &m_publish;
    const std::wstring path = TarballPath.wstring();
    PWSTR error = nullptr;
    const HRESULT result =
        WslcImportSessionImageFromFile(m_session, TarballImageName.c_str(), path.c_str(), &options, &error);
    check(result, ErrorKind::Provisioning, "Failed to import image '" + TarballImageName + "'", &error);
    return TarballImageName;
}

bool ImageResolver::ImageExists(const std::string& Image, std::stop_token token)
{
    // Enumeration is fast; still observe cancellation between calls.
    ThrowIfStopped(token);

    WslcImageInfo* images = nullptr;
    std::uint32_t count = 0;
    const HRESULT result = WslcListSessionImages(m_session, &images, &count);
    if (FAILED(result))
    {
        // Fall back to pulling; a persistent failure is visible through the pull error.
        m_publish(LogLine::Diagnostic("could not enumerate cached images (" + HresultHex(result) + "); pulling '" +
                                      Image + "'"));
        return false;
    }

    bool exists = false;
    for (std::uint32_t i = 0; i < count && !exists; i++)
    {
        exists = MatchesImage(images[i].name, Image);
    }

    if (images != nullptr)
    {
        CoTaskMemFree(images);
    }

    return exists;
}

bool ImageResolver::MatchesImage(std::string_view candidate, std::string_view Image)
{
    const auto candidate_reference = TryParseReference(candidate);
    const auto image_reference = TryParseReference(Image);
    if (!candidate_reference || !image_reference)
    {
        return false;
    }

    return EqualsIgnoreCase(candidate_reference->repository, image_reference->repository) &&
           EqualsIgnoreCase(candidate_reference->tag, image_reference->tag);
}

} // namespace wslc::internal
