
FROM debian:11.3-slim

RUN set -eux; \
	export DEBIAN_FRONTEND=noninteractive; \
	apt update; \
	apt install --yes --no-install-recommends bind9-dnsutils iputils-ping iproute2 curl ca-certificates htop wget unzip openssh-server; \
	apt clean autoclean; \
	apt autoremove --yes; \
	rm -rf /var/lib/{apt,dpkg,cache,log}/; \
	echo "Installed base utils!"

ADD https://github.com/tstack/lnav/releases/download/v0.11.0/lnav-0.11.0-musl-64bit.zip /
RUN unzip lnav-0.11.0-musl-64bit.zip

COPY docs/tutorials tutorials
RUN gunzip /tutorials/playground/logs/*.gz

RUN useradd -rm -d /home/playground -s /bin/bash playground
RUN echo 'playground:playground' | chpasswd
RUN passwd -d playground

RUN useradd -rm -d /home/tutorial1 -s /bin/bash tutorial1
RUN echo 'tutorial1:tutorial1' | chpasswd
RUN passwd -d tutorial1

USER playground
RUN /lnav-0.11.0/lnav -nN -c ":config /ui/theme monocai"

USER tutorial1
RUN /lnav-0.11.0/lnav -nN -c ":config /ui/theme monocai"

USER root

RUN echo 'Match User playground' >> /etc/ssh/sshd_config
RUN echo 'ForceCommand env PATH=/lnav-0.11.0:$PATH /tutorials/playground/run.sh' >> /etc/ssh/sshd_config
RUN echo 'PermitEmptyPasswords yes' >> /etc/ssh/sshd_config
RUN echo 'Match User tutorial1' >> /etc/ssh/sshd_config
RUN echo 'ForceCommand env PATH=/lnav-0.11.0:$PATH /tutorials/tutorial1/run.sh' >> /etc/ssh/sshd_config
RUN echo 'PermitEmptyPasswords yes' >> /etc/ssh/sshd_config
RUN service ssh start
EXPOSE 22

CMD ["/usr/sbin/sshd", "-D"]
