"""
This module contains the server CoAP class
"""
import asyncio
import aiocoap.resource as resource
import aiocoap

class HW(resource.Resource):
    """
    Said HELLO
    """
    def __init(self):
        super(HW, self).__init__()

    @asyncio.coroutine
    def render_get(self, request):
        payload = ("Hello World").encode('ascii')
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

class CoAP_Server():
    """
    Initialize CoAP server
    """
    def __init__(self):
        """
        Start the context 
        """
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

        """
        if isinstance(path, str):
            path = self.url_to_tuple(path)

        if cls == 'GO':
            resource_cls = GETONLY_coap
        elif cls == 'HW':
            resource_cls = HW
        else :
            print("No type given for the new coap resource.\nNothing done.")
            return

        if obj == None :
            self._root.add_resource(path,resource_cls())
        else :
            self._root.add_resource(path,resource_cls(obj))

    def remove_path(self, path):
        """
        Remove a path from the CoAP server
        """
        
        if isinstance(path, str):
            path = self.url_to_tuple(path)
            
        self._root.remove_resource(path)

