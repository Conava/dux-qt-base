#pragma once

#include <qobject.h>
#include <qtemporarydir.h>
#include <qtmetamacros.h>

class TestToolSupport: public QObject {
	Q_OBJECT;

private slots:
	void initTestCase();
	static void waitsForOneShotWriter();
	void writeTooling();

private:
	QTemporaryDir runtime;
};
