#ifndef OpenAVControllerApp_h
#define OpenAVControllerApp_h

#include <string>
#include "IZRCSDK.h"

const char* getInputLine();

class OpenAVControllerApp
{
public:
    void AppInit( std::string device );
    void HeartBeat();
    void ReceiveCommand(std::string command);

private:
    void InitServices();

private:
    NS_ZRCSDK::IZoomRoomsService* m_roomService = nullptr;
};

#endif    // !OpenAVControllerApp_h
