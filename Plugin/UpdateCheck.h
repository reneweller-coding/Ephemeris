/**
 * @file UpdateCheck.h
 * @brief Whether a newer Ephemeris is out (26.09.2026): once a day, in the background, a question to GitHub about the
 *        latest release -- nothing is sent but the request itself, and nothing is downloaded or installed; the status
 *        row shows the version with a link to its page. The check can be switched off (the application's settings,
 *        not the project's: Ephemeris/Ephemeris.updates in the user's application data). One check is shared by every
 *        instance of the plugin in a host.
 * @author Rene Weller
 */
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>

/** @brief The update check (see the file comment); use it through juce::SharedResourcePointer. */
class UpdateCheck final : private juce::Thread
{
public:
    UpdateCheck();             ///< reads the settings and asks GitHub if a day has passed
    ~UpdateCheck() override;   ///< waits for a question on its way
    /**
     * whether the check runs
     * @return true when the check runs
     */
    bool enabled() const;
    /**
     * switches it (asks at once when switched on and due)
     * @param[in] on  true to run the check
     */
    void setEnabled( bool on );
    /**
     * a newer version found ("1.1.0"), empty if none
     * @return the newer version found, empty if none
     */
    juce::String newer() const;
    /**
     * its release page
     * @return its release page
     */
    juce::String page() const;
    /**
     * @brief Whether version @p a ("v1.2.3", "1.2") is later than @p b.
     * @param[in] a  a version
     * @param[in] b  another version
     * @return true when a is later than b
     */
    static bool later( const juce::String &a, const juce::String &b );

private:
    /** @brief The thread: asks GitHub for the latest release and keeps what it says. */
    void run() override;
    /** @brief Starts the thread if the check is on and a day has passed since the last question. */
    void askIfDue();
    std::unique_ptr<juce::PropertiesFile> m_settings;   ///< the check's own settings file
    mutable juce::CriticalSection         m_lock;       ///< guards m_newer and m_page
    juce::String                          m_newer;      ///< a newer version found, empty if none
    juce::String                          m_page;       ///< its release page
};
