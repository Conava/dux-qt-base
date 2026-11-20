#include "icontheme.hpp"

#include <qcoreapplication.h>
#include <qcoreevent.h>
#include <qdir.h>
#include <qfile.h>
#include <qicon.h>
#include <qobject.h>
#include <qsignalspy.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtenvironmentvariables.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qtestsupport_core.h>
#include <qthreadpool.h>
#include <qwindow.h>

#include "../iconimageprovider.hpp"
#include "../icontheme.hpp"

namespace {
bool writeFile(const QString& path, const QByteArray& data) {
	auto file = QFile(path);
	return file.open(QFile::WriteOnly) && file.write(data) == data.size();
}

void sendToApplication(QEvent::Type type) {
	auto event = QEvent(type);
	QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
}
} // namespace

QString TestIconTheme::folderOf(const QString& theme) const {
	return this->data.filePath("icons/" + theme + "/scalable/places/qs-test-icon.svg");
}

void TestIconTheme::initTestCase() {
	QVERIFY(this->data.isValid());

	for (const auto* theme: {"qs-test-a", "qs-test-b"}) {
		const auto dir = this->data.filePath(QString("icons/") + theme);
		QVERIFY(QDir().mkpath(dir + "/scalable/places"));
		QVERIFY(writeFile(dir + "/index.theme", "[Icon Theme]\nName=test\nDirectories=\n"));
		QVERIFY(writeFile(this->folderOf(theme), "<svg xmlns=\"http://www.w3.org/2000/svg\"/>"));
	}

	qputenv("XDG_DATA_HOME", this->data.path().toUtf8());
	qputenv("XDG_DATA_DIRS", this->data.filePath("system").toUtf8());
}

void TestIconTheme::parsesRequests_data() {
	QTest::addColumn<QString>("id");
	QTest::addColumn<QString>("icon");
	QTest::addColumn<QString>("path");
	QTest::addColumn<QString>("fallback");

	// clang-format off
	QTest::newRow("name") << "folder" << "folder" << "" << "";
	QTest::newRow("fallback") << "folder?fallback=inode-directory" << "folder" << "" << "inode-directory";
	QTest::newRow("path") << "tray?path=/opt/app/icons" << "tray" << "/opt/app/icons" << "";
	QTest::newRow("path with ampersand") << "tray?path=/opt/a&b/icons" << "tray" << "/opt/a&b/icons" << "";
	QTest::newRow("path with key-like ampersand") << "tray?path=/tmp/a&b=c" << "tray" << "/tmp/a&b=c" << "";
	QTest::newRow("fallback with key-like ampersand") << "x?fallback=/tmp/a&b=c%2B+.png" << "x" << "" << "/tmp/a&b=c%2B+.png";
	QTest::newRow("path takes the rest") << "tray?path=/opt/app?fallback=folder" << "tray" << "/opt/app?fallback=folder" << "";
	QTest::newRow("revision") << "folder?rev=3" << "folder" << "" << "";
	QTest::newRow("fallback then revision") << "folder?fallback=inode-directory&rev=12" << "folder" << "" << "inode-directory";
	QTest::newRow("path then revision") << "tray?path=/opt/a&b/icons&rev=3" << "tray" << "/opt/a&b/icons" << "";
	QTest::newRow("revision not last") << "folder?rev=3&fallback=x" << "folder?rev=3&fallback=x" << "" << "";
	QTest::newRow("fragment after revision") << "folder?rev=3#frag" << "folder?rev=3#frag" << "" << "";
	QTest::newRow("empty revision") << "folder?rev=" << "folder?rev=" << "" << "";
	QTest::newRow("unknown key") << "folder?size=48" << "folder?size=48" << "" << "";
	QTest::newRow("question mark in file name") << "/tmp/what?.png" << "/tmp/what?.png" << "" << "";
	QTest::newRow("key-like file name") << "/tmp/a?b=c.png" << "/tmp/a?b=c.png" << "" << "";
	QTest::newRow("question mark then revision") << "/tmp/a?b.png?rev=3" << "/tmp/a?b.png" << "" << "";
	QTest::newRow("question mark then fallback") << "/tmp/a?b.png?fallback=missing&rev=3" << "/tmp/a?b.png" << "" << "missing";
	// clang-format on
}

void TestIconTheme::parsesRequests() {
	QFETCH(const QString, id);
	QFETCH(const QString, icon);
	QFETCH(const QString, path);
	QFETCH(const QString, fallback);

	const auto request = IconImageProvider::parseRequest(id);
	QCOMPARE(request.icon, icon);
	QCOMPARE(request.path, path);
	QCOMPARE(request.fallback, fallback);
}

