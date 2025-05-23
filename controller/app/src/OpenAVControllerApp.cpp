#include "OpenAVControllerApp.h"
#include "ZRCSDKSink.h"
extern std::string device ;
#include "sinks.h"

USING_NS_ZRCSDK

#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <thread>
#include <chrono>

#define BUFFER_MAX 1024

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#include <process.h>

char   g_nbstdinBuffer[2][BUFFER_MAX];
HANDLE g_input[2];
HANDLE g_process[2];

DWORD WINAPI consoleInput(LPVOID lpPara)
{
    while (true)
    {
        for (int i = 0; i < 2; i++)
        {
            fgets(g_nbstdinBuffer[i], BUFFER_MAX, stdin);
            SetEvent(g_input[i]);
            WaitForSingleObject(g_process[i], INFINITE);
        }
    }
    return 0;
}

void createNBStdin()
{
    DWORD tid;
    CreateThread(nullptr, 1024, &consoleInput, 0, 0, &tid);
    for (int i = 0; i < 2; i++)
    {
        g_input[i] = CreateEvent(nullptr, false, false, nullptr);
        g_process[i] = CreateEvent(nullptr, false, false, nullptr);
        g_nbstdinBuffer[i][0] = '\0';
    }
}

const char* getInputLine()
{
    DWORD n = WaitForMultipleObjects(2, g_input, false, 0);
    if (n == WAIT_OBJECT_0 || n == WAIT_OBJECT_0 + 1)
    {
        n = n - WAIT_OBJECT_0;
        SetEvent(g_process[n]);
        return g_nbstdinBuffer[n];
    }
    else
    {
        return nullptr;
    }
}

#endif


#if defined(__linux) || defined(__linux__) || defined(linux)

char            g_nbstdinBuffer[BUFFER_MAX];
char            g_input[BUFFER_MAX];
char            g_output[BUFFER_MAX];
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  empty = PTHREAD_COND_INITIALIZER;
bool            available = true;


bool input_available() {
    fd_set fds;
    struct timeval timeout = {0, 0};  // non-blocking
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &timeout) > 0;
}


