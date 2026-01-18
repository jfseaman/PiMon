OPTS   = -O2 -Wall
ENTRY  = server.c
LIBS   = ws2_32.lib user32.lib gdi32.lib
TARGET = PiMon_Server

# COMPILE:
compile:
	cl ${OPTS} ${ENTRY} ${LIBS} -o ${TARGET} /link /SUBSYSTEM:WINDOWS /MACHINE:X64

clean:
	del ${TARGET}

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
	systemctl restart ${TARGET}

stop:
	systemctl stop ${TARGET}

enable:
	systemctl enable ${TARGET}.service

status:
	systemctl status ${TARGET}
