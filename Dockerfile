FROM ubuntu:20.04

ENV TZ=America/Los_Angeles
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
LABEL maintainer="Couchbase"

RUN mkdir -p /try-cb-lcb

ADD . /try-cb-lcb

WORKDIR /try-cb-lcb

RUN apt-get update -y && apt-get install -y \
    software-properties-common \
    build-essential cmake \
    libssl-dev libjwt-dev uuid-dev \
    kore jq curl wget

# Configure APT repository for libcouchbase
RUN wget https://packages.couchbase.com/clients/c/repos/deb/couchbase.key \
    && apt-key add ./couchbase.key \
    && rm ./couchbase.key \
    && apt-add-repository "deb https://packages.couchbase.com/clients/c/repos/deb/ubuntu2004 focal focal/main" \
    && apt-get update

# Install libcouchbase
RUN apt-get install -y libcouchbase3 \
    libcouchbase-dev libcouchbase3-tools \
    libcouchbase-dbg libcouchbase3-libev \
    libcouchbase3-libevent

RUN kodev clean && kodev build

# Expose ports
EXPOSE 8080

# Set the entrypoint
ENTRYPOINT ["./wait-for-couchbase.sh", "kore", "-n", "-r", "-c", "/try-cb-lcb/conf/try-cb-lcb.conf"]
