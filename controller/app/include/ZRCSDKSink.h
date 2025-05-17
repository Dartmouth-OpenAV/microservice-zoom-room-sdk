#pragma once

#include "include/IZRCSDK.h"

USING_NS_ZRCSDK

class CZRCSDkSink : public IZRCSDKSink
{
public:
    virtual ~CZRCSDkSink() {}

    std::string OnGetDeviceManufacturer() { return "OpenAV"; }

    virtual std::string OnGetDeviceModel() { return "Linux"; }

    virtual std::string OnGetDeviceSerialNumber() { return ""; }

    virtual std::string OnGetDeviceMacAddress() { return ""; }

    virtual std::string OnGetDeviceIP() { return ""; }

    virtual std::string OnGetFirmwareVersion() { return "1.0"; }

    virtual std::string OnGetAppName() { return "OpenAV Zoom Room Controller"; }

    virtual std::string OnGetAppVersion() { return "1.0"; }

    virtual std::string OnGetAppDeveloper() { return "Dartmouth"; }

    virtual std::string OnGetAppContact() { return "openav@thayer.dartmouth.edu"; }

    virtual std::string OnGetAppContentDirPath()
    {
        // specify a path that ZRC SDK can read & write.
        return "";
    }

    virtual bool OnPromptToInputUserNamePasswordForProxyServer(const std::string& proxyHost, uint32_t port, const std::string& description)
    {
        return true;
    }
};
