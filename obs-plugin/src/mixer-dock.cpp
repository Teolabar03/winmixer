/*
obs-winmixer
Copyright (C) 2026 Matteo La Barbera <matteo.labarbera@digitalsupport-tech.com>

Layout of VolumeControl and VolumeSlider adapted from OBS Studio
frontend/components/VolumeControl.cpp and AbsoluteSlider.cpp,
Copyright (C) OBS Studio contributors, GPL-2.0-or-later.

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

#include "mixer-dock.hpp"
#include "volume-meter.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace {

constexpr int METER_INTERVAL_MS = 16; // come il timer dei VolumeMeter di OBS
constexpr int SESSION_INTERVAL_MS = 1500;
constexpr double FADER_PRECISION = 4096.0;
constexpr int NAME_MAX_WIDTH = 280;
constexpr int NAME_MAX_WIDTH_VERTICAL = 90;

/* Curva logaritmica del fader di OBS (libobs/obs-audio-controls.c, OBS_FADER_LOG) */
constexpr float LOG_OFFSET_DB = 6.0f;
constexpr float LOG_RANGE_DB = 96.0f;
constexpr float LOG_OFFSET_VAL = -0.77815125038364363f;
constexpr float LOG_RANGE_VAL = -2.00860017176191756f;

float defToDb(float def)
{
	if (def >= 1.0f)
		return 0.0f;
	if (def <= 0.0f)
		return -std::numeric_limits<float>::infinity();
	return -(LOG_RANGE_DB + LOG_OFFSET_DB) * std::pow((LOG_RANGE_DB + LOG_OFFSET_DB) / LOG_OFFSET_DB, -def) +
	       LOG_OFFSET_DB;
}

float dbToDef(float db)
{
	if (db >= 0.0f)
		return 1.0f;
	if (db <= -96.0f)
		return 0.0f;
	return (-std::log10(-db + LOG_OFFSET_DB) - LOG_RANGE_VAL) / (LOG_OFFSET_VAL - LOG_RANGE_VAL);
}

float amplitudeToDb(float amplitude)
{
	return amplitude > 0.0f ? 20.0f * std::log10(amplitude) : -std::numeric_limits<float>::infinity();
}

float dbToAmplitude(float db)
{
	return std::isfinite(db) ? std::pow(10.0f, db / 20.0f) : 0.0f;
}

QString fromWide(const std::wstring &text)
{
	return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Classi di stile come idian::Utils di OBS: proprieta' "class" separata da spazi */
void setClass(QWidget *widget, const QString &name, bool enabled)
{
	QStringList classes = widget->property("class").toString().split(' ', Qt::SkipEmptyParts);
	if (classes.contains(name) == enabled)
		return;
	if (enabled)
		classes.append(name);
	else
		classes.removeAll(name);
	widget->setProperty("class", classes.join(' '));
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

} // namespace

/* ------------------------------------------------------------ VolumeSlider */

VolumeSlider::VolumeSlider(Qt::Orientation orientation, QWidget *parent) : QSlider(orientation, parent)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
}

int VolumeSlider::valueAt(QMouseEvent *event)
{
	QStyleOptionSlider opt;
	initStyleOption(&opt);
	const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
	const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

	int pos, sliderMin, sliderMax;
	if (orientation() == Qt::Horizontal) {
		pos = event->pos().x();
		sliderMin = groove.left() + handle.width() / 2;
		sliderMax = groove.right() - handle.width() / 2 + 1;
	} else {
		pos = event->pos().y();
		sliderMin = groove.top() + handle.height() / 2;
		sliderMax = groove.bottom() - handle.height() / 2 + 1;
	}
	return QStyle::sliderValueFromPosition(minimum(), maximum(), pos - sliderMin, sliderMax - sliderMin,
					       opt.upsideDown);
}

void VolumeSlider::mousePressEvent(QMouseEvent *event)
{
	dragging_ = event->buttons() & (Qt::LeftButton | Qt::MiddleButton);
	if (dragging_) {
		setSliderDown(true);
		setValue(valueAt(event));
	}
	event->accept();
}

