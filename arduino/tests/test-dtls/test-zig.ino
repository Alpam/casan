#include <ZigMsg.h>
#include <stdarg.h>

#define	CHANNEL	25		// use "c" to change it while running

#define	PANID		CONST16 (0xbe, 0xef)
#define	SENDADDR	CONST16 (0x01, 0x02)
#define	RECVADDR	CONST16 (0x10, 0x11)

#define	MSGBUF_SIZE	10	// each msgbuf consumes ~130 bytes


#ifdef __GNUC__
#define UNUSED_PARAM __attribute__((unused))
#else
#define UNUSED_PARAM
#endif /* __GNUC__ */

#define	PERIODIC	100000

int channel = CHANNEL ;

//#define MSG_FREEMEM
//#define MSG_DEBUG

//#define MSG_DELAY    50
//#define SIZE_TO_SEND 87
//#define MSG_RECEIVED

extern "C" {
#include "free_memory.h"
#include <dtls.h>
};

#define PSK_DEFAULT_IDENTITY "Client_identity"
#define PSK_DEFAULT_KEY      "secretPSK"
#define LOGLVL          DTLS_LOG_DEBUG
#define DTLS_RECORD_HEADER(M) ((dtls_record_header_t *)(M))
#define dtls_get_content_type(H) ((H)->content_type & 0xff)

/*
 * TODO
 * envoyer les messages d'erreur DTLS au lieu de simplement
 * afficher une erreur et boucler
*/

/******************************************************************************
Utilities
*******************************************************************************/

#define	HEXCHAR(c)	((c) < 10 ? (c) + '0' : (c) - 10 + 'a')

void print_free_mem()
{
    int memory = freeMemory();
    Serial.print(F("mémoire disponible : "));
    Serial.println(memory);
}

unsigned long get_random (unsigned int max) {
    return random(max);
}

unsigned long get_the_time (void) {
    return micros();
}

void instant_print (char * format, ...)
{
    va_list args;
    va_start(args, format);
    char msg[100];
    vsnprintf(msg, sizeof msg, format, args);
    va_end(args);
    Serial.print(msg);
}

void something_to_say (log_t loglvl, char * format, ...)
{
    if(loglvl > LOGLVL)
        return;

    va_list args;
    va_start(args, format);
    char msg[100];
    vsnprintf(msg, sizeof msg, format, args);
    va_end(args);

    Serial.print(msg);

    if(strlen(msg) > 4) {
        if(msg[0] == 'W' && msg[1] == 'A' && msg[2] == 'I' && msg[3] == 'T')
            delay(2000);
    }
}

static inline size_t
print_timestamp_(char *s, size_t len, clock_time_t t)
{
  return snprintf(s, len, "%u.%03u", 
		  (unsigned int)(t / CLOCK_SECOND), 
		  (unsigned int)(t % CLOCK_SECOND));
}

void 
something_to_hexdump(log_t loglvl, char *name, unsigned char *buf
        , size_t length, int extend)
{

    if(loglvl > LOGLVL)
        return;

    static char timebuf[32];
    int n = 0;

    if (print_timestamp_(timebuf,sizeof(timebuf), get_the_time()))
        instant_print(const_cast<char *>("%s "), timebuf);

    if (extend) {
        instant_print(const_cast<char *>("%s: (%d bytes):\n\r"), name, length);

        while (length--) {
            if (n % 16 == 0)
                instant_print(const_cast<char *>("%08X "), n);

            instant_print(const_cast<char *>("%02X "), *buf++);

            n++;
            if (n % 8 == 0) {
                if (n % 16 == 0)
                    instant_print(const_cast<char *>("\n\r"));
                else
                    instant_print(const_cast<char *>(" "));
            }
        }
    } else {
        instant_print(const_cast<char *>("%s: (%d bytes): "), name, length);
        while (length--) 
            instant_print(const_cast<char *>("%02X"), *buf++);
    }
    instant_print(const_cast<char *>("\n\r"));
}

