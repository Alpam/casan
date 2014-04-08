#include "sos.h"

#define	SOS_NAMESPACE1		".well-known"
#define	SOS_NAMESPACE2		"sos"
#define	SOS_HELLO		"hello=%ld"
#define	SOS_DISCOVER_SLAVEID	"slave=%ld"
#define	SOS_DISCOVER_MTU	"mtu=%d"
#define	SOS_ASSOC		"assoc=%ld"

static struct
{
    const char *path ;
    int len ;
} sos_namespace [] =
{
    {  SOS_NAMESPACE1, sizeof SOS_NAMESPACE1 - 1 },
    {  SOS_NAMESPACE2, sizeof SOS_NAMESPACE2 - 1 },
} ;

extern l2addr_eth l2addr_eth_broadcast ;

// TODO : set all variables
Sos::Sos (l2net *l2, long int uuid)
{
    master_ = l2->bcastaddr () ;
    reset_master () ;		// master_ becomes a broadcast

    // XXXX
    // ///////////////// l2_->set_master_addr (master_) ;
    current_message_id_ = 1 ;

    l2_ = l2 ;
    nttl_ = SOS_DEFAULT_TTL ;
    uuid_ = uuid ;

    rmanager_ = new Rmanager () ;
    retransmission_handler_ = new Retransmit (&master_) ;
    status_ = SL_COLDSTART ;

    current_time.cur () ;
}

void Sos::reset_master (void) 
{
    if (master_)
    {
	PRINT_DEBUG_STATIC ("OLD ADDR") ;
	((l2addr_eth *) master_)->print () ;
    }
    else
    {
	// Can't happen
	PRINT_DEBUG_STATIC ("\033[31mERROR : master not set yet ! \033[00m") ;
    }

    PRINT_DEBUG_STATIC ("\033[36mset master addr to broadcast\033[00m") ;
    *((l2addr_eth *) master_) = l2addr_eth_broadcast ;
    ((l2addr_eth *) master_)->print () ;
    hlid_ = 0 ;
}

void Sos::register_resource (Resource *r) 
{
    rmanager_->add_resource (r) ;
}

// TODO : we need to restart all the application, 
// delete all the history of the exchanges
void Sos::reset (void) 
{
    status_ = SL_COLDSTART ;
    current_message_id_ = 1 ;
    nttl_ = SOS_DEFAULT_TTL ;
    rmanager_->reset () ;
    retransmission_handler_->reset () ;
    reset_master () ;
}

