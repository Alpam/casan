/**
 * @file casan.h
 * @brief Casan class interface
 */

#ifndef __CASAN_H__
#define __CASAN_H__

#include "resource.h"		// => msg.h => l2.h + option.h
#include "retrans.h"		// => time.h
#include "debug.h"		// for user comfort

#define	COAP_CODE_OK		COAP_RETURN_CODE (2, 5)
#define	COAP_CODE_BAD_REQUEST	COAP_RETURN_CODE (4, 0)
#define	COAP_CODE_NOT_FOUND	COAP_RETURN_CODE (4, 4)
#define	COAP_CODE_TOO_LARGE	COAP_RETURN_CODE (4,13)


/**
 * @brief CASAN engine
 *
 * This is the main CASAN engine. It basically provides initialization
 * methods (to be called in the `setup` application function) and a
 * `loop` method (to be called in the `loop` application function).
 *
 * There must be one and only one instance of this class, which
 * must be created in the application.
 * As this class relies on a separate library (L2-*) to provide
 * network access, a specific network object must be given to the
 * Casan object during creation.
 *
 * To summarize initialization, the application `setup` function must:
 * * create an object of appropriate class `l2net-xxx`
 * * initialize it with ad-hoc * parameters (such as Ethernet type for
 *	`l2net-eth` or channel id for `l2net-154` for example)
 * * next, the Casan object may be created with a pointer to the just
 *	created `l2net-xxx` object.
 * * resources must then be created (with some attributes such as
 *	a title, a name, etc.) and one or more handlers to answer
 *	requests for GET, POST, etc.
 * * at last, resources are registered to the Casan engine
 *
 * The application `loop` function must then just call the
 * `Casan::loop` method. It is advised to use the `Debug` class
 * in order to monitor available memory and detect memory leaks.
 *
 * @bug Current limitations:
 * * partial support for retransmission
 * * this class supports at most one master on the current L2
 *	network. If there are more than one master, behaviour is not
 *	guaranteed.
 * * no support for master pairing
 * * no support for DTLS cryptography
 * * no support for block transfer
 * * no support for resource observation
 */

class Casan {
    public:
	Casan (l2net *l2, int mtu, long int slaveid) ;
	~Casan () ;

	void reset (void) ;
	void loop () ;

	void register_resource (Resource *res) ;
	void print_resources (void) ;

	// private methods which are made public for test programs
	void process_request (Msg &in, Msg &out) ;

    private:
	enum slave_status {
	    SL_COLDSTART = 1,
	    SL_WAITING_UNKNOWN,
	    SL_RUNNING,
	    SL_RENEW,
	    SL_WAITING_KNOWN,
	} ;

	struct reslist
	{
	    Resource *res ;
	    reslist *next ;
	} ;

	reslist *reslist_ ;

	time_t curtime_ ;
	Retrans retrans_ ;
	l2addr *master_ ;		// NULL <=> broadcast
	l2net *l2_ ;
	int defmtu_ ;			// default (user specified) MTU
	int curmtu_ ;			// current (negociated) MTU
	long int slaveid_ ;		// slave id, manually config'd
	slave_status status_ ;
	time_t sttl_ ;			// slave ttl, given in assoc msg
	long int hlid_ ;		// hello ID
	int curid_ ;			// current message id

	// various timers handled by function
	Twait  twait_ ;
	Trenew trenew_ ;

	/*
	 * Private methods
	 */

	void reset_master (void) ;
	void change_master (long int hlid, int mtu) ;
	bool same_master (l2addr *a) ;	

	void reset_mtu (void) ;
	void negociate_mtu (int mtu) ;

	bool is_ctl_msg (Msg &m) ;
	bool is_hello (Msg &m, long int &hlid) ;
	bool is_assoc (Msg &m, time_t &sttl, int &mtu) ;

	void mk_ctl_msg (Msg &m) ;
	void send_discover (Msg &m) ;
	void send_assoc_answer (Msg &in, Msg &out) ;

	void request_resource (Msg *in, Msg *out, Resource *res) ;
	void check_observed_resources (Msg &out) ;
	bool get_well_known (Msg &out) ;
	Resource *get_resource (const char *name) ;

	void print_coap_ret_type (l2net::l2_recv_t ret) ;
	void print_status (uint8_t status) ;
} ;

#endif
