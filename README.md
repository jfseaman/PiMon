# PiMon
Raspberry Pi monitoring

I wanted to keep tabs on my various Pi. Just a heart beat and a little telemetry.

There are 3 apps. One for the client Raspberry and one for the PC server and one for xserver on linux.

The clients blindly send UDP packets to the server. If the server is not listening
no problems, the packets disapear on the network. There is only one per second so
a lot less noisy than any Windows machine on the network.

My network does not allow broadcast so at this moment it sends to a specific address.
It would be easier to broadcast but for now the target server is hardcoded. You can easilly
change the code to do that.

It would work for other systems but I haven't got to that because cpu temp and fan 
speed on PC systems is variable.