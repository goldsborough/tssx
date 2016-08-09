#define _GNU_SOURCE

#include <signal.h>

#include "tssx/common-poll-overrides.h"
#include "utility/utility.h"

bool _there_was_an_error(ready_count_t* ready_count) {
	return atomic_load(ready_count) == ERROR;
}


bool _poll_timeout_elapsed(size_t start, int timeout) {
	if (timeout == BLOCK_FOREVER) return false;
	if (timeout == DONT_BLOCK) return true;

	return (current_milliseconds() - start) > timeout;
}

int _install_poll_signal_handler(struct sigaction* old_action) {
	struct sigaction signal_action;

	// Set our function as the signal handling function
	signal_action.sa_handler = _poll_signal_handler;

	// !!!!!!!!!!!!!!!!!!!!!!!!!!!! IMPORTANT !!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// Do not set SA_RESTART! This is precisely the point of all of this!
	// By NOT (NOT NOT NOT) setting SA_RESTART, any interrupted syscall will
	// NOT (NOT NOT NOT) restart automatically. Rather, it will fail with exit
	// code  EINTR, which is precisely what we want.
	signal_action.sa_flags = 0;

	// Don't block any other signals during our exception handling
	sigemptyset(&signal_action.sa_mask);

	if (sigaction(POLL_SIGNAL, &signal_action, old_action) == ERROR) {
		print_error("Error setting signal handler for poll");
		return ERROR;
	}

	return SUCCESS;
}

int _restore_old_signal_action(struct sigaction* old_action) {
	if (sigaction(POLL_SIGNAL, old_action, NULL) == ERROR) {
		print_error("Error restoring old signal handler for poll");
		return ERROR;
	}

	return SUCCESS;
}

void _poll_signal_handler(int signal_number) {
	assert(signal_number == POLL_SIGNAL);
}

void _kill_other_thread(pthread_t other_thread) {
	// Send our POLL_SIGNAL to the thread doing real_poll()
	// This will terminate that thread, avoiding weird edge cases
	// where the other thread would block indefinitely if it detects
	// no changes (e.g. on a single fd), even if there were many events
	// on the TSSX buffer in the main thread. Note: signals are a actually a
	// process-wide concept. But because we installed a signal handler, what
	// we can do is have the signal handler be invoked in the *other_thread*
	// argument. If the disposition of the signal were a default (i.e. if we
	// had installed no signal handler) one, such as TERMINATE for SIGQUIT,
	// then that signal would be delivered to all threads, because all threads
	// run in the same process
	if (pthread_kill(other_thread, POLL_SIGNAL) != SUCCESS) {
		throw("Error killing other thread");
	}
}
