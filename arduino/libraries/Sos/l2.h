#ifndef	L2_H
#define	L2_H

/*
 * These classes are designed to virtualize specific L2 networks
 */

#include "defs.h"
#include "enum.h"

class l2addr
{
    public:
	virtual ~l2addr () {} ;
	virtual bool operator== (const l2addr &other) = 0 ;
	virtual bool operator!= (const l2addr &other) = 0 ;
	// virtual bool operator!= (const unsigned char *rawaddr) = 0 ;
	virtual void print (void) = 0 ;
} ;

class l2net {
    public:
	virtual ~l2net () {} ;
	virtual bool send (l2addr &dest, const uint8_t *data, size_t len) = 0 ;
	// the "recv" method copies the received packet in
	// the instance private variable (see _rbuf/_rbuflen below)
	virtual l2_recv_t recv (void) = 0 ;

	virtual l2addr *bcastaddr (void) = 0 ;		// global variable

	// get various informations from the currently receveid message
	virtual l2addr *get_src (void) = 0 ;		// get a new l2addr
	virtual l2addr *get_dst (void) = 0 ;		// get a new l2addr
	virtual uint8_t *get_payload (int offset) = 0 ;	// ptr in message
	virtual size_t get_paylen (void) = 0 ;

	size_t mtu (void) { return mtu_ ; }

    protected:
	// Note on MTU: this value does not include MAC header
	// thus, it is the maximum size of a SOS-level datagram
	size_t mtu_ ;		// must be initialized in derived classes
} ;

#endif
