/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/

/*  
 *  $Id$
 *
 *                              
 *                    L O S  A L A M O S
 *              Los Alamos National Laboratory
 *               Los Alamos, New Mexico 87545
 *                                  
 *  Copyright, 1986, The Regents of the University of California.
 *                                  
 *           
 *	Author Jeffrey O. Hill
 *	johill@lanl.gov
 *	505 665 1831
 */

#ifndef dbPutNotifyBlockerh
#define dbPutNotifyBlockerh

#ifdef epicsExportSharedSymbols
#define dbPutNotifyBlockerh_restore_epicsExportSharedSymbols
#undef epicsExportSharedSymbols
#endif

#include "shareLib.h"

#include "tsFreeList.h"

#ifdef dbPutNotifyBlockerh_restore_epicsExportSharedSymbols
#define epicsExportSharedSymbols
#endif

#include "shareLib.h"

class dbPutNotifyBlocker : public dbBaseIO {
public:
    dbPutNotifyBlocker ();
    void initiatePutNotify ( epicsGuard < epicsMutex > & locker, 
            cacWriteNotify & notify, struct dbAddr & addr, 
            unsigned type, unsigned long count, const void * pValue );
    void cancel ();
    void show ( unsigned level ) const;
    void destroy ();
    void * operator new ( size_t size, 
        tsFreeList < dbPutNotifyBlocker > & );
    void operator delete ( void *, 
        tsFreeList < dbPutNotifyBlocker > & );
protected:
    virtual ~dbPutNotifyBlocker ();
private:
    putNotify pn;
    //
    // Include a union of all scalar types 
    // including fixed length strings so
    // that in many cases we can avoid 
    // allocating another buffer
    // 
    union {
        dbr_string_t strval;
        dbr_short_t shrtval;
        dbr_short_t intval;
        dbr_float_t fltval;
        dbr_enum_t enmval;
        dbr_char_t charval;
        dbr_long_t longval;
        dbr_double_t doubleval;
    } dbrScalarValue;
    epicsEvent block;
    cacWriteNotify * pNotify;
    unsigned long maxValueSize;
    dbSubscriptionIO * isSubscription ();
    void expandValueBuf ( unsigned long newSize );
    friend void putNotifyCompletion ( putNotify *ppn );
	dbPutNotifyBlocker ( const dbPutNotifyBlocker & );
	dbPutNotifyBlocker & operator = ( const dbPutNotifyBlocker & );
    void * operator new ( size_t size );
    void operator delete ( void * );
};

inline void * dbPutNotifyBlocker::operator new ( size_t size, 
    tsFreeList < dbPutNotifyBlocker > & freeList )
{
    return freeList.allocate ( size );
}

inline void dbPutNotifyBlocker::operator delete ( void *pCadaver, 
    tsFreeList < dbPutNotifyBlocker > & freeList )
{
    freeList.release ( pCadaver );
}

#endif // ifndef dbPutNotifyBlockerh

