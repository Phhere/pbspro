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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/bproc.h>
#include "libpbs.h"
#include "list_link.h"
#include "log.h"
#include "server_limits.h"
#include "attribute.h"
#include "resource.h"
#include "job.h"
#include "mom_mach.h"
#include "mom_func.h"

/**
 * @file
 */
/* Global Variables */

extern int	 exiting_tasks;
extern char	 mom_host[];
extern pbs_list_head svr_alljobs;
extern int	 termin_child;

/* Private variables */

/**
 * @brief
 *      set_job - set up a new job session
 *      Set session id and whatever else is required on this machine
 *      to create a new job.
 *
 * @param[in]   pjob - pointer to job structure
 * @param[in]   sjr  - pointer to startjob_rtn structure
 *
 * @return      session/job id or if error:
 * @retval      -1 - if setsid() fails
 * @retval      -2 - if other, message in log_buffer
 *
 */

int set_job(job *pjob, struct startjob_rtn *sjr)
{
	return (sjr->sj_session = setsid());
}

/**
 * @brief
 *      set_globid - set the global id for a machine type.
 *
 * @param[in] pjob - pointer to job structure
 * @param[in] sjr  - pointer to startjob_rtn structure
 *
 * @return Void
 *
 */

void
set_globid( job *pjob, struct startjob_rtn *sjr)
{
	return;
}

/**
 * @brief
 *      set_mach_vars - setup machine dependent environment variables
 *
 * @param[in] pjob - pointer to job structure
 * @param[in] vtab - pointer to var_table structure
 *
 * @return      int
 * @retval      0       Success
 *
 */

int
set_mach_vars(job *pjob, struct var_table *vtab)
{
	resource	*pres;
	attribute	*at;
	resource_def	*rd;
	int		found, i, j;
	ulong		num;
	char		buf[1024];

	at = &pjob->ji_wattr[(int)JOB_ATR_resource];
	rd = find_resc_def(svr_resc_def, "ncpus", svr_resc_size);
	if (rd == NULL)
		return JOB_EXEC_OK;
	pres = find_resc_entry(at, rd);
	if (pres == NULL)
		return JOB_EXEC_OK;

	num = (ulong)pres->rs_value.at_val.at_long;
	DBPRT(("%s: look for %lu nodes\n", __func__, num))
	if (num == 0)
		return JOB_EXEC_OK;
	if (num > num_pcpus)
		return JOB_EXEC_FAIL1;

	buf[0] = '\0';
	found = 0;
	for (i=0; i<num_pcpus; i++) {
		struct	bnode	*np = &node_array[i];
		char	nodestr[64];

		/* see if this job is already here */
		if (np->n_job == pjob) {
			(void)sprintf(log_buffer,
				"nodes already allocated to %s",
				pjob->ji_qs.ji_jobid);
			log_err(-1, __func__, log_buffer);
			return JOB_EXEC_RETRY;
		}

		if (np->n_job != NULL)
			continue;
		if (bproc_nodestatus(i) != bproc_node_up)
			continue;

		np->n_job = pjob;
		sprintf(nodestr, "%d:", i);
		for (j=0; j<np->n_cpus; j++) {
			DBPRT(("%s: allocate node %d cpu %d\n", __func__, i, j))
			found++;
			strcat(buf, nodestr);
			if (found >= num)
				break;
		}
		if (found >= num)
			break;
	}
	if (found < num) {
		DBPRT(("%s: not enough nodes %d < %d\n", __func__, found, num))
		return JOB_EXEC_RETRY;
	}
	j = strlen(buf);
	buf[j-1] = '\0';	/* trim last : */
	bld_env_variables(vtab, "BEOWULF_JOB_MAP", buf);
	return JOB_EXEC_OK;
}

/**
 * @brief
 *      sets the shell to be used
 *
 * @param[in] pjob - pointer to job structure
 * @param[in] pwdp - pointer to passwd structure
 *
 * @return      string
 * @retval      shellname       Success
 *
 */

