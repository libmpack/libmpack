LINARO_URL=https://releases.linaro.org/15.06/components/toolchain/binaries/4.8/armeb-linux-gnueabihf/gcc-linaro-4.8-2015.06-x86_64_armeb-linux-gnueabihf.tar.xz
QEMU_URL=https://github.com/qemu/qemu/archive/stable-2.4.tar.gz
DEPS=$(pwd)/.deps
PREFIX=${DEPS}/usr
mkdir -p ${PREFIX}

if [ ! -e ${PREFIX}/bin/premake5 ]; then
	make && make clean
fi

if [ ! -e ${PREFIX}/bin/qemu-armeb ]; then
	echo "installing qemu..."
	rm -rf ${DEPS}/src/qemu
	mkdir -p ${DEPS}/src/qemu
	curl -L -o - ${QEMU_URL} | tar xfz - --strip-components=1 -C ${DEPS}/src/qemu
	(
	cd ${DEPS}/src/qemu;
	./configure --prefix=${PREFIX} --target-list=armeb-linux-user
	make install
	)
	rm -rf ${DEPS}/src/qemu
else
	echo "qemu already installed"
fi

TOOLCHAIN_PREFIX=${PREFIX}/bin/armeb-linux-gnueabihf-
export CC=${TOOLCHAIN_PREFIX}gcc
export AR=${TOOLCHAIN_PREFIX}ar
export RANLIB=${TOOLCHAIN_PREFIX}ranlib
export LDFLAGS=-static
export LINK=${CC}

if [ ! -e ${CC} ]; then
	echo "installing linaro toolchain..."
	curl -L -o - ${LINARO_URL} | tar xJf - --strip-components=1 -C ${PREFIX}
else
	echo "linaro already installed."
fi