void Sos::loop () 
{
    Msg in ;
    Msg out ;
    l2_recv_t ret ;
    uint8_t oldstatus ;

    retransmission_handler_->loop (*l2_) ; 	// check all retrans.
    current_time.cur () ;

    oldstatus = status_ ;
    switch (status_) 
    {
	case SL_COLDSTART :
	    mk_discover () ;
	    status_ = SL_WAITING_UNKNOWN ;
	    next_time_inc_ = SOS_DELAY ;
	    // next time we will send a register msg
	    next_register_ = current_time ;
	    next_register_.add (next_time_inc_) ;
	    break ;

	case SL_WAITING_UNKNOWN :
	    if ((ret = in.recv (*l2_)) != L2_RECV_EMPTY)
	    {
		// print_coap_ret_type (ret) ;
		if (ret == L2_RECV_RECV_OK) 
		{
		    Serial.println ("Received a msg") ;
		    retransmission_handler_->check_msg_received (in) ; 

		    // In this state, we only consider control messages
		    if (is_ctl_msg (in))
		    {
			Serial.println ("Received a CTL msg") ;
			if (is_hello (in)) 
			{
			    Serial.println ("Received a CTL HELLO msg") ;
			    in.get_src (master_) ;
			    current_message_id_ = in.get_id () + 1 ;
			    status_ = SL_WAITING_KNOWN ;

			    // ((l2addr_eth *) master_)->print () ;
			    next_time_inc_ = SOS_DELAY ;
			    next_register_ = current_time ;
			    next_register_.add (next_time_inc_) ;
			} 
			else if (is_associate (in)) 
			{
			    Serial.println ("Received a CTL ASSOC msg") ;
			    mk_ack_assoc (in, out) ;
			} 
			else 
			{
			    Serial.println (F ("\033[31mignored ctl msg\033[00m")) ;
			}
		    }
		}
	    }

	    if (next_register_ < current_time)
	    {
		mk_discover () ;

		// compute the next waiting time laps for a message
		next_time_inc_ = 
			(next_time_inc_ + SOS_DELAY_INCR) > SOS_DELAY_MAX ? 
			    SOS_DELAY_MAX : next_time_inc_ + SOS_DELAY_INCR ;
		next_register_ = current_time ;
		next_register_.add (next_time_inc_) ;
	    }
	    break ;

	case SL_WAITING_KNOWN :
	    if ((ret = in.recv (*l2_)) != L2_RECV_EMPTY)
	    {
		if (ret == L2_RECV_RECV_OK) 
		{
		    retransmission_handler_->check_msg_received (in) ; 
		    if (is_ctl_msg (in))
		    {
			if (is_hello (in)) 
			{
			    in.get_src (master_) ;
			    current_message_id_ = in.get_id () + 1 ;

			    next_time_inc_ = SOS_DELAY ;
			    next_register_ = current_time ;
			    next_register_.add (next_time_inc_) ;

			} 
			else if (is_associate (in)) 
			{
			    mk_ack_assoc (in, out) ;
			} 
			else 
			{
			    Serial.println (F ("\033[31mignored ctl msg\033[00m")) ;
			}
		    }
		}
	    }

	    // it is possible to don't be in waiting known status
	    if (status_ == SL_WAITING_KNOWN)
	    {
		// if we reach the max increment and the time is over
		// we enter the "waiting unknown" status
		if (next_time_inc_ == SOS_DELAY_MAX && 
				time::diff (next_register_, current_time) <= 0)
		{
		    reset_master () ;	// master_ becomes a broadcast

		    next_time_inc_ = SOS_DELAY ;
		    next_register_ = current_time ;
		    next_register_.add (next_time_inc_) ;

		    status_ = SL_WAITING_UNKNOWN ;
		}

		if (next_register_ < current_time)
		{
		    mk_discover () ;

		    next_time_inc_ = 
			    (next_time_inc_ + SOS_DELAY_INCR) > SOS_DELAY_MAX ? 
			    SOS_DELAY_MAX : next_time_inc_ + SOS_DELAY_INCR ;
		    next_register_ = current_time ;
		    next_register_.add (next_time_inc_) ;
		}
	    }

	    break ;

	case SL_RENEW :
	    if ((ret = in.recv (*l2_)) != L2_RECV_EMPTY)
	    {
		if (ret == L2_RECV_RECV_OK) 
		{
		    retransmission_handler_->check_msg_received (in) ; 
		    if (is_ctl_msg (in))
		    {
			if (is_hello (in)) 
			{
			    in.get_src (master_) ;
			    current_message_id_ = in.get_id () + 1 ;
			    status_ = SL_WAITING_KNOWN ;

			    next_time_inc_ = SOS_DELAY ;
			    next_register_ = current_time ;
			    next_register_.add (next_time_inc_) ;

			    mk_discover () ;
			} 
			else if (is_associate (in)) 
			{
			    mk_ack_assoc (in, out) ;
			} 
			else 
			{
			    Serial.println (F ("\033[31mignored ctl msg\033[00m")) ;
			}
		    }
		}

	    }

	    if (status_ == SL_RENEW)
	    {
		if ( next_register_ < current_time)
		{
		    next_time_inc_ = 
			    (next_time_inc_ + SOS_DELAY_INCR) > SOS_DELAY_MAX ? 
			    SOS_DELAY_MAX : next_time_inc_ + SOS_DELAY_INCR ;

		    next_register_ = current_time ;
		    next_register_.add (next_time_inc_) ;

		    mk_discover () ;
		}

		// we test if we have to go back in waiting unknown status 
		// = ttl timeout
		if (time::diff (ttl_timeout_, current_time) <= 0)
		{
		    reset_master () ;	// master_ becomes a broadcast

		    status_ = SL_WAITING_UNKNOWN ;
		    next_time_inc_ = SOS_DELAY ;
		    mk_discover () ;
		}
	    }

	    break ;

	case SL_RUNNING :	// TODO
	    if ((ret = in.recv (*l2_)) != L2_RECV_EMPTY)
	    {
		if (ret == L2_RECV_RECV_OK) 
		{
		    retransmission_handler_->check_msg_received (in) ; 
		    if (is_ctl_msg (in))
		    {
			if (is_hello (in)) 
			{
			    in.get_src (master_) ;
			    current_message_id_ = in.get_id () + 1 ;
			    status_ = SL_WAITING_KNOWN ;

			    next_time_inc_ = SOS_DELAY ;
			    next_register_ = current_time ;
			    next_register_.add (next_time_inc_) ;
			}
			else if (is_associate (in)) 
			{
			    mk_ack_assoc (in, out) ;
			} 
			else 
			{
			    Serial.println (F ("\033[31mignored ctl msg\033[00m")) ;
			}
		    }
		    else
		    {
			uint8_t ret = rmanager_->request_resource (in, out) ;
			if (ret > 0)
			{
			    Serial.println (F ("\033[31mThere is a problem with the request\033[00m")) ;
			}
			else
			{
			    Serial.println (F ("\033[36mWe sent the answer\033[00m")) ;
			    out.send (*l2_, *master_) ;
			}
		    }
		}
		else if (ret == L2_RECV_TRUNCATED)
		{
		    Serial.println (F ("\033[36mRequest too large\033[00m")) ;
		    out.set_type (COAP_TYPE_ACK) ;
		    out.set_id (in.get_id ()) ;
		    out.set_token (in.get_toklen (), in.get_token ()) ;
		    option o ;
		    o.optcode (option::MO_Size1) ;
		    o.optval (l2_->mtu ()) ;
		    out.push_option (o) ;
		    out.set_code (COAP_RETURN_CODE (4,13)) ;
		    out.send (*l2_, *master_) ;
		}
	    }

	    if (status_ == SL_RUNNING)
	    {
		// if current time <= ttl timeout /2
		if (time::diff (ttl_timeout_mid_, current_time) <= 0)
		{
		    mk_discover () ;
		    next_time_inc_ = SOS_DELAY ;
		    next_register_ = current_time ;
		    next_register_.add (next_time_inc_) ;
		    status_ = SL_RENEW ;
		}
	    }

	    //	deduplicate () ;
	    rmanager_->request_resource (in, out) ;

	    break ;

	default :
	    Serial.println (F ("Error : sos status not known")) ;
	    PRINT_DEBUG_DYNAMIC (status_) ;
	    break ;
    }

    if (oldstatus != status_)
    {
	Serial.print (F ("Status: ")) ;
	print_status (oldstatus) ;
	Serial.print (F ("->")) ;
	print_status (status_) ;
	Serial.println () ;
    }

    // delay (5) ;
}

