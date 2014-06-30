"""
This module contains the Msg class and a few helper functions that it needs.
"""
from datetime import datetime, timedelta
from sos import l2
from enum import Enum
from util.debug import *
from .option import Option
from random import randrange

glob_msg_id = randrange(0xFFFF)

# CoAP Related constants
COAP_MAX_TOKLEN = 8
MAX_RETRANSMIT = 4

# SOS related constants
SOS_VERSION = 1
SOS_NAMESPACE1 = '.well-known'
SOS_NAMESPACE2 = 'sos'
sos_namespace = (SOS_NAMESPACE1, SOS_NAMESPACE2)

# Some helper functions that don't really belong to the msg clas
def coap_ver(mess):
    """
    Extract the CoAP protocol version from the frame header.
    """
    return (mess[0] >> 6) & 0x03

def coap_type(mess):
    """
    Extract the CoAP message type from the frame header.
    """
    return Msg.MsgTypes((mess[0] >> 4) & 0x03)

def coap_toklen(mess):
    """
    Extract the CoAP token length from the frame header.
    """
    return mess[0] & 0x0F

def coap_id(mess):
    """
    Extract the CoAP message ID from the frame header.
    """
    return (mess[2] << 8) | mess[3]

def coap_code(mess):
    """
    Extract the CoAP method code from the frame header.
    """
    return Msg.MsgCodes(mess[1])