void VolumeSlider::mouseMoveEvent(QMouseEvent *event)
{
	if (dragging_)
		setValue(valueAt(event));
	event->accept();
}

void VolumeSlider::mouseReleaseEvent(QMouseEvent *event)
{
	dragging_ = false;
	setSliderDown(false);
	event->accept();
}

void VolumeSlider::wheelEvent(QWheelEvent *event)
{
	if (!hasFocus()) {
		event->ignore();
		return;
	}
	QSlider::wheelEvent(event);
}

/* ----------------------------------------------------------- VolumeControl */

VolumeControl::VolumeControl(const QString &name, bool vertical, QWidget *parent) : QFrame(parent), vertical_(vertical)
{
	categoryLabel_ = new QLabel(this);
	categoryLabel_->setAlignment(Qt::AlignCenter);
	setClass(categoryLabel_, "mixer-category", true);
	setClass(categoryLabel_, "text-tiny", true);

	nameLabel_ = new QLabel(this);
	nameLabel_->setMaximumWidth(vertical_ ? NAME_MAX_WIDTH_VERTICAL : NAME_MAX_WIDTH);
	setClass(nameLabel_, "text-small", true);
	setClass(nameLabel_, "mixer-name", true);

	volumeLabel_ = new QLabel(this);
	volumeLabel_->setIndent(0);
	volumeLabel_->setObjectName("volLabel");

	muteButton_ = new QPushButton(this);
	muteButton_->setCheckable(true);
	muteButton_->setCursor(Qt::PointingHandCursor);
	setClass(muteButton_, "btn-mute", true);

	slider_ = new VolumeSlider(vertical_ ? Qt::Vertical : Qt::Horizontal, this);
	slider_->setRange(0, int(FADER_PRECISION));
	slider_->setPageStep(int(FADER_PRECISION / 20));

	meter_ = new VolumeMeter(vertical_, this);
	meter_->setFocusProxy(slider_);

	auto *meterFrame = new QFrame(this);
	meterFrame->setObjectName("volMeterFrame");

	auto *mainLayout = new QBoxLayout(QBoxLayout::TopToBottom, this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	if (vertical_) {
		/* Colonna: categoria, nome, dB, cursore + indicatore (si allungano), muto */
		setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

		auto *categoryLayout = new QHBoxLayout;
		auto *nameLayout = new QHBoxLayout;
		auto *volLayout = new QHBoxLayout;
		auto *meterLayout = new QHBoxLayout(meterFrame);
		auto *controlLayout = new QHBoxLayout;
		for (QBoxLayout *l : {static_cast<QBoxLayout *>(categoryLayout), static_cast<QBoxLayout *>(nameLayout),
				      static_cast<QBoxLayout *>(volLayout), static_cast<QBoxLayout *>(meterLayout),
				      static_cast<QBoxLayout *>(controlLayout)}) {
			l->setContentsMargins(0, 0, 0, 0);
			l->setSpacing(0);
			l->setAlignment(Qt::AlignCenter);
		}

		meter_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
		slider_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
		nameLabel_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
		nameLabel_->setAlignment(Qt::AlignCenter);
		categoryLabel_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
		volumeLabel_->setAlignment(Qt::AlignLeft);

		categoryLayout->addWidget(categoryLabel_);
		nameLayout->addWidget(nameLabel_);
		volLayout->addWidget(volumeLabel_);
		volLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Maximum));
		meterLayout->addWidget(slider_);
		meterLayout->addWidget(meter_);
		controlLayout->addWidget(muteButton_);

		mainLayout->addLayout(categoryLayout);
		mainLayout->addLayout(nameLayout);
		mainLayout->addLayout(volLayout);
		mainLayout->addWidget(meterFrame, 1);
		mainLayout->addLayout(controlLayout);
	} else {
		/* Riga: nome ... categoria dB / muto | cursore sopra all'indicatore (si allungano) */
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

		auto *textLayout = new QHBoxLayout;
		auto *controlLayout = new QHBoxLayout;
		auto *meterLayout = new QVBoxLayout(meterFrame);
		auto *buttonLayout = new QVBoxLayout;
		for (QBoxLayout *l : {static_cast<QBoxLayout *>(textLayout), static_cast<QBoxLayout *>(meterLayout),
				      static_cast<QBoxLayout *>(buttonLayout)}) {
			l->setContentsMargins(0, 0, 0, 0);
			l->setSpacing(0);
		}

		meter_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
		slider_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
		nameLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		categoryLabel_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
		volumeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

		textLayout->addWidget(nameLabel_);
		textLayout->addStretch();
		textLayout->addWidget(categoryLabel_);
		textLayout->addWidget(volumeLabel_);

		meterLayout->addWidget(slider_);
		meterLayout->addWidget(meter_);
		meterLayout->setStretch(0, 2);
		meterLayout->setStretch(1, 2);

		buttonLayout->addWidget(muteButton_);
		controlLayout->addLayout(buttonLayout);
		controlLayout->addWidget(meterFrame, 1);

		mainLayout->addLayout(textLayout, 3);
		mainLayout->addLayout(controlLayout, 6);
	}

	setName(name);
	showDb(0.0f);
	showMuted(false);

	connect(slider_, &QSlider::valueChanged, this, [this](int value) {
		const float db = defToDb(float(value) / float(FADER_PRECISION));
		showDb(db);
		emit volumeChanged(db);
	});
	connect(muteButton_, &QPushButton::toggled, this, [this](bool checked) {
		showMuted(checked);
		emit muteToggled(checked);
	});
}

