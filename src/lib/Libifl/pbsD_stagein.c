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
/**
 * @file	pbs_stagein.c
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "dis.h"
#include "pbs_ecl.h"


/**
 * @brief
 *	-sends stagein job batch request and get reply.
 *
 * @param[in] c - communication handle
 * @param[in] jobid - job identifier
 * @param[in] location - destination for job 
 * @param[in] extend - extend string for request
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */

int
pbs_stagein(int c, char *jobid, char *location, char *extend)
{
	int	rc;
	struct batch_reply   *reply;
	int	sock;


	if ((jobid == (char *)0) || (*jobid == '\0'))
		return (pbs_errno = PBSE_IVALREQ);
	if (location == (char *)0)
		location = "";


	sock = connection[c].ch_socket;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_setup(sock);

	/* send stagein request, a run request with a different id */

	if ((rc = encode_DIS_ReqHdr(sock, PBS_BATCH_StageIn, pbs_current_user)) ||
		(rc = encode_DIS_Run(sock, jobid, location, 0)) ||
		(rc = encode_DIS_ReqExtend(sock, extend))) {
		connection[c].ch_errtxt = strdup(dis_emsg[rc]);
		if (connection[c].ch_errtxt == NULL) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	if (DIS_tcp_wflush(sock)) {
		pbs_errno = PBSE_PROTOCOL;
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	/* get reply */

	reply = PBSD_rdrpy(c);
	rc = connection[c].ch_errno;

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return rc;
}
