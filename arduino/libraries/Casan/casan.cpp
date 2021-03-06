/**
 * @file casan.cpp
 * @brief Casan class implementation
 */

#include "casan.h"

#define	CASAN_NAMESPACE1	".well-known"
#define	CASAN_NAMESPACE2	"casan"
#define	CASAN_HELLO		"hello=%ld"
#define	CASAN_DISCOVER_SLAVEID	"slave=%ld"
#define	CASAN_DISCOVER_MTU	"mtu=%ld"
#define	CASAN_ASSOC_TTL		"ttl=%ld"
#define	CASAN_ASSOC_MTU		CASAN_DISCOVER_MTU

#define	CASAN_BUF_LEN		50	// > sizeof hello=.../slave=..../etc


#define CASAN_RESOURCES_ALL	"resources"

static struct
{
    const char *path ;
    int len ;
} casan_namespace [] =
{
    {  CASAN_NAMESPACE1, sizeof CASAN_NAMESPACE1 - 1 },
    {  CASAN_NAMESPACE2, sizeof CASAN_NAMESPACE2 - 1 },
} ;

/******************************************************************************
Constructor and simili-destructor
******************************************************************************/

/**
 * @brief Constructor
 *
 * The constructor method needs an already initialized L2 network
 * object, and a slave-id.
 *
 * @param l2 pointer to an already initialized network object (l2net-*)
 * @param mtu slave MTU (including MAC header and trailer) or 0 for default
 * @param slaveid unique slave-id
 */

Casan::Casan (l2net *l2, int mtu, long int slaveid)
{
    memset (this, 0, sizeof *this) ;

    l2_ = l2 ;
    slaveid_ = slaveid ;

    curtime = 0 ;			// global variable
    sync_time (curtime) ;

    defmtu_ = l2->mtu () ;		// get default L2 MTU
    if (mtu > 0 && mtu < defmtu_)
	defmtu_ = mtu ;			// set a different default MTU
    reset_master () ;			// master_ is reset (broadcast addr, mtu)

    hlid_ = -1 ;
    curid_ = 1 ;

    retrans_.master (&master_) ;
    status_ = SL_COLDSTART ;
}

/*
 * @brief Reset CASAN engine
 *
 * This method is used to reset the CASAN engine.
 *
 * @bug as this method is not used, implementation is very rudimentary.
 *	One should restart all the engine, delete all exchange history, etc.
 */

void Casan::reset (void)
{
    status_ = SL_COLDSTART ;
    curid_ = 1 ;

    // remove resources from the list
    while (reslist_ != NULL)
    {
	reslist *r ;

	r = reslist_->next ;
	delete reslist_ ;
	reslist_ = r ;
    }

    retrans_.reset () ;
    reset_master () ;
}

/******************************************************************************
MTU handling
******************************************************************************/

/**
 * @brief Reset MTU value to user specified MTU
 *
 * This method resets the current MTU to the value specified by the
 * user (which is the default network MTU if supplied as 0).
 */

void Casan::reset_mtu (void)
{
    negociate_mtu (0) ;
}

/**
 * @brief Change MTU value for a negociated value
 *
 * This method sets the current MTU to a value negociated between
 * the slave and the master. Parameter is the MTU value announced
 * by the master in its Assoc message, or 0 to reset the MTU to the
 * default value (which is the value specified by the user or the
 * default network specific MTU).
 */

void Casan::negociate_mtu (int mtu)
{
    if (mtu > 0 && mtu <= defmtu_)
	curmtu_ = mtu ;
    else
	curmtu_ = defmtu_ ;		// reset MTU to default value
    l2_->mtu (curmtu_) ;		// notify L2 network
}

/******************************************************************************
Master handling
******************************************************************************/

/**
 * Reset master coordinates to an unknown master:
 * * address is null
 * * hello-id is -1 (i.e. unknown hello-id)
 * * current MTU is reset to default (user initialized) MTU
 */

void Casan::reset_master (void)
{
    if (master_ != NULL)
	delete master_ ;
    master_ = NULL ;
    hlid_ = -1 ;
    reset_mtu () ;			// reset MTU to default
    DBGLN1 (F ("Master reset to broadcast address and default MTU")) ;
}

/**
 * Does master address match the given address (which cannot be a
 * NULL pointer)?
 */

bool Casan::same_master (l2addr *a)
{
    return master_ != NULL && *a == *master_ ;
}

