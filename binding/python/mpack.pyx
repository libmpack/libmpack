from __future__ import unicode_literals
from future.utils import bytes_to_native_str

from libc.string cimport memcpy
from cpython.mem cimport PyMem_Malloc, PyMem_Realloc, PyMem_Free
from cpython cimport array, bool, PY_MAJOR_VERSION, PY_MINOR_VERSION

from cmpack cimport *

import array
import sys


__all__ = ('Packer', 'Unpacker', 'Session', 'pack', 'unpack')


def tobytes(array):
    if PY_MAJOR_VERSION >= 3 and PY_MINOR_VERSION >= 2:
        return array.tobytes()
    return array.tostring()


cdef extern from "mpack-src/mpack.c":

    void MPACK_THROW(mpack_parser_t *parser)
    size_t MPACK_PARSER_STRUCT_SIZE(size_t n)
    size_t MPACK_RPC_SESSION_STRUCT_SIZE(size_t n)
    mpack_node_t* MPACK_PARENT_NODE(mpack_node_t* node)
    mpack_token_t mpack_pack_float(double f)
    double mpack_unpack_float(mpack_token_t tok)


class MpackException(Exception):
    pass


class MpackRecursiveUseException(MpackException):
    def __init__(self):
        msg = (
            'The ext handler tried to invoke its Packer/Unpacker recursively. '
            'If you need to pack/unpack from the ext handler, use the module '
            'functions or another instance of the Packer/Unpacker.'
        )
        super(MpackRecursiveUseException, self).__init__(msg)


class MpackUserException(MpackException):
    def __init__(self, exc):
        msg = 'User callback raised exception: {0}'.format(repr(exc))
        super(MpackUserException, self).__init__(msg)
        self.exc = exc


cdef class Ref:
    cdef public object obj
    cdef public int count
    
    def __init__(self, object obj):
        self.obj = obj
        self.count = 0


cdef class Registry:
    """Base for classes that need to keep python objects alive.

    The goal of this class is to keep python objects alive while being processed
    by C code.
    
    For instance, when the Packer is serializing a dict, it creates an iterator
    over the dict's pairs, and this iterator needs to be kept alive even though
    it is only used by C.
    """
    cdef object registry
    cdef object exception
    cdef bool multiple_refs

    def __cinit__(self):
        self.multiple_refs = False
        self.registry = {}
        self.exception = None

    def __dealloc__(self):
        self.registry = None

    def check_exception(self):
        if self.exception:
            if (isinstance(self.exception, MpackUserException) and
                isinstance(self.exception.exc, MpackRecursiveUseException)):
                raise self.exception.exc
            raise self.exception

    cdef void* ref(self, object obj):
        obj_id = id(obj)
        r = self.registry.get(obj_id, None)
        if not r:
            r = Ref(obj)
            self.registry[obj_id] = r
        if r.count and not self.multiple_refs:
            raise Exception('Object is already referenced')
        r.count += 1
        return <void*>obj

    cdef object unref(self, void* ptr):
        obj_id = id(<object>ptr)
        r = self.registry.get(obj_id, None)
        if not r:
            raise Exception('Object is not referenced')
        assert r.count
        r.count -= 1
        if not r.count:
            self.registry.pop(obj_id)
        return r.obj


cdef class Parser(Registry):
    """Encapsulate logic common to Packer and Unpacker classes.

    The Packer and Unpacker share a common structure: `mpack_parser_t`. This
    class implements logic managing `mpack_parser_t` memory.
    """
    cdef int working
    cdef mpack_parser_t* parser
    cdef object root

    def __cinit__(self):
        self.root = None
        self.working = 0
        self.parser = <mpack_parser_t*>PyMem_Malloc(sizeof(mpack_parser_t))
        if not self.parser:
            raise MemoryError()
        mpack_parser_init(self.parser, 0)
        self.parser.data.p = <void*>self

    def __dealloc__(self):
        if self.parser:
            PyMem_Free(self.parser)

    cdef grow_parser(self):
        cdef mpack_uint32_t new_capacity = self.parser.capacity * 2
        cdef mpack_parser_t* new_parser = <mpack_parser_t*>PyMem_Malloc(
            MPACK_PARSER_STRUCT_SIZE(new_capacity))
        if not new_parser:
            raise MemoryError()
        mpack_parser_init(new_parser, new_capacity);
        mpack_parser_copy(new_parser, self.parser);
        PyMem_Free(self.parser);
        self.parser = new_parser


