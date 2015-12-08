workspace 'mpack'
  configurations {
    'debug', 'asan', 'ubsan', 'msan', 'coverage', 'profile', 'release'
  }
  language 'C'
  location 'build'
  includedirs 'include'
  flags {'ExtraWarnings'}
  buildoptions {'-Wconversion', '-Wstrict-prototypes', '-pedantic'}

  filter {'configurations:debug or *san or coverage or profile'}
    defines 'DEBUG'
    optimize 'Off'
    flags {'Symbols'}

  filter {'configurations:*san'}
    buildoptions {'-fno-omit-frame-pointer', '-fno-optimize-sibling-calls'}

  filter {'configurations:asan'}
    buildoptions {'-fsanitize=address'}
    linkoptions {'-fsanitize=address'}

  filter {'configurations:ubsan'}
    buildoptions {
      '-fsanitize=undefined', '-fno-sanitize-recover',
      '-fsanitize-blacklist=../.ubsan-blacklist'
    }
    linkoptions {'-fsanitize=undefined'}

  filter {'configurations:msan'}
    buildoptions {'-fsanitize=memory', '-fsanitize-memory-track-origins'}
    linkoptions {'-fsanitize=memory'}

  filter {'configurations:release'}
    optimize 'Speed'
    flags {'FatalWarnings'}

  filter {'configurations:profile'}
    buildoptions {'-pg'}
    linkoptions {'-pg'}

  project 'mpack'
    kind 'StaticLib'
    buildoptions {'-ansi'}
    files {
      'src/*.c'
    }
    filter {'configurations:coverage'}
      buildoptions {'--coverage'}
      linkoptions {'--coverage'}

  project 'tap'
    kind 'StaticLib'
    buildoptions {
      '-std=gnu99', '-Wno-conversion', '-Wno-unused-parameter'
    }
    files {
      'test/deps/tap/*.c'
    }

  project 'mpack-test'
    kind 'ConsoleApp'
    includedirs {'test/deps/tap'}
    files {
      'test/*.c'
    }
    buildoptions {'-std=c99'}
    links {'m', 'mpack', 'tap'}
    filter {'configurations:coverage'}
      linkoptions {'--coverage'}
