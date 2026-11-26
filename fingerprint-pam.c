#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <security/pam_appl.h>
#include "fingerprint.h"

#define FP_SERVICE "lok-fingerprint"
#define FP_CONFIG "/etc/pam.d/" FP_SERVICE

/* No password prompts or user selection: only informational PAM messages. */
static int
conversation(int n, const struct pam_message **msg,
             struct pam_response **response, void *data)
{
	int i;
	(void)data;
	if (n <= 0 || n > PAM_MAX_NUM_MSG)
		return PAM_CONV_ERR;
	for (i = 0; i < n; i++)
		if (!msg[i] || (msg[i]->msg_style != PAM_TEXT_INFO &&
		               msg[i]->msg_style != PAM_ERROR_MSG))
			return PAM_CONV_ERR;
	*response = calloc(n, sizeof(**response));
	return *response ? PAM_SUCCESS : PAM_BUF_ERR;
}

int
fingerprint_authenticate(const char *username)
{
	struct stat st;
	pam_handle_t *pamh = NULL;
	struct pam_conv conv = { conversation, NULL };
	int rc, end;

	/* Never fall back to PAM's "other" service when our file is missing.
	 * The system administrator owns this policy, never the launching user. */
	if (lstat(FP_CONFIG, &st) || !S_ISREG(st.st_mode) ||
	    st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)))
		return 0;
	rc = pam_start(FP_SERVICE, username, &conv, &pamh);
	if (rc != PAM_SUCCESS)
		return 0;
	rc = pam_authenticate(pamh, PAM_DISALLOW_NULL_AUTHTOK);
	end = pam_end(pamh, rc);
	return rc == PAM_SUCCESS && end == PAM_SUCCESS;
}

#ifdef LOK_PAM_MAIN
int
main(int argc, char **argv)
{
	/* Exec resets dumpability. Restore the supervisor's protection. */
	if (argc != 2 || getuid() != 0 || prctl(PR_SET_DUMPABLE, 0) < 0)
		return 1;
	return fingerprint_authenticate(argv[1]) ? 0 : 1;
}
#endif
