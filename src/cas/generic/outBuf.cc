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
 *      $Id$
 *
 *      Author  Jeffrey O. Hill
 *              johill@lanl.gov
 *              505 665 1831
 */

#include "server.h"
#include "outBufIL.h" // outBuf in line func
#include "osiWireFormat.h"

//
// outBuf::outBuf()
//
outBuf::outBuf ( outBufClient & clientIn, 
                clientBufMemoryManager & memMgrIn ) : 
    client ( clientIn ), memMgr ( memMgrIn ), bufSize ( 0 ), 
        stack ( 0u ), ctxRecursCount ( 0u )
{
    casBufferParm bufParm = memMgr.allocate ( 1 );
    this->pBuf = bufParm.pBuf;
    this->bufSize = bufParm.bufSize;
    memset ( this->pBuf, '\0', this->bufSize );
}

//
// outBuf::~outBuf()
//
outBuf::~outBuf()
{
    assert ( this->ctxRecursCount == 0 );
    memMgr.release ( this->pBuf, this->bufSize );
}

//
// outBuf::allocRawMsg ()
//
caStatus outBuf::allocRawMsg ( bufSizeT msgsize, void **ppMsg )
{
    bufSizeT stackNeeded;

    msgsize = CA_MESSAGE_ALIGN ( msgsize );

    this->mutex.lock ();

    if ( msgsize > this->bufSize ) {
        this->mutex.unlock ();
        return S_cas_hugeRequest;
    }

    stackNeeded = this->bufSize - msgsize;

    if ( this->stack > stackNeeded ) {
		
        //
        // Try to flush the output queue
        //
        this->flush ( this->stack-stackNeeded );

        //
        // If this failed then the fd is nonblocking 
        // and we will let select() take care of it
        //
        if ( this->stack > stackNeeded ) {
            this->mutex.unlock();
            this->client.sendBlockSignal();
            return S_cas_sendBlocked;
        }
    }

    //
    // it fits so commitMsg() will move the stack pointer forward
    //
    *ppMsg = (void *) &this->pBuf[this->stack];

    return S_cas_success;
}

caStatus outBuf::copyInHeader ( ca_uint16_t response, ca_uint32_t payloadSize,
    ca_uint16_t dataType, ca_uint32_t nElem, ca_uint32_t cid, 
    ca_uint32_t responseSpecific, void **ppPayload )
{
    ca_uint32_t alignedPayloadSize = CA_MESSAGE_ALIGN ( payloadSize );
    ca_uint32_t hdrSize;

    if ( alignedPayloadSize < 0xffff && nElem < 0xffff ) {
        hdrSize = sizeof ( caHdr );
    }
    else {
        hdrSize = sizeof ( caHdr ) + 2 * sizeof (ca_uint32_t);
    }

    caHdr * pHdr;
    caStatus status = this->allocRawMsg ( hdrSize + alignedPayloadSize,
                                          reinterpret_cast < void ** > ( & pHdr ) );
    if ( status != S_cas_success ) {
        if ( status == S_cas_hugeRequest ) {
            this->expandBuffer ();
            status = this->allocRawMsg ( hdrSize + alignedPayloadSize, 
                                         reinterpret_cast < void ** > ( & pHdr ) );
            if ( status != S_cas_success ) {
                return status;
            }
        }
        else {
            return status;
        }
    }

    pHdr->m_cmmd = epicsHTON16 ( response );
    pHdr->m_dataType = epicsHTON16 ( dataType );
    pHdr->m_cid = epicsHTON32 ( cid );
    pHdr->m_available = epicsHTON32 ( responseSpecific );
    char * pPayload;
    if ( hdrSize == sizeof ( caHdr ) ) {
        pHdr->m_postsize =
            static_cast < ca_uint16_t > ( epicsHTON32 ( alignedPayloadSize ) );
        pHdr->m_count = static_cast < ca_uint16_t > ( epicsHTON32 ( nElem ) );
        pPayload = reinterpret_cast < char * > ( pHdr + 1 );
    }
    else {
        pHdr->m_postsize = epicsHTON16 ( 0xffff );
        pHdr->m_count = epicsHTON16 ( 0 );
        ca_uint32_t * pLW = reinterpret_cast <ca_uint32_t *> ( pHdr + 1 );
        pLW[0] = epicsHTON32 ( alignedPayloadSize );
        pLW[1] = epicsHTON32 ( nElem );
        pPayload = reinterpret_cast < char * > ( pLW + 2 );
    }

    /* zero out pad bytes */
    if ( alignedPayloadSize > payloadSize ) {
        memset ( pPayload + payloadSize, '\0', 
                 alignedPayloadSize - payloadSize );
    }

    if ( ppPayload ) {
        *ppPayload = pPayload;
    }

    return status;
}

