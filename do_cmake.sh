#!/usr/bin/env bash
set -ex

git submodule update --init --recursive

PWD=$(pwd)
BUILD_DIR=${PWD}/build
rm -rf ${BUILD_DIR}

: ${CEPH_GIT_DIR:=..}

if [ -e $BUILD_DIR ]; then
    echo "'$BUILD_DIR' dir already exists; either rm -rf '$BUILD_DIR' and re-run, or set BUILD_DIR env var to a different directory name"
    exit 1
fi

PYBUILD="2"
if [ -r /etc/os-release ]; then
  source /etc/os-release
  case "$ID" in
      fedora)
          PYBUILD="3.7"
          if [ "$VERSION_ID" -ge "32" ] ; then
              PYBUILD="3.8"
          fi
          ;;
      rhel|centos)
          MAJOR_VER=$(echo "$VERSION_ID" | sed -e 's/\..*$//')
          if [ "$MAJOR_VER" -ge "8" ] ; then
              PYBUILD="3.6"
          fi
          ;;
      opensuse*|suse|sles)
          PYBUILD="3"
          ARGS+=" -DWITH_RADOSGW_AMQP_ENDPOINT=OFF"
          ARGS+=" -DWITH_RADOSGW_KAFKA_ENDPOINT=OFF"
          ;;
  esac
elif [ "$(uname)" == FreeBSD ] ; then
  PYBUILD="3"
  ARGS+=" -DWITH_RADOSGW_AMQP_ENDPOINT=OFF"
  ARGS+=" -DWITH_RADOSGW_KAFKA_ENDPOINT=OFF"
else
  echo Unknown release
  exit 1
fi

if [[ "$PYBUILD" =~ ^3(\..*)?$ ]] ; then
    ARGS+=" -DWITH_PYTHON3=${PYBUILD}"
fi

if type ccache > /dev/null 2>&1 ; then
    echo "enabling ccache"
    ARGS+=" -DWITH_CCACHE=ON"
fi

mkdir $BUILD_DIR
cd $BUILD_DIR
if type cmake3 > /dev/null 2>&1 ; then
    CMAKE=cmake3
else
    CMAKE=cmake
fi
#${CMAKE} $ARGS "$@" $CEPH_GIT_DIR || exit 1
${CMAKE} -DCMAKE_INSTALL_PREFIX=/usr \
	-DCMAKE_INSTALL_LIBDIR=/usr/lib64 \
	-DCMAKE_INSTALL_LIBEXECDIR=/usr/lib \
	-DCMAKE_INSTALL_LOCALSTATEDIR=/var \
	-DCMAKE_INSTALL_SYSCONFDIR=/etc \
	-DCMAKE_INSTALL_MANDIR=/usr/share/man \
        -DCMAKE_INSTALL_DOCDIR=/usr/share/doc \
        -DCMAKE_INSTALL_INCLUDEDIR=/usr/include \
        -DCMAKE_BUILD_TYPE=Debug -DWITH_TESTS=OFF \
        -DWITH_LIBRADOSSTRIPER=ON \
	-DWITH_MGR_DASHBOARD_FRONTEND=OFF \
	-DCMAKE_C_FLAGS="-W -Wall -Wfatal-errors -O0 -g3 -gdwarf-4" \
	$ARGS "$@" $CEPH_GIT_DIR || exit 1
set +x

# minimal config to find plugins
cat <<EOF > ceph.conf
[global]
plugin dir = lib
erasure code dir = lib
EOF

echo done.

if [[ ! $ARGS =~ "-DCMAKE_BUILD_TYPE" ]]; then
  cat <<EOF

****
WARNING: do_cmake.sh now creates debug builds by default. Performance
may be severely affected. Please use -DCMAKE_BUILD_TYPE=RelWithDebInfo
if a performance sensitive build is required.
****
EOF
fi

# ִ�� build
pushd ${BUILD_DIR}
make -j2
#make install
#MON=1 OSD=1 MDS=0 MGR=1 RGW=0 ../src/vstart.sh -d -b -n -i 10.112.88.1
#MGR=1 MON=1 OSD=1 MDS=0 RGW=0 ../src/vstart.sh -n -x --without-dashboard --bluestore --crimson --nodaemon --redirect-output --osd-args "--memory 4G"
#ceph osd pool create pool1
#rados put -p pool001 Doxyfile ./Doxyfile
#rados get -p pool001 Doxyfile
popd