void TestIconTheme::buildsRequests() {
	QCOMPARE(IconImageProvider::requestString("folder"), "image://icon/folder");
	QCOMPARE(IconImageProvider::requestString("tray", "/opt/app"), "image://icon/tray?path=/opt/app");
	QCOMPARE(IconImageProvider::requestString("a", "", "b"), "image://icon/a?fallback=b");
}

void TestIconTheme::keepsEmptyPath() {
	QVERIFY(IconImageProvider::parseRequest("tray?path=").hasPath);
	QVERIFY(IconImageProvider::parseRequest("tray?path=&rev=3").hasPath);
	QVERIFY(!IconImageProvider::parseRequest("tray").hasPath);
	QVERIFY(!IconImageProvider::parseRequest("tray?fallback=folder").hasPath);
}

// setThemeName() stands in for the platform theme; the index learns of it only from the event,
// and announces it from the event loop.
void TestIconTheme::followsSystemTheme() {
	auto* index = IconThemeIndex::instance();
	auto themeChanges = QSignalSpy(index, &IconThemeIndex::themeChanged);
	auto revisionChanges = QSignalSpy(index, &IconThemeIndex::themeRevisionChanged);
	const auto revision = index->themeRevision();
	const auto started = index->started;
	const auto scans = index->revision();
	const auto first = QString(QIcon::themeName() == "qs-test-a" ? "qs-test-b" : "qs-test-a");
	const auto second = QString(first == "qs-test-a" ? "qs-test-b" : "qs-test-a");

	QIcon::setThemeName(first);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 0);
	sendToApplication(QEvent::ThemeChange);
	QCOMPARE(themeChanges.count(), 0);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 1);
	QCOMPARE(revisionChanges.count(), 1);
	QCOMPARE(index->themeRevision(), revision + 1);

	sendToApplication(QEvent::ThemeChange);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 1);
	QCOMPARE(index->themeRevision(), revision + 1);

	// Qt 6.8 sends the palette event before it updates the icon theme name.
	sendToApplication(QEvent::ApplicationPaletteChange);
	QIcon::setThemeName(second);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 2);
	QCOMPARE(index->themeRevision(), revision + 2);

	// Two events in one turn of the event loop announce once.
	QIcon::setThemeName(first);
	sendToApplication(QEvent::ThemeChange);
	sendToApplication(QEvent::ApplicationPaletteChange);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 3);
	QCOMPARE(index->themeRevision(), revision + 3);

	// Following the platform theme alone starts no index.
	QCOMPARE(index->started, started);
	if (!started) QCOMPARE(index->revision(), scans);
}

// Qt 6.8 sends ThemeChange to the windows, not to the application. A window being shown
// stands in where the platform theme sends no event at all.
void TestIconTheme::followsWindowEvents() {
	auto* index = IconThemeIndex::instance();
	auto themeChanges = QSignalSpy(index, &IconThemeIndex::themeChanged);
	auto window = QWindow();
	auto object = QObject();

	// Qt forwards a palette event sent to the application; let an earlier one settle.
	QCoreApplication::processEvents();
	themeChanges.clear();

	QIcon::setThemeName(QIcon::themeName() == "qs-test-a" ? "qs-test-b" : "qs-test-a");
	auto shown = QEvent(QEvent::Show);
	QCoreApplication::sendEvent(&object, &shown);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 0);

	auto themed = QEvent(QEvent::ThemeChange);
	QCoreApplication::sendEvent(&window, &themed);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 1);

	QIcon::setThemeName(QIcon::themeName() == "qs-test-a" ? "qs-test-b" : "qs-test-a");
	QCoreApplication::sendEvent(&window, &shown);
	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 2);
}

// The announcement must not run while Qt delivers the event to the window.
void TestIconTheme::survivesHandlerDeletingWindow() {
	auto* index = IconThemeIndex::instance();
	auto* window = new QWindow();
	const auto context = QObject();
	const auto destroy = [&] {
		delete window;
		window = nullptr;
	};
	QCoreApplication::processEvents();
	QObject::connect(index, &IconThemeIndex::themeChanged, &context, destroy);

	QIcon::setThemeName(QIcon::themeName() == "qs-test-a" ? "qs-test-b" : "qs-test-a");
	auto themed = QEvent(QEvent::ThemeChange);
	QCoreApplication::sendEvent(window, &themed);
	QVERIFY(window != nullptr);

	auto palette = QEvent(QEvent::ApplicationPaletteChange);
	QCoreApplication::sendEvent(window, &palette);
	QVERIFY(window != nullptr);

	QCoreApplication::processEvents();
	QVERIFY(window == nullptr);
}

