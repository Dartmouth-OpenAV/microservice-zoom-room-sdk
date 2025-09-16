#pragma once

#include "include/IZRCSDK.h"

extern std::string global_device ;


USING_NS_ZRCSDK

class CZRCSDkSink : public IZRCSDKSink
{
public:
    std::string device = "default" ;

    // void set_device( std::string device ) {
    //     this->device = device ;
    // }

    virtual ~CZRCSDkSink() {}

    std::string OnGetDeviceManufacturer() { return "OpenAV"; }

    virtual std::string OnGetDeviceModel() { return "Linux"; }

    virtual std::string OnGetDeviceSerialNumber() { return "1234567890"; }

    virtual std::string OnGetDeviceMacAddress() { return "12:34:de:ad:be:ef"; }

    virtual std::string OnGetDeviceIP() { return "127.0.0.1"; }

    virtual std::string OnGetFirmwareVersion() { return "1.2"; }

    virtual std::string OnGetAppName() { return "OpenAV Zoom Room Controller"; }

    virtual std::string OnGetAppVersion() { return "1.1"; }

    virtual std::string OnGetAppDeveloper() { return "Dartmouth"; }

    virtual std::string OnGetAppContact() { return "openav@thayer.dartmouth.edu"; }

    virtual std::string OnGetAppContentDirPath()
    {
        // specify a path that ZRC SDK can read & write.
        return "/data/" + global_device ;
    }

    virtual bool OnPromptToInputUserNamePasswordForProxyServer(const std::string& proxyHost, uint32_t port, const std::string& description)
    {
        return true;
    }
};
