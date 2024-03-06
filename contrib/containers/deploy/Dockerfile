FROM phusion/baseimage:focal-1.0.0
LABEL maintainer="Osmium Developers <dev@osmium.org>"
LABEL description="Dockerised OsmiumCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /home/osmium

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} osmium && \
    useradd -u ${USER_ID} -g osmium -s /bin/bash -m -d /home/osmium osmium && \
    mkdir /home/osmium/.osmiumcore && \
    chown ${USER_ID}:${GROUP_ID} -R /home/osmium

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER osmium

VOLUME ["/home/osmium"]

EXPOSE 9968 9969 19968 19969

WORKDIR /home/osmium
