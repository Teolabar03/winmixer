/*
obs-winmixer
Copyright (C) 2026 Matteo La Barbera <matteo.labarbera@digitalsupport-tech.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "audio-sessions.hpp"

#include <functiondiscoverykeys_devpkey.h>
#include <util/base.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

std::wstring fileDescription(const std::wstring &path)
{
	DWORD handle = 0;
	DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
	if (!size)
		return {};

	std::vector<BYTE> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
		return {};

	struct LangCodePage {
		WORD language;
		WORD codePage;
	} *translations = nullptr;
	UINT bytes = 0;
	if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void **)&translations, &bytes) ||
	    bytes < sizeof(LangCodePage))
		return {};

	wchar_t key[64];
	swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].language,
		   translations[0].codePage);

	wchar_t *value = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.data(), key, (void **)&value, &length) || length <= 1)
		return {};

	std::wstring description(value);
	while (!description.empty() && iswspace(description.back()))
		description.pop_back();
	return description;
}

std::wstring processName(DWORD pid)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process)
		return L"PID " + std::to_wstring(pid);

	wchar_t path[MAX_PATH * 2];
	DWORD length = static_cast<DWORD>(std::size(path));
	std::wstring name;
	if (QueryFullProcessImageNameW(process, 0, path, &length)) {
		name = fileDescription(path);
		if (name.empty()) {
			std::wstring full(path, length);
			size_t slash = full.find_last_of(L"\\/");
			name = slash == std::wstring::npos ? full : full.substr(slash + 1);
			size_t dot = name.find_last_of(L'.');
			if (dot != std::wstring::npos)
				name.resize(dot);
		}
	}
	CloseHandle(process);
	return name.empty() ? L"PID " + std::to_wstring(pid) : name;
}

} // namespace

/* Le sessioni di un processo si comandano insieme: si mostra il volume piu' alto
 * e il muto solo se tutte le sessioni sono mute */
float AppAudio::volume() const
{
	float highest = 0.0f;
	for (const auto &v : volumes) {
		float value = 0.0f;
		if (SUCCEEDED(v->GetMasterVolume(&value)))
			highest = (std::max)(highest, value);
	}
	return highest;
}

bool AppAudio::muted() const
{
	bool any = false;
	for (const auto &v : volumes) {
		BOOL value = FALSE;
		if (FAILED(v->GetMute(&value)))
			continue;
		if (!value)
			return false;
		any = true;
	}
	return any;
}

namespace {

int readChannelPeaks(IAudioMeterInformation *meter, float *out, int maxChannels)
{
	UINT count = 0;
	if (FAILED(meter->GetMeteringChannelCount(&count)) || count == 0)
		return 0;

	/* GetChannelsPeakValues vuole esattamente il numero di canali del flusso */
	std::vector<float> values(count);
	if (FAILED(meter->GetChannelsPeakValues(count, values.data())))
		return 0;

	int written = (std::min)(static_cast<int>(count), maxChannels);
	for (int i = 0; i < written; ++i)
		out[i] = values[i];
	return written;
}

} // namespace

int AppAudio::channelPeaks(float *out, int maxChannels) const
{
	int channels = 0;
	for (int i = 0; i < maxChannels; ++i)
		out[i] = 0.0f;

	for (const auto &m : meters) {
		float values[32] = {};
		int count = readChannelPeaks(m.Get(), values, (std::min)(maxChannels, 32));
		for (int i = 0; i < count; ++i)
			out[i] = (std::max)(out[i], values[i]);
		channels = (std::max)(channels, count);
	}
	return channels;
}

void AppAudio::setVolume(float value) const
{
	for (const auto &v : volumes)
		v->SetMasterVolume(value, nullptr);
}

void AppAudio::setMuted(bool value) const
{
	for (const auto &v : volumes)
		v->SetMute(value ? TRUE : FALSE, nullptr);
}

AudioSessions::AudioSessions()
{
	/* Il thread dell'interfaccia di OBS e' gia' inizializzato da Qt (STA):
	 * in quel caso CoInitializeEx restituisce RPC_E_CHANGED_MODE e va bene cosi'. */
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	comInitialized_ = SUCCEEDED(hr);

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
	if (FAILED(hr))
		obs_log(LOG_WARNING, "MMDeviceEnumerator non disponibile: 0x%08lX", hr);
}

AudioSessions::~AudioSessions()
{
	shutdown();
	if (comInitialized_)
		CoUninitialize();
}

void AudioSessions::shutdown()
{
	release();
	enumerator_.Reset();
}

void AudioSessions::release()
{
	apps_.clear();
	sessionManager_.Reset();
	endpointMeter_.Reset();
	endpointVolume_.Reset();
	device_.Reset();
	deviceId_.clear();
	deviceName_.clear();
}

