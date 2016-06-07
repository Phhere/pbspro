/*
 * Copyright (C) 1994-2016 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *  
 * This file is part of the PBS Professional ("PBS Pro") software.
 * 
 * Open Source License Information:
 *  
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free 
 * Software Foundation, either version 3 of the License, or (at your option) any 
 * later version.
 *  
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#ifndef WIN32
#include <stdlib.h>
#include <poll.h>
#include <sys/resource.h>
#endif

#include "portability.h"
#include "server_limits.h"
#include "pbs_ifl.h"
#include "net_connect.h"
#include "log.h"
#include "libsec.h"
#include "pbs_error.h"
#include "pbs_internal.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "svrfunc.h"

/**
 * @file	net_server.c
 */
/* Global Data (I wish I could make it private to the library, sigh, but
 * C don't support that scope of control.)
 *
 * This array of connection structures is used by the server to maintain
 * a record of the open I/O connections, it is indexed by the socket number.
 */

struct	connection *svr_conn;

/*
 * The following data is private to this set of network interface routines.
 */

int	max_connection = -1;
static int	num_connections = 0;
static int	net_is_initialized = 0;
#ifdef WIN32
static fd_set	readset; /* for select() on WIN32 */
static fd_set	selset;
#else
static int	maxfdx = 0; /* max index in pollfds[] */
static struct	pollfd	*pollfds; /* for poll() on UNIX variants */
#endif
static void	(*read_func[2])(int);
static char	logbuf[256];

/* Private function within this file */

static void 	 accept_conn();
static void	 cleanup_conn(int);
static int 	 selpoll_init();
static void 	 selpoll_fd_set(int condx);
static void 	 selpoll_fd_clr(int condx);
static int 	 selpoll_fd_isset(int condx);

/**
 * @brief
 * 	connection_find_usable_index: returns an index slot in
 *	connection table that is unused.
 *
 * @param[in] sock - socket descriptor 
 *
 * @return	int
 * @retval	connection table index	success
 * @retval	-1			if table full
 *
 */
int
connection_find_usable_index(int sock)
{

	int     i, j;

	/* let's look for an empty slot */
	j = (sock % max_connection); /* hash against socket */

	i = j;
	while (svr_conn[i].cn_sock != -1) {

		i = ((i+1) % max_connection); /* rehash */
		if (i == j)
			return (-1); /* table is full! */
	}
	return (i);
}

/**
 * @brief
 * 	connection_find_actual_index: returns an index slot in
 *	connection table that matches 'sock'. This will return -1 if
 *	it can't find the 'sock' entry.
 *
 * @param[in] sock - socket descriptor
 *
 * @return	int
 * @retval	matched index with sock		success
 * @retval	-1				if no match
 *
 */
int
connection_find_actual_index(int sock)
{

	int     i, j;

	j = (sock % max_connection); /* hash against socket */

	i = j;
	while ((svr_conn[i].cn_sock != (unsigned int)sock)) {

		i = ((i+1) % max_connection); /* rehash */

		if (i == j)
			return (-1); /* didn't find entry! */
	}
	return (i);
}

/**
 * @brief
 *	initialize the connection.
 *
 */
int
connection_init(void)
{
	int i;
	if(max_connection < 0) {
#ifdef WIN32
		max_connection = FD_SETSIZE;
#else
		int idx;
		int nfiles;
		struct rlimit rl;

		idx = getrlimit(RLIMIT_NOFILE, &rl);
		if ((idx == 0) && (rl.rlim_cur != RLIM_INFINITY))
			nfiles = rl.rlim_cur;
		else
			nfiles = getdtablesize();

		if (nfiles > 0)
			max_connection = nfiles;
		else
		  return (-1);
#endif
		svr_conn = (struct connection *)malloc(sizeof(struct connection) * max_connection);
		if (svr_conn == (struct connection *)0) {
			log_err(errno, "connection_init", "Insufficient system memory for svr_conn");
			return (-1);
		}
	}

	for (i=0; i< max_connection; i++) {
		svr_conn[i].cn_sock = -1;
		svr_conn[i].cn_active = Idle;
		svr_conn[i].cn_username[0] = '\0';
		svr_conn[i].cn_hostname[0] = '\0';
		svr_conn[i].cn_data = (void *)0;
	}
	return (0);

}

