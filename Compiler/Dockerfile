FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      bison \
      build-essential \
      ca-certificates \
      cmake \
      flex \
      git && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

RUN cmake -S . -B build
RUN cmake --build build
RUN bash scripts/run_tests.sh

CMD ["bash", "scripts/run_tests.sh"]