void VolumeControl::setName(const QString &name)
{
	if (name == name_)
		return;
	name_ = name;
	nameLabel_->setText(nameLabel_->fontMetrics().elidedText(name, Qt::ElideRight, nameLabel_->maximumWidth()));
	nameLabel_->setToolTip(name);
	slider_->setAccessibleName(name);
}

void VolumeControl::setCategory(const QString &label, const QString &stateClass)
{
	categoryLabel_->setText(label);
	if (stateClass == stateClass_)
		return;
	if (!stateClass_.isEmpty())
		setClass(this, stateClass_, false);
	if (!stateClass.isEmpty())
		setClass(this, stateClass, true);
	stateClass_ = stateClass;
	meter_->setUseDisabledColors(stateClass == QStringLiteral("volume-inactive"));
}

void VolumeControl::setState(float db, bool muted)
{
	/* Non disturbare l'utente mentre trascina il cursore */
	if (!slider_->isSliderDown()) {
		QSignalBlocker block(slider_);
		slider_->setValue(int(std::lround(dbToDef(db) * FADER_PRECISION)));
		showDb(db);
	}
	if (muteButton_->isChecked() != muted) {
		QSignalBlocker block(muteButton_);
		muteButton_->setChecked(muted);
		showMuted(muted);
	}
}

void VolumeControl::setPeaks(const float *peaks, int channels)
{
	meter_->setPeaks(peaks, channels);
}

void VolumeControl::showDb(float db)
{
	if (!std::isfinite(db) || db < -96.0f)
		volumeLabel_->setText(QStringLiteral("-inf dB"));
	else
		volumeLabel_->setText(QString::number(db, 'f', 1) + QStringLiteral(" dB"));
}

void VolumeControl::showMuted(bool muted)
{
	setClass(muteButton_, "checked", muted);
	muteButton_->setToolTip(text(muted ? "WinMixer.Unmute" : "WinMixer.Mute"));
	meter_->setMuted(muted);

	/* Temi senza icona per .btn-mute: ripiega sul testo */
	if (muteButton_->icon().isNull())
		muteButton_->setText(text(muted ? "WinMixer.Unmute" : "WinMixer.Mute"));
	else
		muteButton_->setText(QString());
}

/* ------------------------------------------------------------ WinMixerDock */

