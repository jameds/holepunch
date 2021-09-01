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
#include <sys/select.h>
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

int fd4;/* listening socket - IPv4 */
int fd6;/* IPv6 */

char buffer[22];

int packet;

#define is_v4() (packet == 10)

union sockaddr_munge {
	struct sockaddr sa;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
} addr;

/* report line number */
#define report(l) \
	fprintf(stderr, "%s:%d: ", __FILE__, l)

/* perror plus line number */
#define perror_at(p,l) (report(l), perror(p))
#define perror_here(p) perror_at(p, __LINE__)

/* not one of the reserved "local" addresses */
int
is_external_address4 (const void *p)
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

int
is_external_address6 (const void *p)
{
	const int a = ((const unsigned char*)p)[0];
	char s[15] = {0};

	switch (a)
	{
		case 0xfc:/* fc::/7 */
		case 0xfc:
			return 0;
		case 0:/* :: */
		case 1:/* ::1 */
			return memcmp(&((const char*)p)[1], s, 15);
		default:
			return 1;
	}
}

int
is_external_address (const void *p)
{
	return is_v4() ?
		is_external_address4(p) :
		is_external_address6(p);
}

const void *
get_address (void)
{
	return is_v4() ?
		(void*)&addr.sin.sin_addr :
		(void*)&addr.sin6.sin6_addr;
}

char *
address_string (void)
{
	static char str[INET6_ADDRSTRLEN + sizeof "65535"];

	inet_ntop(addr.sa.sa_family,
			get_address(), str, sizeof str);

	sprintf(&str[strlen(str)], "p%d", ntohs(is_v4() ?
				addr.sin.sin_port : addr.sin6.sin6_port));

	return str;
}

void
set_v6only (int fd)
{
	int n = 1;

	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
				&n, sizeof n) == -1)
	{
		perror_here("setsockopt");
		exit(1);
	}
}

int
create_socket (int family)
{
	union sockaddr_munge end;

	int fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);

	if (fd == -1)
	{
		perror_here("socket");
		exit(1);
	}

	end.sa.sa_family = family;

	if (family == AF_INET)
	{
		end.sin.sin_port = htons(PORT);
		end.sin.sin_addr.s_addr = INADDR_ANY;/* 0.0.0.0 */
	}
	else/* IPv6 */
	{
		end.sin6.sin6_port = htons(PORT);
		end.sin6.sin6_addr = in6addr_any;/* :: */

		/* restrict socket to ipv6 traffic only, this is
			required for dual stack networking */
		set_v6only(fd);
	}

	if (bind(fd, &end.sa, sizeof end) == -1)
	{
		perror_here("bind");
		exit(1);
	}

	return fd;
}

void
outgoing
(		int fd,
		int line)
{
	int c = sendto(fd, buffer, packet, 0,
			&addr.sa, sizeof addr);

	if (c < packet)
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
					" (%d < %d)\n", c, packet);
		}
	}
}

#define outgoing(n) outgoing(n, __LINE__)

/* request hole punch */
void
relay (int fd)
{
	union sockaddr_munge end;

	printf("%s ->", address_string());

	end.sa.sa_family = AF_INET;

	if (is_v4())
	{
		end.sin.sin_addr.s_addr = *(int*)&buffer[4];
		end.sin.sin_port = *(short*)&buffer[8];

		*(int*)&buffer[4] = addr.sin.sin_addr.s_addr;
		*(short*)&buffer[8] = addr.sin.sin_port;
	}
	else
	{
		end.sin6.sin6_addr = *(struct in6_addr*)&buffer[4];
		end.sin6.sin6_port = *(short*)&buffer[20];

		*(struct in6_addr*)&buffer[4] = addr.sin6.sin6_addr;
		*(short*)&buffer[20] = addr.sin6.sin6_port;
	}

	addr = end;

	printf(" %s\n", address_string());

	outgoing(fd);
}

void
incoming
(		int fd,
		int n)
{
	socklen_t addr_size = sizeof addr;

	packet = recvfrom(fd, buffer, n, 0,
			&addr.sa, &addr_size);

	if (packet == n)
	{
		if (*(int*)buffer == *(int*)magic &&
				is_external_address(get_address()) &&
				is_external_address(&buffer[4]))
		{
			relay(fd);
		}
	}
	else if (packet == -1)
		perror_here("recvfrom");
}

int
main
(		int ac,
		char ** av)
{
	fd_set rfds;

	puts("Binding IPv4 socket...");
	fd4 = create_socket(AF_INET);

	puts("Binding IPv6 socket...");
	fd6 = create_socket(AF_INET6);

	/* line buffer stdout (for journal) */
	setvbuf(stdout, NULL, _IOLBF, 0);

	printf(
			"\n%.*s\n"
			"Bound to port %d.\n"
			"Git rev. %s\n",
			(int)(unsigned long)_binary_NOTICE_size,
			_binary_NOTICE_start, PORT, TOSTR (COMMIT));

	FD_ZERO (&rfds);

	do
	{
		FD_SET (fd4, &rfds);
		FD_SET (fd6, &rfds);

		if (select(fd6 + 1, &rfds, NULL, NULL, NULL) == -1)
			perror_here("select");
		else
		{
			if (FD_ISSET(fd4, &rfds))
				incoming(fd4, 10);

			if (FD_ISSET(fd6, &rfds))
				incoming(fd6, 22);
		}
	}
	while (1);
}
