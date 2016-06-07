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
 * @file	get_dataservice_usr.c
 * @brief
 *	Retrieves the database user. The database user-id is retrieved from
 *	the file PBS_HOME/server_priv/db_user.
 */
#include <pbs_config.h>   /* the master config generated by configure */
#include "cmds.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/**
 * @brief
 *	Retrieves the database user. The database user-id is retrieved from
 *	the file PBS_HOME/server_priv/db_user.
 *	If this file is not found, then the default username
 *	"pbsdata" is returned as the default db user.
 *
 *      NOTE: pbs_get_dataservice_usr() was put into a separate file because
 *      the other database functions are only used by the server
 *
 * @param[out]  errmsg - Details of the error
 * @param[in]   len    - length of error messge variable
 *
 * @return      username String
 * @retval	 NULL - Failed to retrieve user-id
 * @retval	!NULL - Pointer to allocated memory with user-id string.
 *			Caller should free this memory after usage.
 *
 */
char *
pbs_get_dataservice_usr(char *errmsg, int len)
{
	char usr_file[MAXPATHLEN+1];
	int fd = 0;
	struct stat st = {0};
	char buf[MAXPATHLEN+1];

#ifdef WIN32
	snprintf(usr_file, MAXPATHLEN+1, "%s\\server_priv\\db_user", pbs_conf.pbs_home_path);
	if ((fd = open(usr_file, O_RDONLY | O_BINARY)) == -1)
#else
	snprintf(usr_file, MAXPATHLEN+1, "%s/server_priv/db_user", pbs_conf.pbs_home_path);
	if ((fd = open(usr_file, O_RDONLY)) == -1)
#endif
	{
		if (access(usr_file, F_OK) == 0) {
			snprintf(errmsg, len, "%s: open failed, errno=%d", usr_file, errno);
			return NULL; /* file exists but open failed */
		} else  {
			return strdup(PBS_DATA_SERVICE_USER); /* return default */
		}
	} else {
		if (fstat(fd, &st) == -1) {
			close(fd);
			snprintf(errmsg, len, "%s: stat failed, errno=%d", usr_file, errno);
			return NULL;
		}
		if (st.st_size >= sizeof(buf)) {
			close(fd);
			snprintf(errmsg, len, "%s: file too large", usr_file);
			return NULL;
		}

		if (read(fd, buf, st.st_size) != st.st_size) {
			close(fd);
			snprintf(errmsg, len, "%s: read failed, errno=%d", usr_file, errno);
			return NULL;
		}
		buf[st.st_size] = 0;
		close(fd);

		return (strdup(buf));
	}
}
