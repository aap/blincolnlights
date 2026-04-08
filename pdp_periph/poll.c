struct pollfd pfds[100];
typedef struct {
	void (*handler)(void *arg);
	void *arg;
} PollHandler;
PollHandler pollHandlers[100];
int nPolls;

struct pollfd*
addPollHandler(void (*handler)(void*), void *arg)
{
	struct pollfd *pfd = &pfds[nPolls];
	PollHandler *ph = &pollHandlers[nPolls++];
	pfd->fd = -1;
	if(arg == nil) arg = pfd;
	ph->handler = handler;
	ph->arg = arg;
	return pfd;
}

typedef struct {
	const char *string;
	void *owner;
	void (*handle)(void *owner, char **args);
} CliCmd;
CliCmd cliCmds[100];
int nCliCmds;

void
addCliCmd(const char *string, void *owner, void (*handle)(void*,char**))
{
	CliCmd *cmd = &cliCmds[nCliCmds++];
	cmd->string = string;
	cmd->owner = owner;
	cmd->handle = handle;
}

void
cli(char *line)
{
	int n;
	char *p;

	if(p = strchr(line, '\r'), p) *p = '\0';
	if(p = strchr(line, '\n'), p) *p = '\0';

	char **args = split(line, &n);
	if(n > 0) for(int i = 0; i < nCliCmds; i++) {
		CliCmd *cmd = &cliCmds[i];
		if(strcmp(args[0], cmd->string) == 0)
			cmd->handle(cmd->owner, args);
	}
}

struct pollfd *networkcli;
void
handleAccept(void *arg)
{
printf("handleAccept\n");
	struct pollfd *pfd = arg;
	struct sockaddr_in client;
	socklen_t len = sizeof(client);
	networkcli->fd = accept(pfd->fd, (struct sockaddr*)&client, &len);
	if(networkcli->fd >= 0)
		networkcli->events = POLLIN;
}

void
handleCli(void *arg)
{
printf("handleCli\n");
	struct pollfd *pfd = arg;
	char line[1024];
        int n = read(pfd->fd, line, sizeof(line)-1);
	if(n <= 0) {
		close(pfd->fd);
		pfd->fd = -1;
		pfd->events = 0;
	} else {
		line[n] = '\0';
		cli(line);
	}
}

void*
pollthread(void *arg)
{
	struct pollfd *acceptpfd = addPollHandler(handleAccept, nil);
	struct pollfd *cli = addPollHandler(handleCli, nil);
	networkcli = addPollHandler(handleCli, nil);

	acceptpfd->fd = socketlisten(cliport);
	acceptpfd->events = POLLIN;
	cli->fd = clifd[0];
	cli->events = POLLIN;

	for(;;) {
		int ret = poll(pfds, nPolls, -1);
		if(ret < 0)
			exit(0);
		if(ret == 0)
			continue;

		for(int i = 0; i < nPolls; i++)
			if(pfds[i].revents & POLLIN)
				pollHandlers[i].handler(pollHandlers[i].arg);
	}
}

