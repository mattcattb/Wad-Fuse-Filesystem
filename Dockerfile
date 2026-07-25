FROM debian:bookworm-slim AS builder

WORKDIR /src
RUN apt-get update \
  && apt-get install --yes --no-install-recommends g++ make \
  && rm -rf /var/lib/apt/lists/*
COPY Makefile ./
COPY libWad ./libWad
COPY wadsvr ./wadsvr
RUN make

FROM debian:bookworm-slim

RUN apt-get update \
  && apt-get install --yes --no-install-recommends netcat-openbsd \
  && rm -rf /var/lib/apt/lists/* \
  && useradd --system --uid 10001 --create-home wad \
  && mkdir /data \
  && chown wad:wad /data

COPY --from=builder /src/wadsrv-bin /usr/local/bin/wadsrv

ENV PORT=7373
ENV WAD_LISTEN_PORT=7373
ENV WAD_DATA_DIR=/data
ENV WAD_MAX_WRITE_BYTES=26214400
ENV WAD_WORKERS=8
ENV WAD_MAX_QUEUED_CONNECTIONS=128
EXPOSE 7373
VOLUME ["/data"]

USER wad
CMD ["wadsrv"]
