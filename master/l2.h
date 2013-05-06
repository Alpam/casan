#ifndef	SOS_L2_H
#define	SOS_L2_H

#include "sos.h"

class l2addr
{
    public:
	virtual ~l2addr () {} ;
	virtual int	  operator== (const l2addr &other) = 0 ;
	virtual int	  operator!= (const l2addr &other) = 0 ;
    protected:
	byte *addr_ ;
} ;

class l2net
{
    public:
	virtual int	  init (const char *iface) = 0 ;
	virtual void	  term (void) = 0 ;
	virtual int	  send (l2addr *daddr, void *data, int len) = 0 ;
	virtual int	  bsend (void *data, int len) = 0 ;
	virtual pktype_t  recv (l2addr **saddr, void *data, int *len) = 0 ;
	virtual l2addr *  bcastaddr (void) = 0 ;
	int mtu (void) ;
	int maxlatency (void) ;
    protected:
	int mtu_ ;			// initialized in the init method
	int maxlatency_ ;		// initialized in the init method
} ;

#endif
