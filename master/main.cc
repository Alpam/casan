#include <iostream>

#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define	NTAB(t)		(sizeof (t)/sizeof (t)[0])

#include "l2.h"
#include "l2eth.h"
#include "engine.h"

int main (int argc, char *argv [])
{
    l2net *l ;
    // l2addr *la ;
    l2addr_eth *sa ;			// slave address
    slave s ;				// slave
    engine e ;

    // start SOS engine machinery
    e.init () ;

    // start new interface
    l = new l2net_eth ;
    if (l->init ("eth0") == -1)
    {
	perror ("init") ;
	exit (1) ;
    }
    // register network
    e.start_net (l) ;
    std::cout << "eth0 initialized\n" ;

    // register new slave
    sa = new l2addr_eth ("90:a2:da:80:0a:d4") ;
    s.addr (sa) ;
    s.l2 (l) ;
    e.add_slave (&s) ;

    sleep (1000) ;

}