/**
 * Change master to a known master.
 * - address is taken from the current incoming message
 * - hello-id is given, may be -1 if value is currently not known
 * - mtu is given, may be -1 if current value must not be changed
 */

void Casan::change_master (long int hlid, int mtu)
{
    l2addr *newmaster ;

    newmaster = l2_->get_src () ;	// get a new address
    if (master_ != NULL)
    {
	if (*newmaster == *master_)
	{
	    if (hlid != -1)
		hlid_ = hlid ;
	    delete newmaster ;
	}
	else
	{
	    delete master_ ;
	    master_ = newmaster ;
	    hlid_ = hlid ;
	}
    }
    else
    {
	master_ = newmaster ;
	hlid_ = hlid ;
    }

    if (mtu != -1)
	negociate_mtu (mtu) ;

    DBG1 (F ("Master set to ")) ;
    master_->print () ;
    DBG1 (F (", helloid=")) ;
    DBG1 (hlid_) ;
    DBG1 (F (", mtu=")) ;
    DBG1 (curmtu_) ;
    DBGLN0 () ;
}

/******************************************************************************
Resource handling
******************************************************************************/

/**
 * @brief Register a resource to the CASAN engine
 *
 * This method is used to register a resource with the CASAN engine.
 * This resource will then be advertised with the `/.well-known/casan`
 * resource during the next association or with a specific request
 * from the master. Thus, registering a resource after an association
 * (when the slave returns an association answer containing the
 * `/.well-known/casan`) will not provoke a new association. One must
 * wait the next association renewal for the resource to be published
 * and thus known by the master.
 *
 * @param res Address of the resource to register
 */

void Casan::register_resource (Resource *res)
{
    reslist *newr, *prev, *cur ;

    /*
     * Register resource in last position of the list to respect
     * order provided by the application
     */

    newr = new reslist ;
    newr->res = res ;

    prev = NULL ;
    for (cur = reslist_ ; cur != NULL ; cur = cur->next)
	prev = cur ;
    if (prev != NULL)
	prev->next = newr ;
    else
	reslist_ = newr ;
    newr->next = NULL ;
}

/**
 * @brief Process an incoming message requesting for a resource
 *
 * This methods:
 * * analyze uri_path option to find the resource
 * * either give answer if this is the /resources URI
 * * or call the handler for user-defined resources
 * * or return 4.04 code
 * * pack the answer in the outgoing message
 *
 * This method is made public for testing purpose.
 *
 * @bug Only one level of path is allowed (i.e. /a, and not /a/b nor /a/b/c)
 *
 * @param in Incoming message
 * @param out Message which will be sent in return
 */

void Casan::process_request (Msg &in, Msg &out) 
{
    option *o ;
    bool rfound = false ;		// resource found

    in.reset_next_option () ;
    for (o = in.next_option () ; o != NULL ; o = in.next_option ())
    {
	if (o->optcode () == option::MO_Uri_Path)
	{
	    // request for all resources
	    if (o->optlen () == (int) (sizeof CASAN_RESOURCES_ALL - 1)
		&& memcmp (o->optval ((int *) 0), CASAN_RESOURCES_ALL, 
				    sizeof CASAN_RESOURCES_ALL - 1) == 0)
	    {
		rfound = true ;
		out.set_type (COAP_TYPE_ACK) ;
		out.set_id (in.get_id ()) ;
		out.set_token (in.get_token ()) ;
		out.set_code (COAP_CODE_OK) ;
		(void) get_well_known (out) ;
	    }
	    else
	    {
		Resource *res ;

		// we benefit from the added '\0' at the end of an option
		res = get_resource ((char *) o->optval ((int *) 0)) ;
		if (res != NULL)
		{
		    option *obs ;
		    uint32_t obsval ;

		    rfound = true ;

		    obs = in.search_option (option::MO_Observe) ;
		    if (obs != NULL)
			obsval = obs->optval () ;

		    if (obs != NULL && obsval == 0)
			res->observed (true, &in) ;
		    else
			res->observed (false, NULL) ;

		    out.set_type (COAP_TYPE_ACK) ;
		    out.set_id (in.get_id ()) ;
		    out.set_token (in.get_token ()) ;

		    if (obs != NULL && obsval == 0)
		    {
			option robs (option::MO_Observe, res->next_serial ()) ;
			out.push_option (robs) ;
		    }

		    request_resource (&in, &out, res) ;
		}
	    }
	    break ;
	}
    }

    if (! rfound)
    {
	out.set_type (COAP_TYPE_ACK) ;
	out.set_id (in.get_id ()) ;
	out.set_token (in.get_token ()) ;
	out.set_code (COAP_CODE_NOT_FOUND) ;
    }
}

