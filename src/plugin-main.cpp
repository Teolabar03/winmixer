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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QMainWindow>

#include "mixer-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static constexpr const char *DOCK_ID = "winmixer";
static WinMixerDock *dock = nullptr;

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("WinMixer.Description");
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	/* Rilascia le sessioni Core Audio e toglie il dock finche' il frontend e' ancora valido */
	if (event == OBS_FRONTEND_EVENT_EXIT && dock) {
		dock->shutdown();
		obs_frontend_remove_dock(DOCK_ID);
		dock = nullptr;
	}
}

bool obs_module_load(void)
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	dock = new WinMixerDock(main_window);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("WinMixer.Title"), dock)) {
		obs_log(LOG_ERROR, "impossibile aggiungere il dock '%s'", DOCK_ID);
		delete dock;
		dock = nullptr;
		return false;
	}

	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_log(LOG_INFO, "plugin caricato (versione %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	if (dock) {
		dock->shutdown();
		obs_frontend_remove_dock(DOCK_ID);
		dock = nullptr;
	}
	obs_log(LOG_INFO, "plugin scaricato");
}
