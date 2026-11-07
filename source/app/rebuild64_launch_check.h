#pragma once
class QApplication;
class MainWindow;
// Explicit isolated diagnostic only. Ordinary startup never installs hooks.
int initializeRebuild64LaunchCheck(int argc, char** argv);
void installRebuild64LaunchCheck(QApplication& app);
void attachRebuild64LaunchCheck(MainWindow& window);
void markRebuild64LaunchWindowReady();

class CSettings;
void applyRebuild64LaunchFixture(CSettings& settings);
