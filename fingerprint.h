enum fingerprint_state { FP_WAITING, FP_READING, FP_FAILED };
/* Private, one-shot channel to the isolated PAM supervisor. */
struct fingerprint {
	int fd;
	pid_t pid;
	enum fingerprint_state state;
};
int fingerprint_prepare(struct fingerprint *fp, const char *username);
void fingerprint_start(struct fingerprint *fp, int show_feedback);
int fingerprint_result(struct fingerprint *fp);
void fingerprint_stop(struct fingerprint *fp);