cdef class Packer(Parser):
    """Encapsulate options/state for serializing python objects to msgpack.
    """
    cdef object ext

    def __init__(self, ext=None):
        if callable(ext):
            self.ext = ext
        elif isinstance(ext, dict):
            def extfn(obj):
                handler = ext.get(type(obj), None)
                if handler:
                    return handler(obj)
                return None
            self.ext = extfn
        else:
            self.ext = None

    def __call__(self, object obj):
        if self.exception:
            raise MpackException(
                "Packer instance has thrown an exception and is invalid."
            )
        cdef array.array[char] buf = array.array(bytes_to_native_str(b'b'),
                                                 [0] * 8)
        cdef size_t pos = self.pack(obj, buf, 0)
        return tobytes(buf)[:pos]

    cdef long pack(self, object obj, array.array[char] buf,
                   size_t pos) except -100:
        if self.working:
            raise MpackRecursiveUseException()

        cdef int result
        cdef size_t bl_init
        cdef char* b = buf.data.as_chars + pos
        cdef size_t bl = len(buf) - pos
        self.root = obj

        if b is NULL:
            raise MemoryError()

        while True:
            bl_init = bl
            self.working = 1
            result = mpack_unparse(self.parser, &b, &bl, unparse_enter,
                                   unparse_exit)
            self.working = 0
            self.check_exception()

            pos += bl_init - bl

            if result == MPACK_NOMEM:
                self.grow_parser()

            if not bl:
                array.resize_smart(buf, len(buf) * 2)
                b = buf.data.as_chars + pos
                bl = len(buf) - pos

            if result not in [MPACK_EOF, MPACK_NOMEM]: break

        self.root = None
        assert len(self.registry) == 0
        return pos


cdef class Unpacker(Parser):
    """Encapsulate options/state for deserializing python objects from msgpack.
    """
    cdef object ext

    def __init__(self, ext=None):
        if callable(ext):
            self.ext = ext
        elif isinstance(ext, dict):
            def extfn(code, data):
                handler = ext.get(code, None)
                if handler:
                    return handler(code, data)
                return None
            self.ext = extfn
        else:
            self.ext = None

    def __call__(self, bytes data, size_t offset=0):
        if self.exception:
            raise MpackException(
                "Unpacker instance has thrown an exception and is invalid."
            )

        if offset >= <size_t>len(data):
            raise ValueError('offset must be less then the input string length')

        self.root = None
        cdef const char* buf_init = data
        cdef const char* buf = <const char*>data + offset
        cdef size_t buflen = len(data) - offset
        cdef int result = self.unpack(&buf, &buflen)
        return self.root, buf - buf_init

    cdef long unpack(self, const char** b, size_t* bl) except -100:
        if self.working:
            raise MpackRecursiveUseException()

        cdef long rv

        while True:
            self.working = 1
            rv = mpack_parse(self.parser, b, bl, parse_enter, parse_exit)
            self.working = 0
            self.check_exception()
            if rv == MPACK_NOMEM:
                self.grow_parser()
            else:
                break

        if rv == MPACK_ERROR:
            raise MpackException('invalid msgpack data')

        return rv


