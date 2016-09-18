import doctest
import re
import sys
import unittest

unicode_str = re.compile("u([\"'])(.*?)\\1")
byte_str = re.compile("b([\"'])(.*?)\\1")
hex_addr = re.compile('at 0x[0-9a-fA-F]+')
mpack_exc = re.compile(r'^mpack\.(Mpack.+)$')

class Py2And3StringChecker(doctest.OutputChecker):
    def check_output(self, want, got, optionflags):
        # normalize addresses
        got = hex_addr.sub('at 0xffffff', got)
        # normalize exceptions(python 3 adds "mpack." prefix to the exception
        # class name)
        got = mpack_exc.sub('\\1', got)
        # normalize unicode/byte strings
        if sys.version_info[0] > 2:
            want = unicode_str.sub('\\1\\2\\1', want)
        else:
            want = byte_str.sub('\\1\\2\\1', want)
        return doctest.OutputChecker.check_output(self, want, got, optionflags)

def load_tests(loader, tests, ignore):
    tests.addTests(doctest.DocFileSuite('README.rst',
                                        checker=Py2And3StringChecker()))
    return tests

if __name__ == '__main__':
    unittest.main()
