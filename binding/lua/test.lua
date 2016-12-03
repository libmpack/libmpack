local mpack = require('mpack')

-- helpers convert binary strings from/to hex representation
function fhex(s)
  local rv = {}
  for h in s:gmatch('%x+') do
    rv[#rv + 1] = string.char(tonumber(h, 16))
  end
  return table.concat(rv)
end

function thex(s)
  local rv = {}
  for c in s:gmatch('.') do
    rv[#rv + 1] = ('%02x'):format(string.byte(c))
  end
  return table.concat(rv, ' ')
end

describe('mpack', function()
  local msgpack = fhex(
     '81 a4 6b 65 79 31 93 01 81 a4 6b 65 79 32 82 a4 6b 65 79 33 93 a3 '..
     '61 76 64 a2 63 64 03 a4 6b 65 79 34 a7 6a 66 73 64 61 6c 6b 96 cd '..
     '01 43 c3 c2 c0 cf 00 1f ff ff ff ff ff ff d3 ff e0 00 00 00 00 00 '..
     '01')
    
  local object = {
    key1 = {
      1,
      {key2 = {key3 = {'avd', 'cd', 3}, key4 = 'jfsdalk'}},
      {
        323,
        true,
        false,
        mpack.NIL,
        9007199254740991,
        -9007199254740991
      }
    }
  }

  before_each(function()
    collectgarbage()
  end)

  describe('unpack', function()
    local unpack, pos, unpacked;

    before_each(function()
      unpack = mpack.Unpacker()
    end)

    describe('a msgpack chunk', function()
      it('returns object and consumed count', function()
        unpacked, pos = unpack(msgpack)
        assert.are_same(object, unpacked)
        assert.are_same(#msgpack + 1, pos)
      end)
    end)

    describe('a msgpack chunk with trailing data', function()
      it('returns object and consumed count', function()
        unpacked, pos = unpack(msgpack..fhex('ab cd'))
        assert.are_same(object, unpacked)
        assert.are_same(#msgpack + 1, pos)
      end)
    end)

    describe('an incomplete msgpack chunk', function()
      it('returns nil and consumed count', function()
        unpacked, pos = unpack(msgpack:sub(1, #msgpack - 1))
        assert.is_nil(unpacked)
        assert.are_same(#msgpack, pos)
      end)
    end)

    describe('msgpack chunk split over multiple calls', function()
      it('has the same result as if the complete chunk was passed in a single call', function()
        -- in chunks of 1
        local mp = msgpack
        while #mp > 1 do
          unpacked = unpack(mp:sub(1, 1))
          assert.is_nil(unpacked)
          mp = mp:sub(2)
        end
        unpacked = unpack(mp)
        assert.are_same(object, unpacked)
        -- in chunks of 2
        mp = msgpack
        while #mp > 2 do
          unpacked = unpack(mp:sub(1, 2))
          assert.is_nil(unpacked)
          mp = mp:sub(3)
        end
        unpacked = unpack(mp)
        assert.are_same(object, unpacked)
      end)
    end)

    describe('when used across coroutine boundaries', function()
      it('has the same behavior as when not', function()
        unpacked, pos = coroutine.wrap(function() return unpack(msgpack) end)()
        assert.are_same(object, unpacked)
        assert.are_same(#msgpack + 1, pos)
      end)
    end)
  end)

  describe('pack', function()
    local pack, packed;

    before_each(function()
      pack = mpack.Packer()
      collectgarbage()
    end)

    describe('an object graph', function()
      it('returns the corresponding msgpack blob', function()
        packed = pack(object)
        -- there's no guarantee that the order the keys are packed packed will
        -- the same as the one in the `msgpack` variable. verify that pack
        -- works, we unpack it and compare with `object`
        local unpack = mpack.Unpacker()
        local o = unpack(packed)
        assert.are_same(object, o)
      end)
    end)

    describe('when used across coroutine boundaries', function()
      it('has the same behavior as when not', function()
        packed = coroutine.wrap(function() return pack(object) end)()
        local unpack = mpack.Unpacker()
        local o = unpack(packed)
        assert.are_same(object, o)
      end)
    end)
  end)

  describe('handling cyclic references', function()
    it('ok', function()
      local tbl1 = {1}
      table.insert(tbl1, tbl1)
      table.insert(tbl1, 2)
      local tbl2 = {1, 2, 3}
      local tbl3 = {4, 5, tbl2, 6}
      local tbl4 = {7, tbl3}
      table.insert(tbl2, tbl4)
      local unpack = mpack.Unpacker()
      local pack = mpack.Packer()
      assert.are_same({1, mpack.NIL, 2}, unpack(pack(tbl1)))
      assert.are_same({1, 2, 3, {7, {4, 5, mpack.NIL, 6}}}, unpack(pack(tbl2)))
    end)
  end)

  describe('very large array', function()
    it('ok', function()
      local arr = {}
      for i = 1, 10000 do
        table.insert(arr, 'item'..i)
      end
      local unpack = mpack.Unpacker()
      local pack = mpack.Packer()
      local a = unpack(pack(arr))
      assert.are_same(arr, a)
    end)
  end)

  describe('very deep table', function()
    it('ok', function()
      -- 33 levels of nested tables. The initial stack size is 32, so this will
      -- make the internal unpacker/packer to grow once.
      local obj = {
        k1 = {
          {
            k2 = {{{{{{{{{{{{{{{{{{{{{{{{{{{{{1}}}}}}}}}}}}}}}}}}}}}}}}}}}}}
          }
        }
      }
      local unpack = mpack.Unpacker()
      local pack = mpack.Packer()
      local o = unpack(pack(obj))
      assert.are_same(obj, o)
    end)
  end)

  describe('cases', function()
    -- cases.mpac/cases_compact.mpac(taken from msgpack-c tests)
    local cases = {
      false, true, mpack.NIL, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1,
      127, 127, 255, 65535, 4294967295, -32, -32, -128, -32768, -2147483648,
      0, -0, 1, -1, "a", "a", "a", "", "", "", { 0 }, { 0 }, { 0 }, {}, {},
      {}, {}, {}, {}, { a = 97 }, { a = 97 }, { a = 97 }, { {} }, { { "a" } },
      false, true, mpack.NIL, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1,
      127, 127, 255, 65535, 4294967295, -32, -32, -128, -32768, -2147483648,
      0, -0, 1, -1, "a", "a", "a", "", "", "", { 0 }, { 0 }, { 0 }, {}, {},
      {}, {}, {}, {}, { a = 97 }, { a = 97 }, { a = 97 }, { {} }, { { "a" } }
    }

    it('ok', function()
      local blob = io.open('cases.mpac'):read('*a') .. io.open('cases_compact.mpac'):read('*a')
      local unpack = mpack.Unpacker()
      local pack = mpack.Packer()
      local unpacked = {}
      local pos = 1
      while pos <= #blob do
        local item
        item, pos = unpack(blob, pos)
        unpacked[#unpacked + 1] = item
      end
      assert.are_same(cases, unpacked)
      assert.are_same(unpacked, unpack(pack(unpacked)))
    end)
  end)

  describe('ext packing/unpacking', function()
    local Type = {}
    Type.__index = Type

    function Type.new(a, b, c)
      return setmetatable({a = a, b = b, c = c}, Type)
    end
    function Type:get_a() return self.a end
    function Type:get_b() return self.b end
    function Type:get_c() return self.c end

    local ext_unpackers = {
      [5] = function(code, str)
        assert(code == 5)
        local s = mpack.unpack(str)
        return Type.new(s._a, s._b, s._c)
      end
    }

    local ext_packers = {
      [Type] = function(obj)
        return 5, mpack.pack({_a = obj.a, _b = obj.b, _c = obj.c})
      end
    }

    it('ok', function()
      local unpacker = mpack.Unpacker({ext = ext_unpackers})
      local packer = mpack.Packer({ext = ext_packers})
      local t1 = Type.new('field a', 2, {1, 2})
      local t2 = unpacker(packer({key = {3, t1}})).key[2]
      assert.are_same('field a', t2:get_a())
      assert.are_same(2, t2:get_b())
      assert.are_same({1, 2}, t2:get_c())
    end)

    it('errors on recursive packing or unpacking', function()
      local unpack, pack
      local ext_unpackers = {
        [5] = function(code, str)
          local s = unpack(str)
          return Type.new(s._a, s._b, s._c)
        end
      }
      local ext_packers = {
        [Type] = function(obj)
          return 5, pack({_a = obj.a, _b = obj.b, _c = obj.c})
        end
      }
      unpack = mpack.Unpacker({ext = ext_unpackers})
      pack = mpack.Packer({ext = ext_packers})
      assert.has_error(function()
        pack(Type.new('field a', 2, {1, 2}))
      end, "Packer instance already working. Use another Packer or the "..
           "module's \"pack\" function if you need to pack from the ext "..
           "handler")
      assert.has_error(function()
        unpack(fhex('c7 16 05 83 a2 5f 61 a7 66 69 65 6c 64 20 61 a2 5f '..
                    '63 92 01 02 a2 5f 62 02'))
      end, "Unpacker instance already working. Use another Unpacker or the "..
           "module's \"unpack\" function if you need to unpack from the ext "..
           "handler")
    end)
  end)

  it('should not leak memory', function()
    -- get the path to the lua interpreter, taken from
    -- http://stackoverflow.com/a/18304231
    local i_min = 0
    while arg[ i_min ] do i_min = i_min - 1 end
    i_min = i_min + 1
    local res = io.popen(arg[i_min]..' leak_test.lua'):read('*a')
    assert.are_same('ok\n', res)
  end)

  describe('is_bin option', function()
    it('controls if strings are serialized to BIN or STR', function()
      local isbin = false
      local pack = mpack.Packer({is_bin = function(str)
        isbin = not isbin
        return isbin
      end})
      assert.are_same('c4 03 73 74 72', thex(pack('str')))
      assert.are_same('a3 73 74 72', thex(pack('str')))
      assert.are_same('c4 03 73 74 72', thex(pack('str')))
      assert.are_same('a3 73 74 72', thex(pack('str')))
      pack = mpack.Packer({is_bin = true})
      assert.are_same('c4 03 73 74 72', thex(pack('str')))
      assert.are_same('c4 03 73 74 72', thex(pack('str')))
      pack = mpack.Packer()
      assert.are_same('a3 73 74 72', thex(pack('str')))
      assert.are_same('a3 73 74 72', thex(pack('str')))
    end)
  end)

  describe('rpc', function()
    it('ok', function()
      local session = mpack.Session()
      assert.are_same(fhex('94 00 00'), session:request('req1'))
      assert.are_same(fhex('94 00 01'), session:request('req2'))
      assert.are_same(fhex('94 00 02'), session:request('req3'))
      assert.are_same(fhex('93 02'), session:notify())
      assert.are_same({'request', 0, 4}, {session:receive(fhex('94 00 00'))})
      assert.are_same({'request', 1, 4}, {session:receive(fhex('94 00 01'))})
      assert.are_same({'notification', nil, 3}, {session:receive(fhex('93 02'))})
      assert.are_same({'response', 'req3', 4}, {session:receive(fhex('94 01 02'))})
      assert.are_same({'response', 'req1', 4}, {session:receive(fhex('94 01 00'))})
      assert.are_same({'response', 'req2', 4}, {session:receive(fhex('94 01 01'))})
      assert.are_same({nil, nil, 2}, {session:receive(fhex('94'))})
      assert.are_same({nil, nil, 2}, {session:receive(fhex('00'))})
      assert.are_same({'request', 7, 2}, {session:receive(fhex('07'))})
      assert.are_same(fhex('94 00 03'), session:request('req4'))
      assert.are_same({nil, nil, 2}, {session:receive(fhex('94'))})
      assert.are_same({nil, nil, 2}, {session:receive(fhex('01'))})
      assert.are_same({'response', 'req4', 2}, {session:receive(fhex('03'))})
      assert.are_same({nil, nil, 2}, {session:receive(fhex('93'))})
      assert.are_same({'notification', nil, 2}, {session:receive(fhex('02'))})
      assert.has_errors(function() session:receive(fhex('94 01 09')) end)
    end)

    it('can unpack automatically if an unpacker is passed as option', function()
      local session = mpack.Session({unpack = mpack.Unpacker()})
      assert.are_same({'request', 0, 'method', {1, 'a'}, 15},
        {session:receive(fhex('94 00 00 a6 6d 65 74 68 6f 64 92 01 a1 61'))})
      assert.are_same(fhex('94 00 00'), session:request('req1'))
      assert.are_same({'response', 'req1', mpack.NIL, 'result', 12},
        {session:receive(fhex('94 01 00 c0 a6 72 65 73 75 6c 74'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('94'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('00'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('01'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('a6'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('6d'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('65'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('74'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('68'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('6f'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('64'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('92'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('01'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('a1'))})
      assert.are_same({'request', 1, 'method', {1, 'a'}, 2},
        {session:receive(fhex('61'))})
      assert.are_same(fhex('94 00 01'), session:request('req2'))
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('94'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('01'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('01'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('c0'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('a6'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('72'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('65'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('73'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('75'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('6c'))})
      assert.are_same({'response', 'req2', mpack.NIL, 'result', 2},
        {session:receive(fhex('74'))})
      assert.are_same({'notification', nil, 'method', {1, 'a'}, 14},
        {session:receive(fhex('93 02 a6 6d 65 74 68 6f 64 92 01 a1 61'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('93'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('02'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('a6'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('6d'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('65'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('74'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('68'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('6f'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('64'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('92'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('01'))})
      assert.are_same({nil, nil, nil, nil, 2}, {session:receive(fhex('a1'))})
      assert.are_same({'notification', nil, 'method', {1, 'a'}, 2},
        {session:receive(fhex('61'))})
    end)

    describe('when used across coroutine boundaries', function()
      it('has the same behavior as when not', function()
        local input = fhex('94 00 00 a6 6d 65 74 68 6f 64 92 01 a1 61')
        local received = {'request', 0, 'method', {1, 'a'}, 15}
        local session = mpack.Session({unpack = mpack.Unpacker()})
        assert.are_same(received, coroutine.wrap(function() return {session:receive(input)} end)())
      end)
    end)

    it('grows session capacity', function()
      local session = mpack.Session()
      assert.are_same(fhex('94 00 00'), session:request())
      assert.are_same(fhex('94 00 01'), session:request())
      assert.are_same(fhex('94 00 02'), session:request())
      assert.are_same(fhex('94 00 03'), session:request())
      assert.are_same(fhex('94 00 04'), session:request())
      assert.are_same(fhex('94 00 05'), session:request())
      assert.are_same(fhex('94 00 06'), session:request())
      assert.are_same(fhex('94 00 07'), session:request())
      assert.are_same(fhex('94 00 08'), session:request())
      assert.are_same(fhex('94 00 09'), session:request())
      assert.are_same(fhex('94 00 0a'), session:request())
      assert.are_same(fhex('94 00 0b'), session:request())
      assert.are_same(fhex('94 00 0c'), session:request())
      assert.are_same(fhex('94 00 0d'), session:request())
      assert.are_same(fhex('94 00 0e'), session:request())
      assert.are_same(fhex('94 00 0f'), session:request())
      assert.are_same(fhex('94 00 10'), session:request())
      assert.are_same(fhex('94 00 11'), session:request())
      assert.are_same(fhex('94 00 12'), session:request())
      assert.are_same(fhex('94 00 13'), session:request())
      assert.are_same(fhex('94 00 14'), session:request())
      assert.are_same(fhex('94 00 15'), session:request())
      assert.are_same(fhex('94 00 16'), session:request())
      assert.are_same(fhex('94 00 17'), session:request())
      assert.are_same(fhex('94 00 18'), session:request())
      assert.are_same(fhex('94 00 19'), session:request())
      assert.are_same(fhex('94 00 1a'), session:request())
      assert.are_same(fhex('94 00 1b'), session:request())
      assert.are_same(fhex('94 00 1c'), session:request())
      assert.are_same(fhex('94 00 1d'), session:request())
      assert.are_same(fhex('94 00 1e'), session:request())
      assert.are_same(fhex('94 00 1f'), session:request())
      assert.are_same(fhex('94 00 20'), session:request())
      assert.are_same('response', session:receive(fhex('94 01 00')))
      assert.are_same('response', session:receive(fhex('94 01 01')))
      assert.are_same('response', session:receive(fhex('94 01 02')))
      assert.are_same('response', session:receive(fhex('94 01 03')))
      assert.are_same('response', session:receive(fhex('94 01 04')))
      assert.are_same('response', session:receive(fhex('94 01 05')))
      assert.are_same('response', session:receive(fhex('94 01 06')))
      assert.are_same('response', session:receive(fhex('94 01 07')))
      assert.are_same('response', session:receive(fhex('94 01 08')))
      assert.are_same('response', session:receive(fhex('94 01 09')))
      assert.are_same('response', session:receive(fhex('94 01 0a')))
      assert.are_same('response', session:receive(fhex('94 01 0b')))
      assert.are_same('response', session:receive(fhex('94 01 0c')))
      assert.are_same('response', session:receive(fhex('94 01 0d')))
      assert.are_same('response', session:receive(fhex('94 01 0e')))
      assert.are_same('response', session:receive(fhex('94 01 0f')))
      assert.are_same('response', session:receive(fhex('94 01 10')))
      assert.are_same('response', session:receive(fhex('94 01 11')))
      assert.are_same('response', session:receive(fhex('94 01 12')))
      assert.are_same('response', session:receive(fhex('94 01 13')))
      assert.are_same('response', session:receive(fhex('94 01 14')))
      assert.are_same('response', session:receive(fhex('94 01 15')))
      assert.are_same('response', session:receive(fhex('94 01 16')))
      assert.are_same('response', session:receive(fhex('94 01 17')))
      assert.are_same('response', session:receive(fhex('94 01 18')))
      assert.are_same('response', session:receive(fhex('94 01 19')))
      assert.are_same('response', session:receive(fhex('94 01 1a')))
      assert.are_same('response', session:receive(fhex('94 01 1b')))
      assert.are_same('response', session:receive(fhex('94 01 1c')))
      assert.are_same('response', session:receive(fhex('94 01 1d')))
      assert.are_same('response', session:receive(fhex('94 01 1e')))
      assert.are_same('response', session:receive(fhex('94 01 1f')))
      assert.are_same('response', session:receive(fhex('94 01 20')))
      assert.has_errors(function() session:receive(fhex('94 01 21')) end)
    end)
  end)

  describe('NIL', function()
    it('tostring() is human-readable', function()
      assert.are_same("mpack.NIL", tostring(mpack.NIL))
    end)
  end)
end)
