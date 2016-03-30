-- sanity checks for memory leak(eg: leaving dead references alive in the
-- registry)
local mpack = require('mpack')
local packed, unpacked

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
    local data = mpack.unpack(str)
    return Type.new(data._a, data._b, data._c)
  end
}

local ext_packers = {
  [Type] = function(obj)
    local data = {_a = obj.a, _b = obj.b, _c = obj.c}
    return 5, mpack.pack(data)
  end
}

function simple_unpack()
  local blob = io.open('cases.mpac'):read('*a') .. io.open('cases_compact.mpac'):read('*a')
  -- simple unpack
  local unpack = mpack.Unpacker()
  local pos = 1
  unpacked = {}
  while pos <= #blob do
    local item
    item, pos = unpack(blob, pos)
    unpacked[#unpacked + 1] = item
  end
end

function simple_pack()
  local items = {
    false, true, mpack.NIL, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1,
    127, 127, 255, 65535, 4294967295, -32, -32, -128, -32768, -2147483648,
    0, -0, 1, -1, "a", "a", "a", "", "", "", { 0 }, { 0 }, { 0 }, {}, {},
    {}, {}, {}, {}, { a = 97 }, { a = 97 }, { a = 97 }, { {} }, { { "a" } },
    false, true, mpack.NIL, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1,
    127, 127, 255, 65535, 4294967295, -32, -32, -128, -32768, -2147483648,
    0, -0, 1, -1, "a", "a", "a", "", "", "", { 0 }, { 0 }, { 0 }, {}, {},
    {}, {}, {}, {}, { a = 97 }, { a = 97 }, { a = 97 }, { {} }, { { "a" } }
  }
  local pack = mpack.Packer()
  local chunks = {}
  for i = 1, #items do
    chunks[#chunks + 1] = pack(items[i])
  end
  packed = pack(items)
  assert(table.concat(chunks, '') == packed:sub(4))
end

function unpack_chunks()
  local unpack = mpack.Unpacker()
  local blob = io.open('cases.mpac'):read('*a') .. io.open('cases_compact.mpac'):read('*a')
  while #blob > 1 do
    unpacked = unpack(blob:sub(1, 1))
    blob = blob:sub(2)
  end
  unpacked = unpack(blob)
end

function pack_unpack_big_table()
  local arr = {}
  for i = 1, 10000 do
    table.insert(arr, ('item%d'):format(i))
  end
  local unpack = mpack.Unpacker()
  local pack = mpack.Packer()
  packed = pack(arr)
  unpacked = unpack(packed)
end

function pack_unpack_ext()
  local unpacker = mpack.Unpacker({ext = ext_unpackers})
  local packer = mpack.Packer({ext = ext_packers})
  local t1 = Type.new('field a', 2, {1, 2})
  local t2 = unpacker(packer({k={1,t1}})).k[2]
  assert(t1:get_a() == t2:get_a())
  assert(t1:get_b() == t2:get_b())
end

function cyclic_ref()
  local tbl1 = {1}
  table.insert(tbl1, tbl1)
  table.insert(tbl1, 2)
  local tbl2 = {1, 2, 3}
  local tbl3 = {4, 5, tbl2, 6}
  local tbl4 = {7, tbl3}
  table.insert(tbl2, tbl4)
  local unpack = mpack.Unpacker()
  local pack = mpack.Packer()
  unpack(pack(tbl1))
  unpack(pack(tbl2))
end

function rpc()
  local session = mpack.Session()
  session:request('req1')
  session:request('req2')
  session:request('req3')
  session:notify()
  session:receive('\148\000\000')
  session:receive('\148\000\001')
  session:receive('\147\002')
  session:receive('\148\001\002')
  session:receive('\148\001\000')
  session:receive('\148\001\001')
  session:receive('\148')
  session:receive('\000')
  session:receive('\007')
  session:request('req4')
  session:receive('\148')
  session:receive('\001')
  session:receive('\003')
  session:receive('\147')
  session:receive('\002')
  local failed = true
  pcall(function()
    session:receive('\148\001\009')
    failed = false
  end)
  assert(failed)
end

function run()
  simple_unpack()
  simple_pack()
  unpack_chunks()
  pack_unpack_big_table()
  pack_unpack_ext()
  cyclic_ref()
  rpc()
end

function collect()
  -- run multiple times to ensure lua has a chance to reclaim all unused memomry
  for i = 1, 10 do
    collectgarbage()
  end
  return collectgarbage('count')
end

run() -- run once to "initialize" the state

before = collect()
run()
after = collect()

if before ~= after then
  print('fail .. leaked '..((after - before) * 1024)..' kilobytes')
  os.exit(1)
else
  print('ok')
  os.exit(0)
end