/**
 * @brief
 * 	init_network - initialize the network interface
 *
 * @par	Functionality:
 *    	Normal call, port > 0
 *	allocate a socket and bind it to the service port,
 *	add the socket to the readset/pollfds for select()/poll(),
 *	add the socket to the connection structure and set the
 *	processing function to accept_conn()
 *    	Special call, port == 0
 *	Only initial the connection table and poll pollfds or select readset.
 *
 * @param[in] port - port number
 * @param[in] readfunc - callback function which indicates type of request 
 *
 * @return	int
 * @retval	0	success
 * @retval	-1	error
 */

int
init_network(unsigned int port, void (*readfunc)(int))
{
	int		 i;
	size_t		 j;
#ifdef WIN32
	struct  linger   li;
#endif
	static int	 initialized = 0;
	int 		 sock;
	struct sockaddr_in socname;
	enum conn_type   type;

	if (initialized == 0) {
		if(connection_init() < 0) 
			return (-1);
		if (selpoll_init() < 0)
			return (-1);
		type = Primary;
	} else if (initialized == 1)
		type = Secondary;
	else
		return (-1);		/* too many main connections */

	net_is_initialized = 1;		/* flag that net stuff is initialized */

	if (port == 0)
		return 0;	/* that all for the special init only call */

	/* for normal calls ...						*/
	/* save the routine which should do the reading on connections	*/
	/* accepted from the parent socket				*/

	read_func[initialized++] = readfunc;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
#ifdef WIN32
		errno = WSAGetLastError();
#endif
		log_err(errno, "init_network", "socket() failed");
		return (-1);
	}

	i = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&i, sizeof(i));

#ifdef WIN32
	li.l_onoff = 1;
	li.l_linger = 5;
	setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&li, sizeof(li));
#endif

	/* name that socket "in three notes" */

	j = sizeof(socname);
	memset((void *)&socname, 0, j);
	socname.sin_port= htons((unsigned short)port);
	socname.sin_addr.s_addr = INADDR_ANY;
	socname.sin_family = AF_INET;
	if (bind(sock, (struct sockaddr *)&socname, sizeof(socname)) < 0) {
#ifdef WIN32
		errno = WSAGetLastError();
		(void)closesocket(sock);
#else
		(void)close(sock);
#endif
		log_err(errno, "init_network" , "bind failed");
		return (-1);
	}

	/* record socket in connection structure and select set
	 *
	 * remark: passing 0 as port value causing entry's member
	 *         cn_authen to have bit PBS_NET_CONN_PRIVIL set
	 */

	(void)add_conn(sock, type, (pbs_net_t)0, 0, accept_conn);

	/* start listening for connections */

	if (listen(sock, 256) < 0) {
#ifdef WIN32
		errno = WSAGetLastError();
		(void)closesocket(sock);
#else
		(void)close(sock);
#endif
		log_err(errno, "init_network", "listen failed");
		return (-1);
	}
	return (0);
}

/**
 * @brief
 *	checks for any connection timeout.
 *
 */
void
connection_idlecheck(void)
{
	time_t          now;
	int             i;

	/* have any connections timed out ?? */

	now = time((time_t *)0);
	for (i=0; i<max_connection; i++) {
		struct connection *cp = &svr_conn[i];
		u_long			ipaddr;

		if (cp->cn_active != FromClientDIS)
			continue;
		if ((now - cp->cn_lasttime) <= PBS_NET_MAXCONNECTIDLE)
			continue;
		if (cp->cn_authen & PBS_NET_CONN_NOTIMEOUT)
			continue;	/* do not time-out this connection */

		ipaddr = cp->cn_addr;
		sprintf(logbuf, "timeout connection from %lu.%lu.%lu.%lu",
			(ipaddr & 0xff000000) >> 24,
			(ipaddr & 0x00ff0000) >> 16,
			(ipaddr & 0x0000ff00) >> 8,
			(ipaddr & 0x000000ff));
		log_err(0, "wait_request", logbuf);
		close_conn(cp->cn_sock);
	}
}

