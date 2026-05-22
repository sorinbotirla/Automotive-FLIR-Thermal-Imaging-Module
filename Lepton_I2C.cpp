#include "Lepton_I2C.h"

#include "leptonSDKEmb32PUB/LEPTON_SDK.h"
#include "leptonSDKEmb32PUB/LEPTON_SYS.h"
#include "leptonSDKEmb32PUB/LEPTON_OEM.h"
#include "leptonSDKEmb32PUB/LEPTON_Types.h"

bool _connected;

LEP_CAMERA_PORT_DESC_T _port;

int lepton_connect() {
    LEP_OpenPort(1, LEP_CCI_TWI, 400, &_port);
    _connected = true;
    return 0;
}

void lepton_perform_ffc() {
    if (!_connected) {
        lepton_connect();
    }
    LEP_RunSysFFCNormalization(&_port);
}

bool lepton_set_ffc_manual() {
    if (!_connected) {
        lepton_connect();
    }

    LEP_SYS_FFC_SHUTTER_MODE_OBJ_T obj;
    LEP_RESULT r;

    r = LEP_GetSysFfcShutterModeObj(&_port, &obj);
    if (r != LEP_OK) return false;

    obj.shutterMode = LEP_SYS_FFC_SHUTTER_MODE_MANUAL;
    obj.ffcDesired = LEP_SYS_DISABLE;
    obj.videoFreezeDuringFFC = LEP_SYS_DISABLE;
    obj.desiredFfcPeriod = 0xFFFFFFFF;
    obj.desiredFfcTempDelta = 0xFFFF;
    obj.imminentDelay = 0xFFFF;
    obj.explicitCmdToOpen = LEP_TRUE;

    r = LEP_SetSysFfcShutterModeObj(&_port, obj);
    return (r == LEP_OK);
}

bool lepton_set_ffc_auto() {
    if (!_connected) {
        lepton_connect();
    }

    LEP_SYS_FFC_SHUTTER_MODE_OBJ_T obj;
    LEP_RESULT r;

    r = LEP_GetSysFfcShutterModeObj(&_port, &obj);
    if (r != LEP_OK) return false;

    obj.shutterMode = LEP_SYS_FFC_SHUTTER_MODE_AUTO;
    obj.ffcDesired = LEP_SYS_DISABLE;
    obj.videoFreezeDuringFFC = LEP_SYS_ENABLE;
    obj.desiredFfcPeriod = 180000;
    obj.desiredFfcTempDelta = 150;
    obj.imminentDelay = 52;
    obj.explicitCmdToOpen = LEP_TRUE;

    r = LEP_SetSysFfcShutterModeObj(&_port, obj);
    return (r == LEP_OK);
}

void lepton_reboot() {
    if (!_connected) {
        lepton_connect();
    }
    LEP_RunOemReboot(&_port);
}