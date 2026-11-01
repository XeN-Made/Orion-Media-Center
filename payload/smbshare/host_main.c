// ORION Media Center - The SMB share server on a PC, to test it from Windows without a console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
//   smb-share-host <port> <user> <password> <name>=<folder> [<name>=<folder>...]
//
// Then, on Windows (11 24H2 or later, which can use another port):
//   net use \\<this machine's address>\<name> /user:<user> <password> /TCPPORT:<port>

#include "smb_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stopping = 0;

static void on_signal(int number)
{
    (void)number;
    stopping = 1;
}

static int keep_going(void *context)
{
    (void)context;
    return !stopping;
}

static void print_line(void *context, const char *line)
{
    (void)context;
    printf("%s\n", line);
    fflush(stdout);
}

int main(int count, char **arguments)
{
    if (count < 5)
    {
        fprintf(stderr, "usage: %s <port> <user> <password> <name>=<folder>...\n", arguments[0]);
        return 2;
    }
    smb_config_t config;
    memset(&config, 0, sizeof(config));
    config.port = atoi(arguments[1]);
    snprintf(config.computer, sizeof(config.computer), "PS5");
    smb_set_password(&config, arguments[2], arguments[3]);
    for (int i = 4; i < count; ++i)
    {
        char *equals = strchr(arguments[i], '=');
        if (equals == NULL)
            continue;
        *equals = 0;
        smb_add_share(&config, arguments[i], equals + 1);
    }
    config.keep_going = keep_going;
    config.log = print_line;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    const int error = smb_serve(&config);
    if (error != 0)
        fprintf(stderr, "could not listen on port %d (errno %d)\n", config.port, error);
    return error != 0;
}
