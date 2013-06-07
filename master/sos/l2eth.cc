#include <iostream>
#include <cstring>

#include <sys/types.h>
#include <unistd.h>

#ifdef USE_PF_PACKET
#include <sys/socket.h>
#include <netpacket/packet.h>
#endif
#ifdef USE_PCAP
#include <pcap/pcap.h>
#endif

#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "global.h"

#include "l2.h"
#include "l2eth.h"

namespace sos {

/******************************************************************************
 * l2addr_eth methods
 */

// constructor
l2addr_eth::l2addr_eth ()
{
    addr_ = new byte [ETHADDRLEN] ;
}

// constructor
l2addr_eth::l2addr_eth (const char *a)
{
    int i = 0 ;
    byte b = 0 ;

    addr_ = new byte [ETHADDRLEN] ;

    while (*a != '\0' && i < ETHADDRLEN)
    {
	if (*a == ':')
	{
	    addr_ [i++] = b ;
	    b = 0 ;
	}
	else if (isxdigit (*a))
	{
	    byte x ;
	    char c ;

	    c = tolower (*a) ;
	    x = isdigit (c) ? (c - '0') : (c - 'a' + 10) ;
	    b = (b << 4) + x ;
	}
	else
	{
	    for (i = 0 ; i < ETHADDRLEN ; i++)
		addr_ [i] = 0 ;
	    break ;
	}
	a++ ;
    }
    if (i < ETHADDRLEN)
	addr_ [i] = b ;
}

l2addr_eth l2addr_eth_broadcast ("ff:ff:ff:ff:ff:ff") ;

// copy constructor
l2addr_eth::l2addr_eth (const l2addr_eth &x)
{
    addr_ = new byte [ETHADDRLEN] ;
    std::memcpy (addr_, x.addr_, ETHADDRLEN) ;
}

// assignment operator
l2addr_eth & l2addr_eth::operator = (const l2addr_eth &x)
{
    if (addr_ == x.addr_)
	return *this ;

    if (addr_ == NULL)
	addr_ = new byte [ETHADDRLEN] ;
    std::memcpy (addr_, x.addr_, ETHADDRLEN) ;
    return *this ;
}

// destructor
l2addr_eth::~l2addr_eth ()
{
    delete [] addr_ ;
}

/******************************************************************************
 * Dump address
 */

void l2addr_eth::print (std::ostream &os) const
{
    if (addr_)
    {
	for (int i = 0  ; i < ETHADDRLEN ; i++)
	{
	    if (i > 0)
		os << ":" ;
	    os << std::hex << addr_ [i] ;
	}
    }
    else os << "(null)" ;
}

/******************************************************************************
 * l2net_eth methods
 */

int l2addr_eth::operator== (const l2addr &other)
{
    l2addr_eth *oe = (l2addr_eth *) &other ;
    return std::memcmp (this->addr_, oe->addr_, ETHADDRLEN) == 0 ;
}

int l2addr_eth::operator!= (const l2addr &other)
{
    l2addr_eth *oe = (l2addr_eth *) &other ;
    return std::memcmp (this->addr_, oe->addr_, ETHADDRLEN) != 0 ;
}

int l2net_eth::init (const char *iface)
{
#ifdef USE_PF_PACKET
    struct sockaddr_ll sll ;

    mtu_ = ETHMTU ;
    maxlatency_ = ETHMAXLATENCY ;

    /* Get interface index */

    ifidx_ = if_nametoindex (iface) ;
    if (ifidx_ == 0)
	return -1 ;

    /* Open socket */

    fd_ = socket (PF_PACKET, SOCK_DGRAM, htons (ETHTYPE_SOS)) ;
    if (fd_ == -1)
	return -1 ;

    /* Bind the socket to the interface */

    std::memset (&sll, 0, sizeof sll) ;
    sll.sll_family = AF_PACKET ;
    sll.sll_protocol = htons (ETHTYPE_SOS) ;
    sll.sll_ifindex = ifidx_ ;
    if (bind (fd_, (struct sockaddr *) &sll, sizeof sll) == -1)
    {
	close (fd_) ;
	return -1 ;
    }

    return 0 ;
#endif
#ifdef USE_PCAP
    struct bpf_program *bpfp ;
    char buf [MAXBUF] ;

    fd_ = pcap_create (iface, errbuf_) ;
    if (fd_ == NULL)
	return -1 ;

    snprintf (buf, sizeof buf, "ether proto 0x%x", ETHTYPE_SOS) ;
    if (pcap_compile (fd_, bpfp, buf, 1, PCAP_NETMASK_UNKNOWN) != 0)
    {
	pcap_close (fd_) ;
	return -1 ;
    }

    if (pcap_setfilter (fd_, bpfp) != 0)
    {
	pcap_close (fd_) ;
	return -1 ;
    }
    pcap_freecode (bpfp) ;

    if (pcap_activate (fd_) != 0)
    {
	pcap_close (fd_) ;
	return -1 ;
    }

    return 0 ;
#endif
}

void l2net_eth::term (void)
{
#ifdef USE_PF_PACKET
    close (fd_) ;
#endif
#ifdef USE_PCAP
    pcap_close (fd_) ;
#endif
}

int l2net_eth::send (l2addr *daddr, void *data, int len)
{
#ifdef USE_PF_PACKET
    struct sockaddr_ll sll ;
    l2addr_eth *a = (l2addr_eth *) daddr ;
    int r ;

    std::memset (&sll, 0, sizeof sll) ;
    sll.sll_family = AF_PACKET ;
    sll.sll_protocol = htons (ETHTYPE_SOS) ;
    sll.sll_halen = ETHADDRLEN ;
    sll.sll_ifindex = ifidx_ ;
    std::memcpy (sll.sll_addr, a->addr_, ETHADDRLEN) ;

    r = sendto (fd_, data, len, 0, (struct sockaddr *) &sll, sizeof sll) ;
    return r ;
#endif
#ifdef USE_PCAP
    return 0 ;
#endif
}

int l2net_eth::bsend (void *data, int len)
{
    return send (&l2addr_eth_broadcast, data, len) ;
}

l2addr *l2net_eth::bcastaddr (void)
{
    return &l2addr_eth_broadcast ;
}

pktype_t l2net_eth::recv (l2addr **saddr, void *data, int *len)
{
#ifdef USE_PF_PACKET
    struct sockaddr_ll sll ;
    socklen_t ssll ;
    l2addr_eth **a = (l2addr_eth **) saddr ;
    int r ;
    pktype_t pktype ;

    *a = new l2addr_eth ;

    std::memset (&sll, 0, sizeof sll) ;
    sll.sll_family = AF_PACKET ;
    sll.sll_ifindex = ifidx_ ;
    sll.sll_protocol = htons (ETHTYPE_SOS) ;
    sll.sll_halen = ETHADDRLEN ;

    ssll = sizeof sll ;

    r = recvfrom (fd_, data, *len, 0, (struct sockaddr *) &sll, &ssll) ;
    if (r == -1)
    {
	pktype = PK_NONE ;
    }
    else
    {
	*len = r ;
	switch (sll.sll_pkttype)
	{
	    case PACKET_HOST :
		pktype = PK_ME ;
		break ;
	    case PACKET_BROADCAST :
	    case PACKET_MULTICAST :
		pktype = PK_BCAST ;
		break ;
	    case PACKET_OTHERHOST :
	    case PACKET_OUTGOING :
	    default :
		pktype = PK_NONE ;
		break ;
	}

	std::memcpy ((*a)->addr_, sll.sll_addr, ETHADDRLEN) ;
    }

    return pktype ;
#endif
#ifdef USE_PCAP
    struct pcap_pkthdr pkthdr ;
    const u_char *d ;
    pktype_t pktype ;

    d = pcap_next (fd_, &pkthdr) ;
    if (d == NULL)
    {
	pktype = PK_NONE ;
    }
    else
    {
	if (*len >= (int) pkthdr.caplen)
	    *len = (int) pkthdr.caplen ;
	std::memcpy (data, d, *len) ;

	pktype = PK_ME ;
    }
    return pktype ;
#endif
}

}					// end of namespace sos
