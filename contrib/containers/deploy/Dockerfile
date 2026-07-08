FROM phusion/baseimage:focal-1.0.0
LABEL maintainer="Maximus Developers <dev@maximuschain.com>"
LABEL description="Dockerised MaximusCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /home/maximus

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} maximus && \
    useradd -u ${USER_ID} -g maximus -s /bin/bash -m -d /home/maximus maximus && \
    mkdir /home/maximus/.maximuscore && \
    chown ${USER_ID}:${GROUP_ID} -R /home/maximus

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER maximus

VOLUME ["/home/maximus"]

EXPOSE 9938 9939 19938 19939

WORKDIR /home/maximus
