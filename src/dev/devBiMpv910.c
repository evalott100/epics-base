/* devBiMpv910.c */
/* share/src/dev $Id$ */

/* devBiMpv910.c - Device Support Routines for  Burr Brown MPV 910  Binary input */
/*
 *      Original Author: Bob Dalesio
 *      Current Author:  Marty Kraimer
 *      Date:            6-1-90
 *
 *      Experimental Physics and Industrial Control System (EPICS)
 *
 *      Copyright 1991, the Regents of the University of California,
 *      and the University of Chicago Board of Governors.
 *
 *      This software was produced under  U.S. Government contracts:
 *      (W-7405-ENG-36) at the Los Alamos National Laboratory,
 *      and (W-31-109-ENG-38) at Argonne National Laboratory.
 *
 *      Initial development by:
 *              The Controls and Automation Group (AT-8)
 *              Ground Test Accelerator
 *              Accelerator Technology Division
 *              Los Alamos National Laboratory
 *
 *      Co-developed with
 *              The Controls and Computing Group
 *              Accelerator Systems Division
 *              Advanced Photon Source
 *              Argonne National Laboratory
 *
 * Modification Log:
 * -----------------
 * .01  11-11-91        jba     Moved set of alarm stat and sevr to macros
 * .02	03-13-92	jba	ANSI C changes
 *      ...
 */


#include	<vxWorks.h>
#include	<types.h>
#include	<stdioLib.h>
#include	<string.h>

#include	<alarm.h>
#include	<dbDefs.h>
#include	<dbAccess.h>
#include        <recSup.h>
#include	<devSup.h>
#include	<module_types.h>
#include	<biRecord.h>


/* Create the dset for devAiBiMpv910 */
static long init_record();
static long read_bi();

struct {
	long		number;
	DEVSUPFUN	report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record;
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	read_bi;
}devBiMpv910={
	5,
	NULL,
	NULL,
	init_record,
	NULL,
	read_bi};

static long init_record(pbi)
    struct biRecord	*pbi;
{
    struct vmeio *pvmeio;

    /* bi.inp must be an VME_IO */
    switch (pbi->inp.type) {
    case (VME_IO) :
	pvmeio = (struct vmeio *)&(pbi->inp.value);
	pbi->mask=1;
	pbi->mask <<= pvmeio->signal;
	break;
    default :
	recGblRecordError(S_db_badField,(void *)pbi,
		"devBiMpv910 (init_record) Illegal INP field");
	return(S_db_badField);
    }
    return(0);
}

static long read_bi(pbi)
    struct biRecord	*pbi;
{
	struct vmeio 	*pvmeio;
	int	    	status;
	unsigned int    value,mask;

	
	pvmeio = (struct vmeio *)&(pbi->inp.value);
	mask = pbi->mask;
	status = bb910_driver(pvmeio->card,mask,&value);
	if(status==0) {
		pbi->rval = value;
		return(0);
	} else {
                if(recGblSetSevr(pbi,READ_ALARM,INVALID_ALARM) && errVerbose
		&& (pbi->stat!=READ_ALARM || pbi->sevr!=INVALID_ALARM))
			recGblRecordError(-1,(void *)pbi,"bb910_driver Error");
		return(2);
	}
}