void phex (uint8_t c)
{
    char x ;
    x = (c >> 4) & 0xf ; x = HEXCHAR (x) ; Serial.print (x) ;
    x = (c     ) & 0xf ; x = HEXCHAR (x) ; Serial.print (x) ;
}

void pdec (int c, int ndigits, char fill)
{
    int c2 ;
    int n ;

    /*
     * compute number of digits of c
     */

    c2 = c ;
    n = 1 ;
    while (c2 > 0)
    {
	n++ ;
	c2 /= 10 ;
    }

    /*
     * Complete with fill characters
     */

    for ( ; n <= ndigits ; n++)
	Serial.print (fill) ;
    Serial.print (c) ;
}

void phexn (uint8_t *c, int len, char sep)
{
    int i ;

    for (i = 0 ; i < len ; i++)
    {
	if (i > 0 && sep != '\0')
	    Serial.print (sep) ;
	phex (c [i]) ;
    }
}

/* flen = field length = nominal number of chars in a "normal" row */
void phexascii (uint8_t buf [], int len, int flen)
{
    int i ;

    phexn (buf, len, ' ') ;
    for (i = 0 ; i < (flen - len) * 3 + 4 ; i++)
	Serial.print (' ') ;
    for (i = 0 ; i < len ; i++)
	Serial.print ((char) (buf [i] >= ' ' && buf [i] < 127 ? buf [i] : '.')) ;
}

#define	NTAB(t)		((int) (sizeof (t) / sizeof (t)[0]))

/******************************************************************************
Channel
*******************************************************************************/

void init_chan (char line [])
{
    char *p ;
    int ch = 0 ;

    p = line ;
    while (*p == ' ' || *p == '\t')
	p++ ;

    while (*p >= '0' && *p <= '9')
    {
	ch = ch * 10 + (*p - '0') ;
	p++ ;
    }

    if (ch < 11 || ch > 26)
	Serial.println (F("Invalid channel")) ;
    else
    {
	channel = ch ;
	Serial.print (F("Channel set to ")) ;
	Serial.println (channel) ;
    }

    Serial.println (F("Entering idle mode")) ;
}

void stop_chan (void)
{
}

void do_chan (void)
{
}

//  DTLS common

bool am_i_server;
#ifdef MESURE_TIME_START_PUSHING_MSG
uint32_t time_start_pushing_msg;
#endif
uint32_t time_end_pushing_msg;
uint32_t time_start_nego;
uint32_t time_end_nego;
uint32_t time ;
static unsigned char buf[DTLS_MAX_BUF];
static size_t len = 0;
session_t session;
dtls_context_t *the_context = NULL;

char whatisent[DTLS_MAX_BUF];

static int
send_to_peer(struct dtls_context_t *ctx, 
        session_t *session, uint8 *data, size_t len)
{
#ifdef MSG_DEBUG
    dtls_debug_hexdump("TO SEND", data, len);
#endif

#ifdef MESURE_TIME_START_PUSHING_MSG
    if(time_start_pushing_msg != 0)
    {
        time_end_pushing_msg = micros() - time_start_pushing_msg;
        Serial.print("duration between asking and sending the msg : ");
        Serial.println(time_end_pushing_msg);
    }
#endif

    int ret = zigmsg.sendto(session->addr, len, data);
    return ret ? len : -1;
}

static void
try_send(struct dtls_context_t *ctx)
{
#ifdef MSG_DEBUG
    Serial.print(F("Sending "));
    Serial.print(len);
    Serial.println(F(" octets."));
#endif
#ifdef MESURE_TIME_START_PUSHING_MSG
    time_start_pushing_msg = micros();
#endif
    int res;
    res = dtls_write(ctx, &session, (uint8 *)buf, len);
    if (res >= 0) {
        memmove(buf, buf + res, len - res);
        len -= res;
    }
}

// DTLS server

/* This function is the "key store" for tinyDTLS. It is called to
 * retrieve a key for the given identity within this particular
 * session. */