char *set_shell(job *pjob, struct passwd  *pwdp)
{
	char *cp;
	int   i;
	char *shell;
	struct array_strings *vstrs;
	/*
	 * find which shell to use, one specified or the login shell
	 */

	shell = pwdp->pw_shell;
	if ((pjob->ji_wattr[(int)JOB_ATR_shell].at_flags & ATR_VFLAG_SET) &&
		(vstrs = pjob->ji_wattr[(int)JOB_ATR_shell].at_val.at_arst)) {
		for (i = 0; i < vstrs->as_usedptr; ++i) {
			cp = strchr(vstrs->as_string[i], '@');
			if (cp) {
				if (!strncmp(mom_host, cp+1, strlen(cp+1))) {
					*cp = '\0';	/* host name matches */
					shell = vstrs->as_string[i];
					break;
				}
			} else {
				shell = vstrs->as_string[i];	/* wildcard */
			}
		}
	}
	return (shell);
}

/**
 * @brief
 *      scan_for_terminated - scan the list of runnings jobs for a task whose
 *      session id matched that of a terminated child pid.  Mark that
 *      task as Exiting.
 *
 * @return      Void
 *
 */
void scan_for_terminated()
{
	int		exiteval;
	pid_t		pid;
	job		*pjob;
	task		*ptask;
	int		statloc;
	int		i;

	/* update the latest intelligence about the running jobs;         */
	/* must be done before we reap the zombies, else we lose the info */

	termin_child = 0;

	if (mom_get_sample() == PBSE_NONE) {
		pjob = (job *)GET_NEXT(svr_alljobs);
		while (pjob) {
			mom_set_use(pjob);
			pjob = (job *)GET_NEXT(pjob->ji_alljobs);
		}
	}

	/* Now figure out which task(s) have terminated (are zombies) */

	while ((pid = waitpid(-1, &statloc, WNOHANG)) > 0) {
		if (WIFEXITED(statloc))
			exiteval = WEXITSTATUS(statloc);
		else if (WIFSIGNALED(statloc))
			exiteval = WTERMSIG(statloc) + 0x100;
		else
			exiteval = 1;

		pjob = (job *)GET_NEXT(svr_alljobs);
		while (pjob) {
			/*
			 ** see if process was a child doing a special
			 ** function for MOM
			 */
			if ((pid == pjob->ji_momsubt) != 0)
				break;
			/*
			 ** look for task
			 */
			ptask = (task *)GET_NEXT(pjob->ji_tasks);
			while (ptask) {
				if (ptask->ti_qs.ti_sid == pid)
					break;
				ptask = (task *)GET_NEXT(ptask->ti_jobtask);
			}
			if (ptask != NULL)
				break;
			pjob = (job *)GET_NEXT(pjob->ji_alljobs);
		}

		if (pjob == NULL) {
			DBPRT(("%s: pid %d not tracked, exit %d\n",
				__func__, pid, exiteval))
			continue;
		}

		if (pid == pjob->ji_momsubt) {
			pjob->ji_momsubt = 0;
			if (pjob->ji_mompost) {
				pjob->ji_mompost(pjob, exiteval);
			}
			(void)job_save(pjob, SAVEJOB_QUICK);
			continue;
		}
		DBPRT(("%s: task %08.8X pid %d exit value %d\n", __func__,
			ptask->ti_qs.ti_task, pid, exiteval))
		kill_session(ptask->ti_qs.ti_sid, SIGKILL, 0);
		ptask->ti_qs.ti_exitstat = exiteval;
		ptask->ti_qs.ti_status = TI_STATE_EXITED;
		(void)task_save(ptask);
		sprintf(log_buffer, "task %08.8X terminated",
			ptask->ti_qs.ti_task);
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG,
			pjob->ji_qs.ji_jobid, log_buffer);

		exiting_tasks = 1;

		/*
		 **	Clean up node_array.  This is where we take
		 **	a job out of the array and set node ownership
		 **	back to root.
		 */
		DBPRT(("%s: check nodes %d job %lu\n", __func__, num_pcpus, pjob))
		for (i=0; i<num_pcpus; i++) {
			struct	bnode	*np = &node_array[i];

			DBPRT(("%s: node %d job %lu\n", __func__, i, np->n_job))
			if (np->n_job == pjob) {
				DBPRT(("%s: clear node %d\n", __func__, i))
				if (bproc_chown(i, 0) != 0)
					log_err(errno, __func__, "bproc_chown");
				if (bproc_chgrp(i, 0) != 0)
					log_err(errno, __func__, "bproc_chgrp");
				if (bproc_chmod(i, 0) != 0)
					log_err(errno, __func__, "bproc_chmod");
				np->n_job = NULL;
			}
		}
	}
}