void Sos::mk_ack_assoc (Msg &in, Msg &out)
{
    // we get the new master
    in.get_src (master_) ;

    // the current mid is the next after the id of the received msg
    current_message_id_ = in.get_id () + 1 ;

    // we are now in running mode
    status_ = SL_RUNNING ;

    // send back an acknowledgement message
    out.set_type (COAP_TYPE_ACK) ;
    out.set_code (COAP_RETURN_CODE (2,5)) ;
    out.set_id (in.get_id ()) ;
    // mk_ctl_msg (out) ; useless because it's an ACK

    // will get the resources and set them in the payload in the right format
    rmanager_->get_all_resources (out) ;

    // send the packet
    out.send (*l2_, *master_) ;

    // we received an assoc msg : the timer is renewed
    next_time_inc_ = SOS_DELAY ;

    current_time.cur () ;

    // the ttl timeout is set to current time + nttl_
    ttl_timeout_ = current_time ;
    ttl_timeout_.add (nttl_) ;

    // there is a mid-term ttl setted for passing in renew status
    ttl_timeout_mid_ = current_time ;
    ttl_timeout_mid_.add (nttl_ / 2) ;
}

/**********************
 * SOS control messages
 */

bool Sos::is_ctl_msg (Msg &m)
{
    int i = 0 ;

    m.reset_next_option () ;
    for (option *o = m.next_option () ; o != NULL ; o = m.next_option ())
    {
	if (o->optcode () == option::MO_Uri_Path)
	{
	    if (i >= NTAB (sos_namespace))
		return false ;
	    if (sos_namespace [i].len != o->optlen ())
		return false ;
	    if (memcmp (sos_namespace [i].path, (const void *) o->val (), o->optlen ()))
		return false ;
	    i++ ;
	}
    }
    m.reset_next_option () ;
    if (i != NTAB (sos_namespace))
	return false ;

    return true ;
}

void Sos::mk_ctl_msg (Msg &m)
{
    option path1 (option::MO_Uri_Path, (void*) sos_namespace [0].path,
						sos_namespace [0].len) ;
    option path2 (option::MO_Uri_Path, (void*) sos_namespace [1].path,
						sos_namespace [1].len) ;
    m.push_option (path1) ;
    m.push_option (path2) ;
}

