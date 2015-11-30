workspace 'mpack'
  configurations {'debug', 'debug-asan', 'debug-ubsan', 'debug-msan', 'release'}
  language 'C'
  location(path.join(_MAIN_SCRIPT_DIR, 'build'))
  includedirs(path.join(_MAIN_SCRIPT_DIR, 'include'))
  flags {'ExtraWarnings'}
  buildoptions {'-Wconversion', '-Wstrict-prototypes', '-pedantic'}


filter {'configurations:debug*'}
  defines 'DEBUG'
  optimize 'Off'
  flags {'Symbols'}


filter {'configurations:*san'}
  toolset 'clang'
  buildoptions {'-fno-omit-frame-pointer', '-fno-optimize-sibling-calls'}


filter {'configurations:debug-asan'}
  buildoptions {'-fsanitize=address'}
  linkoptions {'-fsanitize=address'}


filter {'configurations:debug-ubsan'}
  buildoptions {'-fsanitize=undefined', '-fno-sanitize-recover'}
  linkoptions {'-fsanitize=undefined'}


filter {'configurations:debug-msan'}
  buildoptions {'-fsanitize=memory', '-fsanitize-memory-track-origins'}
  linkoptions {'-fsanitize=memory'}


filter {'configurations:release'}
  optimize 'Speed'
  flags {'FatalWarnings'}


project 'mpack-core'
  kind 'StaticLib'
  buildoptions {'-ansi'}
  files {
    'src/*.c'
  }

local checkflags = io.popen('pkg-config --libs check'):read('*a')
local janssonflags = io.popen('pkg-config --libs jansson'):read('*a')

-- both check and jansson are required for the tests
if #checkflags and #janssonflags then
  local libs = {checkflags, janssonflags}
  local ldflags = {}
  for i = 1, #libs do
    for ldflag in libs[i]:gmatch('%S+') do
      table.insert(ldflags, ldflag)
    end
  end
  project 'mpack-test'
    kind 'ConsoleApp'
    files {
      'test/*.c'
    }
    buildoptions '-std=c99'
    linkoptions(ldflags)
    links {
      'mpack-core'
    }
end
