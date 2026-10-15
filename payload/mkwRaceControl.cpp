#if RMC

#  include "import/mkw/system/RaceConfig.hpp"
#  include "import/mkw/system/RaceManager.hpp"
#  include "import/mkw/system/System.hpp"
#  include "import/mkw/ui/page/MessagePopupPage.hpp"
#  include "import/mkw/ui/section/SectionManager.hpp"
#  include "mkwRaceControl.hpp"
#  include "wwfcGPReport.hpp"

namespace wwfc::mkw::RaceControl
{

namespace
{

constexpr u16 MESSAGE_WAIT_FRAMES        = 120;
constexpr u16 NATIVE_TIMEOUT_WAIT_FRAMES = 900;
constexpr u8  NATIVE_TIMEOUT_MAX_APPLIES = 8;

bool s_endRaceRequested = false;
u16  s_endRaceFrames    = 0;
bool s_nativeTimeoutSet = false;
u8   s_timeoutApplies   = 0;
bool s_messageShown     = false;
bool s_waitReported     = false;

bool PushServerDecisionMessage()
{
    UI::SectionManager* sectionManager = UI::SectionManager::Instance();
    if (sectionManager == nullptr) {
        return false;
    }

    UI::Section* section = sectionManager->currentSection();
    if (section == nullptr) {
        return false;
    }

    UI::MessagePopupPage* messagePopupPage =
        section->getPage<UI::MessagePopupPage>(UI::EPageID::POPUP_MESSAGE);
    if (messagePopupPage == nullptr) {
        return false;
    }

    static const wchar_t s_serverDecisionMessage[] =
        L"The server has decided.\n"
        L"The race will conclude.";

    UI::FormatParam formatParam{};
    formatParam.strings[0] = s_serverDecisionMessage;

    messagePopupPage->reset();
    messagePopupPage->setWindowMessage(0x19CA, &formatParam);

    static const UI::EPageID racePages[] = {
        UI::EPageID::WIFI_RACE_1P,
        UI::EPageID::WIFI_RACE_2P,
        UI::EPageID::WIFI_FRIEND_RACE,
        UI::EPageID::WIFI_FRIEND_TEAM_RACE,
    };

    for (UI::EPageID racePageId : racePages) {
        UI::Page* racePage = section->getPage<UI::Page>(racePageId);
        if (racePage == nullptr) {
            continue;
        }

        racePage->push(UI::EPageID::POPUP_MESSAGE, UI::Page::ENextType::FORWARD);
        return true;
    }

    return false;
}

void ClearRequest()
{
    s_endRaceRequested = false;
    s_endRaceFrames    = 0;
    s_nativeTimeoutSet = false;
    s_timeoutApplies   = 0;
    s_messageShown     = false;
    s_waitReported     = false;
}

// Puts the race timer just before the online time limit so the game ends
// the race by itself.
void ApplyNativeTimeout(Time& raceTime)
{
    raceTime.m_minutes      = 5;
    raceTime.m_seconds      = 59;
    raceTime.m_milliseconds = 900;
    raceTime.m_valid        = true;
    ++s_timeoutApplies;
}

} // namespace

void RequestEndRace()
{
    ClearRequest();
    s_endRaceRequested = true;
}

void Tick()
{
    if (!s_endRaceRequested) {
        return;
    }

    if (!System::Instance().isCurrentSceneID(Scene::ESceneID::RACE)) {
        GPReport::Report("wl:mkw_end_race", "not_in_race");
        ClearRequest();
        return;
    }

    RaceConfig& config = RaceConfigManager::Instance()->getConfig();
    if (!config.isWifiVSRace() && !config.isWifiBattle()) {
        GPReport::Report("wl:mkw_end_race", "not_in_race");
        ClearRequest();
        return;
    }

    RaceManager* raceManager = RaceManager::Instance();
    if (raceManager == nullptr || raceManager->m_timer == nullptr) {
        return;
    }

    switch (raceManager->m_state) {
    case RaceManager::EState::INTRO:
    case RaceManager::EState::COUNTDOWN:
        // The game resets the race timer when the race starts, so wait for it.
        if (!s_waitReported) {
            s_waitReported = true;
            GPReport::Report("wl:mkw_end_race", "waiting_for_race");
        }
        return;

    case RaceManager::EState::RACE:
        break;

    case RaceManager::EState::FINISHED:
    case RaceManager::EState::FINISHED_TIME_ATTACK:
        GPReport::Report("wl:mkw_end_race", "finished");
        ClearRequest();
        return;
    }

    if (!s_messageShown) {
        s_messageShown  = true;
        s_endRaceFrames = 0;
        GPReport::Report(
            "wl:mkw_end_race",
            PushServerDecisionMessage() ? "server_decision_message" :
                                          "server_decision_message_failed"
        );
        return;
    }

    if (s_endRaceFrames < MESSAGE_WAIT_FRAMES && !s_nativeTimeoutSet) {
        ++s_endRaceFrames;
        return;
    }

    Time& raceTime = raceManager->m_timer->m_time[0];
    if (!s_nativeTimeoutSet) {
        ApplyNativeTimeout(raceTime);
        s_nativeTimeoutSet = true;
        s_endRaceFrames    = 0;
        GPReport::Report("wl:mkw_end_race", "native_timeout");
        return;
    }

    // Something set the timer back: apply the timeout again, a few times.
    if (raceTime.m_minutes < 5 && s_timeoutApplies < NATIVE_TIMEOUT_MAX_APPLIES) {
        ApplyNativeTimeout(raceTime);
    }

    if (++s_endRaceFrames >= NATIVE_TIMEOUT_WAIT_FRAMES) {
        // The server closes the peer links when the race does not end.
        GPReport::Report("wl:mkw_end_race", "native_timeout_failed");
        ClearRequest();
    }
}

} // namespace wwfc::mkw::RaceControl

#endif // RMC
