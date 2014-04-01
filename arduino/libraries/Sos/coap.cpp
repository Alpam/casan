#include "coap.h"

Coap::Coap(l2net *e) {
	_l2 = e;
}

Coap::~Coap(void) {
	delete _l2;
}

void Coap::send(l2addr &mac_addr_dest, Message &m) 
{
	uint8_t sbuf[150] = {0};
	size_t sbuflen(0);
	encode(m, sbuf, sbuflen);
	_l2->send( mac_addr_dest, sbuf, sbuflen);
}

/*
 * returns 1 if it's not the master the sender;
 * returns 2 if the dest is wrong (not the good mac address or the broadcast address)
 * returns 3 if it's the wrong eth type
 * return 0 if ok
 */
l2_recv_t Coap::recv(Message &m) {
	m.reset();
	l2_recv_t ret = _l2->recv();
	if(ret == L2_RECV_RECV_OK)
		decode(m, _l2->get_offset_payload(0), 
				_l2->get_payload_length() - 2);
	return ret;
}

void Coap::set_master_addr(l2addr *master_addr) {
	_l2->set_master_addr(master_addr);
}

uint8_t Coap::get_type(void) {
	return (uint8_t) ((*(_l2->get_offset_payload(COAP_OFFSET_TYPE))) >> 4 ) & 0x03;
}

uint8_t Coap::get_code(void) {
	return *(_l2->get_offset_payload(COAP_OFFSET_CODE));
}

int Coap::get_id(void) {
	uint8_t *idp = _l2->get_offset_payload(COAP_OFFSET_ID);
	int ret = ((*(idp) & 0xff) << 8) | (*(idp+1) & 0xff);
	return ret;
}

uint8_t Coap::get_token_length(void) {
	return (*(_l2->get_offset_payload(COAP_OFFSET_TKL))) & 0x0F;
}

uint8_t * Coap::get_token(void) {
	return _l2->get_offset_payload(COAP_OFFSET_TOKEN);
}

void Coap::get_mac_src(l2addr * mac_src) {
	_l2->get_mac_src(mac_src);
}

/* returns true if message is decoding was successful */
bool Coap::decode(Message &m, uint8_t rbuf[], size_t rbuflen) {
	m.set_type(get_type());
	m.set_code(get_code());
	m.set_id(get_id());
	m.set_token(get_token_length(), get_token());

	bool success(true);

	int i ;
	int opt_nb(0);
	int paylen_(0);

	i = 4 + get_token_length();

	/*
	 * Options analysis
	 */

	opt_nb = 0 ;
	while (success && i < rbuflen && rbuf [i] != 0xff)
	{
		int opt_delta(0), opt_len(0) ;
		option o ;

		opt_delta = (rbuf [i] >> 4) & 0x0f ;
		opt_len   = (rbuf [i]     ) & 0x0f ;
		i++ ;
		switch (opt_delta)
		{
			case 13 :
				opt_delta = rbuf [i] + 13 ;
				i += 1 ;
				break ;
			case 14 : // the opt_delta is on two bytes (the next)
				opt_delta = (rbuf [i] << 8) + rbuf [i+1] + 269 ;
				i += 2 ;
				break ;
			case 15 :
				success = false ;			// recv failed
				break ;
		}
		opt_nb += opt_delta ;

		switch (opt_len)
		{
			case 13 :
				opt_len = rbuf [i] + 13 ;
				i += 1 ;
				break ;
			case 14 : // the length is encoded on two bytes (after opt_delta)
				opt_len = (rbuf [i] << 8) + rbuf [i+1] + 269 ;
				i += 2 ;
				break ;
			case 15 :
				success = false ;			// recv failed
				break ;
		}

		/* register option */
		if (success)
		{

			o.optcode (option::optcode_t (opt_nb)) ;
			o.optval ((void *)(rbuf + i), (int)opt_len) ;
			m.push_option (o) ;

			i += opt_len ;
		}
		else
			PRINT_DEBUG_STATIC ("\033[31mOPTION unrecognized\033[00m") ;
	}

	paylen_ = rbuflen - i - 1 ;
	if (success && paylen_ > 0)
	{
		if (rbuf [i] != 0xff)
		{
			success = false ;
		}
		else
		{
			i++ ;
			m.set_payload(paylen_, rbuf + i);
		}
	}
	else paylen_ = 0 ;			// protect further operations

	return success ;
}

