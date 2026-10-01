/**
 * @file UpdateCheck.cpp
 * @brief The update check (UpdateCheck.h).
 */
#include "UpdateCheck.h"
#include <cstdlib>

namespace {
const char* const kReleases = "https://api.github.com/repos/reneweller-coding/Ephemeris/releases/latest";   ///< GitHub's API for the latest release
constexpr juce::int64 kDay = 24 * 60 * 60 * 1000;   ///< a day, ms

/** @brief The check's settings file: Ephemeris.updates beside the standalone's settings. */
juce::PropertiesFile::Options options()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Ephemeris";
    o.filenameSuffix = ".updates";   // its own file: the standalone keeps its window and audio in Ephemeris.settings
    o.folderName = "Ephemeris";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}
} // namespace

UpdateCheck::UpdateCheck() : juce::Thread("Ephemeris update check"), settings_(std::make_unique<juce::PropertiesFile>(options()))
{
    // What the last check found, until the next one.
    const juce::String latest = settings_->getValue("latest");
    if (later(latest, EPH_VERSION)) {
        newer_ = latest.trimCharactersAtStart("vV");
        page_ = settings_->getValue("page");
    }
    askIfDue();
}

UpdateCheck::~UpdateCheck()
{
    stopThread(10000);
}

bool UpdateCheck::enabled() const
{
    return settings_->getBoolValue("check", true);
}

void UpdateCheck::setEnabled(bool on)
{
    settings_->setValue("check", on);
    settings_->saveIfNeeded();
    if (!on) {
        const juce::ScopedLock g(lock_);
        newer_.clear();
    }
    askIfDue();
}

juce::String UpdateCheck::newer() const
{
    const juce::ScopedLock g(lock_);
    return newer_;
}

juce::String UpdateCheck::page() const
{
    const juce::ScopedLock g(lock_);
    return page_;
}

bool UpdateCheck::later(const juce::String& a, const juce::String& b)
{
    auto parts = [](const juce::String& v) {
        juce::StringArray p = juce::StringArray::fromTokens(v.trim().trimCharactersAtStart("vV").upToFirstOccurrenceOf("-", false, false), ".", "");
        std::array<int, 3> n = { 0, 0, 0 };
        for (int i = 0; i < 3 && i < p.size(); ++i) n[static_cast<size_t>(i)] = p[i].getIntValue();
        return n;
    };
    if (a.trim().isEmpty()) return false;
    return parts(a) > parts(b);
}

void UpdateCheck::askIfDue()
{
    if (!enabled() || isThreadRunning()) return;
    const juce::int64 last = settings_->getValue("lastCheck", "0").getLargeIntValue();
    if (juce::Time::currentTimeMillis() - last < kDay) return;
    startThread(juce::Thread::Priority::low);
}

void UpdateCheck::run()
{
    settings_->setValue("lastCheck", juce::String(juce::Time::currentTimeMillis()));
    settings_->saveIfNeeded();
    // EPH_UPDATE_URL points the question elsewhere (a test aid: another repository's releases).
    const char* other = std::getenv("EPH_UPDATE_URL");
    const auto request = juce::URL(other != nullptr ? juce::String(other) : juce::String(kReleases)).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(8000)
        .withExtraHeaders(juce::String("User-Agent: Ephemeris/") + EPH_VERSION + "\r\nAccept: application/vnd.github+json"));
    if (request == nullptr || threadShouldExit()) return;
    const juce::var release = juce::JSON::parse(request->readEntireStreamAsString());
    const juce::String tag = release.getProperty("tag_name", "").toString(), url = release.getProperty("html_url", "").toString();
    if (tag.isEmpty()) return;   // no release yet, or the repository not public
    settings_->setValue("latest", tag);
    settings_->setValue("page", url);
    settings_->saveIfNeeded();
    const juce::ScopedLock g(lock_);
    if (later(tag, EPH_VERSION)) {
        newer_ = tag.trimCharactersAtStart("vV");
        page_ = url;
    } else {
        newer_.clear();
    }
}
