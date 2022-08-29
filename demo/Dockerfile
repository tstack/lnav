
FROM debian:11.3-slim

RUN set -eux; \
	export DEBIAN_FRONTEND=noninteractive; \
	apt update; \
	apt install --yes --no-install-recommends bind9-dnsutils iputils-ping iproute2 curl ca-certificates htop wget unzip openssh-server; \
	apt clean autoclean; \
	apt autoremove --yes; \
	rm -rf /var/lib/{apt,dpkg,cache,log}/; \
	echo "Installed base utils!"

ADD https://github.com/tstack/lnav/releases/download/v0.11.0-beta2/lnav-0.11.0-musl-64bit.zip /
RUN unzip lnav-0.11.0-musl-64bit.zip

COPY docs/tutorials tutorials

RUN useradd -rm -d /home/logs -s /bin/bash logs
RUN echo 'logs:logs' | chpasswd
RUN passwd -d logs

RUN useradd -rm -d /home/tutorial1 -s /bin/bash tutorial1
RUN echo 'tutorial1:tutorial1' | chpasswd
RUN passwd -d tutorial1

RUN useradd -rm -d /home/debug -s /bin/bash debug
RUN echo 'debug:debug' | chpasswd

USER tutorial1
RUN /lnav-0.11.0/lnav -nN -c ":config /ui/theme monocai"

USER root

RUN echo 'Match User logs' >> /etc/ssh/sshd_config
RUN echo 'ForceCommand env LNAVSECURE=1 TERM=xterm-256color /lnav-0.11.0/lnav -d /tmp/lnav.err /demo' >> /etc/ssh/sshd_config
RUN echo 'PermitEmptyPasswords yes' >> /etc/ssh/sshd_config
RUN echo 'Match User tutorial1' >> /etc/ssh/sshd_config
RUN echo 'ForceCommand env PATH=/lnav-0.11.0:$PATH /tutorials/tutorial1/run.sh' >> /etc/ssh/sshd_config
RUN echo 'PermitEmptyPasswords yes' >> /etc/ssh/sshd_config
RUN service ssh start
EXPOSE 22

CMD ["/usr/sbin/sshd", "-D"]
