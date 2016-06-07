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
 * @file	dec_Reg.c
 * @brief
 * 	decode_DIS_Register() - decode a Register Dependency Batch Request
 *
 *	The batch_request structure must already exist (be allocated by the
 *	caller.   It is assumed that the header fields (protocol type,
 *	protocol version, request type, and user name) have already be decoded.
 *
 * @par Data items are:
 * 			string		job owner
 *			string		parent job id
 *			string		child job id
 *			unsigned int	dependency type
 *			unsigned int	operation
 *			signed long	cost
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include "libpbs.h"
#include "list_link.h"
#include "server_limits.h"
#include "attribute.h"
#include "credential.h"
#include "batch_request.h"
#include "dis.h"

/**
 * @brief -
 *	decode a Register Dependency Batch Request
 *
 * @par	Functionality:
 *		The batch_request structure must already exist (be allocated by the
 *      	caller.   It is assumed that the header fields (protocol type,
 *      	protocol version, request type, and user name) have already be decoded
 *
 * @par	Data items are:
 *		string          job owner\n
 *		string          parent job id\n
 *		string          child job id\n
 *		unsigned int    dependency type\n
 *		unsigned int    operation\n
 *		signed long     cost\n
 *
 * @param[in] sock - socket descriptor
 * @param[out] preq - pointer to batch_request structure
 *
 * @return      int
 * @retval      DIS_SUCCESS(0)  success
 * @retval      error code      error
 *
 */

int
decode_DIS_Register(int sock, struct batch_request *preq)
{
	int   rc;

	rc = disrfst(sock, PBS_MAXUSER, preq->rq_ind.rq_register.rq_owner);
	if (rc)
		return rc;
	rc = disrfst(sock, PBS_MAXSVRJOBID, preq->rq_ind.rq_register.rq_parent);
	if (rc)
		return rc;
	rc = disrfst(sock, PBS_MAXCLTJOBID, preq->rq_ind.rq_register.rq_child);
	if (rc)
		return rc;
	preq->rq_ind.rq_register.rq_dependtype = disrui(sock, &rc);

	if (rc)
		return rc;

	preq->rq_ind.rq_register.rq_op = disrui(sock, &rc);
	if (rc)
		return rc;

	preq->rq_ind.rq_register.rq_cost = disrsl(sock, &rc);

	return rc;
}
