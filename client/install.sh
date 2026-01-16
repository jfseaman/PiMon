#!/bin/bash

echo "Starting PiMon_Client installation script..."

if [ "$( id -u )" != "0" ]; then
    echo "This script must be run as root" 1>&2
    exit 1
fi

echo "Checking for git and build-essential dependencies..."

if ! dpkg -s git build-essential >/dev/null 2>&1; then

    echo "Ensuring git and build-essential installed..."
    apt update -y
    apt install -y git build-essential

else

    echo "git and build-essential already installed!"
fi

echo "Installing PiMon_Client..."
echo "Building, installing, and starting PiMon_Client..."

# Build and install PiMon_Client
make compile
make strip
make install

echo "Done! PiMon_Client installed as persistent system service!"
echo ""
echo "  IMPORTANT! Next steps:"
echo ""
echo "  Configure the service (PIMON_SERVER, etc. - see readme.md):"
echo "     sudo nano /etc/systemd/system/PiMon_Client.service"
echo "     systemctl daemon-reload"
echo ""
echo "  Start the service:"
echo "     service PiMon_Client start"
echo ""
echo "  Enable service at boot:"
echo "     systemctl enable PiMon_Client.service"
echo ""
echo "  Misc:"
echo "     service PiMon_Client status"
echo "     service PiMon_Client stop"
echo "     service PiMon_Client restart"
echo ""