static int
get_psk_info(struct dtls_context_t *ctx, const session_t *session,
        dtls_credentials_type_t type,
        const unsigned char *id, size_t id_len,
        unsigned char *result, size_t result_length)
{

    struct keymap_t {
        unsigned char *id;
        size_t id_length;
        unsigned char *key;
        size_t key_length;
    } psk[3] = {
        { (unsigned char *)"Client_identity", 15,
            (unsigned char *)"secretPSK", 9 },
        { (unsigned char *)"default identity", 16,
            (unsigned char *)"\x11\x22\x33", 3 },
        { (unsigned char *)"\0", 2,
            (unsigned char *)"", 1 }
    };

    if (type != DTLS_PSK_KEY) {
        return 0;
    }

    if (id) {
        unsigned int i;
        for (i = 0; i < sizeof(psk)/sizeof(struct keymap_t); i++) {
            if (id_len == psk[i].id_length && 
                    memcmp(id, psk[i].id, id_len) == 0) {
                if (result_length < psk[i].key_length) {
                    //dtls_warn("buffer too small for PSK");
                    Serial.println(F("buf 2 small 4 PSK"));
                    return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
                }

                memcpy(result, psk[i].key, psk[i].key_length);
                return psk[i].key_length;
            }
        }
    }

    return dtls_alert_fatal_create(DTLS_ALERT_DECRYPT_ERROR);
}

#define DTLS_SERVER_CMD_CLOSE "server:close"
#define DTLS_SERVER_CMD_RENEGOTIATE "server:renegotiate"

// to read then to resend the received data (for the client)
//              or to test if the received data is the same that we sent
static int
read_from_peer(struct dtls_context_t *ctx, 
        session_t *session, uint8 *data, size_t llen)
{

    char mbuffer_recv[DTLS_MAX_BUF];
    memset(mbuffer_recv, '\0', DTLS_MAX_BUF);

    if(am_i_server == false)
    {
        // to send back what we get

        memcpy(buf, data, llen);
        len = llen;
        memset(mbuffer_recv, '\0', DTLS_MAX_BUF);
        memcpy(mbuffer_recv, data, llen);
        try_send(the_context);
#ifdef MSG_RECEIVED
        Serial.print("mbuffer_recv[0]: ");
        Serial.println(mbuffer_recv[0]);
        Serial.print("mbuffer_recv : ");
        Serial.println(mbuffer_recv);
#endif

    }
    else
    {
        // or to compare the received data with what we sent

        memcpy(mbuffer_recv, data, SIZE_TO_SEND);
        len = 0;

        if(memcmp(mbuffer_recv, whatisent, SIZE_TO_SEND) != 0)
        {
            Serial.println(F("ERROR : Sent != Received"));

            // check if there isn't another msg in queue
            ZigMsg::ZigReceivedFrame *z ;

            static int n = 0;
            bool found = false;

            while(! found && ++n % PERIODIC != 0)
            {
                while ((z = zigmsg.get_received ()) != NULL)
                {
                    memcpy(buf, z->payload, z->paylen);
                    len += z->paylen; // TODO FIXME : pas sûr de += ou =
                    zigmsg.skip_received () ;
                    int ret = dtls_handle_message(the_context, session
                            , (uint8_t*)buf, len);
                    len = 0; // TODO do we have to put the length of the received pkt to 0 ?
                    if(ret) {
                        Serial.print(F("\r\nerr d_hdl_msg > d_h_read : "));
                        Serial.println(ret);
                        return ret;
                    }
                    found = true;
                }
            }
        }
        else
        {
#ifdef MSG_DEBUG
            Serial.print(F("Sent == Received : "));

            Serial.print(F("sent : "));
            Serial.print(whatisent);
            Serial.print(F(" received : "));
            Serial.print(mbuffer_recv);
            Serial.print(F(" len "));
            Serial.print(llen);
            Serial.print(F(" "));
#endif
        }

    }

    /*
    if(len > 0) {
        size_t MTU = 127;
        char x[MTU +1];
        memcpy(x, data, (len > MTU) ? MTU : len);
        x[MTU] = '\0';
        Serial.println(x);
    }
    */

    return 1;
}

