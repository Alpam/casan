/*
 * Example program for CASAN
 */

#include "casan.h"

#ifdef L2_ETH
    #include "l2-eth.h"

    l2addr *myaddr = new l2addr_eth ("00:01:02:03:04:05") ;
    l2net_eth l2 ;
    // MTU is less than 0.25 * (free memory in SRAM after initialization)
    #define	MTU		200
#endif
#ifdef L2_154
    #include "l2-154.h"

    l2addr *myaddr = new l2addr_154 ("45:67") ;
    l2net_154 l2 ;
    // #define	CHANNEL		25
    #define	CHANNEL		26
    #define	PANID		CONST16 (0xca, 0xfe)
    #define	MTU		0
#endif

#define	DEBUGINTERVAL	10
#define	SLAVEID		169

int tmp_sensor = A0 ;
int led = A2 ;

Casan *casan ;
Debug debug ;

uint8_t process_temp1 (Msg *in, Msg *out) 
{
    char payload [10] ;

    out->max_age (true, 0) ;		// answer is not cachable

    DBGLN1 (F ("process_temp")) ;

    int sensorValue = analogRead (tmp_sensor) ;
    snprintf (payload, 10, "%d", sensorValue) ;

    out->set_payload ((uint8_t *) payload,  strlen (payload)) ;


    return COAP_RETURN_CODE (2, 5) ;
}

uint8_t process_put (Msg *in, Msg *out) 
{
    out->max_age (true, 0) ;		// answer is not cachable

    DBGLN1 (F ("process_put")) ;
    out->set_payload (in->get_payload(), in->get_paylen()) ;
    return COAP_RETURN_CODE (2, 5) ;
}

uint8_t process_delete (Msg *in, Msg *out) 
{
    out->max_age (true, 0) ;		// answer is not cachable

    DBGLN1 (F ("process_delete")) ;
    out->set_payload (in->get_payload(), in->get_paylen()) ;
    return COAP_RETURN_CODE (2, 5) ;
}

uint8_t process_post (Msg *in, Msg *out) 
{
    out->max_age (true, 0) ;		// answer is not cachable

    DBGLN1 (F ("process_post")) ;
    out->set_payload (in->get_payload(), in->get_paylen()) ;
    return COAP_RETURN_CODE (2, 5) ;
}

uint8_t process_temp2 (Msg *in, Msg *out) 
{
    char payload [10] ;

    // out->max_age (true, 60) ;	// answer is cachable (default)

    DBGLN1 (F ("process_temp")) ;

    int sensorValue = analogRead (tmp_sensor) ;
    snprintf (payload, 10, "%d", sensorValue) ;

    out->set_payload ((uint8_t *) payload,  strlen (payload)) ;

    return COAP_RETURN_CODE (2, 5) ;
}

uint8_t process_led (Msg *in, Msg *out) 
{
    DBGLN1 (F ("process_led")) ;

    int n ;
    char *payload = (char*) in->get_payload () ;
    if (payload != NULL && sscanf ((const char *) payload, "val=%d", &n) == 1)
    {
	analogWrite (led, n) ;
    }

    return COAP_RETURN_CODE (2,5) ;
}

void setup () 
{
    pinMode (tmp_sensor, INPUT) ;     
    pinMode (led, OUTPUT) ;     

    Serial.begin (38400) ;
#ifdef L2_ETH
    l2.start (myaddr, false, ETH_TYPE) ;
#endif
#ifdef L2_154
    l2.start (myaddr, false, CHANNEL, PANID) ;
#endif
    casan = new Casan (&l2, MTU, SLAVEID) ;

    debug.start (DEBUGINTERVAL) ;

    /* definitions for a resource: name (in URL), title, rt for /.well-known */

    Resource *r1 = new Resource ("t1", "Desk temp", "celsius") ;
    r1->handler (COAP_CODE_GET, process_temp1) ;
    r1->handler (COAP_CODE_PUT, process_put) ;
    r1->handler (COAP_CODE_POST, process_post) ;
    r1->handler (COAP_CODE_DELETE, process_delete) ;
    casan->register_resource (r1) ;

    Resource *r2 = new Resource ("t2", "B201 temp", "celsius") ;
    r2->handler (COAP_CODE_GET, process_temp2) ;
    casan->register_resource (r2) ;

#if 0
    Resource *r3 = new Resource ("led", "Nice LED", "light") ;
    // Resource *r3 = new Resource ("led", "A beautiful LED", "light") ;
    r3->handler (COAP_CODE_GET, process_led) ;
    casan->register_resource (r3) ;
#endif

    casan->print_resources () ;
}

void loop () 
{
    debug.heartbeat () ;
    casan->loop () ;
}
