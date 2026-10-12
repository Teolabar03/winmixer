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

#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <wrl/client.h>

#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

/* Tutte le sessioni audio di un processo, controllate come un'unica voce
 * (come fa il Mixer volume di Windows). pid 0 = suoni di sistema. */
struct AppAudio {
	DWORD pid = 0;
	std::wstring name;
	bool systemSounds = false;
	bool active = false;
	std::vector<ComPtr<ISimpleAudioVolume>> volumes;
	std::vector<ComPtr<IAudioMeterInformation>> meters;

	float volume() const;
	bool muted() const;
	/* Picco per canale (0..1), il massimo tra le sessioni del processo.
	 * Restituisce il numero di canali scritti in out. */
	int channelPeaks(float *out, int maxChannels) const;
	void setVolume(float value) const;
	void setMuted(bool value) const;
};

/* Accesso al dispositivo di uscita predefinito e alle sue sessioni.
 * Va usato sempre dallo stesso thread (quello dell'interfaccia di OBS). */
class AudioSessions {
public:
	AudioSessions();
	~AudioSessions();

	/* Rilegge il dispositivo predefinito (se cambiato) e l'elenco delle app.
	 * Restituisce false se non c'e' nessun dispositivo di uscita. */
	bool refresh();
	void release();
	/* Rilascia tutto, compreso l'enumeratore: dopo non si puo' piu' usare */
	void shutdown();

	const std::vector<AppAudio> &apps() const { return apps_; }
	const std::wstring &deviceName() const { return deviceName_; }

	/* Volume del dispositivo in dB (come lo mostra il driver) */
	float masterVolumeDb() const;
	void setMasterVolumeDb(float db) const;
	bool masterMuted() const;
	int masterChannelPeaks(float *out, int maxChannels) const;
	void setMasterMuted(bool value) const;

private:
	bool bindDefaultDevice();

	bool comInitialized_ = false;
	ComPtr<IMMDeviceEnumerator> enumerator_;
	ComPtr<IMMDevice> device_;
	std::wstring deviceId_;
	std::wstring deviceName_;
	ComPtr<IAudioEndpointVolume> endpointVolume_;
	ComPtr<IAudioMeterInformation> endpointMeter_;
	ComPtr<IAudioSessionManager2> sessionManager_;
	std::vector<AppAudio> apps_;
};
