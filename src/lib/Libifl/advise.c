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

#include <stdarg.h>
#include <stdio.h>
/**
 * @file	advise.c
 */

/**
 * @brief - prints advise from user (variable  argument list ) to standard error file.
 *
 * @param[in] who - user name
 * @param[in] variable arguments
 *
 * @return	Void
 *
 */
void
advise(char *who, ...)
{
	va_list args;
	char *fmt;

#ifndef NDEBUG

	va_start(args, who);

	if (who == NULL || *who == '\0')
		fprintf(stderr, "advise:\n");
	else
		fprintf(stderr, "advise from %s:\n", who);
	fmt = va_arg(args, char *);
	fputs("\t", stderr);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	fputc('\n', stderr);
	fflush(stderr);
#endif
}
