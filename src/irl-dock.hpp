#pragma once

#include <QPointer>
#include <QWidget>

class SettingsDialog;

class QLabel;
class QPushButton;
class QTimer;

// Dock panel showing the live health-check state with manual controls.
class IrlDock : public QWidget {
	Q_OBJECT

public:
	explicit IrlDock(QWidget *parent = nullptr);

private Q_SLOTS:
	void refresh();
	void onTogglePause();
	void onForceOnline();
	void onForceOffline();
	void onOpenSettings();

private:
	QLabel *statusLabel = nullptr;
	QLabel *rttLabel = nullptr;
	QLabel *offlineLabel = nullptr;
	QLabel *statsLabel = nullptr;
	QLabel *errorLabel = nullptr;
	QPushButton *pauseButton = nullptr;
	QPushButton *onlineButton = nullptr;
	QPushButton *offlineButton = nullptr;
	QPushButton *settingsButton = nullptr;
	QTimer *timer = nullptr;
	QPointer<SettingsDialog> settingsDialog;
};