cdef class Session(Registry):
    cdef mpack_rpc_session_t *session
    cdef Packer packer
    cdef Unpacker unpacker
    cdef int type
    cdef mpack_rpc_message_t msg
    cdef void* method_or_error
    cdef void* args_or_result

    def __cinit__(self):
        self.multiple_refs = True
        self.type = MPACK_EOF
        self.method_or_error = NULL
        self.args_or_result = NULL
        self.session = <mpack_rpc_session_t*>PyMem_Malloc(
            sizeof(mpack_rpc_session_t))
        if not self.session:
            raise MemoryError()
        mpack_rpc_session_init(self.session, 0)

    def __dealloc__(self):
        if self.session:
            PyMem_Free(self.session)
    
    def __init__(self, packer=None, unpacker=None):
        self.packer = packer or Packer()
        self.unpacker = unpacker or Unpacker()

    def request(self, method, args, data=None):
        return self.send(method, args, type=MPACK_RPC_REQUEST, data=data)

    def notify(self, method, args):
        return self.send(method, args, type=MPACK_RPC_NOTIFICATION)

    def reply(self, mpack_uint32_t request_id, object data, bool error=False):
        if error:
            return self.send(data, None, type=MPACK_RPC_RESPONSE,
                             data=request_id)
        else:
            return self.send(None, data, type=MPACK_RPC_RESPONSE,
                             data=request_id)
        
    def receive(self, bytes data, size_t offset=0):
        if offset >= <size_t>len(data):
            raise ValueError('offset must be less then the input string length')

        cdef const char* buf_init = data
        cdef const char* buf = <const char*>data + offset
        cdef size_t buflen = len(data) - offset
        done = False

        while not done:
            if self.type == MPACK_EOF:
                self.type = mpack_rpc_receive(self.session, &buf, &buflen,
                                              &self.msg)
                if self.type == MPACK_EOF:
                    break

            result = self.unpacker.unpack(&buf, &buflen)

            if result == MPACK_EOF:
                break

            unpacked = self.unpacker.root

            if self.method_or_error == NULL:
                self.method_or_error = self.ref(unpacked)
            else:
                self.args_or_result = self.ref(unpacked)
                done = True

        pos = buf - buf_init

        if done:
            me = self.unref(self.method_or_error)
            ar = self.unref(self.args_or_result)
            t = self.type
            self.method_or_error = NULL
            self.args_or_result = NULL
            self.type = MPACK_EOF
            if t == MPACK_RPC_REQUEST:
                return pos, 'request', me, ar, self.msg.id
            elif t == MPACK_RPC_RESPONSE:
                return pos, 'response', me, ar, self.unref(self.msg.data.p)
            elif t == MPACK_RPC_NOTIFICATION:
                return pos, 'notification', me, ar, None
            else:
                assert False

        return pos, None, None, None, None

    cdef send(self, method_or_error, args_or_result, int type, data=None):
        cdef array.array[char] buf = array.array(bytes_to_native_str(b'b'),
                                                 [0] * 8)
        cdef char* b = buf.data.as_chars
        cdef size_t bl = len(buf)
        cdef size_t bl_init = bl
        cdef mpack_data_t d

        if type == MPACK_RPC_REQUEST:
            d.p = self.ref(data)

        while True:
            result = -1
            if type == MPACK_RPC_REQUEST:
                result = mpack_rpc_request(self.session, &b, &bl, d)
            elif type == MPACK_RPC_RESPONSE:
                result = mpack_rpc_reply(self.session, &b, &bl, data)
            elif type == MPACK_RPC_NOTIFICATION:
                result = mpack_rpc_notify(self.session, &b, &bl)
            if result == MPACK_NOMEM:
                self.grow_session()
            else:
                break

        assert result == MPACK_OK
        cdef size_t pos = bl_init - bl
        pos = self.packer.pack(method_or_error, buf, pos)
        pos = self.packer.pack(args_or_result, buf, pos)
        return tobytes(buf)[:pos]

    cdef grow_session(self):
        cdef mpack_uint32_t new_capacity = self.session.capacity * 2
        cdef mpack_rpc_session_t* new_session = \
                <mpack_rpc_session_t*>PyMem_Malloc(
                    MPACK_RPC_SESSION_STRUCT_SIZE(new_capacity))
        if not new_session:
            raise MemoryError()
        mpack_rpc_session_init(new_session, new_capacity);
        mpack_rpc_session_copy(new_session, self.session);
        PyMem_Free(self.session);
        self.session = new_session



cdef void unparse_enter(mpack_parser_t* parser, mpack_node_t* node):
    cdef mpack_node_t* parent = MPACK_PARENT_NODE(node)
    cdef Packer packer = <Packer>parser.data.p

    if parent:
        parent_obj = <object>parent.data[0].p

        if parent.tok.type > MPACK_TOKEN_MAP:
            node.tok = mpack_pack_chunk(parent_obj, parent.tok.length)
            return

        if parent.tok.type == MPACK_TOKEN_ARRAY:
            obj = next(parent_obj)
        elif parent.tok.type == MPACK_TOKEN_MAP:
            if parent.key_visited:
                # decrease refcount
                n = packer.unref(parent.data[1].p)
                # store value
                obj = n[1]
            else:
                # fetch the next pair
                n = next(parent_obj)
                # increase refcount
                parent.data[1].p = packer.ref(n)
                # store the key
                obj = n[0]
    else:
        obj = packer.root

    if isinstance(obj, bool):
        node.tok = mpack_pack_boolean(<unsigned>obj)
    elif isinstance(obj, (int, long)):
        if obj >= 0:
            node.tok = mpack_pack_uint(<unsigned long long>obj)
        else:
            node.tok = mpack_pack_sint(<long long>obj)
    elif isinstance(obj, float):
        node.tok = mpack_pack_float(<double>obj)
    elif isinstance(obj, bytes):
        node.tok = mpack_pack_bin(len(obj))
    elif isinstance(obj, unicode):
        obj = obj.encode('utf-8')
        node.tok = mpack_pack_str(len(obj))
    elif isinstance(obj, (list, tuple)):
        node.tok = mpack_pack_array(len(obj))
        obj = iter(obj)
    elif isinstance(obj, dict):
        node.tok = mpack_pack_map(len(obj))
        obj = iter(obj.items())
    elif packer.ext:
        try:
            codeobj = packer.ext(obj)
        except Exception as e:
            packer.exception = MpackUserException(e)
        if packer.exception:
            MPACK_THROW(parser)
        if codeobj[0] is None:
            node.tok = mpack_pack_nil();
            obj = None
        else:
            code, obj = codeobj
            if not isinstance(code, int) or code < 0 or code >= 0x80:
                packer.exception = (
                    MpackException('ext code must be int, >= 0 and < 0x80'))
            if not isinstance(obj, bytes):
                packer.exception = (
                    MpackException('ext data must be a byte string'))
            if packer.exception:
                MPACK_THROW(parser)
            node.tok = mpack_pack_ext(code, len(obj))

    else:
        node.tok = mpack_pack_nil();

    node.data[0].p = packer.ref(obj)


