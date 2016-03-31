local git_tag = '1.0.1'

package = 'mpack'
version = git_tag .. '-0'
source = {
  url = 'https://github.com/tarruda/libmpack/archive/' .. git_tag .. '.tar.gz',
  dir = 'libmpack-' .. git_tag .. '/binding/lua',
}

description = {
  summary = 'Lua binding to libmpack',
  license = 'MIT'
}

build = {
  type = 'builtin',
  modules = {
    ['mpack'] = {
      sources = {'lmpack.c'}
    }
  }
}