/**
 * @brief
 *	engage_authentication - Use the security library interface to
 * 	engage the appropriate connection authentication.
 *
 * @param[in] pconn  pointer to a "struct connection" variable
 *
 * @return	 0  successful
 * @retval	-1 unsuccessful
 *
 * @par Remark:	
 *	If the authentication fails, messages are logged to
 *	the server's log file and the connection's security
 *	information is closed out (freed).
 */
static int
engage_authentication(struct connection *pconn)
{
	int	ret;
	int	sd;
	char	ebuf[ PBS_MAXHOSTNAME + 1 ];

	if (pconn == NULL || (sd = pconn->cn_sock) <0) {

		log_err(-1, "engage_authentication",
			"Bad arguments, unable to authenticate.");
		return (-1);
	}

	if ((ret = CS_server_auth(sd)) == CS_SUCCESS) {
		pconn->cn_authen |= PBS_NET_CONN_AUTHENTICATED;
		return (0);
	}

	if (ret == CS_AUTH_CHECK_PORT) {
		/*dealing with STD security's  "equivalent of"  CS_sever_auth*/

		if (pconn->cn_authen & PBS_NET_CONN_FROM_PRIVIL)
			pconn->cn_authen |= PBS_NET_CONN_AUTHENTICATED;
		return (0);
	}

	(void)get_connecthost(sd, ebuf, sizeof(ebuf));

	sprintf(logbuf,
		"Unable to authenticate connection from (%s:%d)",
		ebuf, pconn->cn_port);

	log_err(-1, "engage_authentication", logbuf);

	return (-1);
}


/**
 * @brief
 * 	wait_request - wait for a request (socket with data to read)
 *	This routine does a poll()/select() on the pollfds/readset
 *	of sockets when data is ready, the processing routine associated
 *	with the socket is invoked.
 *
 * @param[in] waittime - value for wait time.
 *
 * @return	int
 * @retval	0	success
 * @retval	-1	error
 *
 */