/**
 * Build a response message
 *
 * Build a CASAN response message for an incoming request or when
 * an observed resource triggered an event. Part of the message
 * is already built (type, id, token, observe option), answer must
 * be completed by the application handler.
 *
 * @param pin pointer to incoming message or NULL
 * @param pout pointer to the output message being built
 * @param res addressed resource
 */

void Casan::request_resource (Msg *pin, Msg *pout, Resource *res)
{
    Resource::handler_t h ;
    uint8_t code ;
    coap_code_t op ;

    op = (pin == NULL) ?  COAP_CODE_GET : (coap_code_t) pin->get_code () ;
    h = res->handler (op) ;
    if (h == NULL)
    {
	code = COAP_CODE_BAD_REQUEST ;
    }
    else
    {
	// add Content Format option
	pout->content_format (false, option::cf_text_plain) ;
	code = (*h) (pin, pout) ;
    }
    pout->set_code (code) ;
}

/**
 * Check all observed resources in order to detect changes and
 * send appropriate observe message.
 *
 * @param out an output message
 */

void Casan::check_observed_resources (Msg &out)
{
    Resource *res ;
    reslist *rl ;

    for (rl = reslist_ ; rl != NULL ; rl = rl->next)
    {
	res = rl->res ;
	if (res->check_trigger ())
	{
	    DBG1 (F (B_BLUE "Observed resource '")) ;
	    DBG1 (rl->res->name ()) ;
	    DBG1 (F ("' triggered" C_RESET)) ;
	    DBGLN0 () ;

	    out.reset () ;

	    out.set_type (COAP_TYPE_ACK) ;
	    out.set_token (res->get_token ()) ;
	    out.set_id (curid_++) ;
	    out.set_code (COAP_CODE_OK) ;

	    option obs (option::MO_Observe, res->next_serial ()) ;
	    out.push_option (obs) ;

	    request_resource (NULL, &out, res) ;

	    out.send (*master_) ;
	}
    }
}

/**
 * Prepare the payload for an assoc answer message (answer to the
 *	CON POST /.well-known/casan ? assoc=<sttl>
 * message).
 *
 * The answer will have a payload similar to:
 *	</temp>;
 *		title="the temp";
 *		rt="Temp"
 *              ,
 *      </light>;
 *		title="Luminosity";
 *		rt="light-lux"
 * (with the newlines removed)
 *
 * @return true if message is succesfully built (enough space)
 */

bool Casan::get_well_known (Msg &out) 
{
    char *buf ;
    size_t size ;
    reslist *rl ;
    size_t avail ;
    bool reset ;

    reset = false ;
    out.content_format (reset, option::cf_text_plain) ;

    avail = out.avail_space () ;
    buf = (char *) malloc (avail) ;

    size = 0 ;
    for (rl = reslist_ ; rl != NULL ; rl = rl->next) 
    {
	int len ;

	if (size > 0)			// separator "," between resources
	{
	    if (size + 2 < avail)
	    {
		buf [size++] = ',' ;
		buf [size] = '\0' ;
	    }
	    else break ;		// too large
	}

	len = rl->res->well_known (buf + size, avail - size) ;
	if (len == -1)
	    break ;

	size += len - 1 ;		// exclude '\0'
    }

    out.set_payload ((uint8_t *) buf, size) ;
    free (buf) ;


    /*
     * Did all resources fitted in the message, or do we left the loop
     * before its term?
     */

    if (rl != NULL)
    {
	DBG1 (F (B_RED "Resource '")) ;
	DBG1 (rl->res->name ()) ;
	DBG1 (F ("' do not fit in buffer of ")) ;
	DBG1 (avail) ;
	DBGLN1 (F (" bytes" C_RESET)) ;
    }

    return rl == NULL ;			// true if all res are in the message
}

/**
 * Find a particular resource by its name
 */

Resource *Casan::get_resource (const char *name)
{
    reslist *rl ;

    for (rl = reslist_ ; rl != NULL ; rl = rl->next)
    if (strcmp (name, rl->res->name ()) == 0)
	break ;
    return rl != NULL ? rl->res : NULL ;
}

