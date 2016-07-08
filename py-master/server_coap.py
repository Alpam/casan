"""
This module contains the server CoAP class
"""
import datetime
import asyncio
import aiocoap.resource as resource
import aiocoap
import msg
import option
import observer

class CASAN_slave(resource.Resource,object):
    """
    creat a path to a slave
    _cache is the current server cache
    _engine is the current description of the server
    """
    def __init__(self,list):
        super(CASAN_slave, self).__init__()
        self._cache = list[1]
        self._engine = list[0]

    def build_request(self,request):
        #XXX the path reception is weak, only one pattern is allowed **/sid/res
        #cf CoAP_Server, new_resource
        """
        ll_ssp is a list of two aiocoap object, the following code extract the uri
        from them.
        """
        
        ll_ssp=list(request.opt._options.keys())
        list_ssp=list()
        for ssp in request.opt._options[ll_ssp[0]]:
            list_ssp.append(ssp)
        vpath=list()
        vpath.append(str(list_ssp[-1]))
        meth = str(request.code)
        sid = str(list_ssp[-2])

        #
        # Find slave and resource
        #

        sl = self._engine.find_slave (sid)
        if sl is None:
            raise aiocoap.error.NoResource ()

        if sl is None or not sl.isrunning ():
            raise aiocoap.error.NoResource ()

        res = sl.find_resource (vpath)
        if res is None:
            return None

        #
        # Build request
        #

        mreq = msg.Msg ()
        mreq.peer = sl.addr
        mreq.l2n = sl.l2n
        mreq.msgtype = msg.Msg.Types.CON
        mreq.payload = request.payload

        if meth == 'GET':
            mreq.msgcode = msg.Msg.Codes.GET
        elif meth == 'POST':
            mreq.msgcode = msg.Msg.Codes.POST
        elif meth == 'DELETE':
            mreq.msgcode = msg.Msg.Codes.DELETE
        elif meth == 'PUT':
            mreq.msgcode = msg.Msg.Codes.PUT
        else:
            raise aiocoap.error.NoResource ()

        up = option.Option.Codes.URI_PATH
        for p in vpath:
            mreq.optlist.append (option.Option (up, optval=p))

        return mreq

    @asyncio.coroutine
    def render_put(self,request):
        mreq=self.build_request(request)
        mrep = yield from mreq.send_request ()
        payload = mrep.payload.decode ().encode ('ascii')
        return aiocoap.Message(code=aiocoap.CHANGED, payload=payload)

    @asyncio.coroutine
    def render_post(self,request):
        mreq=self.build_request(request)
        mrep = yield from mreq.send_request ()
        payload = mrep.payload.decode ().encode ('ascii')
        return aiocoap.Message(code=aiocoap.CHANGED, payload=payload)

    @asyncio.coroutine
    def render_delete(self,request):
        mreq=self.build_request(request)
        mrep = yield from mreq.send_request ()
        payload = mrep.payload.decode ().encode ('ascii')
        return aiocoap.Message(code=aiocoap.CONTENT, payload=payload)

    @asyncio.coroutine
    def render_get(self, request):
        mreq=self.build_request(request)
        #
        # Is the request already present in the cache?
        #
        mc = self._cache.get (mreq)
        if mc is not None:
            # Request found in the cache
            mreq = mc
            mrep = mc.req_rep

        else:
            # Request not found in the cache: send it and wait for a result
            mrep = yield from mreq.send_request ()

            if mrep is not None:
                # Add the request (and the linked answer) to the cache
                self._cache.add (mreq)
            else:
                return aiocoap.error.RequestTimedOut (Error)
        # Python black magic: aiohttp.web.Response expects a
        # bytes argument, but mrep.payload is a bytearray
        payload = mrep.payload.decode ().encode ('ascii')

        return aiocoap.Message(code=aiocoap.CONTENT, payload=payload)

class GETONLY_coap(resource.Resource,object):
    """
    Return the asked file
    """
    def __init__(self, txt):
        super(GETONLY_coap, self).__init__()
        self._txt = txt

    @asyncio.coroutine
    def render_get(self, request):
        payload = str(self._txt).encode('ascii')
        return aiocoap.Message(code=aiocoap.CONTENT, payload=payload)

class GET_casan(resource.Resource,object):
    """
    Return the asked file
    """
    def __init__(self, txt):
        super(GET_casan, self).__init__()
        self._txt = txt

    @asyncio.coroutine
    def render_get(self, request):
        payload = self._txt.resource_list ().encode()
        return aiocoap.Message(code=aiocoap.CONTENT, payload=payload)

    @asyncio.coroutine
    def render_delete(self, request):
        payload = self._txt.resource_list ().encode()
        return aiocoap.Message(code=aiocoap.CONTENT, payload=payload)

