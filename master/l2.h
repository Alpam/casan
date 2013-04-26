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
	byte *addr ;
} ;

class l2net
{
    public:
	virtual int	  init (const char *iface) = 0 ;
	virtual void	  term (void) = 0 ;
	virtual int	  getfd (void) ;
	virtual int	  send (l2addr *daddr, void *data, int len) = 0 ;
	virtual int	  bsend (void *data, int len) = 0 ;
	virtual pktype_t  recv (l2addr **saddr, void *data, int *len) = 0 ;
    protected:
	int fd ;
} ;

#endif
