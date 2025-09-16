#pragma once

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <sys/file.h>
#include <sqlite3.h>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "IZRCSDK.h"
#include "ZRCSDKDefines.h"
#include "ZRCSDKTypes.h"

#include "IZoomRoomsService.h"
#include "IMeetingService.h"
#include "ISettingService.h"
#include "IMeetingShareHelper.h"
#include "IMeetingAudioHelper.h"
#include "IMeetingVideoHelper.h"
#include "IMeetingListHelper.h"
#include "IParticipantHelper.h"


USING_NS_ZRCSDK
extern IZoomRoomsService* m_roomService ;

using json = nlohmann::json ;

// ChatGPT: convert MeetingItem vector to json array
json meetingListToJson(const std::vector<MeetingItem>& meetings) {
    json j = json::array();
    for (const auto& m : meetings) {
        j.push_back({
            {"meeting_id", m.meetingNumber},
            {"meeting_name", m.meetingName},
            {"start_time", m.startTime},
            {"end_time", m.endTime},
            {"is_private", m.isPrivate}
        });
    }
    return j;
}

// ChatGPT: split string by '.'
std::vector<std::string> splitKey(const std::string& key) {
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string segment;
    while (std::getline(ss, segment, '.')) {
        parts.push_back(segment);
    }
    return parts;
}