WinMixerDock::WinMixerDock(QWidget *parent) : QWidget(parent)
{
	loadConfig();

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	auto *toolbar = new QHBoxLayout;
	toolbar->setContentsMargins(4, 4, 4, 4);
	device_ = new QLabel(this);
	device_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	device_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	/* Impostazioni in un menu dietro al pulsante con i tre puntini (icona del tema di OBS) */
	auto *menu = new QMenu(this);
	verticalAction_ = menu->addAction(text("WinMixer.VerticalLayout"));
	verticalAction_->setCheckable(true);
	verticalAction_->setChecked(vertical_);
	menu->addSeparator();
	QAction *refresh = menu->addAction(text("WinMixer.Refresh"));
	refresh->setToolTip(text("WinMixer.RefreshTooltip"));

	optionsButton_ = new QToolButton(this);
	optionsButton_->setMenu(menu);
	optionsButton_->setPopupMode(QToolButton::InstantPopup);
	optionsButton_->setToolTip(text("WinMixer.Options"));
	optionsButton_->setAccessibleName(text("WinMixer.Options"));
	optionsButton_->setCursor(Qt::PointingHandCursor);
	setClass(optionsButton_, "icon-dots-vert", true);
	if (optionsButton_->icon().isNull())
		optionsButton_->setText(text("WinMixer.Options"));

	toolbar->addWidget(device_, 1);
	toolbar->addWidget(optionsButton_);
	root->addLayout(toolbar);

	scroll_ = new QScrollArea(this);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	root->addWidget(scroll_, 1);

	connect(verticalAction_, &QAction::toggled, this, &WinMixerDock::setVertical);
	connect(refresh, &QAction::triggered, this, &WinMixerDock::refreshSessions);

	buildLayout();

	meterTimer_ = new QTimer(this);
	meterTimer_->setTimerType(Qt::PreciseTimer);
	connect(meterTimer_, &QTimer::timeout, this, &WinMixerDock::updateMeters);
	meterTimer_->start(METER_INTERVAL_MS);

	sessionTimer_ = new QTimer(this);
	connect(sessionTimer_, &QTimer::timeout, this, &WinMixerDock::refreshSessions);
	sessionTimer_->start(SESSION_INTERVAL_MS);

	refreshSessions();
}

WinMixerDock::~WinMixerDock()
{
	shutdown();
}

void WinMixerDock::shutdown()
{
	if (meterTimer_)
		meterTimer_->stop();
	if (sessionTimer_)
		sessionTimer_->stop();
	sessions_.shutdown();
}

