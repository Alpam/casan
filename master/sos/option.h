#ifndef SOS_OPTION_H
#define SOS_OPTION_H

namespace sos {

/*
 * Option
 */

class option
{
    public:
	typedef enum {
			MO_None			= 0,
			MO_Content_Format	= 12,
			MO_Etag			= 4,
			MO_Location_Path	= 8,
			MO_Location_Query	= 20,
			MO_Max_Age		= 14,
			MO_Proxy_Uri		= 35,
			MO_Proxy_Scheme		= 39,
			MO_Uri_Host		= 3,
			MO_Uri_Path		= 11,
			MO_Uri_Port		= 7,
			MO_Uri_Query		= 15,
			MO_Accept		= 16,
			MO_If_None_Match	= 5,
			MO_If_Match		= 1,
		    } optcode_t ;
	typedef unsigned long int uint ;

	option () ;				// constructor
	option (optcode_t c) ;			// constructor
	option (optcode_t c, void *v, int l) ;	// constructor
	option (optcode_t c, uint v) ;		// constructor
	option (const option &o) ;		// copy constructor
	option &operator= (const option &o) ;	// copy assignment constructor
	~option () ;				// destructor

	bool operator< (const option &o) ;	// for list sorting in msg.cc

	void reset (void) ;

	// accessors
	optcode_t optcode (void) ;
	void *optval (int *len) ;
	uint optval (void) ;

	// mutators
	void optcode (optcode_t c) ;
	void optval (void *v, int len) ;
	void optval (uint v) ;

    protected:
	optcode_t optcode_ = MO_None ;
	int optlen_ = 0 ;
	byte *optval_ = 0 ;		// 0 if staticval is used
	byte staticval_ [8 + 1] ;	// keep a \0 after, just in case

	friend class msg ;

    private:
	typedef enum optfmt { OF_NONE = 0, OF_OPAQUE, OF_STRING,
	    				OF_EMPTY, OF_UINT } optfmt_t ;
	struct optdesc
	{
	    optfmt_t format ;
	    int minlen ;
	    int maxlen ;
	} ;
	static optdesc *optdesc_ ;

	void static_init (void) ;
} ;

}					// end of namespace sos
#endif
