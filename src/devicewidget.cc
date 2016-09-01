/***
  This file is part of pavucontrol.

  Copyright 2006-2008 Lennart Poettering
  Copyright 2009 Colin Guthrie

  pavucontrol is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  pavucontrol is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with pavucontrol. If not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/ext-device-manager.h>

#include "mainwindow.h"
#include "devicewidget.h"
#include "channelwidget.h"

/*** DeviceWidget ***/
DeviceWidget::DeviceWidget(MainWindow* parent, Glib::ustring deviceType) :
    MinimalStreamWidget(parent),
    mpMainWindow(parent),
    mDeviceType(deviceType),
    offsetButtonEnabled(false) {

    setupUi(this);
    advancedWidget->hide();
    initPeakProgressBar(channelsVBox);

    timeout.setSingleShot(true);
    timeout.setInterval(100);
    connect(&timeout, &QTimer::timeout, this, &DeviceWidget::timeoutEvent);

    // FIXME: this->signal_button_press_event().connect(sigc::mem_fun(*this, &DeviceWidget::onContextTriggerEvent));
    connect(muteToggleButton, &QPushButton::toggled, this, &DeviceWidget::onMuteToggleButton);
    connect(lockToggleButton, &QPushButton::toggled, this, &DeviceWidget::onLockToggleButton);
    connect(defaultToggleButton, &QPushButton::toggled, this, &DeviceWidget::onDefaultToggleButton);

#if 0

    rename.set_label(tr("Rename Device...").toUtf8().constData());
    rename.signal_activate().connect(sigc::mem_fun(*this, &DeviceWidget::renamePopup));
    contextMenu.append(rename);
    contextMenu.show_all();
#endif

    connect(portList, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &DeviceWidget::onPortChange);
    connect(offsetButton, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &DeviceWidget::onOffsetChange);

    for (unsigned i = 0; i < PA_CHANNELS_MAX; i++)
        channelWidgets[i] = NULL;


    // FIXME:
//    offsetAdjustment = Gtk::Adjustment::create(0.0, -2000.0, 2000.0, 10.0, 50.0, 0.0);
//    offsetButton->configure(offsetAdjustment, 0, 2);
}

void DeviceWidget::setChannelMap(const pa_channel_map &m, bool can_decibel) {
    channelMap = m;

    for (int i = 0; i < m.channels; i++) {
        ChannelWidget *cw = channelWidgets[i] = new ChannelWidget(this);
        cw->channel = i;
        cw->can_decibel = can_decibel;
        cw->minimalStreamWidget = this;
        char text[64];
        snprintf(text, sizeof(text), "<b>%s</b>", pa_channel_position_to_pretty_string(m.map[i]));
        cw->channelLabel->setText(QString::fromUtf8(text));
        channelsVBox->addWidget(cw);
    }
    channelWidgets[m.channels-1]->last = true;

    lockToggleButton->setEnabled(m.channels > 1);
    hideLockedChannels(lockToggleButton->isChecked());
}

void DeviceWidget::setVolume(const pa_cvolume &v, bool force) {
    g_assert(v.channels == channelMap.channels);

    volume = v;

    if (!timeout.isActive() || force) { /* do not update the volume when a volume change is still in flux */
        for (int i = 0; i < volume.channels; i++)
            channelWidgets[i]->setVolume(volume.values[i]);
    }
}

void DeviceWidget::updateChannelVolume(int channel, pa_volume_t v) {
    pa_cvolume n;
    g_assert(channel < volume.channels);

    n = volume;
    if (lockToggleButton->isChecked())
        pa_cvolume_set(&n, n.channels, v);
    else
        n.values[channel] = v;

    setVolume(n, true);

    if (!timeout.isActive()) {
        timeout.start();
    }
}

void DeviceWidget::hideLockedChannels(bool hide) {
    for (int i = 0; i < channelMap.channels - 1; i++)
        channelWidgets[i]->setVisible(!hide);

    channelWidgets[channelMap.channels - 1]->channelLabel->setVisible(!hide);
}

void DeviceWidget::onMuteToggleButton() {

    lockToggleButton->setEnabled(!muteToggleButton->isChecked());

    for (int i = 0; i < channelMap.channels; i++)
        channelWidgets[i]->setEnabled(!muteToggleButton->isChecked());
}

void DeviceWidget::onLockToggleButton() {
    hideLockedChannels(lockToggleButton->isChecked());
}