void WinMixerDock::buildLayout()
{
	clearControls();

	/* Stessi nomi del mixer di OBS, cosi' valgono le regole del tema per #hVolumeWidgets / #vVolumeWidgets */
	scroll_->setObjectName(vertical_ ? "vMixerScrollArea" : "hMixerScrollArea");
	scroll_->setHorizontalScrollBarPolicy(vertical_ ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
	scroll_->setVerticalScrollBarPolicy(vertical_ ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);

	auto *container = new QWidget;
	container->setObjectName(vertical_ ? "vVolumeWidgets" : "hVolumeWidgets");
	controlsLayout_ = vertical_ ? static_cast<QBoxLayout *>(new QHBoxLayout(container))
				    : static_cast<QBoxLayout *>(new QVBoxLayout(container));
	controlsLayout_->setContentsMargins(0, 0, 0, 0);
	controlsLayout_->setSpacing(0);

	master_ = new VolumeControl(text("WinMixer.System"), vertical_, container);
	master_->setCategory(text("WinMixer.Output"), QStringLiteral("volume-pinned"));
	connect(master_, &VolumeControl::volumeChanged, this, [this](float db) { sessions_.setMasterVolumeDb(db); });
	connect(master_, &VolumeControl::muteToggled, this, [this](bool m) { sessions_.setMasterMuted(m); });
	controlsLayout_->addWidget(master_);

	empty_ = new QLabel(text("WinMixer.NoApps"), container);
	empty_->setWordWrap(true);
	empty_->setAlignment(Qt::AlignCenter);
	empty_->setMargin(8);
	if (vertical_)
		empty_->setMaximumWidth(180);
	controlsLayout_->addWidget(empty_);
	controlsLayout_->addStretch();

	/* setWidget elimina il contenitore precedente con tutte le sue voci */
	scroll_->setWidget(container);
}

void WinMixerDock::clearControls()
{
	/* Le voci appartengono al contenitore, che viene sostituito da buildLayout */
	controls_.clear();
	master_ = nullptr;
}

const AppAudio *WinMixerDock::findApp(DWORD pid) const
{
	for (const auto &app : sessions_.apps())
		if (app.pid == pid)
			return &app;
	return nullptr;
}

void WinMixerDock::refreshSessions()
{
	if (!sessions_.refresh()) {
		device_->setText(text("WinMixer.NoDevice"));
		qDeleteAll(controls_);
		controls_.clear();
		master_->setEnabled(false);
		empty_->setVisible(true);
		return;
	}

	master_->setEnabled(true);
	device_->setText(fromWide(sessions_.deviceName()));
	device_->setToolTip(device_->text());
	master_->setState(sessions_.masterVolumeDb(), sessions_.masterMuted());

	std::set<DWORD> alive;
	int index = 1; // dopo la voce "Sistema"

	for (const auto &app : sessions_.apps()) {
		alive.insert(app.pid);
		const QString name = app.systemSounds ? text("WinMixer.SystemSounds") : fromWide(app.name);

		VolumeControl *control = controls_.value(app.pid, nullptr);
		if (!control) {
			control = new VolumeControl(name, vertical_, controlsLayout_->parentWidget());
			const DWORD pid = app.pid;
			connect(control, &VolumeControl::volumeChanged, this, [this, pid](float db) {
				if (const AppAudio *a = findApp(pid))
					a->setVolume(dbToAmplitude(db));
			});
			connect(control, &VolumeControl::muteToggled, this, [this, pid](bool m) {
				if (const AppAudio *a = findApp(pid))
					a->setMuted(m);
			});
			controls_.insert(pid, control);
		} else {
			control->setName(name);
		}

		/* Mantiene l'ordine dell'elenco (alfabetico, suoni di sistema in fondo) */
		if (controlsLayout_->indexOf(control) != index) {
			controlsLayout_->removeWidget(control);
			controlsLayout_->insertWidget(index, control);
		}
		++index;

		control->setCategory(text(app.active ? "WinMixer.Active" : "WinMixer.Inactive"),
				     app.active ? QString() : QStringLiteral("volume-inactive"));
		control->setState(amplitudeToDb(app.volume()), app.muted());
	}

	for (auto it = controls_.begin(); it != controls_.end();) {
		if (alive.count(it.key())) {
			++it;
			continue;
		}
		delete it.value();
		it = controls_.erase(it);
	}

	empty_->setVisible(controls_.isEmpty());
}

void WinMixerDock::updateMeters()
{
	if (!isVisible() || !master_)
		return;

	float peaks[VolumeMeter::kMaxChannels];
	int channels = sessions_.masterChannelPeaks(peaks, VolumeMeter::kMaxChannels);
	master_->setPeaks(peaks, channels);

	for (const auto &app : sessions_.apps()) {
		if (VolumeControl *control = controls_.value(app.pid, nullptr)) {
			channels = app.channelPeaks(peaks, VolumeMeter::kMaxChannels);
			control->setPeaks(peaks, channels);
		}
	}
}

void WinMixerDock::setVertical(bool vertical)
{
	if (vertical == vertical_)
		return;
	vertical_ = vertical;
	saveConfig();
	buildLayout();
	refreshSessions();
}

void WinMixerDock::loadConfig()
{
	char *path = obs_module_config_path("config.json");
	if (!path)
		return;
	obs_data_t *data = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!data)
		return;
	vertical_ = strcmp(obs_data_get_string(data, "orientation"), "vertical") == 0;
	obs_data_release(data);
}

void WinMixerDock::saveConfig() const
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	char *path = obs_module_config_path("config.json");
	if (!path)
		return;
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "orientation", vertical_ ? "vertical" : "horizontal");
	obs_data_save_json_safe(data, path, "tmp", "bak");
	obs_data_release(data);
	bfree(path);
}