int
wait_request(time_t waittime)
{
	int i;
	int n;
#ifndef WIN32
	extern sigset_t allsigs;
	int timeout = (int)(waittime * 1000); /* milli seconds */
#else
	struct timeval timeout;

	selset = readset;
	timeout.tv_usec = 0;
	timeout.tv_sec  = waittime;
#endif

#ifndef WIN32
	/* unblock signals */
	if (sigprocmask(SIG_UNBLOCK, &allsigs, NULL) == -1)
		log_err(errno, __func__, "sigprocmask(UNBLOCK)");

	n = poll(pollfds, (maxfdx + 1), timeout);

	/* block signals again */
	i = errno;
	if (sigprocmask(SIG_BLOCK, &allsigs, NULL) == -1)
		log_err(errno, __func__, "sigprocmask(BLOCK)");
	errno = i;
#else
	n = select(FD_SETSIZE, &selset, (fd_set *)0, (fd_set *)0, &timeout);
#endif

	if (n == -1) {
#ifdef WIN32
		/*
		 * errno = WSAGetLastError();
		 * This above call should be required to get the error code
		 * Unfortunately, the mainline code does not have this (a bug)
		 * which allows the failover code to work! Need to fix the
		 * whole logic later.
		 */
		if (errno == EINTR || errno == WSAECONNRESET ||
			errno == WSAEWOULDBLOCK)
#else
		if (errno == EINTR)
#endif
			n = 0;	/* interrupted, cycle around */
		else {
			int	err = errno;

#ifdef WIN32
			sprintf(logbuf, "%s", "select failed");
#else
			sprintf(logbuf, "%s", "poll failed");
#endif
			log_err(errno, "wait_request", logbuf);
			assert(err != EBADF);	/* should not happen */
			return (-1);
		}
	}
#ifdef WIN32
	for (i = 0; (i <= max_connection) && (n > 0); i++) { /*  for select() in WIN32 */
#else
	for (i = 0; (i <= maxfdx) && (n > 0); i++) { /* for poll() in Unix */
#endif
		if (selpoll_fd_isset(i)) { /* this socket has data */

			n--; /* decrement the no. of events */

			svr_conn[i].cn_lasttime = time((time_t *)0);
			if (svr_conn[i].cn_active != Idle) {

				if (svr_conn[i].cn_active != Primary &&
					svr_conn[i].cn_active != RppComm &&
					svr_conn[i].cn_active != Secondary) {

					if (!(svr_conn[i].cn_authen & PBS_NET_CONN_AUTHENTICATED)) {

						if (engage_authentication(&svr_conn[i]) == -1) {
							(void)close_conn(svr_conn[i].cn_sock);
							continue;
						}
					}
				}
				svr_conn[i].cn_func(svr_conn[i].cn_sock);

			} else {
				/* Force this idle connection closed. */
#ifdef WIN32
				(void)closesocket(svr_conn[i].cn_sock);
#else
				(void)close(svr_conn[i].cn_sock);
#endif
				/* Force the svr_conn entry to be reset. */
				cleanup_conn(i);
			}
		}
	}

#ifndef WIN32
	connection_idlecheck();
#endif

	return (0);
}

/*
 * accept_conn - accept request for new connection
 *	this routine is normally associated with the main socket,
 *	requests for connection on the socket are accepted and
 *	the new socket is added to the select set and the connection
 *	structure - the processing routine is set to the external
 *	function: process_request(socket)
 */

/**
 * @brief
 *	accept request for new connection
 *	this routine is normally associated with the main socket,
 *	requests for connection on the socket are accepted and
 *	the new socket is added to the select set and the connection
 *	structure - the processing routine is set to the external
 *	function: process_request(socket)Makes a PBS_BATCH_Connect request to
 *	'server'.
 *
 * @param[in]   sd - main socket with connection request pending
 *
 * @return void
 */
static void
accept_conn(int sd)
{
	int newsock;
	struct sockaddr_in from;
	pbs_socklen_t fromsize;

	int conn_idx = connection_find_actual_index(sd);
	if (conn_idx == -1)
		return;

	/* update lasttime of main socket */

	svr_conn[conn_idx].cn_lasttime = time((time_t *)0);

	fromsize = sizeof(from);
	newsock = accept(sd, (struct sockaddr *)&from, &fromsize);
	if (newsock == -1) {
#ifdef WIN32
		errno = WSAGetLastError();
#endif
		log_err(errno, "accept_conn", "accept failed");
		return;
	}

#ifdef WIN32
	if (num_connections >= max_connection) {
		(void)closesocket(newsock);
		log_err(PBSE_CONNFULL, "accept_conn", "connection refused");
		return;		/* too many current connections */
	}
#else
	if (num_connections >= max_connection) {
		(void)close(newsock);
		log_err(PBSE_CONNFULL, "accept_conn", "connection refused");
		return;		/* too many current connections */
	}
#endif

	/* add the new socket to the select set and connection structure */

	(void)add_conn(newsock, FromClientDIS,
		(pbs_net_t)ntohl(from.sin_addr.s_addr),
		(unsigned int)ntohs(from.sin_port),
		read_func[(int)svr_conn[conn_idx].cn_active]);

	return;
}

/**
 * @brief
 *	add_conn - add a connection to the svr_conn array.
 *
 * @par Functionality:
 *	Find an empty slot in the connection table.  This is done by hashing
 *	on the socket (file descriptor).  On Windows, this is not a small
 *	interger.  The socket is then added to the poll/select set.
 *
 * @param[in]	sock: socket or file descriptor
 * @param[in]	type: (enumb conn_type)
 * @param[in]	addr: host IP address in host byte order
 * @param[in]	port: port number in host byte order
 * @param[in]	func: pointer to function to call when data is ready to read
 *
 * @return	int - the index of the table entry used for the added entry
 * @return	int - -1 on error such as table is full
 */

int
add_conn(int sock, enum conn_type type,pbs_net_t addr, unsigned int port, void (*func)(int) )
{
	int 	conn_idx;
	conn_idx = connection_find_usable_index(sock);

	if (conn_idx == -1)
		return -1;

	num_connections++;

	svr_conn[conn_idx].cn_sock     = sock;
	svr_conn[conn_idx].cn_active   = type;
	svr_conn[conn_idx].cn_addr     = addr;
	svr_conn[conn_idx].cn_port     = (unsigned short)port;
	svr_conn[conn_idx].cn_lasttime = time((time_t *)0);
	svr_conn[conn_idx].cn_func     = func;
	svr_conn[conn_idx].cn_oncl     = 0;
	svr_conn[conn_idx].cn_authen   = 0;

	if (port < IPPORT_RESERVED)
		svr_conn[conn_idx].cn_authen |= PBS_NET_CONN_FROM_PRIVIL;

	selpoll_fd_set(conn_idx);
	return (conn_idx);
}


/*
 * close_conn - close a network connection
 *	does physical close, also marks the connection table
 */
/**
 * @brief
 *	close_conn - close a connection in the svr_conn array.
 *
 * @par Functionality:
 *	Validate the socket (file descriptor).  For Unix/Linux it is a small
 *	integer less than the max number of connections.  For Windows, it is
 *	a valid socket value (not equal to INVALID_SOCKET).
 *	The entry in the table corresponding to the socket is found.
 *	If the entry is for a network socket (not a pipe), it is closed via
 *	CS_close_socket() which typically just does a close; for Windows,
 *	closesocket() is used.
 *	For a pipe (not a network socket), plain close() is called.
 *	If there is a function to be called, see cn_oncl table entry, that
 *	function is called.
 *	The table entry is cleared and marked "Idle" meaning it is free for
 *	reuse.
 *
 * @param[in]	sock: socket or file descriptor
 *
 * @return	void
 */
void
close_conn(int sd)
{
	int conn_idx;

#ifdef WIN32
	if ((sd == INVALID_SOCKET) || max_connection <= num_connections)
#else
	if ((sd < 0) || max_connection <= sd)
#endif
		return;

	conn_idx = connection_find_actual_index(sd);
	if (conn_idx == -1)
		return;

	if (svr_conn[conn_idx].cn_active == Idle)
		return;

	if (svr_conn[conn_idx].cn_active != ChildPipe) {
		if (CS_close_socket(sd) != CS_SUCCESS) {

			char	ebuf[ PBS_MAXHOSTNAME + 1 ];

			(void)get_connecthost(sd, ebuf, sizeof(ebuf));
			sprintf(logbuf,
				"Problem closing security context for %s:%d",
				ebuf, svr_conn[conn_idx].cn_port);

			log_err(-1, "close_conn", logbuf);
		}

#ifdef WIN32
		(void)closesocket(sd);
#else
		(void)close(sd);
#endif
	} else
		(void)close(sd);	/* pipe so use normal close */

	/* if there is a function to call on close, do it */

	if (svr_conn[conn_idx].cn_oncl != 0)
		svr_conn[conn_idx].cn_oncl(sd);

	cleanup_conn(conn_idx);
	num_connections--;
}

/**
 * @brief
 *	cleanup_conn - reset a connection entry in the svr_conn array.
 *
 * @par Functionality:
 * 	Given an index within the svr_conn array, reset all fields back to
 * 	their defaults and clear any select/poll related flags.
 *
 * @param[in]	cndx: index of the svr_conn entry
 *
 * @return	void
 */
void
cleanup_conn(int cndx)
{
	selpoll_fd_clr(cndx);

	svr_conn[cndx].cn_sock = -1;
	svr_conn[cndx].cn_addr = 0;
	svr_conn[cndx].cn_handle = -1;
	svr_conn[cndx].cn_active = Idle;
	svr_conn[cndx].cn_func = (void (*)())0;
	svr_conn[cndx].cn_authen = 0;
	svr_conn[cndx].cn_username[0] = '\0';
	svr_conn[cndx].cn_hostname[0] = '\0';
	svr_conn[cndx].cn_data = (void *)0;
}

/**
 * @brief
 * 	net_close - close all network connections but the one specified,
 *	if called with impossible socket number (-1), all will be closed.
 *	This function is typically called when a server is closing down and
 *	when it is forking a child.
 *
 * @par	Note:
 *	We clear the cn_oncl field in the connection table to prevent any
 *	"special on close" functions from being called.
 *
 * @param[in] but - socket number
 *
 * @par	Note:
 *	free() the dynamically allocated data.
 */

void
net_close(int but)
{
	int i;

	if (net_is_initialized == 0)
		return;		/* not initialized, just return */

	for (i=0; i<max_connection; i++) {
		if (svr_conn[i].cn_sock != but) {
			svr_conn[i].cn_oncl = 0;
			close_conn(svr_conn[i].cn_sock);
		}
	}
#ifndef WIN32
	if ((but == -1) && (pollfds != NULL)) {
		free(pollfds);
		pollfds = NULL;
	}
#endif
	if (but == -1)
		net_is_initialized = 0;	/* closed everything */
}

/**
 * @brief
 * 	get_connectaddr - return address of host connected via the socket
 *	This is in host order.
 *
 * @param[in] sock - socket descriptor
 *
 * @return	pbs_net_t
 * @retval	address of host		success
 * @retval	0			error
 *
 */
pbs_net_t
get_connectaddr(int sock)
{
	int conn_idx = connection_find_actual_index(sock);

	if (conn_idx == -1)
		return (0);

	return (svr_conn[conn_idx].cn_addr);
}

/**
 * @brief
 * 	get_connecthost - return name of host connected via the socket
 *
 * @param[in] sock - socket descriptor
 * @param[out] namebuf - buffer to hold host name
 * @param[out] size - size of buffer
 *
 * @return	int
 * @retval	0	success
 * @retval	-1	error
 *
 */

int
get_connecthost(int sock, char *namebuf, int size)
{
	int             i;
	struct hostent *phe;
	struct in_addr  addr;
	int	namesize = 0;
#if !defined(WIN32) && !defined(__hpux)
	char	dst[INET_ADDRSTRLEN+1]; /* for inet_ntop */
#endif
	int	conn_idx = connection_find_actual_index(sock);

	if (conn_idx == -1)
		return (-1);

	size--;
	addr.s_addr = htonl(svr_conn[conn_idx].cn_addr);

	if ((phe = gethostbyaddr((char *)&addr, sizeof(struct in_addr),
		AF_INET)) == (struct hostent *)0) {
#if defined(WIN32) || defined(__hpux)
			/*inet_ntoa is thread-safe on windows & hpux */
			(void)strcpy(namebuf, inet_ntoa(addr));
#else
			(void)strcpy(namebuf,
				inet_ntop(AF_INET, (void *) &addr, dst, INET_ADDRSTRLEN));
#endif
	}
	else {
		namesize = strlen(phe->h_name);
		for (i=0; i<size; i++) {
			*(namebuf+i) = tolower((int)*(phe->h_name+i));
			if (*(phe->h_name+i) == '\0')
				break;
		}
		*(namebuf+size) = '\0';
	}
	if (namesize > size)
		return (-1);
	else
		return (0);
}

/**
 * @brief
 *	Initialize maximum connetions.
 *	Init the pollset i.e. socket descriptors to be polled.
 *
 * @par Functionality:
 *	For select() in WIN32, max_connection is decided based on the
 *	FD_SETSIZE (max vaue, select() can handle) but for Unix variants
 *	that is decided by getrlimit() or getdtablesize(). For poll(),
 *	allocate memory for pollfds[] and init the table.
 *
 * @par Linkage scope:
 *	static (local)
 *
 *
 * @retval	 0 for success
 * @retval	-1 for failure
 *
 * @par Reentrancy
 *	MT-unsafe
 *
 */
static int
selpoll_init(void)
{
#ifdef WIN32
	FD_ZERO(&readset);
	FD_ZERO(&selset);
#else
	int idx;

	/** Allocate memory for pollfds[] */
	pollfds = (struct pollfd *)malloc(sizeof(struct pollfd) * max_connection);
	if (pollfds == (struct pollfd *)0) {
		log_err(errno, "selpoll_init", "Insufficient system memory");
		return -1;
	}

	/** Initialize the pollfds[] */
	for (idx = 0; idx < max_connection; idx++) {
		pollfds[idx].fd = -1;
		pollfds[idx].events = POLLIN;
		pollfds[idx].revents = 0;
	}
#endif
	return 0;
}

/**
 * @brief
 *	ADD/SET the socket descriptor to the polling set. And for
 *	Non-Windows, set the maximum index (maxfdx) in the pollset.
 *
 * @par Functionality:
 *	Fetch the socket descriptor from the svr_conn[] table using the
 *	index. For select() in WIN32, FD_SET() macro is used to set this
 *	in pollset. For UNIX, same index is used in pollfds[] table to
 *	set the socket. If the cndx is greater than maxfdx, adjust
 *	the maxfdx.
 *
 * @par Linkage scope:
 *	static (local)
 *
 * @param[in]	cndx: index into svr_conn[] table.
 *
 * @return	void
 *
 * @par Reentrancy
 *	MT-unsafe
 *
 */
static void
selpoll_fd_set(int cndx)
{
	/** fetch the socket at the index */
	int sock = svr_conn[cndx].cn_sock;
#ifdef WIN32
	FD_SET(sock, &readset);
#else
	pollfds[cndx].fd = sock;
	pollfds[cndx].revents = 0;
	if (cndx > maxfdx)
		maxfdx = cndx;
#endif
}

/**
 * @brief
 *	CLEAR/UNSET the socket descriptor in the polling set. And for
 *	Non-Windows, set the maximum index (maxfdx) in the pollset.
 *
 * @par Functionality:
 *	Fetch the socket descriptor from the svr_conn[] table using the
 *	index. For select() in WIN32, unset the sockfd using FD_CLR() macro
 *	but for poll() in Unix, set the sock at same index to -1. If the
 *	cndx to be cleared is maxfdx, then adjust maxfdx accordingly (for
 *	UNIX only).
 *
 * @par Linkage scope:
 *	static (local)
 *
 * @param[in]	cndx: index into svr_conn[] table.
 *
 * @return	void
 *
 * @par Reentrancy
 *	MT-unsafe
 *
 */
static void
selpoll_fd_clr(int cndx)
{
#ifdef WIN32
	int sock = svr_conn[cndx].cn_sock;
	FD_CLR(sock, &readset);
#else
	pollfds[cndx].fd = -1;
	pollfds[cndx].revents = 0;
	/**
	 * If the 'cndx' is the maxfdx, then decrement the maxfdx.
	 * Continue to decrement if there are more vacant slots
	 * i.e. with value -1.
	 */
	if (cndx == maxfdx) {
		--maxfdx;
		while ((maxfdx > 0) && (pollfds[maxfdx].fd == -1))
			--maxfdx;
	}
#endif
}

/**
 * @brief
 *	Check the socket descriptor if it is ready for the I/O.
 *
 * @par Functionality:
 *	Fetch the socket descriptor using the parameter (cndx) and check
 *	if that is ready for the I/O oepration, using FD_ISSET() macro
 *	for select() [WIN32] or by reading the read events from pollfd
 *	structure [Unix].
 *
 * @par Linkage scope:
 *	static (local)
 *
 * @param[in]   cndx: index into svr_conn[] table.
 *
 * @return	int
 * @retval	0: sock is not ready for i/o or got error.
 * @retval	1: sock is ready for i/o.
 *
 * @par Reentrancy
 *	MT-unsafe
 *
 */
static int
selpoll_fd_isset(int cndx)
{
	int sock = svr_conn[cndx].cn_sock;
	if (sock < 0)
		return 0;
#ifdef WIN32
	return FD_ISSET(sock, &selset);
#else
	if (pollfds[cndx].fd < 0)
		return 0;
	if (pollfds[cndx].fd != sock) {
		int target;
		log_err(-1, "selpoll_fd_isset",
			"svr_conn[] and pollfds[] arrays are out of sync.");
		/* Try to find the svr_conn entry that the pollfds entry references. */
		target = connection_find_actual_index(pollfds[cndx].fd);
		if (target < 0) {
			/* pollfds is invalid, reset it */
			(void)close(pollfds[cndx].fd);
			selpoll_fd_clr(cndx);
		}
		return 0;
	}

	if ((pollfds[cndx].revents) & (POLLIN|POLLHUP|POLLERR|POLLNVAL))
		return 1;

	return 0;
#endif
}