bool AudioSessions::bindDefaultDevice()
{
	if (!enumerator_)
		return false;

	ComPtr<IMMDevice> device;
	if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
		release();
		return false;
	}

	LPWSTR id = nullptr;
	if (FAILED(device->GetId(&id)))
		return false;
	std::wstring newId(id);
	CoTaskMemFree(id);

	if (newId == deviceId_ && sessionManager_)
		return true;

	release();

	HRESULT hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpointVolume_);
	if (SUCCEEDED(hr))
		hr = device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, &endpointMeter_);
	if (SUCCEEDED(hr))
		hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &sessionManager_);
	if (FAILED(hr)) {
		obs_log(LOG_WARNING, "Impossibile attivare il dispositivo di uscita: 0x%08lX", hr);
		release();
		return false;
	}

	device_ = device;
	deviceId_ = newId;

	ComPtr<IPropertyStore> props;
	if (SUCCEEDED(device_->OpenPropertyStore(STGM_READ, &props))) {
		PROPVARIANT value;
		PropVariantInit(&value);
		if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
			deviceName_ = value.pwszVal;
		PropVariantClear(&value);
	}

	obs_log(LOG_INFO, "Dispositivo di uscita: %ls", deviceName_.c_str());
	return true;
}

bool AudioSessions::refresh()
{
	if (!bindDefaultDevice())
		return false;

	ComPtr<IAudioSessionEnumerator> sessions;
	if (FAILED(sessionManager_->GetSessionEnumerator(&sessions)))
		return false;

	int count = 0;
	sessions->GetCount(&count);

	std::map<DWORD, AppAudio> byPid;
	for (int i = 0; i < count; ++i) {
		ComPtr<IAudioSessionControl> control;
		if (FAILED(sessions->GetSession(i, &control)))
			continue;

		AudioSessionState state = AudioSessionStateInactive;
		if (SUCCEEDED(control->GetState(&state)) && state == AudioSessionStateExpired)
			continue;

		ComPtr<IAudioSessionControl2> control2;
		if (FAILED(control.As(&control2)))
			continue;

		DWORD pid = 0;
		control2->GetProcessId(&pid);
		bool system = control2->IsSystemSoundsSession() == S_OK;
		if (system)
			pid = 0;

		ComPtr<ISimpleAudioVolume> volume;
		ComPtr<IAudioMeterInformation> meter;
		if (FAILED(control.As(&volume)))
			continue;
		control.As(&meter);

		AppAudio &app = byPid[pid];
		app.pid = pid;
		app.systemSounds = system;
		app.active = app.active || state == AudioSessionStateActive;
		app.volumes.push_back(volume);
		if (meter)
			app.meters.push_back(meter);
	}

	std::vector<AppAudio> result;
	result.reserve(byPid.size());
	for (auto &[pid, app] : byPid) {
		if (!app.systemSounds)
			app.name = processName(pid);
		result.push_back(std::move(app));
	}

	/* Suoni di sistema in fondo, il resto in ordine alfabetico */
	std::sort(result.begin(), result.end(), [](const AppAudio &a, const AppAudio &b) {
		if (a.systemSounds != b.systemSounds)
			return b.systemSounds;
		return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
	});

	apps_ = std::move(result);
	return true;
}

float AudioSessions::masterVolumeDb() const
{
	float value = 0.0f;
	if (endpointVolume_)
		endpointVolume_->GetMasterVolumeLevel(&value);
	return value;
}

bool AudioSessions::masterMuted() const
{
	BOOL value = FALSE;
	if (endpointVolume_)
		endpointVolume_->GetMute(&value);
	return value != FALSE;
}

int AudioSessions::masterChannelPeaks(float *out, int maxChannels) const
{
	return endpointMeter_ ? readChannelPeaks(endpointMeter_.Get(), out, maxChannels) : 0;
}

void AudioSessions::setMasterVolumeDb(float db) const
{
	if (!endpointVolume_)
		return;

	float minDb = -96.0f, maxDb = 0.0f, step = 0.0f;
	endpointVolume_->GetVolumeRange(&minDb, &maxDb, &step);

	/* Il driver non scende sotto il suo minimo: in fondo alla corsa si usa lo 0 del livello scalare */
	if (!std::isfinite(db) || db <= minDb)
		endpointVolume_->SetMasterVolumeLevelScalar(0.0f, nullptr);
	else
		endpointVolume_->SetMasterVolumeLevel((std::min)(db, maxDb), nullptr);
}

void AudioSessions::setMasterMuted(bool value) const
{
	if (endpointVolume_)
		endpointVolume_->SetMute(value ? TRUE : FALSE, nullptr);
}