static int
connection_handle(void)
{
    ZigMsg::ZigReceivedFrame *z ;
    size_t i = 0;

    while ((z = zigmsg.get_received ()) != NULL)
    {
        memcpy(buf, z->payload, z->paylen);
        len += z->paylen; // TODO FIXME : pas sûr de += ou =
        zigmsg.skip_received () ;

#ifdef MSG_DEBUG
        dtls_debug_hexdump("connection_handle: RECEIVED MSG"
                , z->payload, z->paylen);
#endif

        static int is_first_ch = 1;
        dtls_record_header_t *header = DTLS_RECORD_HEADER(buf);

        if(am_i_server && is_first_ch && 
                dtls_get_content_type(header) == DTLS_CT_HANDSHAKE)
        {
            is_first_ch = 0;
            time_start_nego = micros();
        }

        int ret = dtls_handle_message(the_context, &session, (uint8_t*)buf, len);
        len = 0; // TODO do we have to put the length of the received pkt to 0 ?

        if(ret) {
            Serial.print(F("\r\nerr d_hdl_msg > d_h_read : "));
            Serial.println(ret);
            return ret;
        }

#ifdef MSG_FREEMEM
    print_free_mem();
#endif
        i++;

#ifdef MSG_DEBUG
        Serial.print("Passage : ");
        Serial.println(i);
#endif

    }

    return 1;
}

void
dtls_send_then_read()
{
    ZigMsg::ZigReceivedFrame *z ;

    static int n = 0 ;
    n = 0;

    bool found = false;
#ifdef MSG_DURATION
    uint32_t duration;
#endif
    time = micros () ;

#ifdef MSG_DEBUG
    Serial.print(time, HEX);
    Serial.print(' ');
#endif

    // sending the msg

    // each msg should not be like the previous one
    whatisent[0] = (whatisent[0] == 'z') ? 'a' : whatisent[0] + 1;
    memset(buf, '\0', DTLS_MAX_BUF);
    memcpy(buf, whatisent, SIZE_TO_SEND);
    len = SIZE_TO_SEND;

    try_send(the_context);
    len = 0;

    // receiving the msg

    while(! found && ++n % PERIODIC != 0)
    {

        while ((z = zigmsg.get_received ()) != NULL)
        {
            memset(buf, '\0', DTLS_MAX_BUF);
            memcpy(buf, z->payload, z->paylen);
            len += z->paylen;

#ifdef MSG_DEBUG
            Serial.print(F("len : "));
            Serial.println(z->paylen);
#endif

            zigmsg.skip_received () ;

            int ret = dtls_handle_message(the_context, &session
                    , (uint8_t*)buf, len);
            len = 0;

            if(ret) {
                Serial.print(F("\r\nerr d_hdl_msg > d_h_read : "));
                Serial.println(ret);
                return ;
            }

            found = true;
        }
    }

#ifdef MSG_DURATION
    duration = micros () - time ;
    Serial.print(F("duration: "));
    Serial.println(duration);
#endif

#ifdef MSG_FREEMEM
    print_free_mem();
#endif

    delay(MSG_DELAY);
}

static dtls_handler_t cb_server = {
    .write = send_to_peer,
    .read  = read_from_peer,
    .event = NULL,
    .get_psk_info = get_psk_info,
};

void init_dtls_server (char line [])
{
    zigmsg.channel (channel) ;
    zigmsg.panid (PANID) ;
    zigmsg.addr2 (RECVADDR) ;
    zigmsg.promiscuous (false) ;
    zigmsg.start () ;

    memset(&session, 0, sizeof(session_t));
    session.addr = SENDADDR;
    session.size = sizeof(session.addr);

    //log_t log_level = DTLS_LOG_WARN;
    //dtls_set_log_level(log_level);

    randomSeed(get_the_time());
    dtls_init(get_the_time);

    the_context = dtls_new_context(get_random);
    the_context->smth_to_say = something_to_say;
    the_context->smth_to_hexdump = something_to_hexdump;

    dtls_set_handler(the_context, &cb_server);
    am_i_server = true;
    memset(whatisent, 'a', SIZE_TO_SEND);
}

