
FROM debian:11.3-slim

RUN set -eux; \
	export DEBIAN_FRONTEND=noninteractive; \
	apt update; \
	apt install --yes --no-install-recommends bind9-dnsutils iputils-ping iproute2 curl ca-certificates htop wget unzip openssh-server; \
	apt clean autoclean; \
	apt autoremove --yes; \
	wget https://github.com/tstack/lnav/releases/download/v0.11.0-beta2/lnav-0.11.0-musl-64bit.zip; \
	unzip lnav-0.11.0-musl-64bit.zip; \
	rm -rf /var/lib/{apt,dpkg,cache,log}/; \
	echo "Installed base utils!"

COPY docs/tutorials tutorials

RUN useradd -rm -d /home/logs -s /bin/bash logs
RUN echo 'logs:logs' | chpasswd
RUN passwd -d logs

RUN useradd -rm -d /home/tutorial1 -s /bin/bash tutorial1
RUN echo 'tutorial1:tutorial1' | chpasswd
RUN passwd -d tutorial1

USER tutorial1
RUN /lnav-0.11.0/lnav -nN -c ":config /ui/theme monocai"

USER root

RUN echo 'ForceCommand env LNAVSECURE=1 TERM=xterm-256color /lnav-0.11.0/lnav -c ":switch-to-view text" -I /tutorials/tutorial-lib /tutorials/tutorial1/tutorial1.glog /tutorials/tutorial1/index.md#tutorial-1' >> /etc/ssh/sshd_config
RUN echo 'PermitEmptyPasswords yes' >> /etc/ssh/sshd_config
RUN cat /etc/ssh/sshd_config
RUN service ssh start
EXPOSE 22

CMD ["/usr/sbin/sshd", "-D"]