//
// outBuf::commitMsg ()
//
void outBuf::commitMsg ()
{
    ca_uint32_t payloadSize;
    ca_uint32_t elementCount;
    ca_uint32_t hdrSize;

    const caHdr * mp = ( caHdr * ) & this->pBuf[ this->stack ];
    if ( mp->m_postsize == 0xffff || mp->m_count == 0xffff ) {
        const ca_uint32_t *pLW = reinterpret_cast <const ca_uint32_t *> ( mp + 1 );
        payloadSize = epicsNTOH32 ( pLW[0] );
        elementCount = epicsNTOH32 ( pLW[1] );
        hdrSize = sizeof ( caHdr ) + 2 * sizeof ( ca_uint32_t );
    }
    else {
        payloadSize = epicsNTOH16 ( mp->m_postsize );
        elementCount = epicsNTOH16 ( mp->m_count );
        hdrSize = sizeof ( caHdr );
    }

    this->commitRawMsg ( hdrSize + payloadSize );

    if ( this->client.getDebugLevel() ) {
        errlogPrintf (
            "CAS Response => cmd=%d id=%x typ=%d cnt=%d psz=%d avail=%x outBuf ptr=%p \n",
            epicsNTOH16 ( mp->m_cmmd ), epicsNTOH32 ( mp->m_cid ), 
            epicsNTOH16 ( mp->m_dataType ), elementCount, payloadSize,
            epicsNTOH32 ( mp->m_available ), static_cast <const void *> ( mp ) );
    }
}

//
// outBuf::commitMsg ()
//
void outBuf::commitMsg ( ca_uint32_t reducedPayloadSize )
{
    caHdr * mp = ( caHdr * ) & this->pBuf[ this->stack ];
    reducedPayloadSize = CA_MESSAGE_ALIGN ( reducedPayloadSize );
    if ( mp->m_postsize == 0xffff || mp->m_count == 0xffff ) {
        ca_uint32_t *pLW = reinterpret_cast <ca_uint32_t *> ( mp + 1 );
        assert ( reducedPayloadSize <= epicsNTOH32 ( pLW[0] ) );
        pLW[0] = epicsHTON32 ( reducedPayloadSize );
    }
    else {
        assert ( reducedPayloadSize <= epicsNTOH16 ( mp->m_postsize ) );
        mp->m_postsize = epicsHTON16 ( static_cast <ca_uint16_t> ( reducedPayloadSize ) );
    }
    this->commitMsg ();
}

//
// outBuf::flush ()
//
outBufClient::flushCondition outBuf::flush ( bufSizeT spaceRequired )
{
    bufSizeT nBytes;
    bufSizeT nBytesRequired;
    outBufClient::flushCondition cond;

    epicsGuard < epicsMutex > guard ( this->mutex );

    if ( this->ctxRecursCount > 0 ) {
        return outBufClient::flushNone;
    }

    if ( spaceRequired > this->bufSize ) {
        nBytesRequired = this->stack;
    }
    else {
        bufSizeT stackPermitted;

        stackPermitted = this->bufSize - spaceRequired;
        if ( this->stack > stackPermitted ) {
            nBytesRequired = this->stack - stackPermitted;
        }
        else {
            nBytesRequired = 0u;
        }
    }

    cond = this->client.xSend ( this->pBuf, this->stack, 
                                nBytesRequired, nBytes );
    if ( cond == outBufClient::flushProgress ) {
        bufSizeT len;

        if ( nBytes >= this->stack ) {
            this->stack = 0u;	
        }
        else {
            len = this->stack-nBytes;
            //
            // memmove() is ok with overlapping buffers
            //
            memmove ( this->pBuf, &this->pBuf[nBytes], len );
            this->stack = len;
        }

        if ( this->client.getDebugLevel () > 2u ) {
            char buf[64];
            this->client.hostName ( buf, sizeof ( buf ) );
            errlogPrintf ( "CAS: Sent a %d byte reply to %s\n",
                           nBytes, buf );
        }
    }
    return cond;
}

//
// outBuf::pushCtx ()
//
const outBufCtx outBuf::pushCtx ( bufSizeT headerSize, // X aCC 361
                                 bufSizeT maxBodySize,
                                 void *&pHeader ) 
{
    bufSizeT totalSize = headerSize + maxBodySize;
    caStatus status;

    status = this->allocRawMsg ( totalSize, & pHeader );
    if ( status != S_cas_success ) {
        return outBufCtx ();
    }
    else if ( this->ctxRecursCount >= UINT_MAX ) {
        return outBufCtx ();
    }
    else {
        outBufCtx result ( *this );
        this->pBuf = this->pBuf + this->stack + headerSize;
        this->stack = 0;
        this->bufSize = maxBodySize;
        this->ctxRecursCount++;
        return result;
    }
}

//
// outBuf::popCtx ()
//
bufSizeT outBuf::popCtx (const outBufCtx &ctx) // X aCC 361
{
    if (ctx.stat==outBufCtx::pushCtxSuccess) {
        bufSizeT bytesAdded = this->stack;
        this->pBuf = ctx.pBuf;
        this->bufSize = ctx.bufSize;
        this->stack = ctx.stack;
        assert (this->ctxRecursCount>0u);
        this->ctxRecursCount--;
        return bytesAdded;
    }
    else {
        return 0;
    }
}

//
// outBuf::show (unsigned level)
//
void outBuf::show (unsigned level) const
{
    if ( level > 1u ) {
        printf("\tUndelivered response bytes = %d\n", this->bytesPresent());
    }
}

void outBuf::expandBuffer ()
{
    bufSizeT max = this->memMgr.maxSize();
    if ( this->bufSize < max  ) {
        casBufferParm bufParm = this->memMgr.allocate ( max );
        memcpy ( bufParm.pBuf, this->pBuf, this->stack );
        this->memMgr.release ( this->pBuf, this->bufSize );
        this->pBuf = bufParm.pBuf;
        this->bufSize = bufParm.bufSize;
    }
}