void stop_dtls_server (void)
{
    dtls_free_context(the_context);
    Serial.println(F("stop_d_serv"));
}

void do_dtls_server (void)
{

    static char only_print_one_time_the_handshake_duration = 0;
    dtls_peer_t * peer = dtls_get_peer(the_context, &session);

    // if no received message and handshake complete
    if(peer && peer->state == DTLS_STATE_CONNECTED)
    {
        if(only_print_one_time_the_handshake_duration == 0)
        {
            only_print_one_time_the_handshake_duration++;
            time_end_nego = micros() - time_start_nego;

            Serial.print("Handshake duration : ");
            Serial.println(time_end_nego);
        }

        dtls_send_then_read();
    }
    else
    {
        connection_handle();
    }
}

// DTLS client

// The PSK information for DTLS
#define PSK_ID_MAXLEN 256
#define PSK_MAXLEN 256
static unsigned char psk_id[PSK_ID_MAXLEN];
static size_t psk_id_length = 0;
static unsigned char psk_key[PSK_MAXLEN];
static size_t psk_key_length = 0;

// This function is the "key store" for tinyDTLS. It is called to
// retrieve a key for the given identity within this particular
// session.
static int
get_psk_info_cli(struct dtls_context_t *ctx UNUSED_PARAM,
        const session_t *session UNUSED_PARAM,
        dtls_credentials_type_t type,
        const unsigned char *id, size_t id_len,
        unsigned char *result, size_t result_length)
{

    switch (type) {
        case DTLS_PSK_IDENTITY:
            if (id_len) {
                dtls_debug("got psk_identity_hint: '%.*s'\n\r", id_len, id);
            }

            if (result_length < psk_id_length) {
                dtls_warn("cannot set psk_identity -- buffer too small\n\r");
                return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
            }

            memcpy(result, psk_id, psk_id_length);
            return psk_id_length;
        case DTLS_PSK_KEY:
            if (id_len != psk_id_length || memcmp(psk_id, id, id_len) != 0) {
                dtls_warn("PSK for unknown id requested, exiting\n\r");
                return dtls_alert_fatal_create(DTLS_ALERT_ILLEGAL_PARAMETER);
                break;
            } else if (result_length < psk_key_length) {
                dtls_warn("cannot set psk -- buffer too small\n\r");
                return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
            }

            memcpy(result, psk_key, psk_key_length);
            return psk_key_length;
        default:
            Serial.print("unsupported req type: ");
            Serial.println(type);
    }

    return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
}


static dtls_handler_t cb_cli = {
    .write = send_to_peer,
    .read  = read_from_peer,
    .event = NULL,
    .get_psk_info = get_psk_info_cli,
};
#define DTLS_CLIENT_CMD_CLOSE "client:close"
#define DTLS_CLIENT_CMD_RENEGOTIATE "client:renegotiate"

void init_dtls_client (char line [])
{
    zigmsg.channel (channel) ;
    zigmsg.panid (PANID) ;
    zigmsg.addr2 (SENDADDR) ;
    zigmsg.promiscuous (false) ;
    zigmsg.start () ;

#ifdef MSG_DEBUG
    Serial.println(F("Start cli")) ;
#endif

    memset(&session, 0, sizeof(session_t));
    session.addr = RECVADDR;
    session.size = sizeof(session.addr);

    randomSeed(get_the_time());
    dtls_init(get_the_time);

    // PSK IDENTITY & KEY
    psk_id_length = strlen(PSK_DEFAULT_IDENTITY);
    psk_key_length = strlen(PSK_DEFAULT_KEY);
    memcpy(psk_id, PSK_DEFAULT_IDENTITY, psk_id_length);
    memcpy(psk_key, PSK_DEFAULT_KEY, psk_key_length);

    the_context = dtls_new_context(get_random);
    the_context->smth_to_say = something_to_say;
    the_context->smth_to_hexdump = something_to_hexdump;

    if (!the_context) {
        while(1) { Serial.println(F("can't create ctxt")); delay(1000); }
    }

    dtls_set_handler(the_context, &cb_cli);

    time_start_nego = micros () ;
    dtls_connect(the_context, &session);
    am_i_server = false;
}