cdef void unparse_exit(mpack_parser_t* parser, mpack_node_t* node):
    cdef Packer packer = <Packer>parser.data.p
    if node.tok.type != MPACK_TOKEN_CHUNK:
        packer.unref(node.data[0].p)


cdef void parse_enter(mpack_parser_t* parser, mpack_node_t* node):
    cdef array.array strbuf
    cdef char* b
    cdef mpack_node_t* parent
    cdef Unpacker unpacker = <Unpacker>parser.data.p
    obj = None

    if node.tok.type == MPACK_TOKEN_BOOLEAN:
        obj = True if mpack_unpack_boolean(node.tok) else False
    elif node.tok.type == MPACK_TOKEN_UINT:
        obj = mpack_unpack_uint(node.tok)
    elif node.tok.type == MPACK_TOKEN_SINT:
        obj = mpack_unpack_sint(node.tok)
    elif node.tok.type == MPACK_TOKEN_FLOAT:
        obj = mpack_unpack_float(node.tok)
    elif node.tok.type == MPACK_TOKEN_CHUNK:
        parent = MPACK_PARENT_NODE(node)
        strbuf = <array.array>parent.data[0].p
        b = strbuf.data.as_chars
        memcpy(b + parent.pos, node.tok.data.chunk_ptr, node.tok.length)
        return
    elif node.tok.type in [MPACK_TOKEN_BIN, MPACK_TOKEN_STR, MPACK_TOKEN_EXT]:
        obj = array.array(bytes_to_native_str(b'b'), node.tok.length * [0])
    elif node.tok.type == MPACK_TOKEN_ARRAY:
        obj = []
    elif node.tok.type == MPACK_TOKEN_MAP:
        obj = {}
    else:
        assert node.tok.type == MPACK_TOKEN_NIL

    node.data[0].p = unpacker.ref(obj)


cdef void parse_exit(mpack_parser_t* parser, mpack_node_t* node):
    if node.tok.type == MPACK_TOKEN_CHUNK:
        return

    cdef Unpacker unpacker = <Unpacker>parser.data.p
    obj = unpacker.unref(node.data[0].p)

    if node.tok.type in [MPACK_TOKEN_BIN, MPACK_TOKEN_STR, MPACK_TOKEN_EXT]:
        obj = tobytes(obj)
        if node.tok.type == MPACK_TOKEN_STR:
            obj = obj.decode('utf-8')
        elif node.tok.type == MPACK_TOKEN_EXT:
            code = node.tok.data.ext_type
            if unpacker.ext:
                try:
                    obj = unpacker.ext(code, obj)
                except Exception as e:
                    unpacker.exception = MpackUserException(e)
            else:
                obj = None

    cdef mpack_node_t* parent = MPACK_PARENT_NODE(node)
    cdef list l
    cdef dict d

    if parent:
        if parent.tok.type == MPACK_TOKEN_ARRAY:
            l = <list>parent.data[0].p
            l.append(obj)
        elif parent.tok.type == MPACK_TOKEN_MAP:
            if parent.key_visited:
                # save key on registry
                parent.data[1].p = unpacker.ref(obj)
            else:
                # set pair
                d = <dict>parent.data[0].p
                k = unpacker.unref(parent.data[1].p)
                d[k] = obj
    else:
        unpacker.root = obj


def unpack(data):
    cdef Unpacker unpacker = Unpacker()
    obj, offset = unpacker(data)
    if offset > len(data):
        raise ValueError('Trailing data in msgpack string')
    elif offset < len(data):
        raise ValueError('Invalid msgpack string')
    return obj


def pack(obj):
    cdef Packer packer = Packer()
    return packer(obj)
