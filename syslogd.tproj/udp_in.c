/*
 * Copyright (c) 2004-2008 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include "daemon.h"

#define forever for(;;)

#define UDP_SOCKET_NAME "NetworkListener"
#define MY_ID "udp_in"
#define MAXLINE 4096

#define MAXSOCK 16
static int nsock = 0;
static int ufd[MAXSOCK];

static char uline[MAXLINE + 1];

#define FMT_LEGACY 0
#define FMT_ASL 1

asl_msg_t *
udp_in_acceptmsg(int fd)
{
	socklen_t fromlen;
	ssize_t len;
	struct sockaddr_storage from;
	char fromstr[64], *r, *p;
	struct sockaddr_in *s4;
	struct sockaddr_in6 *s6;

	fromlen = sizeof(struct sockaddr_storage);
	memset(&from, 0, fromlen);

	len = recvfrom(fd, uline, MAXLINE, 0, (struct sockaddr *)&from, &fromlen);
	if (len <= 0) return NULL;

	fromstr[0] = '\0';
	r = NULL;

	if (from.ss_family == AF_INET)
	{
		s4 = (struct sockaddr_in *)&from;
		inet_ntop(from.ss_family, &(s4->sin_addr), fromstr, 64);
		r = fromstr;
		asldebug("%s: recvfrom %s len %d\n", MY_ID, fromstr, len);
	}
	else if (from.ss_family == AF_INET6)
	{
		s6 = (struct sockaddr_in6 *)&from;
		inet_ntop(from.ss_family, &(s6->sin6_addr), fromstr, 64);
		r = fromstr;
		asldebug("%s: recvfrom %s len %d\n", MY_ID, fromstr, len);
	}

	uline[len] = '\0';

	p = strrchr(uline, '\n');
	if (p != NULL) *p = '\0';

	return asl_input_parse(uline, len, r, 0);
}

int
udp_in_init(void)
{
	int i, rbufsize, len;
	launch_data_t sockets_dict, fd_array, fd_dict;

	asldebug("%s: init\n", MY_ID);
	if (nsock > 0) return 0;
	if (global.launch_dict == NULL)
	{
		asldebug("%s: laucnchd dict is NULL\n", MY_ID);
		return -1;
	}

	sockets_dict = launch_data_dict_lookup(global.launch_dict, LAUNCH_JOBKEY_SOCKETS);
	if (sockets_dict == NULL)
	{
		asldebug("%s: laucnchd lookup of LAUNCH_JOBKEY_SOCKETS failed\n", MY_ID);
		return -1;
	}

	fd_array = launch_data_dict_lookup(sockets_dict, UDP_SOCKET_NAME);
	if (fd_array == NULL)
	{
		asldebug("%s: laucnchd lookup of UDP_SOCKET_NAME failed\n", MY_ID);
		return -1;
	}

	nsock = launch_data_array_get_count(fd_array);
	if (nsock <= 0)
	{
		asldebug("%s: laucnchd fd array is empty\n", MY_ID);
		return -1;
	}

	for (i = 0; i < nsock; i++)
	{
		fd_dict = launch_data_array_get_index(fd_array, i);
		if (fd_dict == NULL)
		{
			asldebug("%s: laucnchd file discriptor array element 0 is NULL\n", MY_ID);
			return -1;
		}

		ufd[i] = launch_data_get_fd(fd_dict);

		rbufsize = 128 * 1024;
		len = sizeof(rbufsize);

		if (setsockopt(ufd[i], SOL_SOCKET, SO_RCVBUF, &rbufsize, len) < 0)
		{
			asldebug("%s: couldn't set receive buffer size for socket %d: %s\n", MY_ID, ufd[i], strerror(errno));
			close(ufd[i]);
			ufd[i] = -1;
			continue;
		}

		if (fcntl(ufd[i], F_SETFL, O_NONBLOCK) < 0)
		{
			asldebug("%s: couldn't set O_NONBLOCK for socket %d: %s\n", MY_ID, ufd[i], strerror(errno));
			close(ufd[i]);
			ufd[i] = -1;
			continue;
		}
	}

	for (i = 0; i < nsock; i++) if (ufd[i] != -1) aslevent_addfd(ufd[i], 0, udp_in_acceptmsg, NULL, NULL);
	return 0;
}

int
udp_in_reset(void)
{
	return 0;
}

int
udp_in_close(void)
{
	int i;

	if (nsock == 0) return 1;

	for (i = 0; i < nsock; i++)
	{
		if (ufd[i] != -1) close(ufd[i]);
		ufd[i] = -1;
	}

	nsock = 0;

	return 0;
}
