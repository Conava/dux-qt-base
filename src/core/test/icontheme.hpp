#pragma once

#include <qobject.h>
#include <qtemporarydir.h>
#include <qtmetamacros.h>

class TestIconTheme: public QObject {
	Q_OBJECT;

private slots:
	void initTestCase();
	static void parsesRequests_data(); // NOLINT(readability-identifier-naming)
	static void parsesRequests();
	static void buildsRequests();
	static void keepsEmptyPath();
	static void followsSystemTheme();
	static void followsWindowEvents();
	static void survivesHandlerDeletingWindow();
	void reindexesStartedIndex();
	void keepsStaleScanFromHandlers();
	void replacesScanFinishedBeforeFollow();
	void keepsThemeSetByHandler();
	static void announcesSetOverPendingChange();
	void setsTheme();

private:
	[[nodiscard]] QString folderOf(const QString& theme) const;

	QTemporaryDir data;
};