// ChatGPT
json paths_to_json(const std::string& input)
{
    json root;                       // final result
    std::istringstream ss(input);
    std::string line;

    auto trim = [](std::string& s) {
        s.erase(s.begin(),  std::find_if(s.begin(),  s.end(),
                 [](unsigned char c){ return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(),
                 [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
    };

    while (std::getline(ss, line))
    {
        trim(line);
        if (line.empty()) continue;

        auto colon = line.find(':');
        if (colon == std::string::npos)                 // malformed line
            throw std::runtime_error("Missing ':' in \"" + line + '"');

        std::string path  = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        trim(path);
        trim(value);

        // Try to interpret the value as an integer; fallback to string
        json leaf;
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        if (lower == "true")      { leaf = true;  }
        else if (lower == "false"){ leaf = false; }
        else {
            try          { leaf = std::stoi(value); }   // integer?
            catch (...)  { leaf = value; }              // fallback → string
        }

        // Walk / create the hierarchy
        std::istringstream pstream(path);
        std::string segment;
        json* cursor = &root;
        std::vector<std::string> segments;

        while (std::getline(pstream, segment, '/'))
            segments.push_back(segment);

        for (size_t i = 0; i < segments.size(); ++i)
        {
            const std::string& key = segments[i];
            if (i == segments.size() - 1)
            {
                (*cursor)[key] = leaf;      // final segment: assign value
            }
            else
            {
                cursor = &((*cursor)[key]); // dive deeper (creates objs as needed)
            }
        }
    }
    return root;
}


// ChatGPT
void check( int rc, sqlite3* db, const char* what ) {
    if( rc!=SQLITE_OK &&
        rc!=SQLITE_DONE &&
        rc!=SQLITE_ROW ) {
        std::cerr << "SQLite error (" << what << "): " << sqlite3_errmsg( db ) << std::endl ;
        sqlite3_close( db ) ;
    }
}


std::string get_state( bool as_json=true ) {
    std::cout << "+ get_state: " << std::endl ;

    sqlite3* db = nullptr ;
    int rc = sqlite3_open( "/dev/shm/microservice.db", &db ) ;
    check( rc, db, "open" ) ;

    const char* select_sql =
        "SELECT path,"
        "       datum FROM data WHERE device=:device" ;
    sqlite3_stmt* select = nullptr ;
    rc = sqlite3_prepare_v2( db, select_sql, -1, &select, nullptr ) ;
    check( rc, db, "select prepare" ) ;

    sqlite3_bind_text( select, sqlite3_bind_parameter_index(select, ":device"), global_device.c_str(), -1, SQLITE_TRANSIENT ) ;

    std::string to_return = "" ;
    while( (rc = sqlite3_step(select))==SQLITE_ROW ) {
        std::string path  = reinterpret_cast<const char*>( sqlite3_column_text(select, 0) ) ;
        std::string datum = reinterpret_cast<const char*>( sqlite3_column_text(select, 1) ) ;
        to_return += path + ": " + datum + "\n" ;
    }
    check(rc, db, "select loop done");
    sqlite3_finalize(select);
    sqlite3_close( db ) ;

    if( as_json ) {
        to_return = paths_to_json( to_return ).dump( 4 ) ;
    }

    return to_return ;
}


void update_state( const std::string& path, const std::string& datum ) {
    std::cout << "+ update_state: " << path << "=" << datum << std::endl ;

    sqlite3* db = nullptr ;
    int rc = sqlite3_open( "/dev/shm/microservice.db", &db ) ;
    check( rc, db, "open" ) ;

    const char* insert_or_update_sql =
        "INSERT INTO data (device,"
        "                  path,"
        "                  datum,"
        "                  no_refresh,"
        "                  last_refreshed_timestamp) VALUES (:device,"
        "                                                    :path,"
        "                                                    :datum,"
        "                                                    'true',"
        "                                                    CURRENT_TIMESTAMP)"
        " ON CONFLICT(device,"
        "             path) DO UPDATE SET datum=:datum,"
        "                                 last_refreshed_timestamp=CURRENT_TIMESTAMP;" ;
    sqlite3_stmt* insert_or_update = nullptr ;
    rc = sqlite3_prepare_v2( db, insert_or_update_sql, -1, &insert_or_update, nullptr ) ;
    check( rc, db, "insert or update prepare" ) ;

    sqlite3_bind_text( insert_or_update, sqlite3_bind_parameter_index(insert_or_update, ":device"), global_device.c_str(), -1, SQLITE_TRANSIENT ) ;
    sqlite3_bind_text( insert_or_update, sqlite3_bind_parameter_index(insert_or_update, ":path"), path.c_str(), -1, SQLITE_TRANSIENT ) ;
    sqlite3_bind_text( insert_or_update, sqlite3_bind_parameter_index(insert_or_update, ":datum"), datum.c_str(), -1, SQLITE_TRANSIENT ) ;

    rc = sqlite3_step( insert_or_update ) ;
    check( rc, db, "insert or update execute" ) ;
    sqlite3_finalize( insert_or_update ) ;
    sqlite3_close( db ) ;
}


void delete_state( const std::string& path ) {
    std::cout << "+ delete_state: " << path << std::endl ;

    sqlite3* db = nullptr ;
    int rc = sqlite3_open( "/dev/shm/microservice.db", &db ) ;
    check( rc, db, "open" ) ;

    const char* delete_sql =
        "DELETE FROM data WHERE path LIKE :path" ;
    sqlite3_stmt* delete_ = nullptr ;
    rc = sqlite3_prepare_v2( db, delete_sql, -1, &delete_, nullptr ) ;
    check( rc, db, "delete prepare" ) ;

    sqlite3_bind_text( delete_, sqlite3_bind_parameter_index(delete_, ":path"), path.c_str(), -1, SQLITE_TRANSIENT ) ;

    rc = sqlite3_step( delete_ ) ;
    check( rc, db, "delete execute" ) ;
    sqlite3_finalize( delete_ ) ;
    sqlite3_close( db ) ;
}


class AutoIZoomRoomsServiceSink : public IZoomRoomsServiceSink
{

    // implements IZoomRoomsServiceSink
    virtual void OnPairRoomResult(int32_t result) override {
        std::cout << "< OnPairRoomResult: " << result << std::endl ;

        if( result==0 ) {
            update_state( "paired", "true" ) ;
        } else {
            update_state( "paired", "false" ) ;
        }
    }

    virtual void OnRoomUnpairedReason(RoomUnpairedReason reason) override {
        std::cout << "< OnRoomUnpairedReason: " << reason << std::endl ;

        update_state( "paired", "false" ) ;
    }
};


class AutoISettingServiceSink : public ISettingServiceSink
{
    virtual void OnMicrophoneListChanged (const std::vector< Device > &microphones) override {}

    virtual void OnSpeakerListChanged (const std::vector< Device > &speakers) override {}

    virtual void OnCameraListChanged (const std::vector< Device > &cameras) override {}

    virtual void OnUpdateCOMList (const std::vector< Device > &comList) override {}

    virtual void OnCompanionZRDeviceUpdateNotification (const CompanionZRDeviceUpdateNot &noti) override {}

    virtual void OnCurrentMicrophoneChanged (bool exist, const Device &microphone) override {}

    virtual void OnCurrentSpeakerChanged (bool exist, const Device &speaker) override {}

    virtual void OnCurrentCameraChanged (bool exist, const Device &camera) override {}

    virtual void OnCurrentMicrophoneVolumeChanged (float volume) override {}

    virtual void OnCurrentSpeakerVolumeChanged (float volume) override {}

    virtual void OnUpdateHardwareStatus (const HardwareStatus &status) override {}

    virtual void OnUpdatedGenericSettings (const GenericSettings &genericSettings) override {}

    virtual void OnUpdateRoomProfileList (const RoomProfileList &list) override {}

    virtual void OnUpdateZoomRoomCapability (const RoomCapability &roomCapability) override {}

    virtual void OnCurrentSelectedMicrophoneMuted (bool muted) override {}

    virtual void OnMicrophoneTestingNotification (int32_t volume) override {}

    virtual void OnMicrophoneRecordingNotification (MicRecordTestStatus status) override {}

    virtual void OnSpeakerTestingNotification (int32_t volume, bool isEnabled) override {}

    virtual void OnSpeakerTestingResult (int32_t result, float duration, bool isStopped) override {}

    virtual void OnStatisticalInfoNotification (const StatisticalInfo &info) override {}

    virtual void OnAudioCheckupNotification (const AudioCheckupInfo &info) override {}

    virtual void OnAudioSystemFailureNotification (bool isDismiss) override {}

    virtual void OnScreenInfosNotification (const ScreenInfos &screenInfos) override {}

    virtual void OnAdjustScreensResponse (const AdjustScreensRes &response) override {}

    virtual void OnZoomPresenceScreenSaverNotification (bool running) override {}

    virtual void OnUpdatedOperationTimeStatusNotification (bool shouldDimScreen) override {}

    virtual void OnDirectorCalibrationNotification (const DirectorCalibrationNot &noti) override {}

    virtual void OnIntelligentDirectorInfoNotification (const IntelligentDirectorInfo &info) override {}

    virtual void OnCameraBoundaryConfigurationInfoNotification (const CameraBoundaryConfigurationInfo &info) override {}

    virtual void OnUpdateDiagnosticInfo (const DiagnosticInfo &info) override {}

    virtual void OnChangeWindowsPasswordNotification (int32_t result) override {}

    virtual void OnUpdateNetworkAudioDeviceList (const std::string &virtualDeviceID, NetworkAudioDeviceListAction action, const std::vector< NetworkAudioDevice > &networkAudioDeviceList, bool isUsedDanteController) override {}

    virtual void OnNetworkAdapterUpdateInfo (const std::vector< NetworkAdapterInfo > &networkAdapterInfos) override {}
};


class AutoIMeetingServiceSink : public IMeetingServiceSink
{


    // implements IMeetingServiceSink
    virtual void OnStartMeetingResult(int32_t result) override {}

    virtual void OnStartPmiResult(int32_t result, const std::string& meetingNumber, MeetingType meetingType) override {}

    virtual void OnStartPmiNotification(bool success) override {}

    virtual void OnUpdateMeetingStatus(MeetingStatus meetingStatus) override {
        std::cout << "< OnUpdateMeetingStatus: " << meetingStatus << std::endl ;
        if( meetingStatus==MeetingStatusNotInMeeting ) {
            update_state( "meeting/status", "not_in_meeting" ) ;
            delete_state( "meeting/info%" ) ;
            delete_state( "meeting/presence%" ) ;
        } else if( meetingStatus==MeetingStatusConnectingToMeeting ) {
            update_state( "meeting/status", "connecting" ) ;
        } else if( meetingStatus==MeetingStatusInMeeting ) {
            update_state( "meeting/status", "in_meeting" ) ;
        } else {
            update_state( "meeting/status", "unknown" ) ;
            delete_state( "meeting/info%" ) ;
            delete_state( "meeting/presence%" ) ;
        }
    }

    virtual void OnConfReadyNotification() override {}

    virtual void OnUpdateMeetingInfoNotification(const MeetingInfo& meetingInfo) override {
        std::cout << "< OnUpdateMeetingInfoNotification" << std::endl ;
        update_state( "meeting/info/meeting_id", meetingInfo.meetingID ) ;
        update_state( "meeting/info/meeting_number", meetingInfo.meetingNumber ) ;
        update_state( "meeting/info/meeting_name", meetingInfo.meetingName ) ;
        std::string meetingType = "unknown" ;
        if( meetingInfo.meetingType==MeetingTypeNone ) {
            meetingType = "none" ;
        } else if( meetingInfo.meetingType==MeetingTypeMeeting ) {
            meetingType = "meeting" ;
        } else if( meetingInfo.meetingType==MeetingTypeSharing  ) {
            meetingType = "sharing" ;
        } else if( meetingInfo.meetingType==MeetingTypePSTNCallout ) {
            meetingType = "PSTN callout" ;
        } else if( meetingInfo.meetingType==MeetingTypeIntegration ) {
            meetingType = "integration" ;
        }
        update_state( "meeting/info/meeting_type", meetingType ) ;
        update_state( "meeting/info/meeting_password", meetingInfo.meetingPassword ) ;
        update_state( "meeting/info/meeting_password_numeric", meetingInfo.numericPassword ) ;
        update_state( "meeting/info/meeting_url", meetingInfo.joinMeetingUrl ) ;
    }

    virtual void OnExitMeetingNotification(int32_t result, ExitMeetingReason reason) override {}

    virtual void OnMeetingErrorNotification(const MeetingErrorInfo& errorInfo) override {}

    virtual void OnMeetingEndedNotification(const MeetingErrorInfo& errorInfo) override {}

    virtual void OnReceiveMeetingInviteNotification(const MeetingInvitationInfo& invitation) override {}

    virtual void OnAnswerMeetingInviteResponse(int32_t result, const MeetingInvitationInfo& invitation, bool accepted) override {}

    virtual void OnTreatedMeetingInviteNotification(const MeetingInvitationInfo& invitation, bool accepted) override {}

    virtual void OnStartMeetingWithHostKeyResult(int32_t result) override {}

    virtual void OnUpdateDataCenterRegionNotification(const DataCenterRegion& dcRegion) override {}

    virtual void OnUpdateE2ESecurityCode(const E2ESecurityCode& code) override {}

    virtual void OnBandwidthLimitNotification(const BandwidthLimitInfo& info) override {}

    virtual void OnSendMeetingInviteEmailNotification(int32_t result) override {}

    virtual void OnSetRoomTempDisplayNameNotification(bool isShow) override
    {
        // std::cout << "OnSetRoomTempDisplayNameNotification, isShow:" << isShow << std::endl;
    }

    virtual void OnMeetingNeedsPasswordNotification(bool showPasswordDialog, bool wrongAndRetry, const ConfDeviceLockStatus& lockStatus) override {
        std::cout << "< OnMeetingNeedsPasswordNotification: " <<
            " showPasswordDialog=" << showPasswordDialog <<
            " wrongAndRetry=" << wrongAndRetry <<
            " locked=" << lockStatus.isLocked <<
        std::endl ;

        if( lockStatus.isLocked ) {
            update_state( "meeting/connection_stage", "locked" ) ;
        } else if( showPasswordDialog && wrongAndRetry ) {
            update_state( "meeting/connection_stage", "needs_passcode_wrong_and_retry" ) ;
        } else if( showPasswordDialog ) {
            update_state( "meeting/connection_stage", "needs_passcode" ) ;
        } else {
            delete_state( "meeting/connection_stage" ) ;
        }
    }

    virtual void OnConfDeviceLockStatusNotification(const ConfDeviceLockStatus& status) override {}

    virtual void OnJBHWaitingHostNotification(bool showWaitForHostDialog, WaitingHostReason reason) override {}

    virtual void OnE2eeMeetingStatusNotification(const E2eeMeetingStatus& e2eeMeetingStatus) override {}

    virtual void OnMeshInfoNotification(const MeshInfoNotification& meshInfo) override {}

    virtual void OnMeetingWillStopAutomatically() override {}

    virtual void OnExtendMeetingResult(int32_t extendMins) override {}
};


class AutoIMeetingAudioHelperSink : public IMeetingAudioHelperSink
{
    // implements IMeetingAudioHelperSink
    virtual void OnUpdateMyAudioStatus(const AudioStatus& audioStatus) override {
        std::cout << "< OnUpdateMyAudioStatus: " << audioStatus.isMuted << std::endl ;

        if( audioStatus.isMuted ) {
            update_state( "meeting/presence/microphone_muted", "true" ) ;
        } else {
            update_state( "meeting/presence/microphone_muted", "false" ) ;
        }
    }

    virtual void OnMuteUserAudioNotification(int32_t userID, const AudioStatus& audioStatus) override {}

    virtual void OnMuteOnEntryNotification(bool isMuteOnEntry) override {}

    virtual void OnAskUnmuteAudioByHostNotification(bool show, AskUnmuteAudioByHostType type) override {}

    virtual void OnAllowAttendeesUnmuteThemselvesNotification(bool canAttendeesUnmuteThemselves) override {}

    virtual void OnEnablePlayJoinOrLeaveChimeNotification(bool enable) override {}

    virtual void OnUpdateAudioTroubleShootingStatus(const AudioTroubleShootingStatus& status) override {}
};


class AutoIMeetingShareHelperSink : public IMeetingShareHelperSink
{
    virtual void OnStartLocalPresentNotification (const LocalPresentationInfo &info) override {}

    virtual void OnStartLocalPresentResult (bool isSharingMeeting, SharingInstructionDisplayState displayState) override {}

    virtual void OnSwitchToNormalMeetingResult (int result) override {}

    virtual void OnShowSharingInstructionResult (int result, bool show, SharingInstructionDisplayState instructionState) override {}

    virtual void OnShareSettingNotification (const ShareSetting &setting) override {}

    virtual void OnSharingStatusNotification (const SharingStatus &status) override {}

    virtual void OnUpdateAirPlayBlackMagicStatus (const AirplayBlackMagicStatus &status) override {
        std::cout << "< OnUpdateAirPlayBlackMagicStatus " << std::endl ;

        update_state( "sharing_key", "\"" + status.directPresentationSharingKey + "\"" ) ;
        update_state( "pairing_code", status.directPresentationPairingCode ) ;
    }

    virtual void OnUpdateCameraSharingStatus (const CameraSharingStatus &status) override {
        std::cout << "< OnUpdateCameraSharingStatus: " << status.deviceID << std::endl ;

        update_state( "meeting/presence/sharing/camera", status.deviceID ) ;
    }

    virtual void OnSharingSourceNotification (const std::vector< ShareSource > &zrShareSources, const std::vector< ShareSource > &zrwShareSources) override {}

    virtual void OnHDMI60FPSShareInfoNotification (bool isAllow, bool isOn, HDMI60FPSShareDisableReason disableReason) override {}

    virtual void OnLocalHDMIShareAudioPlaybackNotification (bool isEnabled) override {}

    virtual void OnUpdateWhiteboardShareStatusNotification (bool isSharing) override {}

    virtual void OnZRWSharingStatusNotification (const ZRWSharingStatus &status) override {}

    virtual void OnUpdateLocalViewStatus (bool isOn) override {}

    virtual void OnIncomingMeetingShareNotification (const IncomingMeetingShareNot &noti) override {}

    virtual void OnSlideControlNotification (const std::vector< SlideControlInfo > &slideControlInfos) override {}

    virtual void OnDocsShareSettingsNotification (const DocsShareSettingsInfo &info) override {}
};


class AutoIMeetingVideoHelperSink : public IMeetingVideoHelperSink
{
    virtual void OnUpdateMyVideoNotification (const VideoStatus &videoStatus) override {
        std::cout << "< OnUpdateMyVideoStatus: " <<
            " hasSource=" << videoStatus.hasSource <<
            " receiving=" << videoStatus.receiving <<
            " sending=" << videoStatus.sending <<
            " canControl=" << videoStatus.canControl <<
        std::endl ;

        if( videoStatus.sending ) {
            update_state( "meeting/presence/video_muted", "false" ) ;
        } else {
            update_state( "meeting/presence/video_muted", "true" ) ;
        }
    }

    virtual void OnMuteUserVideoNotification (int32_t userID, const VideoStatus &videoStatus) override {}

    virtual void OnAskStartVideoByHostNotification (int32_t userID) override {}

    virtual void OnUpdateScreenStatusForPinNotification (const std::vector< ScreenStatusForPin > &pinStatusList, PinShareWarningType warningType) override {}

    virtual void OnSpotlightStatusNotification (const SpotlightStatus &spotlightStatus) override {}

    virtual void OnUpdateAllowAttendeesStartVideo (bool allow) override {}
};

class AutoIMeetingListHelperSink : public IMeetingListHelperSink
{
    virtual void OnUpdateMeetingList (ListMeetingResult result, const std::vector< MeetingItem > &meetingList) override {
        std::cout << "< OnUpdateMeetingList" << std::endl ;
        if (result == ListMeetingResult::LIST_MEETING_SUCCESS ) {
            std::cout << meetingListToJson(meetingList).dump() << std::endl ;
        }
        update_state( "meeting_list", meetingListToJson(meetingList).dump() ) ;
    }

    virtual void OnUpdatedScheduleCalendarEventNotification (ScheduleCalendarEventResult scheduleResult) override {}

    virtual void OnUpdatedDeleteCalendarEventNotification (DeleteCalendarEventResult deleteResult) override {}

    virtual void OnShowUpcomingMeetingAlertResult (int32_t result, const MeetingItem &meetingItem) override {}

    virtual void OnCloseUpcomingMeetingAlertResult (int32_t result) override {}

    virtual void OnMeetingWillReleaseAutomatically (const MeetingItem &meetingItem) override {}

};


class AutoIParticipantHelperSink : public IParticipantHelperSink
{
    // virtual void OnInitMeetingParticipants (const std::vector< MeetingParticipant > &participants, int32_t totalParticipantsCount, bool needCleanUpUserList, ConfSessionType session) override {}
 
    // virtual void OnUserJoin (const std::vector< MeetingParticipant > &participants, ConfSessionType session) override {}
 
    // virtual void OnUserLeave (const std::vector< MeetingParticipant > &participants, ConfSessionType session) override {}
 
    // virtual void OnUserUpdate (const std::vector< MeetingParticipant > &participants, ConfSessionType session) override {}
 
    virtual void OnHostChangedNotification (int32_t hostUserID, bool amIHost) override {}
 
    virtual void OnMeetingParticipantsChanged (ConfSessionType session) override {
        std::cout << "< OnMeetingParticipantsChanged" << std::endl ;

        std::vector<MeetingParticipant> waiting;
        m_roomService->GetMeetingService()->GetParticipantHelper()->GetParticipantsInMeeting(waiting, session);
        for (ZRCSDK::MeetingParticipant meeting_participant : waiting) {
            std::cout << meeting_participant.userName << std::endl ;
        }
        // this works, but it seems problematic to create a new zoomroomsservice for this every time
        // IZRCSDK::GetInstance()->CreateZoomRoomsService( "1337" )->GetMeetingService()->GetParticipantHelper()->GetParticipantsInMeeting(waiting, session );   // session will indicate which list (e.g., waiting room)
        // // handleWaitingRoomUpdated(session, waiting);
        // for (ZRCSDK::MeetingParticipant meeting_participant : waiting) {
        //     count << meeting_participant.userName << std::endl ;
        // }
    }
 
    virtual void OnUpdateHideProfilePictures (bool isHideProfilePictures) override {}
 
    virtual void OnHideFullRoomViewNotification (const std::vector< int32_t > &userIDs) override {}
 
    virtual void OnClaimHostNotification (ClaimHostResult result) override {}
 
    virtual void OnUpdateSharingAnnotationInfo (bool support, bool enable) override {}
 
    virtual void OnAllowAttendeesRenameThemselvesNotification (bool allow) override {}
 
    virtual void OnAllowAttendeesShareWhiteboardsNotification (bool isSupported, bool isAllowed) override {}
 
    virtual void OnAllowRaiseHandForAttendeeNotification (bool canRaiseHandForAttendee) override {}
 
    virtual void OnUpdateOnZRWUserChangeNotification (ZRWUserChangeType type, int32_t zrwUserID) override {}
 
    virtual void OnUpdateHasRemoteControlAdmin (bool isAdminExist) override {}
 
    virtual void OnUpdateHasRemoteControlAssistant (bool isAssistantExist) override {}
 
    virtual void OnDownloadingFinished (const std::string &localFilePath, uint32_t result) override {}

};