void Sos::mk_discover () 
{
    Msg m ;
    char str [SOS_BUF_LEN] ;

    Serial.println (F ("Sending Discover")) ;

    m.set_id (current_message_id_++) ;
    m.set_type (COAP_TYPE_NON) ;
    m.set_code (COAP_CODE_POST) ;
    mk_ctl_msg (m) ;

    snprintf (str, SOS_BUF_LEN, SOS_DISCOVER_SLAVEID, uuid_) ;
    option o1 (option::MO_Uri_Query, str, strlen (str)) ;
    m.push_option (o1) ;

    snprintf (str, SOS_BUF_LEN, SOS_DISCOVER_MTU, l2_->mtu ()) ;
    option o2 (option::MO_Uri_Query, str, strlen (str)) ;
    m.push_option (o2) ;

    m.send (*l2_, *master_) ;
}

bool Sos::is_associate (Msg &m)
{
    bool found = false ;

    m.reset_next_option () ;
    if (m.get_type () == COAP_TYPE_CON && m.get_code () == COAP_CODE_POST)
    {
	for (option *o = m.next_option () ; o != NULL ; o = m.next_option ()) 
	{
	    if (o->optcode () == option::MO_Uri_Query)
	    {
		long int n ;

		// we benefit from the added nul byte at the end of val
		if (sscanf ((const char *) o->val (), SOS_ASSOC, &n) == 1)
		{
		    nttl_ = n * 1000 ;
		    PRINT_DEBUG_STATIC ("\033[31m TTL recv \033[00m ") ;
		    PRINT_DEBUG_DYNAMIC (nttl_) ;
		    found = true ;
		    // continue, just in case there are other query strings
		}
		else
		{
		    found = false ;
		    break ;
		}
	    }
	}
    }

    return found ;
}

// check if a hello msg is received & new
bool Sos::is_hello (Msg &m)
{
    bool found = false ;

    // a hello msg is NON POST
    if (m.get_type () == COAP_TYPE_NON && m.get_code () == COAP_CODE_POST)
    {
	m.reset_next_option () ;
	for (option *o = m.next_option () ; o != NULL ; o = m.next_option ()) 
	{
	    if (o->optcode () == option::MO_Uri_Query)
	    {
		long int n ;

		// we benefit from the added nul byte at the end of val
		if (sscanf ((const char *) o->val (), SOS_HELLO, &n) == 1)
		{
		    // we only consider a new hello msg if there is
		    // a new hlid number
		    if (hlid_ != n)
		    {
			    hlid_ = n ;
			    found = true ;
		    }

		}
	    }
	}
    }

    return found ;
}

// maybe for further implementation
void check_msg (Msg &in, Msg &out)
{
}

/*****************************************
 * useful display of pieces of information
 */

void Sos::print_coap_ret_type (l2_recv_t ret)
{
    switch (ret)
    {
	case L2_RECV_WRONG_DEST :
	    PRINT_DEBUG_STATIC ("L2_RECV_WRONG_DEST") ;
	    break ;
	case L2_RECV_WRONG_ETHTYPE :
	    PRINT_DEBUG_STATIC ("L2_RECV_WRONG_ETHTYPE") ;
	    break ;
	case L2_RECV_RECV_OK :
	    PRINT_DEBUG_STATIC ("L2_RECV_RECV_OK") ;
	    break ;
	default :
	    PRINT_DEBUG_STATIC ("ERROR RECV") ;
	    break ;
    }
}

void Sos::print_status (uint8_t status)
{
    switch (status)
    {
	case SL_COLDSTART :
	    Serial.print (F ("SL_COLDSTART")) ;
	    break ;
	case SL_WAITING_UNKNOWN :
	    Serial.print (F ("SL_WAITING_UNKNOWN")) ;
	    break ;
	case SL_WAITING_KNOWN :
	    Serial.print (F ("SL_WAITING_KNOWN")) ;
	    break ;
	case SL_RENEW :
	    Serial.print (F ("SL_RENEW")) ;
	    break ;
	case SL_RUNNING :
	    Serial.print (F ("SL_RUNNING")) ;
	    break ;
	default :
	    Serial.print (F ("???")) ;
	    Serial.print (status) ;
	    break ;
    }
}