/******************************************************************************
Main CASAN loop
******************************************************************************/

/**
 * @brief Main CASAN loop
 *
 * This method must be called regularly (typically in the loop function
 * of the Arduino framework) in order to process CASAN events.
 */

void Casan::loop ()
{
    Msg in (l2_) ;
    Msg out (l2_) ;
    l2net::l2_recv_t ret ;
    uint8_t oldstatus ;
    long int hlid ;
    l2addr *srcaddr ;
    int mtu ;				// mtu announced by master in assoc msg

    oldstatus = status_ ;		// keep old value for debug display
    sync_time (curtime) ;		// get current time
    retrans_.loop (*l2_, curtime) ;	// check needed retransmissions

    srcaddr = NULL ;

    ret = in.recv () ;			// get received message
    if (ret == l2net::RECV_OK)
	srcaddr = l2_->get_src () ;	// get a new address

    switch (status_)
    {
	case SL_COLDSTART :
	    send_discover (out) ;
	    twait_.init (curtime) ;
	    status_ = SL_WAITING_UNKNOWN ;
	    break ;

	case SL_WAITING_UNKNOWN :
	    if (ret == l2net::RECV_OK)
	    {
		retrans_.check_msg_received (in) ;

		if (is_ctl_msg (in))
		{
		    if (is_hello (in, hlid))
		    {
			DBGLN1 (F ("Received a CTL HELLO msg")) ;
			change_master (hlid, -1) ;	// don't change mtu
			twait_.init (curtime) ;
			status_ = SL_WAITING_KNOWN ;
		    }
		    else if (is_assoc (in, sttl_, mtu))
		    {
			DBGLN1 (F ("Received a CTL ASSOC msg")) ;
			change_master (-1, mtu) ;	// "unknown" hlid
			send_assoc_answer (in, out) ;
			trenew_.init (curtime, sttl_) ;
			status_ = SL_RUNNING ;
		    }
		    else DBGLN1 (F (RED ("Unkwnon CTL"))) ;
		}
	    }

	    if (status_ == SL_WAITING_UNKNOWN && twait_.next (curtime))
		send_discover (out) ;

	    break ;

	case SL_WAITING_KNOWN :
	    if (ret == l2net::RECV_OK)
	    {
		retrans_.check_msg_received (in) ;

		if (is_ctl_msg (in))
		{
		    if (is_hello (in, hlid))
		    {
			DBGLN1 (F ("Received a CTL HELLO msg")) ;
			change_master (hlid, -1) ;	// don't change mtu
		    }
		    else if (is_assoc (in, sttl_, mtu))
		    {
			DBGLN1 (F ("Received a CTL ASSOC msg")) ;
			change_master (-1, mtu) ;	// unknown hlid
			send_assoc_answer (in, out) ;
			trenew_.init (curtime, sttl_) ;
			status_ = SL_RUNNING ;
		    }
		    else DBGLN1 (F (RED ("Unkwnon CTL"))) ;
		}
	    }

	    if (status_ == SL_WAITING_KNOWN)
	    {
		if (twait_.expired (curtime))
		{
		    reset_master () ;		// master_ is no longer known
		    send_discover (out) ;
		    twait_.init (curtime) ;	// reset timer
		    status_ = SL_WAITING_UNKNOWN ;
		}
		else if (twait_.next (curtime))
		{
		    send_discover (out) ;
		}
	    }

	    break ;

	case SL_RUNNING :
	case SL_RENEW :
	    if (ret == l2net::RECV_OK)
	    {
		retrans_.check_msg_received (in) ;

		if (is_ctl_msg (in))
		{
		    if (is_hello (in, hlid))
		    {
			DBGLN1 (F ("Received a CTL HELLO msg")) ;
			if (! same_master (srcaddr) || hlid != hlid_)
			{
			    int oldhlid = hlid_ ;

			    change_master (hlid, 0) ;	// reset mtu
			    if (oldhlid != -1)
			    {
				twait_.init (curtime) ;
				status_ = SL_WAITING_KNOWN ;
			    }
			}
		    }
		    else if (is_assoc (in, sttl_, mtu))
		    {
			DBGLN1 (F ("Received a CTL ASSOC msg")) ;
			if (same_master (srcaddr))
			{
			    negociate_mtu (mtu) ;
			    send_assoc_answer (in, out) ;
			    trenew_.init (curtime, sttl_) ;
			    status_ = SL_RUNNING ;
			}
		    }
		    else DBGLN1 (F (RED ("Unkwnon CTL"))) ;
		}
		else		// request for a normal resource
		{
		    // deduplicate () ;
		    process_request (in, out) ;
		    out.send (*master_) ;
		}
	    }
	    else if (ret == l2net::RECV_TRUNCATED)
	    {
		DBGLN1 (F (RED ("Request too large"))) ;
		out.set_type (COAP_TYPE_ACK) ;
		out.set_id (in.get_id ()) ;
		out.set_token (in.get_token ()) ;
		option o (option::MO_Size1, l2_->mtu ()) ;
		out.push_option (o) ;
		out.set_code (COAP_CODE_TOO_LARGE) ;
		out.send (*master_) ;
	    }

	    check_observed_resources (out) ;

	    if (status_ == SL_RUNNING && trenew_.renew (curtime))
	    {
		send_discover (out) ;
		status_ = SL_RENEW ;
	    }

	    if (status_ == SL_RENEW && trenew_.next (curtime))
	    {
		send_discover (out) ;
	    }

	    if (status_ == SL_RENEW && trenew_.expired (curtime))
	    {
		reset_master () ;	// master_ is no longer known
		send_discover (out) ;
		twait_.init (curtime) ;	// reset timer
		status_ = SL_WAITING_UNKNOWN ;
	    }

	    break ;

	default :
	    DBGLN1 (F ("Error : casan status not known")) ;
	    DBGLN1 (status_) ;
	    break ;
    }

    if (oldstatus != status_)
    {
	DBG1 (F ("Status: " C_GREEN)) ;
	print_status (oldstatus) ;
	DBG1 (F (C_RESET " -> " C_GREEN)) ;
	print_status (status_) ;
	DBGLN1 (F (C_RESET)) ;
    }

    if (srcaddr != NULL)
	delete srcaddr ;
}

