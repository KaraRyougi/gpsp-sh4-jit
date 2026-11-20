# GCC 14.1.0 + fxSDK; pin the base so rebuilds use the same compiler.
FROM manawyrm/fxsdk@sha256:b5283be1ba98d90aadc2cbc767d0424baa801344c71108c587e922b37392beaf

# Match gint's HAL interface instead of the older libc in the base image.
ARG FXLIBC_REVISION=a6903066cf4777aa96556a6da9699d4704867185
RUN cd /root/.local/share/giteapc/Vhex-Kernel-Core/fxlibc \
    && git fetch origin "$FXLIBC_REVISION" \
    && git checkout --detach "$FXLIBC_REVISION" \
    && make -f giteapc.make configure \
    && make -f giteapc.make build \
    && make -f giteapc.make install

# master lacks CG100 keys/hardware support. Pin the tested dev revision.
ARG GINT_REVISION=2bc6f7ebcb500b423ee9906e11664466ae913424
RUN cd /root/.local/share/giteapc/Lephenixnoir/gint \
    && git fetch origin "$GINT_REVISION" \
    && git checkout --detach "$GINT_REVISION" \
    && fxsdk build-cg -c \
    && fxsdk build-cg \
    && fxsdk build-cg install

# The ClassPad II (HollyHock-3) build links the same gint revision
# configured for that model.
RUN cd /root/.local/share/giteapc/Lephenixnoir/gint \
    && fxsdk build-cp -c \
    && fxsdk build-cp \
    && fxsdk build-cp install
WORKDIR /workspace