void DeviceWidget::onDefaultToggleButton() {
}

void DeviceWidget::onOffsetChange() {
    pa_operation *o;
    int64_t offset;
    std::ostringstream card_stream;
    Glib::ustring card_name;

    if (!offsetButtonEnabled)
        return;

    offset = offsetButton->value() * 1000.0;
    card_stream << card_index;
    card_name = card_stream.str();

    if (!(o = pa_context_set_port_latency_offset(get_context(),
            card_name.c_str(), activePort.c_str(), offset, NULL, NULL))) {
        show_error(tr("pa_context_set_port_latency_offset() failed").toUtf8().constData());
        return;
    }
    pa_operation_unref(o);
}

void DeviceWidget::setDefault(bool isDefault) {
    defaultToggleButton->setChecked(isDefault);
    /*defaultToggleButton->setEnabled(!isDefault);*/
}

bool DeviceWidget::timeoutEvent() {
    executeVolumeUpdate();
    qDebug("TIMEOUT!");
    return false;
}

void DeviceWidget::executeVolumeUpdate() {
}

void DeviceWidget::setLatencyOffset(int64_t offset) {
    offsetButtonEnabled = false;
    offsetButton->setValue(offset / 1000.0);
    offsetButtonEnabled = true;
}

void DeviceWidget::setBaseVolume(pa_volume_t v) {

    for (int i = 0; i < channelMap.channels; i++)
        channelWidgets[i]->setBaseVolume(v);
}

void DeviceWidget::prepareMenu() {
    int idx = 0;
    int active_idx = -1;

    portList->clear();
    /* Fill the ComboBox's Model */
    for (uint32_t i = 0; i < ports.size(); ++i) {
        QByteArray name = ports[i].first.c_str();
        QString desc = ports[i].second.c_str();
        portList->addItem(desc, name);
        if (ports[i].first == activePort)
            active_idx = idx;
        idx++;
    }

    if (active_idx >= 0)
        portList->setCurrentIndex(active_idx);

    if (ports.size() > 0) {
        portSelect->show();

        if (pa_context_get_server_protocol_version(get_context()) >= 27) {
            offsetSelect->show();
            advancedOptions->setEnabled(true);
        } else {
            /* advancedOptions has sensitive=false by default */
            offsetSelect->hide();
        }

    } else {
        portSelect->hide();
        advancedOptions->setEnabled(false);
        offsetSelect->hide();
    }
}

#if 0
bool DeviceWidget::onContextTriggerEvent(GdkEventButton* event) {
    if (GDK_BUTTON_PRESS == event->type && 3 == event->button) {
        contextMenu.popup(event->button, event->time);
        return true;
    }

    return false;
}
#endif

void DeviceWidget::renamePopup() {
    if (updating)
        return;
#if 0
    if (!mpMainWindow->canRenameDevices) {
        Gtk::MessageDialog dialog(
            *mpMainWindow,
            tr("Sorry, but device renaming is not supported.").toUtf8().constData(),
            false,
            Gtk::MESSAGE_WARNING,
            Gtk::BUTTONS_OK,
            true);
        dialog.set_secondary_text(tr("You need to load module-device-manager in the PulseAudio server in order to rename devices").toUtf8().constData());
        dialog.run();
        return;
    }

    Gtk::Dialog* dialog;
    Gtk::Entry* renameText;

    Glib::RefPtr<Gtk::Builder> x = Gtk::Builder::create_from_file(GLADE_FILE, "renameDialog");
    x->get_widget("renameDialog", dialog);
    x->get_widget("renameText", renameText);

    renameText->set_text(description);
    dialog->add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    dialog->add_button(Gtk::Stock::OK, Gtk::RESPONSE_OK);
    dialog->set_default_response(Gtk::RESPONSE_OK);
    if (Gtk::RESPONSE_OK == dialog->run()) {
        pa_operation* o;
        gchar *key = g_markup_printf_escaped("%s:%s", mDeviceType.c_str(), name.c_str());

        if (!(o = pa_ext_device_manager_set_device_description(get_context(), key, renameText->get_text().c_str(), NULL, NULL))) {
            show_error(tr("pa_ext_device_manager_write() failed").toUtf8().constData());
            return;
        }
        pa_operation_unref(o);
        g_free(key);
    }
    delete dialog;
#endif
}