void stop_dtls_client (void)
{
    dtls_free_context(the_context);
    Serial.println(F("stop dtls client"));
}

void do_dtls_client (void)
{

    static char only_print_one_time_the_handshake_duration = 0;
    dtls_peer_t * peer = dtls_get_peer(the_context, &session);

    // if no received message and handshake complete
    if(peer && peer->state == DTLS_STATE_CONNECTED)
    {
        if(only_print_one_time_the_handshake_duration == 0)
        {
            only_print_one_time_the_handshake_duration++;
            time_end_nego = micros() - time_start_nego;

            Serial.print("Handshake duration : ");
            Serial.println(time_end_nego);
        }
    }

    connection_handle();
}


// GUI ;-)

void init_idle (char line [])
{
}

void stop_idle (void)
{
}

void do_idle (void)
{
}

struct gui
{
    char start_key ;			// lowercase
    const char *desc ;
    void (*f_init) (char line []) ;
    void (*f_stop) (void) ;
    void (*f_do) (void) ;
} ;

struct gui gui [] = {
    { 'i', "idle", init_idle, stop_idle, do_idle },
    { 'c', "channel (n)", init_chan, stop_chan, do_chan },
    { 'd', "dtls server", init_dtls_server, stop_dtls_server, do_dtls_server },
    { 't', "dtls client", init_dtls_client, stop_dtls_client, do_dtls_client },
} ;
#define	IDLE_MODE (& gui [0])

struct gui *parse_and_init_or_stop (char line [], struct gui *oldmode)
{
    char *p = line ;
    struct gui *newmode ;

    while (*p == ' ' || *p == '\t')
	p++ ;

    newmode = oldmode ;
    for (int i = 0 ; i < NTAB (gui) ; i++)
    {
	if (*p == gui [i].start_key)
	{
	    newmode = &gui [i] ;
	    break ;
	}
    }

    if (newmode != oldmode)
    {
	(* oldmode->f_stop) () ;
	(* newmode->f_init) (p + 1) ;
    }

    return newmode ;
}

void help (void)
{
    for (int i = 0 ; i < NTAB (gui) ; i++)
    {
	if (i > 0)
	    Serial.print (", ") ;
	Serial.print (gui [i].start_key) ;
	Serial.print (':') ;
	Serial.print (gui [i].desc) ;
    }
    Serial.println () ;
}

/******************************************************************************
Classic Arduino functions
*******************************************************************************/

void setup ()
{
    Serial.begin (38400);	// don't introduce spaces here
    Serial.println (F("Starting...")) ; // signal the start with a new line
    zigmsg.msgbufsize (MSGBUF_SIZE) ;	// space for 10 received messages
    help () ;
}

void loop()
{
    static char line [100], *p = line ;
    static struct gui *curmode = IDLE_MODE ;
    int n ;

    n = Serial.available () ;
    if (n > 0)
    {
	for (int i = 0 ; i < n ; i++)
	{
	    *p = Serial.read () ;
	    Serial.print (*p) ;
	    if (*p == '\r')
	    {
		struct gui *oldmode ;

		Serial.print ('\n') ;
		p = '\0' ;
		oldmode = curmode ;
		curmode = parse_and_init_or_stop (line, curmode) ;
		p = line ;
		if (curmode == oldmode)
		    help () ;
	    }
	    else p++ ;
	}
    }

    (*curmode->f_do) () ;
}

void errHandle(radio_error_t err)
{
    Serial.println();
    Serial.print(F("Error: "));
    Serial.print((uint8_t)err, 10);
    Serial.println();
}
