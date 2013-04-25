#include "ethernetraw.h"

EthernetRaw::EthernetRaw() : _s(0) {//_sock(MAX_SOCK_NUM)
	W5100.init();
	W5100.writeSnMR(_s, SnMR::MACRAW);
	W5100.execCmdSn(_s, Sock_OPEN);
}

void EthernetRaw::setmac(l2addr *mac_address) {
	_mac_addr = mac_address;
	W5100.setMACAddress(_mac_addr->get_raw_addr());
	/*
	Serial.print(F("mac : "));
	for(int i = 0 ; i < 6 ; i++)
	{
		Serial.print(_mac_addr[i]);
	}
	Serial.println();
	*/
}

void EthernetRaw::setethtype(uint8_t * eth_type) {
	_eth_type[0] = eth_type[0];
	_eth_type[1] = eth_type[1];
}

void EthernetRaw::setethtype(uint16_t eth_type) {
	_eth_type[0] = (uint8_t) eth_type >> 8;
	_eth_type[1] = (uint8_t) eth_type & 0xFF;
}

size_t EthernetRaw::send(l2addr *mac_dest, uint8_t data) {
	return send(mac_dest, &data, 1);
}

size_t EthernetRaw::send(l2addr *mac_dest, const uint8_t *data, size_t len) {
	byte *sbuf = NULL;
	sbuf = (byte*) malloc(len + 16);
	int sbuflen;
	size_t reste;

	reste = BUFFER_SIZE < len ? len - BUFFER_SIZE - OFFSET_DATA: 0;
	sbuflen = BUFFER_SIZE < len + OFFSET_DATA ? BUFFER_SIZE : len + OFFSET_DATA;

	memcpy(sbuf + OFFSET_DEST_ADDR, mac_dest, 6);
	memcpy(sbuf + OFFSET_SRC_ADDR, _mac_addr->get_raw_addr(), 6);
	memcpy(sbuf + OFFSET_ETHTYPE, _eth_type, 2);
	memcpy(sbuf + OFFSET_DATA, data, len);

	W5100.send_data_processing(_s, sbuf, sbuflen);
	W5100.execCmdSn(_s, Sock_SEND_MAC);
	//Serial.print(F("Envoi : "));
	//print_packet(sbuf, sbuflen);

	free(sbuf);
	return reste;
}

int EthernetRaw::available() {
	_rbuflen = W5100.getRXReceivedSize(_s) ;
	return get_payload_length();
}

/*
 * returns 1 if it's not the master the sender;
 * returns 2 if the dest is wrong (not the good mac address or the broadcast address)
 * returns 3 if it's the wrong eth type
 * return 0 if ok
 */
uint8_t EthernetRaw::recv(void) {
	return recv(NULL,NULL);
}

uint8_t EthernetRaw::recv(l2addr *mac_addr_src, uint8_t *data, int *len) {
	mac_addr_src = (l2addr_eth *) mac_addr_src;
	W5100.recv_data_processing(_s, _rbuf, _rbuflen);
	W5100.execCmdSn(_s, Sock_RECV);
	// There is an offset in the reception (2 bytes, see the w5100 datasheet)
	byte * packet = _rbuf + OFFSET_RECEPTION;
	// we check the source address (only the master must send informations)
	// TODO : do the part in the l2addr class to compare l2addr_eth and byte *
	if(mac_addr_src != packet + OFFSET_SRC_ADDR )
	{
		return 1;
	}
	// we check the destination address
	{
		l2addr mac_broadcast("ff:ff:ff:ff:ff:ff");
		if( _mac_addr != packet +OFFSET_DEST_ADDR && mac_broadcast != packet + OFFSET_DEST_ADDR)
		{
			return 2;
		}
	}
	// we check the ethernet type
	if(memcmp(packet + OFFSET_ETHTYPE, _eth_type, 2) != 0)
	{
		return 3;
	}
	if(data != NULL)
	{
		*len = _rbuflen - OFFSET_RECEPTION - OFFSET_DATA;
		memcpy(data, packet + OFFSET_DATA, *len);
	}
	//Serial.println(F("Reçoi : "));
	//print_packet(packet, _rbuflen);
	return 0;
}

void EthernetRaw::get_mac_src(l2addr * mac_src) {
	mac_src = (l2addr_eth *) mac_src;
	mac_src->set_addr( _rbuf + OFFSET_RECEPTION + OFFSET_SRC_ADDR );
}

void EthernetRaw::print_eth_addr (byte addr []) {
	int i ;
	for (i = 0 ; i < 6 ; i++)
	{
		if (i > 0)
			Serial.print (':') ;
		Serial.print (addr [i], HEX) ;
	}
}

uint8_t * EthernetRaw::get_offset_payload(int offset) {
	uint8_t off = OFFSET_RECEPTION + OFFSET_DATA + offset;
	//Serial.print(F("OFFSET : "));
	//Serial.println(off);
	return _rbuf + off;
}

void EthernetRaw::print_packet (byte pkt [], int len) {
	word w ;

	w = * (word *) pkt ;
	Serial.print (len) ;
	Serial.print (F(" bytes ") );

	print_eth_addr (pkt + OFFSET_SRC_ADDR) ;
	Serial.print (F("->") );
	print_eth_addr (pkt + OFFSET_DEST_ADDR) ;

	w = * (word *) (pkt + OFFSET_ETHTYPE) ;
	Serial.print (F(" Ethtype: ") );
	Serial.print (w, HEX) ;

	Serial.print (" ") ;

	for (int z = 0 ; z < 10 ; z++)
	{
		Serial.print (' ') ;
		Serial.print (pkt [z + OFFSET_ETHTYPE + 2], HEX) ;
	}
	Serial.println () ;
}

// There is an offset in the reception (2 bytes, see the w5100 datasheet)
int EthernetRaw::get_payload_length(void) {
	return _rbuflen - (OFFSET_DATA + OFFSET_RECEPTION);
}