void Coap::encode (Message &m, uint8_t sbuf[], size_t &sbuflen) {
	int i ;
	int opt_nb ;
	int toklen = m.get_token_length();
	int id = m.get_id();
	int paylen = m.get_payload_length();


	/*
	 * Format message, part 1 : compute message size
	 */

	sbuflen = 4 + toklen;

	opt_nb = 0 ;
	for(option * o = m.get_option() ; o != NULL ; o = m.get_option()) {
		int opt_delta, opt_len ;

		sbuflen++ ;			// 1 byte for opt delta & len

		opt_delta = o->optcode_ - opt_nb ;
		if (opt_delta >= 269)		// delta >= 269 => 2 bytes
			sbuflen += 2 ;
		else if (opt_delta >= 13)	// delta \in [13..268] => 1 byte
			sbuflen += 1 ;
		opt_nb = o->optcode_ ;

		opt_len = o->optlen_ ;
		if (opt_len >= 269)		// len >= 269 => 2 bytes
			sbuflen += 2 ;
		else if (opt_len >= 13)		// len \in [13..268] => 1 byte
			sbuflen += 1 ;
		sbuflen += o->optlen_ ;
	}
	if ( paylen > 0)
		sbuflen += 1 + paylen ;	// don't forget 0xff byte

	/*
	 * Format message, part 2 : build message
	 */

	i = 0 ;

	// header
	sbuf [i++] = FORMAT_BYTE0 (SOS_VERSION, m.get_type(), toklen) ;
	sbuf [i++] = m.get_code() ;
	sbuf [i++] = BYTE_HIGH (id) ;
	sbuf [i++] = BYTE_LOW  (id) ;

	// token
	if (toklen > 0)
	{
		memcpy (sbuf + i, m.get_token(), toklen) ;
		i += toklen ;
	}
	// options
	opt_nb = 0 ;
	for(option * o = m.get_option() ; o != NULL ; o = m.get_option()) {
		int opt_delta, opt_len ;
		int posoptheader = i ;

		sbuf [posoptheader] = 0 ;

		i++ ;
		opt_delta = int (o->optcode_) - opt_nb ;
		if (opt_delta >= 269)		// delta >= 269 => 2 bytes
		{
			opt_delta -= 269 ;
			sbuf [i++] = BYTE_HIGH (opt_delta) ;
			sbuf [i++] = BYTE_LOW  (opt_delta) ;
			sbuf [posoptheader] |= 0xe0 ;
		}
		else if (opt_delta >= 13)	// delta \in [13..268] => 1 byte
		{
			opt_delta -= 13 ;
			sbuf [i++] = BYTE_LOW (opt_delta) ;
			sbuf [posoptheader] |= 0xd0 ;
		}
		else
		{
			sbuf [posoptheader] |= (opt_delta << 4) ;
		}
		opt_nb = o->optcode_ ;

		opt_len = o->optlen_ ;
		if (opt_len >= 269)		// len >= 269 => 2 bytes
		{
			opt_len -= 269 ;
			sbuf [i++] = BYTE_HIGH (opt_len) ;
			sbuf [i++] = BYTE_LOW  (opt_len) ;
			sbuf [posoptheader] |= 0x0e ;
		}
		else if (opt_len >= 13)		// len \in [13..268] => 1 byte
		{
			sbuf [i++] = BYTE_LOW (opt_len) ;
			sbuf [posoptheader] |= 0x0d ;
		}
		else
		{
			sbuf [posoptheader] |= opt_len ;
		}
		memcpy (sbuf + i, OPTVAL (o), o->optlen_) ;
		i += o->optlen_ ;
	}
	// payload
	if (paylen > 0)
	{
		sbuf [i++] = 0xff ;			// start of payload
		memcpy (sbuf + i, m.get_payload(), paylen) ;
	}
}