/******************************************************************************
Recognize control messages
******************************************************************************/

/**
 * Is the incoming message an CASAN control message?
 * Just verify if Uri_Path options match the casan_namespace [] array
 * in the right order
 */

bool Casan::is_ctl_msg (Msg &m)
{
    int i = 0 ;

    m.reset_next_option () ;
    for (option *o = m.next_option () ; o != NULL ; o = m.next_option ())
    {
	if (o->optcode () == option::MO_Uri_Path)
	{
	    if (i >= NTAB (casan_namespace))
		return false ;
	    if (casan_namespace [i].len != o->optlen ())
		return false ;
	    if (memcmp (casan_namespace [i].path, o->optval ((int *) 0), o->optlen ()))
		return false ;
	    i++ ;
	}
    }
    m.reset_next_option () ;
    if (i != NTAB (casan_namespace))
	return false ;

    return true ;
}

/**
 * Check if the control message is a Hello message from the master
 * and returns the contained hello-id
 */

bool Casan::is_hello (Msg &m, long int &hlid)
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
		// we benefit from the added nul byte at the end of val
		if (sscanf ((const char *) o->optval ((int *) 0), CASAN_HELLO, &hlid) == 1)
		    found = true ;
	    }
	}
    }

    return found ;
}

/**
 * Check if the control message is an Assoc message from the master
 * and returns the contained slave-ttl
 */

bool Casan::is_assoc (Msg &m, time_t &sttl, int &mtu)
{
    bool found_ttl = false ;
    bool found_mtu = false ;

    if (m.get_type () == COAP_TYPE_CON && m.get_code () == COAP_CODE_POST)
    {
	m.reset_next_option () ;
	for (option *o = m.next_option () ; o != NULL ; o = m.next_option ())
	{
	    if (o->optcode () == option::MO_Uri_Query)
	    {
		long int n ;		// sscanf "%ld" waits for a long int

		// we benefit from the added nul byte at the end of val
		if (sscanf ((const char *) o->optval ((int *) 0), CASAN_ASSOC_TTL, &n) == 1)
		{
		    DBG1 (BLUE ("TTL recv: ")) ;
		    DBG1 (n) ;
		    DBGLN0 () ;
		    sttl = ((time_t) n) * 1000 ;
		    found_ttl = true ;
		    // continue, just in case there are other query strings
		}
		else if (sscanf ((const char *) o->optval ((int *) 0), CASAN_ASSOC_MTU, &n) == 1)
		{
		    DBG1 (BLUE ("MTU recv: ")) ;
		    DBG1 (n) ;
		    DBGLN0 () ;
		    mtu = n ;
		    found_mtu = true ;
		    // continue, just in case there are other query strings
		}
		else break ;
	    }
	}
    }

    return found_ttl && found_mtu ;
}

