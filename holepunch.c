/* implementation of Bird's Hole Punching Protocol */
/*
Copyright 2020-2021 James R.
All rights reserved.

Redistribution and use in source and binary forms, with or
without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above
   copyright notice, this list of conditions and the
   following disclaimer.
2. Redistributions in binary form must reproduce the above
   copyright notice.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "config.h"

/* runtime copyright notice */
extern const char _binary_NOTICE_start[];
extern const char _binary_NOTICE_size[];

/* git commit to string */
#define TOSTR2(x) #x
#define TOSTR(x) TOSTR2(x)

const char magic[4] = { 0x00, 0x52, 0xEB, 0x11 };

int fd;/* listening socket */

char buffer[10];
struct sockaddr_in addr;

/* report line number */
#define report(l) \
	fprintf(stderr, "%s:%d: ", __FILE__, l)

/* perror plus line number */
#define perror_at(p,l) (report(l), perror(p))
#define perror_here(p) perror_at(p, __LINE__)

/* not one of the reserved "local" addresses */
int
is_external_address (const void *p)
{
	const int a = ((const unsigned char*)p)[0];
	const int b = ((const unsigned char*)p)[1];

	if (*(const int*)p == ~0)/* 255.255.255.255 */
		return 0;

	switch (a)
	{
		case 0:
		case 10:
		case 127:
			return 0;
		case 172:
			return (b & ~15) != 16;/* 16 - 31 */
		case 192:
			return b != 168;
		default:
			return 1;
	}
}

char *
address_string (void)
{
	static char str[sizeof "255.255.255.255p65535"];
	inet_ntop(AF_INET, &addr.sin_addr, str, sizeof str);
	sprintf(&str[strlen(str)], "p%d",
			ntohs(addr.sin_port));
	return str;
}

void
create_socket (void)
{
	struct sockaddr_in end;

	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (fd == -1)
	{
		perror_here("socket");
		exit(1);
	}

	end.sin_family = AF_INET;
	end.sin_port = htons(PORT);
	end.sin_addr.s_addr = INADDR_ANY;/* 0.0.0.0 */

	if (bind(fd, (struct sockaddr*)&end, sizeof end) == -1)
	{
		perror_here("bind");
		exit(1);
	}
}

void
outgoing
(		int n,
		int line)
{
	int c = sendto(fd, buffer, n, 0,
			(struct sockaddr*)&addr, sizeof addr);

	if (c < n)
	{
		if (c == -1)
		{
			perror_at("sendto", line);
		}
		else
		{
			report(line);
			fprintf(stderr,
					"(WEIRD) sendto sent less bytes"
					" (%d < %d)\n", c, n);
		}
	}
}

#define outgoing(n) outgoing(n, __LINE__)

/* request hole punch */
void
relay (void)
{
	struct sockaddr_in end;

	end.sin_family = AF_INET;
	end.sin_addr.s_addr = *(int*)&buffer[4];
	end.sin_port = *(short*)&buffer[8];

	*(int*)&buffer[4] = addr.sin_addr.s_addr;
	*(short*)&buffer[8] = addr.sin_port;

	printf("%s ->", address_string());

	addr = end;

	printf(" %s\n", address_string());

	outgoing(10);
}

/* returns number of bytes (of valid packet) */
int
incoming (void)
{
	socklen_t addr_size;

	int n;

	while ((n = recvfrom(fd, buffer, 10, 0,
					(struct sockaddr*)&addr,
					(addr_size = sizeof addr, &addr_size)))
			!= -1)
	{
		if (n == 10 && *(int*)buffer == *(int*)magic &&
				is_external_address(&addr.sin_addr) &&
				is_external_address(&buffer[4]))
		{
			relay();
		}
	}

	perror_here("recvfrom");
}

int
main
(		int ac,
		char ** av)
{
	create_socket();

	/* line buffer stdout (for journal) */
	setvbuf(stdout, NULL, _IOLBF, 0);

	printf(
			"%.*s\n"
			"Bound to port %d.\n"
			"Git rev. %s\n",
			(int)(unsigned long)_binary_NOTICE_size,
			_binary_NOTICE_start, PORT, TOSTR (COMMIT));

	do
		incoming();
	while (1);
}