/**
 * @brief
 *      This is code adapted from an example for posix_openpt in
 *      The Open Group Base Specifications Issue 6.
 *
 *      On success, this function returns an open descriptor for the
 *      master pseudotty and places a pointer to the (static) name of
 *      the slave pseudotty in *rtn_name;  on failure, -1 is returned.
 *
 * @param[in] rtn_name - holds info tty
 *
 * @return      int
 * @retval      fd      Success
 * @retval      -1      Failure
 *
 */

int
open_master(char **rtn_name)
{
	int		masterfd;
	char		*newslavename;
	static char	slavename[_POSIX_PATH_MAX];
#ifndef	_XOPEN_SOURCE
	extern char	*ptsname(int);
	extern int	grantpt(int);
	extern int	unlockpt(int);
	extern int	posix_openpt(int);
#endif

	masterfd = posix_openpt(O_RDWR | O_NOCTTY);
	if (masterfd == -1)
		return (-1);

	if ((grantpt(masterfd) == -1) ||
		(unlockpt(masterfd) == -1) ||
		((newslavename = ptsname(masterfd)) == NULL)) {
		(void) close(masterfd);
		return (-1);
	}

	(void)strncpy(slavename, newslavename, sizeof(slavename) - 1);
	assert(rtn_name != NULL);
	*rtn_name = slavename;
	return (masterfd);
}

/*
 * struct sig_tbl = map of signal names to numbers,
 * see req_signal() in ../requests.c
 */
struct sig_tbl sig_tbl[] = {
	{ "NULL", 0 },
	{ "HUP", SIGHUP },
	{ "INT", SIGINT },
	{ "QUIT", SIGQUIT },
	{ "ILL",  SIGILL },
	{ "TRAP", SIGTRAP },
	{ "IOT", SIGIOT },
	{ "ABRT", SIGABRT },
	{ "FPE", SIGFPE },
	{ "KILL", SIGKILL },
	{ "BUS", SIGBUS },
	{ "SEGV", SIGSEGV },
	{ "PIPE", SIGPIPE },
	{ "ALRM", SIGALRM },
	{ "TERM", SIGTERM },
	{ "URG", SIGURG },
	{ "STOP", SIGSTOP },
	{ "TSTP", SIGTSTP },
	{ "CONT", SIGCONT },
	{ "CHLD", SIGCHLD },
	{ "CLD",  SIGCHLD },
	{ "TTIN", SIGTTIN },
	{ "TTOU", SIGTTOU },
	{ "IO", SIGIO },
	{ "POLL", SIGPOLL },
	{ "XCPU", SIGXCPU },
	{ "XFSZ", SIGXFSZ },
	{ "VTALRM", SIGVTALRM },
	{ "PROF", SIGPROF },
	{ "WINCH", SIGWINCH },
	{ "USR1", SIGUSR1 },
	{ "USR2", SIGUSR2 },
	{(char *)0, -1 }
};