/******************************************************************************
Send control messages
******************************************************************************/

/**
 * Initialize an "empty" control message
 * Just add the Uri_Path options from the casan_namespace [] array
 */

void Casan::mk_ctl_msg (Msg &out)
{
    int i ;

    for (i = 0 ; i < NTAB (casan_namespace) ; i++)
    {
	option path (option::MO_Uri_Path, (void *) casan_namespace [i].path,
					casan_namespace [i].len) ;
	out.push_option (path) ;
    }
}

/**
 * Send a discover message
 */

void Casan::send_discover (Msg &out)
{
    char tmpstr [CASAN_BUF_LEN] ;
    l2addr *dest ;

    DBGLN1 (F ("Sending Discover")) ;

    out.reset () ;
    out.set_id (curid_++) ;
    out.set_type (COAP_TYPE_NON) ;
    out.set_code (COAP_CODE_POST) ;
    mk_ctl_msg (out) ;

    snprintf (tmpstr, sizeof tmpstr, CASAN_DISCOVER_SLAVEID, slaveid_) ;
    option o1 (option::MO_Uri_Query, tmpstr, strlen (tmpstr)) ;
    out.push_option (o1) ;

    snprintf (tmpstr, sizeof tmpstr, CASAN_DISCOVER_MTU, (long int) defmtu_) ;
    option o2 (option::MO_Uri_Query, tmpstr, strlen (tmpstr)) ;
    out.push_option (o2) ;

    dest = (master_ != NULL) ? master_ : l2_->bcastaddr () ;

    out.send (*dest) ;
}

/**
 * Send the answer to an association message
 * (the association task itself is handled in the CASAN main loop)
 */

void Casan::send_assoc_answer (Msg &in, Msg &out)
{
    l2addr *dest ;

    dest = l2_->get_src () ;

    // send back an acknowledgement message
    out.set_type (COAP_TYPE_ACK) ;
    out.set_code (COAP_CODE_OK) ;
    out.set_id (in.get_id ()) ;

    // will get the resources and set them in the payload in the right format
    (void) get_well_known (out) ;

    // send the packet
    if (! out.send (*dest))
	DBGLN1 (F (RED ("Cannot send the assoc answer message"))) ;

    delete dest ;
}

/******************************************************************************
Debug methods
******************************************************************************/

/**
 * @brief Print the list of resources, used for debug purpose
 */

void Casan::print_resources (void)
{
    reslist *rl ;

    for (rl = reslist_ ; rl != NULL ; rl = rl->next)
	rl->res->print () ;
}

void Casan::print_coap_ret_type (l2net::l2_recv_t ret)
{
    switch (ret)
    {
	case l2net::RECV_WRONG_DEST :
	    DBGLN1 (F ("RECV_WRONG_DEST")) ;
	    break ;
	case l2net::RECV_WRONG_TYPE :
	    DBGLN1 (F ("RECV_WRONG_TYPE")) ;
	    break ;
	case l2net::RECV_OK :
	    DBGLN1 (F ("RECV_OK")) ;
	    break ;
	default :
	    DBGLN1 (F ("ERROR RECV")) ;
	    break ;
    }
}

void Casan::print_status (uint8_t status)
{
    switch (status)
    {
	case SL_COLDSTART :
	    DBG1 (F ("SL_COLDSTART")) ;
	    break ;
	case SL_WAITING_UNKNOWN :
	    DBG1 (F ("SL_WAITING_UNKNOWN")) ;
	    break ;
	case SL_WAITING_KNOWN :
	    DBG1 (F ("SL_WAITING_KNOWN")) ;
	    break ;
	case SL_RENEW :
	    DBG1 (F ("SL_RENEW")) ;
	    break ;
	case SL_RUNNING :
	    DBG1 (F ("SL_RUNNING")) ;
	    break ;
	default :
	    DBG1 (F ("???")) ;
	    DBG1 (status) ;
	    break ;
    }
}
