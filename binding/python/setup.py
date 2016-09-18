import glob
import os
import os.path
import shutil

from setuptools import setup
from setuptools.extension import Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.install import install
from setuptools.command.sdist import sdist
from distutils.command.clean import clean


VERSION = '1.0.3'
REPO = 'https://github.com/tarruda/libmpack'


class Clean(clean):
    def run(self):
        files = []
        with open('.gitignore') as gitignore:
            for pattern in gitignore:
                for f in glob.glob(pattern.strip()):
                    if os.path.isdir(f): shutil.rmtree(f)
                    elif os.path.isfile(f): os.unlink(f)
        clean.run(self)


def with_hooks(cmdclass):
    def _copy_mpack():
        if os.path.exists('mpack-src/mpack.c'): return
        shutil.copytree(os.path.join('..', '..', 'src'), 'mpack-src')

    def _autopxd():
        from autopxd import translate
        mpack_src = 'mpack-src/mpack.c'
        with open(mpack_src) as f:
            hdr = f.read()
        with open('cmpack.pxd', 'w') as f:
            f.write(translate(hdr, mpack_src))

    def _cythonize():
        try:
            from Cython.Build import cythonize
        except ImportError:
            return
        kwargs = {
            'gdb_debug': True,
            'language_level': 3
        }
        if os.getenv('NDEBUG', False):
            kwargs['gdb_debug'] = False
        cythonize([Extension('mpack', ['mpack.pyx'])], **kwargs)

    class Sub(cmdclass):
        def build_extensions(self):
            _autopxd()
            _cythonize()
            cmdclass.build_extensions(self)

        def run(self):
            _copy_mpack()
            cmdclass.run(self)
    return Sub


extensions = [Extension("mpack", ['mpack.c'])]


setup(
    name="mpack",
    version=VERSION,
    description="Python binding to libmpack",
    ext_modules=extensions,
    install_requires=['future'],
    url=REPO,
    download_url='{0}/archive/{1}.tar.gz'.format(REPO, VERSION),
    license='MIT',
    cmdclass={
        'build_ext': with_hooks(build_ext),
        'install': with_hooks(install),
        'sdist': with_hooks(sdist),
        'clean': Clean,
    },
    author="Thiago de Arruda",
    author_email="tpadilha84@gmail.com"
)