void TestIconTheme::reindexesStartedIndex() {
	auto* index = IconThemeIndex::instance();
	auto scans = QSignalSpy(index, &IconThemeIndex::revisionChanged);

	index->setTheme("qs-test-a");
	QTRY_COMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-a"));

	scans.clear();
	QIcon::setThemeName("qs-test-b");
	sendToApplication(QEvent::ThemeChange);
	QVERIFY(scans.wait());
	QCOMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));
}

// A handler that spins the event loop lets the scan of the replaced theme finish under it.
void TestIconTheme::keepsStaleScanFromHandlers() {
	auto* index = IconThemeIndex::instance();
	const auto context = QObject();

	index->setTheme("qs-test-b");
	QTRY_VERIFY(!index->scanInProgress);
	QTRY_COMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));

	auto published = QStringList();
	const auto record = [&] { published.append(index->file("qs-test-icon")); };
	QObject::connect(index, &IconThemeIndex::revisionChanged, &context, record);

	QIcon::setThemeName("qs-test-a");
	sendToApplication(QEvent::ThemeChange);
	QCoreApplication::processEvents();

	auto handled = false;
	const auto wait = [&] {
		if (handled) return;
		handled = true;
		// The scan may have finished, and been published, before this announcement.
		published.clear();
		QVERIFY(QTest::qWaitFor([&] { return !index->scanInProgress; }));
	};
	QObject::connect(index, &IconThemeIndex::themeChanged, &context, wait);

	QIcon::setThemeName("qs-test-b");
	sendToApplication(QEvent::ThemeChange);
	QCoreApplication::processEvents();

	QVERIFY(handled);
	QVERIFY(!published.contains(this->folderOf("qs-test-a")));
	QTRY_COMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));
}

// A scan can finish ahead of the follow that replaces its theme. The follow still wins.
void TestIconTheme::replacesScanFinishedBeforeFollow() {
	auto* index = IconThemeIndex::instance();

	index->setTheme("qs-test-b");
	QTRY_VERIFY(!index->scanInProgress);
	QTRY_COMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));

	QIcon::setThemeName("qs-test-a");
	sendToApplication(QEvent::ThemeChange);
	QCoreApplication::processEvents();
	QVERIFY(QThreadPool::globalInstance()->waitForDone());

	QIcon::setThemeName("qs-test-b");
	sendToApplication(QEvent::ThemeChange);
	QVERIFY(QTest::qWaitFor([&] { return !index->scanInProgress && !index->scanQueued; }));
	QCOMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));
}

// The index ends on the theme a handler set, not on the one being announced.
void TestIconTheme::keepsThemeSetByHandler() {
	auto* index = IconThemeIndex::instance();
	const auto context = QObject();

	index->setTheme("qs-test-b");
	QTRY_VERIFY(!index->scanInProgress);
	QTRY_COMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));

	auto handled = false;
	const auto setBack = [&] {
		if (handled) return;
		handled = true;
		index->setTheme("qs-test-b");
	};
	QObject::connect(index, &IconThemeIndex::themeChanged, &context, setBack);

	index->setTheme("qs-test-a");
	QCOMPARE(QIcon::themeName(), "qs-test-b");
	QVERIFY(QTest::qWaitFor([&] { return !index->scanInProgress && !index->scanQueued; }));
	QCOMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));
}

// A system change read before its announcement still counts when the theme is then set.
void TestIconTheme::announcesSetOverPendingChange() {
	auto* index = IconThemeIndex::instance();
	index->setTheme("qs-test-a");
	QCoreApplication::processEvents();
	auto themeChanges = QSignalSpy(index, &IconThemeIndex::themeChanged);

	QIcon::setThemeName("qs-test-b");
	sendToApplication(QEvent::ThemeChange);
	index->setTheme("qs-test-a");
	QCOMPARE(themeChanges.count(), 1);

	QIcon::setThemeName("qs-test-b");
	sendToApplication(QEvent::ThemeChange);
	index->setTheme("qs-test-b");
	QCOMPARE(themeChanges.count(), 2);

	QCoreApplication::processEvents();
	QCOMPARE(themeChanges.count(), 2);
}

void TestIconTheme::setsTheme() {
	auto* index = IconThemeIndex::instance();
	index->setTheme("qs-test-b");
	QTRY_COMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-b"));

	auto themeChanges = QSignalSpy(index, &IconThemeIndex::themeChanged);
	const auto revision = index->themeRevision();

	index->setTheme("qs-test-a");
	QCOMPARE(QIcon::themeName(), "qs-test-a");
	QCOMPARE(themeChanges.count(), 1);
	QCOMPARE(index->themeRevision(), revision + 1);
	QTRY_COMPARE(index->file("qs-test-icon"), this->folderOf("qs-test-a"));

	index->setTheme("qs-test-a");
	QCOMPARE(themeChanges.count(), 1);
	QCOMPARE(index->themeRevision(), revision + 1);
}

QTEST_MAIN(TestIconTheme);