void* consoleInput(void* param)
{
    while (true)
    {
        if (input_available()) {
            fgets(g_input, BUFFER_MAX, stdin);
            pthread_mutex_lock(&m);
            while (available == false) pthread_cond_wait(&empty, &m);
            memcpy(g_nbstdinBuffer, g_input, BUFFER_MAX); //checked safe
            memset(g_input, 0, BUFFER_MAX); //checked safe
            available = false;
            pthread_mutex_unlock(&m);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    return 0;
}

void createNBStdin()
{
    pthread_t tid;
    if (pthread_create(&tid, NULL, consoleInput, NULL) != 0)
    {
        printf("Unable to create consoleInput thread");
        exit(1);
    }
}

const char* getInputLine()
{
    pthread_mutex_lock(&m);
    memcpy(g_output, g_nbstdinBuffer, BUFFER_MAX); //checked safe
    memset(g_nbstdinBuffer, 0, BUFFER_MAX); //checked safe
    available = true;
    pthread_mutex_unlock(&m);
    pthread_cond_signal(&empty);
    return g_output;
}

#endif

std::string redact75(const std::string& input) {
    size_t len = input.size();
    size_t keep = len / 4; // keep first 25%
    std::string result = input;
    for (size_t i = keep; i < len; ++i) {
        result[i] = 'X';
    }
    return result;
}


void OpenAVControllerApp::InitServices()
{
    IZRCSDKSink* sdkSink = new CZRCSDkSink() ;
    IZRCSDK*     sdk = IZRCSDK::GetInstance() ;
    sdk->RegisterSink( sdkSink ) ;

    m_roomService = sdk->CreateZoomRoomsService() ;
    IZoomRoomsServiceSink* pRoomSink = new AutoIZoomRoomsServiceSink() ;
    m_roomService->RegisterSink( pRoomSink ) ;

    IMeetingService*     meetingService = m_roomService->GetMeetingService() ;
    IMeetingServiceSink* meetingServiceSink = new AutoIMeetingServiceSink() ;
    meetingService->RegisterSink( meetingServiceSink ) ;

    ISettingService*     settingService = m_roomService->GetSettingService() ;
    ISettingServiceSink* settingServiceSink = new AutoISettingServiceSink() ;
    settingService->RegisterSink( settingServiceSink ) ;

    IMeetingShareHelper*     meetingShareHelper = meetingService->GetMeetingShareHelper() ;
    IMeetingShareHelperSink* meetingShareHelperSink = new AutoIMeetingShareHelperSink() ;
    meetingShareHelper->RegisterSink( meetingShareHelperSink ) ;

    IMeetingAudioHelper*     meetingAudioHelper = meetingService->GetMeetingAudioHelper() ;
    IMeetingAudioHelperSink* meetingAudioHelperSink = new AutoIMeetingAudioHelperSink() ;
    meetingAudioHelper->RegisterSink( meetingAudioHelperSink ) ;

    IMeetingVideoHelper*     meetingVideoHelper = meetingService->GetMeetingVideoHelper() ;
    IMeetingVideoHelperSink* meetingVideoHelperSink = new AutoIMeetingVideoHelperSink() ;
    meetingVideoHelper->RegisterSink( meetingVideoHelperSink ) ;

    createNBStdin() ;
}

void OpenAVControllerApp::AppInit( std::string device )
{
    device = device ;
    InitServices() ;
}

void OpenAVControllerApp::HeartBeat()
{
    IZRCSDK* sdk = IZRCSDK::GetInstance();
    sdk->HeartBeat() ;
}

void OpenAVControllerApp::ReceiveCommand(std::string command)
{
    std::istringstream iss( command ) ;
    std::string        api;
    iss >> api ;

    if( api=="help" ) {
        std::cout << "available commands:" << std::endl ;
        std::cout << "  pair <activation code>" << std::endl ;
        std::cout << "  unpair" << std::endl ;
        std::cout << "  get_state" << std::endl ;
        std::cout << "  join_meeting <meeting id>" << std::endl ;
        std::cout << "  send_meeting_passcode <meeting password>" << std::endl ;
        std::cout << "  start_instant_meeting" << std::endl ;
        std::cout << "  launch_sharing_meeting" << std::endl ;
        std::cout << "  leave_meeting" << std::endl ;
        std::cout << "  end_meeting" << std::endl ;
        std::cout << "  mute" << std::endl ;
        std::cout << "  unmute" << std::endl ;
        std::cout << "  mute_video" << std::endl ;
        std::cout << "  unmute_video" << std::endl ;
        std::cout << "  get_camera_list" << std::endl ;
        std::cout << "  get_current_camera" << std::endl ;
        std::cout << "  share_camera" << std::endl ;
        std::cout << "  stop_sharing" << std::endl ;
    }
    else if( api=="get_state" ) {
        std::cout << get_state( true ) << std::endl ;
    }
    else if( api=="pair" ) {
        std::string activationCode ;
        iss >> activationCode ;
        std::cout << "> pair with activation code: " << redact75( activationCode ) << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }

        ZRCSDKError result = m_roomService->PairRoomWithActivationCode( activationCode ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="unpair" ) {
        std::cout << "> unpair" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }

        ZRCSDKError result = m_roomService->UnpairRoom() ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        } else {
            update_state( "paired", "false" ) ;
        }
    }
    else if( api=="join_meeting" ) {
        std::string meetingID ;
        iss >> meetingID ;
        std::cout << "> join_meeting with meetingID: " << meetingID << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->JoinMeeting( meetingID ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="send_meeting_passcode" ) {
        std::string password ;
        iss >> password ;
        std::cout << "> send_meeting_passcode with password: XXXX" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->SendMeetingPassword( password ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="start_instant_meeting" ) {
        std::cout << "> start_instant_meeting" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->StartInstantMeeting() ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="launch_sharing_meeting" ) {
        std::cout << "> launch_sharing_meeting" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService()->GetMeetingShareHelper() ) {
            std::cout << ">   error: no meeting share helper" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->GetMeetingShareHelper()->LaunchSharingMeeting( false, SharingInstructionDisplayStateWhiteboardCamera ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="leave_meeting" ) {
        std::cout << "> leave_meeting" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }

        if( get_state_datum("meeting/connection_stage")!="" ) {
            std::cout << ">   it looks like we were waiting for a passcode, cancelling that instead" << std::endl ;
            ZRCSDKError result = m_roomService->GetMeetingService()->CancelEnteringMeetingPassword() ;
            if( result!=ZRCSDKERR_SUCCESS ) {
                std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
            }
        } else {
            ZRCSDKError result = m_roomService->GetMeetingService()->ExitMeeting( ExitMeetingCmdLeave ) ;
            if( result!=ZRCSDKERR_SUCCESS ) {
                std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
            }
        }
    }
    else if( api=="end_meeting" ) {
        std::cout << "> end_meeting" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }

        if( get_state_datum("meeting/connection_stage")!="" ) {
            std::cout << ">   it looks like we were waiting for a passcode, cancelling that instead" << std::endl ;
            ZRCSDKError result = m_roomService->GetMeetingService()->CancelEnteringMeetingPassword() ;
            if( result!=ZRCSDKERR_SUCCESS ) {
                std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
            }
        } else {
            ZRCSDKError result = m_roomService->GetMeetingService()->ExitMeeting( ExitMeetingCmdEnd ) ;
            if( result!=ZRCSDKERR_SUCCESS ) {
                std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
            }
        }
    }
    else if( api=="mute") {
        std::cout << "> mute" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService()->GetMeetingAudioHelper() ) {
            std::cout << ">   error: no audio helper" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->GetMeetingAudioHelper()->UpdateMyAudioStatus( true ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="unmute") {
        std::cout << "> unmute" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService()->GetMeetingAudioHelper() ) {
            std::cout << ">   error: no audio helper" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->GetMeetingAudioHelper()->UpdateMyAudioStatus( false ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="mute_video") {
        std::cout << "> mute_video" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService()->GetMeetingVideoHelper() ) {
            std::cout << ">   error: no video helper" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->GetMeetingVideoHelper()->UpdateMyVideo( true ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="unmute_video") {
        std::cout << "> unmute_video" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService()->GetMeetingVideoHelper() ) {
            std::cout << ">   error: no video helper" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->GetMeetingVideoHelper()->UpdateMyVideo( false ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="get_camera_list" ) {
        std::cout << "> get_camera_list" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetSettingService() ) {
            std::cout << ">   error: no setting service" ;
            return ;
        }

        std::vector<ZRCSDK::Device> cameras = {};
        m_roomService->GetSettingService()->GetCameraList(cameras) ;
        for (ZRCSDK::Device camera : cameras) {
            std::cout << camera.id << " / " ;
            std::cout << camera.name ;
            std::cout << std::endl ;
        }
    }
    else if( api=="get_current_camera" ) {
        std::cout << "> get_current_camera" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetSettingService() ) {
            std::cout << ">   error: no setting service" ;
            return ;
        }

        ZRCSDK::Device camera ;
        m_roomService->GetSettingService()->GetCurrentCamera( camera ) ;
        if( camera.id!="" ) {
            std::cout << camera.id << " / " ;
            std::cout << camera.name ;
            std::cout << std::endl ;
        }
    }
    else if( api=="share_camera" ) {
        std::string camera_id ;
        iss >> camera_id ;
        std::cout << "> share_camera: " << camera_id << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService()->GetMeetingShareHelper() ) {
            std::cout << ">   error: no meeting share helper" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->GetMeetingShareHelper()->ShareCamera( true, camera_id ) ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api=="stop_sharing" ) {
        std::cout << "> stop_sharing" << std::endl ;

        if( !m_roomService ) {
            std::cout << ">   error: no room service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService() ) {
            std::cout << ">   error: no meeting service" ;
            return ;
        }
        if( !m_roomService->GetMeetingService()->GetMeetingShareHelper() ) {
            std::cout << ">   error: no meeting share helper" ;
            return ;
        }

        ZRCSDKError result = m_roomService->GetMeetingService()->GetMeetingShareHelper()->StopSharing() ;
        if( result!=ZRCSDKERR_SUCCESS ) {
            std::cout << ">   error: unable to send (ZRCSDKError=" << result << ")" << std::endl ;
        }
    }
    else if( api!="" ) {
        std::cout << "> error: unknown command: " << api << std::endl ;
    }
}
