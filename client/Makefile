OPTS   = -g -O2 -Wall
ENTRY  = client.c
LIBS   = -L /usr/local/include -lm -lrt -lpthread
TARGET = PiMon_Client

# COMPILE:
compile:
	gcc ${OPTS} ${ENTRY} ${LIBS} -o ${TARGET}
	chmod +x ${TARGET}

clean:
	rm ${TARGET}

strip:
	strip --strip-unneeded ${TARGET}

# INSTALL/UNINSTALL:
install:
	cp ${TARGET} /usr/sbin/${TARGET}
	chown root:root /usr/sbin/${TARGET}
	cp ${TARGET}.service /etc/systemd/system/${TARGET}.service
	systemctl enable ${TARGET}.service

uninstall:
	systemctl -f stop ${TARGET}.service
	systemctl disable ${TARGET}.service
	systemctl daemon-reload

	rm -f /etc/systemd/system/${TARGET}.service
	rm -f /usr/sbin/${TARGET}

start:
	systemctl daemon-reload
	systemctl start ${TARGET}

restart:
	systemctl daemon-reload
	systemctl restart ${TARGET}

stop:
	systemctl stop ${TARGET}

enable:
	systemctl enable ${TARGET}.service

status:
	systemctl status ${TARGET}

update:
	systemctl -f stop ${TARGET}.service
	cp ${TARGET} /usr/sbin/${TARGET}
	chown root:root /usr/sbin/${TARGET}
