#include "lock.hpp"

#include <qtmetamacros.h>
#include <wayland-ext-session-lock-v1-client-protocol.h>

#include "manager.hpp"

QSWaylandSessionLock::QSWaylandSessionLock(
    QSWaylandSessionLockManager* manager,
    ::ext_session_lock_v1* lock
)
    : manager(manager) {
	this->init(lock); // if isInitialized is false that means we already unlocked.
}

QSWaylandSessionLock::~QSWaylandSessionLock() {
	if (this->isInitialized()) {
		// This will intentionally lock the session if the lock is destroyed without calling unlock.
		this->destroy();
	}
}

void QSWaylandSessionLock::unlock() {
	if (!this->isInitialized() || this->deferredUnlock) return;

	this->release();
	this->secure = false;
	this->manager->active = nullptr;

	emit this->unlocked();
}

void QSWaylandSessionLock::release() {
	if (this->finished) {
		this->destroy();
		return;
	}

	if (this->secure) {
		this->unlock_and_destroy();
		return;
	}

	// unlock_and_destroy is a protocol error before the locked event, and destroy is one after it.
	this->deferredUnlock = true;
}

bool QSWaylandSessionLock::active() const { return this->isInitialized(); }

bool QSWaylandSessionLock::hasCompositorLock() const { return this->secure; }

bool QSWaylandSessionLock::unlockDeferred() const { return this->deferredUnlock; }

void QSWaylandSessionLock::ext_session_lock_v1_locked() {
	if (this->deferredUnlock) {
		this->unlock_and_destroy();
		this->deleteLater();
		return;
	}

	this->secure = true;
	emit this->compositorLocked();
}

void QSWaylandSessionLock::ext_session_lock_v1_finished() {
	if (this->deferredUnlock) {
		this->destroy();
		this->deleteLater();
		return;
	}

	this->secure = false;
	this->finished = true;
	this->unlock();
}
