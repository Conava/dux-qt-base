#include "toolsupport.hpp"
#include <array>
#include <signal.h> // NOLINT(modernize-deprecated-headers)

#include <fcntl.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qscopeguard.h>
#include <qtemporarydir.h>
#include <qtenvironmentvariables.h>
#include <qtest.h>
#include <qtestcase.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../paths.hpp"
#include "../scan.hpp"
#include "../toolsupport.hpp"

namespace {
bool writeFile(const QString& path, const QByteArray& data) {
	auto file = QFile(path);
	return file.open(QFile::WriteOnly) && file.write(data) == data.size();
}
} // namespace

void TestToolSupport::initTestCase() {
	QVERIFY(this->runtime.isValid());
	qputenv("XDG_RUNTIME_DIR", this->runtime.path().toUtf8());
	QsPaths::init("tooling-test", "tooling-test", "", "", "");
	QVERIFY(QsPaths::instance()->shellVfsDir());
}

// A shell starting while the tooling command writes must wait for it, or it loads without the
// mirror and its disk cache. Runs first, as the lock is held for the rest of the process.
void TestToolSupport::waitsForOneShotWriter() {
	auto lockPath = QsPaths::instance()->shellVfsDir()->filePath("tooling.lock").toUtf8();
	auto fds = std::array<int, 2>();
	QVERIFY(pipe(fds.data()) == 0);
	auto [readEnd, writeEnd] = fds;

	auto child = fork();
	QVERIFY(child != -1);

	if (child == 0) {
		close(readEnd);
		auto fd = open(lockPath.constData(), O_WRONLY | O_CREAT, 0644); // NOLINT
		struct flock lock = {
		    .l_type = F_WRLCK,
		    .l_whence = SEEK_SET, // NOLINT
		    .l_start = 1,
		    .l_len = 0,
		    .l_pid = 0,
		};
		char locked = fd != -1 && fcntl(fd, F_SETLK, &lock) == 0; // NOLINT
		if (write(writeEnd, &locked, 1) != 1) _exit(1);
		usleep(500 * 1000);
		_exit(0);
	}

	// A child that dies before writing closes the last write end, so the read can't hang.
	close(writeEnd);
	auto reap = qScopeGuard([&] {
		kill(child, SIGKILL);
		waitpid(child, nullptr, 0);
		close(readEnd);
	});

	char locked = 0;
	QVERIFY(read(readEnd, &locked, 1) == 1);
	QVERIFY(locked);

	using qs::core::QmlToolingSupport;
	QCOMPARE(QmlToolingSupport::lockTooling(), QmlToolingSupport::LockResult::Acquired);
}

// A fresh clone has no .qmlls.ini, which a launched shell never creates.
void TestToolSupport::writeTooling() {
	auto config = QTemporaryDir();
	QVERIFY(config.isValid());

	auto root = QDir(config.path());
	QVERIFY(root.mkpath("Widgets"));
	QVERIFY(writeFile(root.filePath("shell.qml"), "import QtQuick\nItem {}\n"));
	QVERIFY(writeFile(root.filePath("Widgets/Bar.qml"), "import QtQuick\nItem {}\n"));

	auto scanner = QmlScanner(root);
	scanner.scanQmlRoot(root.filePath("shell.qml"));

	QVERIFY(qs::core::QmlToolingSupport::writeTooling(root, scanner));

	auto vfs = QDir(this->runtime.filePath("quickshell/vfs/tooling-test"));
	QCOMPARE(QFileInfo(root.filePath(".qmlls.ini")).symLinkTarget(), vfs.filePath(".qmlls.ini"));
	QVERIFY(QFileInfo(vfs.filePath(".qmlls.ini")).size() > 0);
	QVERIFY(QFileInfo::exists(vfs.filePath("qs/Widgets/Bar.qml")));
}

QTEST_GUILESS_MAIN(TestToolSupport);
