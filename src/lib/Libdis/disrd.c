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
 * @file	disrd.c
 *
 * @par Synopsis:
 * 	double disrd(int stream, int *retval)
 *
 *	Gets a Data-is-Strings floating point number from <stream> and converts
 *	it into a double which it returns.  The number from <stream> consists of
 *	two consecutive signed integers.  The first is the coefficient, with its
 *	implied decimal point at the low-order end.  The second is the exponent
 *	as a power of 10.
 *
 *	*<retval> gets DIS_SUCCESS if everything works well.  It gets an error
 *	code otherwise.  In case of an error, the <stream> character pointer is
 *	reset, making it possible to retry with some other conversion strategy.
 *
 *	By fiat of the author, neither loss of significance nor underflow are
 *	errors.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "dis.h"
#include "dis_.h"
#undef disrd

/**
 * @brief
 *      Gets a Data-is-Strings floating point number from <stream> and converts
 *      it into a double which it returns.  The number from <stream> consists of
 *      two consecutive signed integers.  The first is the coefficient, with its
 *      implied decimal point at the low-order end.  The second is the exponent
 *      as a power of 10.
 *
 * @param[in] stream - socket descriptor
 * @param[out] nchars - character count
 * @param[out] retval - success/error code
 *
 * @return      double
 * @retval      double value    success
 * @retval      0.0		error
 *
 */
double
disrd(int stream, int *retval)
{
	int		expon;
	unsigned	uexpon;
	int		locret;
	int		negate;
	unsigned	ndigs;
	unsigned	nskips;
	dis_long_double_t	ldval;

	assert(retval != NULL);
	assert(disr_commit != NULL);

	ldval = 0.0L;
	locret = disrl_(stream, &ldval, &ndigs, &nskips, DBL_DIG, 1, 0);
	if (locret == DIS_SUCCESS) {
		locret = disrsi_(stream, &negate, &uexpon, 1, 0);
		if (locret == DIS_SUCCESS) {
			expon = negate ? nskips - uexpon : nskips + uexpon;
			if (expon + (int)ndigs > DBL_MAX_10_EXP) {
				if (expon + (int)ndigs > DBL_MAX_10_EXP + 1) {
					ldval = ldval < 0.0L ?
						-HUGE_VAL : HUGE_VAL;
					locret = DIS_OVERFLOW;
				} else {
					ldval *= disp10l_(expon - 1);
					if (ldval > DBL_MAX / 10.0L) {
						ldval = ldval < 0.0L ?
							-HUGE_VAL : HUGE_VAL;
						locret = DIS_OVERFLOW;
					} else
						ldval *= 10.0L;
				}
			} else {
				if (expon < LDBL_MIN_10_EXP) {
					ldval *= disp10l_(expon + (int)ndigs);
					ldval /= disp10l_((int)ndigs);
				} else
					ldval *= disp10l_(expon);
			}
		}
	}
	if ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0)
		locret = DIS_NOCOMMIT;
	*retval = locret;
	return ((double)ldval);
}