class Msg:
    """
    An object of class Msg represents a message,
    either received or to be sent

    This class represents messages to be sent to the network, or
    received from the network.

    Message attributes are tied to CoAP specification: a message has
    a type (CON, NON, ACK, RST), a code (GET, POST, PUT, DELETE, or a numeric
    value for an answer in an ACK), an Id, a token, some options and
    a payload.

    In order to be sent to the network, a message is transparently
    encoded (by the `send` method) according to CoAP specification.
    Similarly, a message is transparently decoded (by the `recv`
    method) upon reception according to the CoAP specification.

    The binary (encoded) form is kept in the message private variable.
    However, each modification of an attribute will squeeze the
    binary form, which will be automatically re-created if needed.

    Each request sent has a "waiter" which represents a thread to
    awake when the answer will be received.

    @bug should wake the waiter if a request is abandoned due to a time-out
    """
    class MsgTypes(Enum):
        MT_CON = 0
        MT_NON = 1
        MT_ACK = 2
        MT_RST = 3
        
    class MsgCodes(Enum):
        MC_EMPTY = 0
        MC_GET = 1
        MC_POST = 2
        MC_PUT = 3
        MC_DELETE = 4
        
    class SosTypes(Enum):
        SOS_NONE = 0
        SOS_DISCOVER = 1
        SOS_ASSOC_REQUEST = 2
        SOS_ASSOC_ANSWER = 3
        SOS_HELLO = 4
        SOS_UNKNOWN = 5

    def __eq__(self, other):
        """
        Equality test operator. Valid for received messages only.
        """
        return self.msg == other.msg_

    def __str__(self):
        """
        Cast to string, allows printing of packet data.
        """
        expstr = str(self.expire - datetime.now())
        ntostr = str(self.next_timeout - datetime.now())
        return ('msg <id=' + str(self.id) + ', toklen=' + str(self.toklen) + ', paylen=' +
                str(self.paylen) + ', ntrans=' + str(self.ntrans) + ', expire=' + expstr +
                ', next_timeout=' + str(ntostr))
    def __init__(self):
        """
        Default constructor
        """
        self.req_rep = None
        self.code = self.MsgCodes.MC_EMPTY
        self.reset_all()

    def reset_all(self):
        """
        Resets the class instance to it's default state.
        """
        self.reset_data()
        self.reset_values()

    def reset_values(self):
        """
        Resets all the values of the class to their defaults, but keeps
        the message binary data, the payload and the options list.
        """
        self.peer = None
        self.req_rep = None
        self.msg, self.msglen = None, 0
        self.payload, self.paylen = None, 0
        self.toklen = 0
        self.ntrans = 0
        self.timeout = timedelta()
        self.next_timeout = datetime.max
        self.expire = datetime.max
        self.pk_t = l2.pktype.PK_NONE
        self.sos_type = self.SosTypes.SOS_UNKNOWN
        self.id = 0

    def reset_data(self):
        """
        Erases the data stored in the object (mesage, payload...)
        """
        self.msg, self.msglen = None, 0
        self.payload, self.paylen = None, 0
        if self.req_rep != None:
            self.req_rep.req_rep = None
            self.req_rep = None
        self.optlist = []

    def recv(self, l2n):
        """
        Receives a message, store it and decode it according to CoAP spec.
        :return : source address
        """
        self.reset_values()
        self.msg = bytearray(l2n.mtu)
        self.pk_t, packet = l2n.recv()
        self.msglen = packet[2]
        self.msg = packet[1]
        if ((self.pk_t in [l2.pktype.PK_ME, l2.pktype.PK_BCAST])
            and self.coap_decode()):
            print_debug(dbg_levels.MESSAGE, 'Valid recv -> ' +
                        self.pk_t.name + ', id=' + str(self.id) +
                        ', len=' + str(self.msglen))
        else:
            print_debug(dbg_levels.MESSAGE, 'Invalid recv -> ' + self.pk_t.name +
                        ', id=' + str(self.id) + ', len=' + str(self.msglen))
        return packet[0]


    def coap_decode(self):
        """
        Decode a CoAP message.
        """
        if coap_ver(self.msg) != SOS_VERSION:
            return False
        self.msg_type = coap_type(self.msg)
        self.toklen = coap_toklen(self.msg)
        self.id = coap_id(self.msg)
        self.code = coap_code(self.msg)
        i = 4 + self.toklen
        if self.toklen > 0:
            self.token = self.msg[4:i]

        success, opt_nb = True, 0
        while i < self.msglen and success and self.msg[i] != 0xFF:
            opt_delta = self.msg[i] >> 4
            opt_len = self.msg[i] & 0x0F
            i = i + 1
            # Handle special values for optdelta/optlen
            if opt_delta == 13:
                opt_delta = self.msg[i] + 13
                i = i + 1
            elif opt_delta == 14:
                opt_delta = ((self.msg[i] << 8) | self.msg[i+1]) + 269
                i = i + 2
            elif opt_delta == 15:
                success = False
            opt_nb = opt_nb + opt_delta
            if opt_len == 13:
                opt_len = self.msg[i] + 13
                i = i + 1
            elif opt_len == 14:
                opt_len = ((self.msg[i] << 8) | self.msg[i+1]) + 269
                i = i + 2
            elif opt_len == 15:
                success = False
            if success:
                print_debug(dbg_levels.OPTION, 'OPTION opt=' + str(opt_nb) +
                            ', len=' + str(opt_len))
                try:
                    o = Option(opt_nb, self.msg[i:], opt_len)
                    self.optlist.append(o)
                except ValueError as e:
                    print_debug(dbg_levels.OPTION, 'Error while decoding '
                                                   'message : invalid value for option.' )
                    success = False
                except Exception as e:
                    print_debug(dbg_levels.OPTION, 'Error while decoding '
                                                   'message : ' + str(e))
                    success = False
                i = i + opt_len
            else: print_debug(dbg_levels.OPTION, 'Unknown option')

        self.paylen = self.msglen - i - 1 # Mind the 0xFF marker
        if success and self.paylen > 0:
            if self.msg[i] != 0xFF: # Oops
                success = False
            else:
                self.payload = self.msg[i+1:]
        else: self.paylen = 0
        return success

    def send(self):
        """
        Sends a CoAP message on the network.
        """
        if self.msg == None:
            self.coap_encode()
        print_debug(dbg_levels.MESSAGE, 'TRANSMIT id=' + str(self.id) +
                    ', ntrans=' + str(self.ntrans))


    def coap_encode(self):
        """
        Encodes a message according to CoAP spec
        """
        # Compute message size
        self.msglen = 4 + self.toklen
        self.optlist.sort()
        opt_code = 0
        for opt in self.optlist:
            opt_delta = opt.optcode - opt_code
            self.msglen = opt.optlen + 1
            if opt_delta > 12:
                self.msglen = self.msglen + 1
            elif opt_delta > 268:
                self.msglen = self.msglen + 2
            opt_code = opt.optcode

            opt_len = opt.optlen
            if opt_len > 12:
                self.msglen = opt.optlen + 1
            elif opt_len > 268:
                self.msglen = opt.optlen + 2
            self.msglen = self.msglen + opt_len
            if opt.paylen > 0:
                self.msglen = self.msglen + opt.paylen + 1

        # Compute an ID
        global glob_msg_id
        if self.id == 0:
            self.id = glob_msg_id
            glob_msg_id = glob_msg_id + 1 if glob_msg_id < 0xFFFF else 1

        # Build the message
        self.msg = bytearray()
        self.msg[0] = (SOS_VERSION << 6) | (self.msg_type << 4) | self.toklen
        self.msg[1] = self.code
        self.msg[2] = (self.id & 0xFF00) >> 8
        self.msg[3] = self.id & 0xFF
        if self.toklen > 0:
            self.msg.append(self.token.to_bytes(self.toklen, 'big'))
        opt_nb = 0
        for opt in self.optlist:
            optheader = 0
            opt_delta = opt.optcode - opt_nb
            opt_len = opt.optlen
            optheader = (optheader | (opt_delta << 8) if opt_delta < 13
                         else optheader | (13 << 8) if opt_delta < 269
            else optheader | (14 << 8))
            optheader = (optheader | (opt_len << 8) if opt_len < 13
                         else optheader | (13 << 8) if opt_len < 269
            else optheader | (14 << 8))
            self.msg.append(optheader)
            if 13 <= opt_delta < 269:
                self.msg.append((opt_delta - 13).to_bytes(1, 'big'))
            elif opt_delta >= 269:
                self.msg.append((opt_delta - 269).to_bytes(2, 'big'))
            if 13 <= opt_len < 269:
                self.msg.append((opt_len - 13).to_bytes(1, 'big'))
            elif opt_len >= 269:
                self.msg.append((opt_len - 269).to_bytes(2, 'big'))
            self.msg.append(opt.optval)
        if self.paylen > 0:
            self.msg.append(b'\xFF')
            self.msg.append(self.payload)
        self.ntrans = 0

    def stop_retransmit(self):
        """
        Prevents the message from being retransmitted.
        """
        self.ntrans = MAX_RETRANSMIT

    def max_age(self):
        """
        Looks for the option Max-Age in the option list and returns it's value
        if any, else, returns None.
        """
        for opt in self.optlist:
            if opt.optcode is Option.optcodes.MO_MAX_AGE:
                return opt.optval
        return None

    def cache_match(self, other):
        """
        Checks whether two messages match for caching.
        See CoAP spec (5.6)
        """
        if self.msg_type != other.msg_type:
            return False
        else:
            # Sort both option lists
            self.optlist.sort()
            other.optlist.sort()
            i, j, imax, jmax = 0, 0, len(self.optlist)-1, len(other.optlist)-1
            while self.optlist[i].is_nocachekey() and i <= imax:
                i = i + 1
            while other.optlist[j].is_nocachekey() and j <= jmax:
                j = j + 1
            while True:
                if j == jmax and i == imax: # Both at the end : success!
                    return True
                elif j == jmax or i == imax: # One at the end : failure
                    return False
                elif self.optlist[i] == other.optlist[j]:
                    i, j = i + 1, j + 1
                else: # No match : failure
                    return False

    @staticmethod
    def link_req_rep(m1, m2):
        """
        Mutually link a request message and it's reply.
        """
        if m2 is None:
            if m1.req_rep is not None:
                # TODO : figure what the hell the C++ code does
                pass
            m1.req_rep = None
        else:
            m1.req_rep = m2
            m2.req_rep = m1

    def is_sos_ctrl_msg(self):
        r = True
        count = 0
        for i, opt in enumerate(self.optlist):
            if opt.optcode == Option.optcodes.MO_URI_PATH:
                r = False
                if i >= len(sos_namespace): break
                if len(sos_namespace[i]) != opt.optlen: break
                if sos_namespace[i] != opt.optval.decode(): break
                r = True
                count += 1
        if r and (count != len(sos_namespace)):
            r = False
        print_debug(dbg_levels.MESSAGE, 'It\'s ' + ('' if r else 'not ') + 'a control message.')
        return r

    def is_sos_discover(self):
        """
        Checks if a message is a SOS discover message.
        :return: a 3-tuple whose first field is True if the message is a SOS discover message, False otherwise.
                 If the message is a SOS discover, then then second field contains the slave id, and the third field
                 contains the mtu.
        """
        sid, mtu = (0, 0)
        if self.code is Msg.MsgCodes.MC_POST and self.msg_type is Msg.MsgTypes.MT_NON and self.is_sos_ctrl_msg():
            for opt in self.optlist:
                if opt.optcode == Option.optcodes.MO_URI_QUERY:
                    if opt.optval.startswith(b'slave='):
                        # Expected value : 'slave=<slave ID>'
                        sid = int(opt.optval.partition(b'=')[2])
                    elif opt.optval.startswith(b'mtu='):
                        mtu = int(opt.optval.partition(b'=')[2])
                    else:
                        sid = 0
                        break
        if sid > 0 and mtu >= 0:
            self.sos_type = self.SosTypes.SOS_DISCOVER
        r = (self.sos_type == self.SosTypes.SOS_DISCOVER,)
        if r[0]:
            r = (r, sid, mtu)
        return r

    def is_sos_associate(self):
        """
        Checks if a message is a SOS associate message.
        :return: True if the message is a SOS associate message, False otherwise.
        """
        found = False
        if self.sos_type is self.SosTypes.SOS_UNKNOWN:
            if (self.msg_type, self.code, self.is_sos_ctrl_msg()) == (self.MsgTypes.MT_NON, self.MsgCodes.MC_POST, True):
                for opt in self.optlist:
                    if opt.optcode is Option.optcodes.MO_URI_QUERY:
                        # TODO: maybe use regular expressions here
                        found = True in (opt.optval.startswith(pattern) for pattern in ['mtu=', 'ttl='])
                if found:
                    self.sos_type = self.SosTypes.SOS_ASSOC_REQUEST
        return self.sos_type == self.SosTypes.SOS_ASSOC_REQUEST

    def sos_type(self, check_req_rep = True):
        if self.sos_type is self.SosTypes.SOS_UNKNOWN:
            if not self.is_sos_associate() and not self.is_sos_discover():
                if check_req_rep and self.req_rep is not None:
                    st = self.req_rep.sos_type(False)
                    if st is self.SosTypes.SOS_ASSOC_REQUEST:
                        self.sos_type = self.SosTypes.SOS_ASSOC_ANSWER
            if self.sos_type is self.SosTypes.SOS_UNKNOWN:
                self.sos_type = self.SosTypes.SOS_NONE
            if self.sos_type is not self.SosTypes.SOS_NONE:
                print_debug(dbg_levels.MESSAGE, 'Got SOS control message.')
        return self.sos_type

    def add_path_ctrl(self):
        for namespace in sos_namespace:
            self.optlist.append(Option(Option.optcodes.MO_URI_PATH, namespace, len(namespace)))

    def mk_ctrl_assoc(self, ttl, mtu):
        self.add_path_ctrl()
        s = 'ttl=' + str(ttl)
        self.optlist.append(Option(Option.optcodes.MO_URI_PATH, s, len(s)))
        s = 'mtu=' + str(mtu)
        self.optlist.append(Option(Option.optcodes.MO_URI_PATH, s, len(s)))