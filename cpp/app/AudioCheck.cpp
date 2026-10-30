// Windows side of the audio check: read-only questions to Core Audio, the registry and the device manager.
// Kept apart from LiveEngine.cpp so windows.h stays out of the JUCE code.
#include "AudioCheck.h"

#define NOMINMAX
#include <windows.h>

#include <initguid.h>

#include <propkeydef.h>

#include <cfgmgr32.h>
#include <devguid.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <setupapi.h>
#include <wrl/client.h>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "setupapi.lib")

using Microsoft::WRL::ComPtr;

static std::string utf8(const wchar_t* s) {
    if (!s || !*s) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    out.resize((size_t)n - 1);
    return out;
}

static bool consent_denied(HKEY root, const wchar_t* path) {
    wchar_t value[16] = {};
    DWORD size = sizeof value;
    return RegGetValueW(root, path, L"Value", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS && wcscmp(value, L"Deny") == 0;
}

static bool mic_blocked() {
    const wchar_t* store = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone";
    const std::wstring desktopApps = std::wstring(store) + L"\\NonPackaged";
    return consent_denied(HKEY_LOCAL_MACHINE, store) || consent_denied(HKEY_CURRENT_USER, store) ||
           consent_denied(HKEY_CURRENT_USER, desktopApps.c_str());
}

static void read_inputs(AudioSystem& sys) {
    // the UI thread may already be in COM with another model: then it is ready and is not ours to close
    const bool ownsCom = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDeviceCollection> list;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
            SUCCEEDED(devices->EnumAudioEndpoints(eCapture, DEVICE_STATEMASK_ALL, &list)) && SUCCEEDED(list->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device;
                ComPtr<IPropertyStore> props;
                DWORD state = 0;
                if (FAILED(list->Item(i, &device)) || FAILED(device->GetState(&state)) ||
                    FAILED(device->OpenPropertyStore(STGM_READ, &props)))
                    continue;
                AudioInput in;
                PROPVARIANT name;
                PropVariantInit(&name);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) in.name = utf8(name.pwszVal);
                PropVariantClear(&name);
                in.state = state == DEVICE_STATE_ACTIVE     ? AudioInput::ACTIVE
                           : state == DEVICE_STATE_DISABLED ? AudioInput::DISABLED
                           : state == DEVICE_STATE_UNPLUGGED ? AudioInput::UNPLUGGED
                                                             : AudioInput::NOT_PRESENT;
                ComPtr<IAudioEndpointVolume> volume;
                if (state == DEVICE_STATE_ACTIVE &&
                    SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &volume))) {
                    BOOL muted = FALSE;
                    if (SUCCEEDED(volume->GetMute(&muted))) in.muted = muted != FALSE;
                    volume->GetMasterVolumeLevelScalar(&in.volume);
                }
                sys.inputs.push_back(std::move(in));
            }
        }
    }
    if (ownsCom) CoUninitialize();
}

// Present, but Windows reports a problem: sound devices, plus USB ones (without a driver they have no class yet).
static void read_bad_drivers(AudioSystem& sys) {
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof dev;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); ++i) {
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, dev.DevInst, 0) != CR_SUCCESS || !(status & DN_HAS_PROBLEM)) continue;
        wchar_t id[64] = {}, name[256] = {};
        SetupDiGetDeviceInstanceIdW(set, &dev, id, 64, nullptr);   // cut short is fine: only the prefix matters
        const bool usbNoDriver = problem == CM_PROB_FAILED_INSTALL && wcsncmp(id, L"USB\\", 4) == 0;
        if (dev.ClassGuid != GUID_DEVCLASS_MEDIA && !usbNoDriver) continue;
        if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_FRIENDLYNAME, nullptr, (BYTE*)name, sizeof name, nullptr))
            SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_DEVICEDESC, nullptr, (BYTE*)name, sizeof name, nullptr);
        sys.bad_drivers.push_back(utf8(name));
    }
    SetupDiDestroyDeviceInfoList(set);
}

AudioSystem read_audio_system() {
    AudioSystem sys;
    sys.mic_blocked = mic_blocked();
    read_inputs(sys);
    read_bad_drivers(sys);
    return sys;
}