"""
#XXX
l'observation ne fonctionne pas il faut rajouter 2 events dans la boulce
principale d'asycio. Un pour stopper l'esclave lors de la reception d'un
code RST. Et un pour passer le message de l'esclave vers le client (et
mettre la valeur dans le cache.). Pour cette partie si il faut modifier la
fonction _l2reader dans engine.py qui reçoit les messages provenant de
l'esclave
"""
"""
class OBS_CASAN_slave(resource.ObservableResource,object):
    def __init__(self,list):
        super(OBS_CASAN_slave, self).__init__()
        self._engine = list[0]
        self._cache = list[1]
        self.obs = None
        self._token = None
        self._event = None

    def start_obs_res(self,request):
        ll_ssp=list(request.opt._options.keys())
        list_ssp=list()
        for ssp in request.opt._options[ll_ssp[0]]:
            list_ssp.append(ssp)
        vpath=list()
        vpath.append(str(list_ssp[-1]))
        meth = str(request.code)
        token=request.token
        self._token = token
        sid = str(list_ssp[-2])
        qs = request.payload

        #
        # Find slave
        #

        sl = self._engine.find_slave (sid)
        if sl is None:
            raise aiocoap.error.NotObservable ()

        self.obs = sl.find_observer (vpath, token)

        #
        # Find slave and resource
        #

        if self.obs is None:
            self.obs = observer.Observer (sl, vpath, token)
            sl.add_observer (self.obs)
        #
        # Build request
        #

        mreq = msg.Msg ()
        mreq.peer = sl.addr
        mreq.l2n = sl.l2n
        mreq.msgtype = msg.Msg.Types.CON
        mreq.payload = request.payload

        if meth == 'GET':
            mreq.msgcode = msg.Msg.Codes.GET
        else:
            raise aiocoap.error.NoResource ()

        up = option.Option.Codes.URI_PATH
        for p in vpath:
            mreq.optlist.append (option.Option (up, optval=p))

        return mreq

    #@asyncio.coroutine
    #def until_the_end(self, mreq):

    @asyncio.coroutine
    def render_get(self, request):

        #self._event = asyncio.Event()

        mreq = self.start_obs_res(request)
        mc = self._cache.get (mreq)
        if mc is not None:
            # Request found in the cache
            mreq = mc
            mrep = mc.req_rep

        else:
            # Request not found in the cache: send it and wait for a result
            mrep = yield from mreq.send_request ()

            if mrep is not None:
                # Add the request (and the linked answer) to the cache
                self._cache.add (mreq)
            else:
                return aiocoap.error.RequestTimedOut (Error)
        # Python black magic: aiohttp.web.Response expects a
        # bytes argument, but mrep.payload is a bytearray
        payload = mrep.payload.decode ().encode ('ascii')
        return aiocoap.Message(code=aiocoap.CONTENT, payload=payload)
"""

class CoAP_Server(object):
    """
    Initialize CoAP server
    """
    def __init__(self,master):
        """
        Start the context 
        """
        self._master = master
        self._root = resource.Site()
        self._root.add_resource(('.well-known','core'), resource.WKCResource(self._root.get_resources_as_linkheader))
        asyncio.async(aiocoap.Context.create_server_context(self._root))

    def url_to_tuple(self, path):
        """
        Change an url of coap by removing the '/' and 'coap:'
        if it's present and convert it in a tuple
        """
        path = path.split('/')
        tuple_path=[]
        for word in path:
            if not(word == 'coap:') and not(word == '') and not(word == ""):
                tuple_path.append(word)
        return tuple(tuple_path)

    def new_resource(self, path, cls, obj=None):
        """
        Take a path as an urlor a tuple and creat the asked resource.
        XXX add a handler for dictionnary like pipe for the coap path during
        a slave addition
        path can be the string or a tuple, it describ the uri
        cls is a string who describ the called classe
        obj is a string who describ  the object needed by the called classe
        """
        if isinstance(path, str):
            path = self.url_to_tuple(path)

        if cls == "GO":
            resource_cls = GETONLY_coap
        elif cls == "GC":
            resource_cls = GET_casan
        elif cls == "casan_slave":
            resource_cls = CASAN_slave
        #elif cls == "obs_slave":
        #   resource_cls =OBS_CASAN_slave
        else :
            print("No type given for the new coap resource.\nNothing done.")
            return

        if obj == None :
            self._root.add_resource(path,resource_cls())
            return
        elif obj == "conf" :
            obj = self._master._conf
        elif obj == "cache" :
            obj = self._master._cache
        elif obj == "engine" :
            obj = self._master._engine
        elif obj == "create_res" :
            obj = [self._master._engine, self._master._cache]

        self._root.add_resource(path,resource_cls(obj))
        return

    def remove_path(self, path):
        """
        Remove a path from the CoAP server
        """

        if isinstance(path, str):
            path = self.url_to_tuple(path)

        self._root.remove_resource(path)

