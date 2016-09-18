.. vim: ft=doctest
Python binding to libmpack
==========================


Usage
=====

Packer
------

An instance of the `Packer` class is callable and accepts a python object as
argument, returning a msgpack representation for it. Examples:

>>> from mpack import Packer
>>> p = Packer()
>>> p(None)
b'\xc0'
>>> p(False)
b'\xc2'
>>> p(True)
b'\xc3'
>>> p(5)
b'\x05'
>>> p(-5)
b'\xfb'
>>> p(5.)
b'\xca@\xa0\x00\x00'
>>> p(5.55555555)
b'\xcb@\x168\xe3\x8d\xd9q\xf7'
>>> p(u'unicode string')
b'\xaeunicode string'
>>> p(b'byte string')
b'\xc4\x0bbyte string'
>>> p([1, 2, 3])
b'\x93\x01\x02\x03'
>>> p([1, (2, (3, 4), 5), 6])
b'\x93\x01\x93\x02\x92\x03\x04\x05\x06'
>>> p({u'k': [1, 2, {b'k2': [u'v2']}]})
b'\x81\xa1k\x93\x01\x02\x81\xc4\x02k2\x91\xa2v2'

Unpacker
--------

An instance of the `Unpacker` class is callable and accepts a bytestring and
optional offset as arguments. It returns a tuple containing the deserialized
python object and the next offset in the input bytestring(which can be used as
the next offset input when unpacking streams. Examples:

>>> from mpack import Unpacker
>>> u = Unpacker()
>>> u(b'\xc0')
(None, 1)
>>> u(b'\xc2')
(False, 1)
>>> u(b'\xc3')
(True, 1)
>>> u(b'\x05')
(5, 1)
>>> u(b'\xfb')
(-5, 1)
>>> u(b'\xca@\xa0\x00\x00')
(5.0, 5)
>>> u(b'\xcb@\x168\xe3\x8d\xd9q\xf7')
(5.55555555, 9)
>>> u(b'\xaeunicode string')
(u'unicode string', 15)
>>> u(b'\xc4\x0bbyte string')
(b'byte string', 13)
>>> u(b'\x93\x01\x02\x03')
([1, 2, 3], 4)
>>> u(b'\x93\x01\x93\x02\x92\x03\x04\x05\x06')
([1, [2, [3, 4], 5], 6], 9)
>>> u(b'\x81\xa1k\x93\x01\x02\x81\xc4\x02k2\x91\xa2v2')
({u'k': [1, 2, {b'k2': [u'v2']}]}, 15)

Ext types
---------

Ext types are handled by passing the `ext` parameter to `Packer`/`Unpacker`
constructors:

>>> import sys
>>> from mpack import pack, unpack
>>> class MyType(object):
...  def __init__(self, a, b, c):
...   self.a = a
...   self.b = b
...   self.c = c
...  def __repr__(self):
...   return '<MyType {a} {b} {c}>'.format(**self.__dict__)
...
>>> def pack_handler(o):
...  return 5, pack([o.a, o.b, o.c])
>>> p = Packer(ext={MyType: pack_handler})
>>> p(MyType(1, 2, 3))
b'\xd6\x05\x93\x01\x02\x03'
>>> def unpack_handler(c, d):
...  return MyType(*unpack(d))
>>> u = Unpacker(ext={5: unpack_handler})
>>> u(b'\xd6\x05\x93\x01\x02\x03')
(<MyType 1 2 3>, 6)

It is also possible to pass a function to the `ext` parameter:

>>> def generic_pack_handler(obj):
...  if isinstance(obj, MyType):
...   return 5, pack([obj.a, obj.b, obj.c])
>>> p = Packer(ext=generic_pack_handler)
>>> p(MyType(1, 2, 3))
b'\xd6\x05\x93\x01\x02\x03'
>>> def generic_unpack_handler(code, data):
...  if code == 5:
...   return MyType(*unpack(data))
>>> u = Unpacker(ext=generic_unpack_handler)
>>> u(b'\xd6\x05\x93\x01\x02\x03')
(<MyType 1 2 3>, 6)

Note that the code returned by the ext packer must follow the msgpack
specification:

>>> p1 = Packer(ext=lambda obj: (0x80, b'',))
>>> p1(MyType(1, 2, 3))
Traceback (most recent call last):
  ...
MpackException: ext code must be int, >= 0 and < 0x80
>>> p2 = Packer(ext=lambda obj: (0, 1,))
>>> p2(MyType(1, 2, 3))
Traceback (most recent call last):
  ...
MpackException: ext data must be a byte string

The pack/unpack handlers cannot recursively invoke their Packer/Unpacker
instances:
>>> p3 = Packer(ext=lambda obj: (0x80, p3([1,2]),))
>>> p3(MyType(1, 2, 3))
Traceback (most recent call last):
  ...
MpackRecursiveUseException: The ext handler tried to invoke its Packer/Unpacker recursively. If you need to pack/unpack from the ext handler, use the module functions or another instance of the Packer/Unpacker.

Users can also raise exception from pack/unpack handler:

>>> def packer_exception(obj):
...  raise Exception('packer exception')
>>> p4 = Packer(ext=packer_exception)
>>> p4(MyType(1, 2, 3))
Traceback (most recent call last):
  ...
MpackUserException: User callback raised exception: Exception('packer exception',)
>>> def unpacker_exception(code, data):
...  raise Exception('unpacker exception')
>>> u2 = Unpacker(ext=unpacker_exception)
>>> u2(b'\xd6\x05\x93\x01\x02\x03')
Traceback (most recent call last):
  ...
MpackUserException: User callback raised exception: Exception('unpacker exception',)

RPC
---

A `Session` instances represents a msgpack-rpc session, and is usually
associated with a socket or byte stream.

The `request` method accepts a method name and argument array, and it returns a
byte string representing the request:

>>> from mpack import Session
>>> s = Session()
>>> s.request(u'req1', [1, 2])
b'\x94\x00\x00\xa4req1\x92\x01\x02'
>>> s.request(u'req2', [3, 4])
b'\x94\x00\x01\xa4req2\x92\x03\x04'
>>> s.request(u'req3', [5, 6])
b'\x94\x00\x02\xa4req3\x92\x05\x06'

Notice how the third byte is automatically incremented, it represents the
request id. `Session` instances keep track of all outgoing requests and can map
those to incoming responses through the `data` argument of `Session.request`.
For example:

>>> def my_callback(): pass
...
>>> s.request(u'add', [1, 2], data=my_callback)
b'\x94\x00\x03\xa3add\x92\x01\x02'

In the above example we are binding `my_callback` to the request, and when a
response is received we'll get it back:

>>> s.receive(b'\x94\x01\x03\xc0\x03')
(5, u'response', None, 3, <function my_callback at 0xffffff>)

The `receive` method should be passed a byte string and optionally an offset,
and it returns a 5-tuple with:

- the new offset in the string after unpacking the message
- the type of message(the strings 'request', 'response' or 'notification')
- the method name if a request or notification, error or `None` if a response.
- the method arguments if a request or notification, the result or `None` if a
  response
- the data object passed to the original request if a response, the message id
  if a request, `None` if a notification.

Some examples:

>>> s.receive(b'\x94\x00\x00\xa7increq1\x92\x01\x02')
(14, u'request', u'increq1', [1, 2], 0)
>>> s.receive(b'garbage\x93\x02\xa7incnot1\x92\x01\x02', offset=7)
(20, u'notification', u'incnot1', [1, 2], None)

To reply an incoming request, simply pass the request id as first argument, the
result as second argument, and optional flag indicating if the response is an
error:

>>> s.reply(0, u'result')
b'\x94\x01\x00\xc0\xa6result'
>>> s.reply(0, u'err!', error=True)
b'\x94\x01\x00\xa4err!\xc0'

Sessions can be created with custom Packer/Unpacker instances. This is required
if you want to pack/unpack custom types:

>>> p = Packer(ext={MyType: lambda o: (5, pack([o.a, o.b, o.c]))})
>>> u = Unpacker(ext={5: lambda c, d: MyType(*unpack(d))})
>>> s = Session(packer=p, unpacker=u)
>>> s.request(u'req1', [MyType(1, 2, 3)])
b'\x94\x00\x00\xa4req1\x91\xd6\x05\x93\x01\x02\x03'
>>> s.request(u'req1', [MyType(4, 5, 6)])
b'\x94\x00\x01\xa4req1\x91\xd6\x05\x93\x04\x05\x06'
>>> s.receive(b'\x94\x00\x00\xa7increq1\x91\xd6\x05\x93\x01\x02\x03')
(18, u'request', u'increq1', [<MyType 1 2 3>], 0)
>>> s.receive(b'garbage\x93\x02\xa7incnot1\x91\xd6\x05\x93\x04\x05\x06', offset=7)
(24, u'notification', u'incnot1', [<MyType 4 5 6>], None)
