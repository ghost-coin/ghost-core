# Bootstrappable Particl Core Builds


    export VERSION=22.0.1.0rc1
    export GUIX_SIGS_REPO=PATH_TO_guix.sigs
    export DETACHED_SIGS_REPO=PATH_TO_detached-sigs
    export BUILD_REPO_DIR=PATH_TO_particl_core_repo
    export SOURCES_PATH=PATH_TO_guix/sources
    export BASE_CACHE=PATH_TO_guix/cache
    export SDK_PATH=PATH_TO_osx_sdk
    mkdir -p $SOURCES_PATH
    mkdir -p $BASE_CACHE

    cd ${BUILD_REPO_DIR}
    git fetch --tags;
    git reset --hard v$VERSION;

    unset V
    export PARTICL_CONFIG_FLAGS="--enable-usbdevice"
    export DISTNAME=particl-${VERSION}
    unset NO_USB
    export HOSTS="x86_64-linux-gnu x86_64-w64-mingw32 i686-w64-mingw32 x86_64-apple-darwin18"
    ./contrib/guix/guix-build


    cd ${BUILD_REPO_DIR}/guix-build-${VERSION}/output/x86_64-w64-mingw32/
    tar xf particl-win-unsigned.tar.gz
    ./detached-sig-create.sh -key /mnt/nfs/theta/work/Particl/keys/comodoCodesign2021/particl.key

    Enter the passphrase for the key when prompted
    signature-win.tar.gz will be created


    cd ${DETACHED_SIGS_REPO}
    rm -rf *;
    cp ${BUILD_REPO_DIR}/guix-build-${VERSION}/output/x86_64-w64-mingw32/signature-win.tar.gz .;
    tar xf signature-win.tar.gz;
    rm signature-win.tar.gz;
    git add .;

    git commit -S -m "${VERSION}";
    git tag -s v${VERSION} -m "${VERSION}";
    git push

    cd ${BUILD_REPO_DIR}

    export HOSTS="x86_64-w64-mingw32"
    ./contrib/guix/guix-codesign


    mv guix-build-${VERSION}/output/x86_64-linux-gnu guix-build-${VERSION}/output/x86_64-linux-gnu_usb
    rm -rf guix-build-${VERSION}/distsrc-${VERSION}-x86_64-linux-gnu

    unset PARTICL_CONFIG_FLAGS
    export DISTNAME=particl-${VERSION}_nousb
    export NO_USB=1
    export HOSTS="x86_64-linux-gnu arm-linux-gnueabihf aarch64-linux-gnu"
    ./contrib/guix/guix-build


    env SIGNER=0x8E517DC12EC1CC37F6423A8A13F13651C9CF0D6B=tecnovert \
    ./contrib/guix/guix-attest

    cd ${GUIX_SIGS_REPO}
    git add .;
    git commit -S -m"v$VERSION"
    git push

