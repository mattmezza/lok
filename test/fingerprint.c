/* Exercise real fork/socket/timeout/reaping logic with a fake PAM backend.
 * No production environment variables or authentication bypasses are added. */
#define FP_TIMEOUT 1
#define FP_RETRY 1
#define lstat fake_lstat
#include "../fingerprint.c"
#undef lstat
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SUCCESS, FAILURE, HANG, END_FAILURE, PROMPT, MISSING, BAD_CONFIG };
static int mode, reports[2];
static const struct pam_conv *saved_conv;

int
fake_lstat(const char *path, struct stat *st)
{
	assert(!strcmp(path, "/etc/pam.d/lok-fingerprint"));
	if (mode == MISSING)
		return -1;
	memset(st, 0, sizeof(*st));
	st->st_mode = S_IFREG | (mode == BAD_CONFIG ? 0666 : 0644);
	return 0;
}

int
pam_start(const char *service, const char *username,
          const struct pam_conv *conv, pam_handle_t **pamh)
{
	pid_t pid = getpid();
	assert(!strcmp(service, "lok-fingerprint"));
	assert(!strcmp(username, "invoking-user"));
	assert(write(reports[1], &pid, sizeof(pid)) == sizeof(pid));
	saved_conv = conv;
	*pamh = (pam_handle_t *)conv;
	return PAM_SUCCESS;
}

int
pam_authenticate(pam_handle_t *pamh, int flags)
{
	struct pam_message msg = { PAM_PROMPT_ECHO_OFF, "Password:" };
	const struct pam_message *msgs[] = { &msg };
	struct pam_response *response = NULL;
	(void)pamh;
	assert(flags == PAM_DISALLOW_NULL_AUTHTOK);
	if (mode == HANG)
		for (;;) pause();
	if (mode == PROMPT)
		return saved_conv->conv(1, msgs, &response, NULL);
	return mode == FAILURE ? PAM_AUTH_ERR : PAM_SUCCESS;
}

int
pam_end(pam_handle_t *pamh, int status)
{
	(void)pamh; (void)status;
	return mode == END_FAILURE ? PAM_SYSTEM_ERR : PAM_SUCCESS;
}

static void
check(int scenario)
{
	struct fingerprint fp;
	struct pollfd pfd;
	pid_t children[16], supervisor;
	int count = 0, success = 0, i;
	time_t until;
	mode = scenario;
	assert(pipe2(reports, O_CLOEXEC | O_NONBLOCK) == 0);
	assert(fingerprint_prepare(&fp, "invoking-user"));
	supervisor = fp.pid;
	pfd = (struct pollfd){ reports[0], POLLIN, 0 };
	assert(poll(&pfd, 1, 200) == 0); /* no scans before locking */
	fingerprint_start(&fp);
	until = now() + (scenario == HANG || scenario == FAILURE ? 4 : 2);
	while (now() < until) {
		pid_t pid;
		while (read(reports[0], &pid, sizeof(pid)) == sizeof(pid)) {
			assert(count < 16);
			children[count++] = pid;
		}
		if (fingerprint_result(&fp)) {
			success = 1;
			break;
		}
		poll(NULL, 0, 20);
	}
	assert(success == (scenario == SUCCESS));
	if (scenario == HANG || scenario == FAILURE)
		assert(count >= 2); /* timeout/failure retries */
	if (scenario == MISSING || scenario == BAD_CONFIG)
		assert(count == 0); /* no fallback to PAM other */
	fingerprint_stop(&fp);
	assert(fp.fd == -1 && fp.pid == -1);
	assert(waitpid(supervisor, NULL, WNOHANG) == -1 && errno == ECHILD);
	for (i = 0; i < count; i++)
		assert(kill(children[i], 0) == -1 && errno == ESRCH);
	fingerprint_stop(&fp); /* idempotent */
	close(reports[0]);
	close(reports[1]);
}

static void
check_bad_message(void)
{
	struct fingerprint fp;
	char forged = 'Y';
	assert(fingerprint_prepare(&fp, "invoking-user"));
	assert(send(fp.fd, &forged, 1, MSG_NOSIGNAL) == 1);
	poll(NULL, 0, 200);
	assert(!fingerprint_result(&fp));
	fingerprint_stop(&fp);
}

static void
check_parent_exit(void)
{
	pid_t parent, worker, attempt;
	int channel[2], status;
	struct fingerprint fp;
	mode = HANG;
	assert(pipe(channel) == 0);
	assert(pipe(reports) == 0);
	parent = fork();
	assert(parent >= 0);
	if (parent == 0) {
		close(channel[0]);
		assert(fingerprint_prepare(&fp, "invoking-user"));
		assert(write(channel[1], &fp.pid, sizeof(fp.pid)) == sizeof(fp.pid));
		fingerprint_start(&fp);
		for (;;) pause();
	}
	close(channel[1]);
	assert(read(channel[0], &worker, sizeof(worker)) == sizeof(worker));
	assert(read(reports[0], &attempt, sizeof(attempt)) == sizeof(attempt));
	kill(parent, SIGKILL);
	assert(waitpid(parent, &status, 0) == parent);
	/* Adopt orphan supervisor so cleanup can be checked without relying
	 * on the container's PID 1 to reap it. */
	assert(waitpid(worker, &status, 0) == worker);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	assert(kill(attempt, 0) == -1 && errno == ESRCH);
	close(channel[0]); close(reports[0]); close(reports[1]);
}

#include <sys/prctl.h>
int
main(void)
{
	int i;
	assert(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
	for (i = SUCCESS; i <= BAD_CONFIG; i++)
		check(i);
	check_bad_message();
	check_parent_exit();
	puts("PASS: fingerprint success, failure, policy, retries, timeout and cleanup");
	return 0;
}
