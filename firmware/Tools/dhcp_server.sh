#!/bin/bash

# ``` bash frame="none"
# sudo apt install isc-dhcp-server
# ```

# ``` bash frame="none"
# sudo nano /etc/default/isc-dhcp-server
# ```
# >
# ``` ini
# INTERFACESv4="enp0s31f6"
# ```

# ``` bash frame="none"
# sudo nano /etc/dhcp/dhcpd.conf
# ```
# >
# ``` txt
# subnet 192.168.2.0 netmask 255.255.255.0 {
#   range 192.168.2.1 192.168.2.254;
#   option subnet-mask 255.255.255.0;
#   option routers 192.168.2.1;
#   option broadcast-address 192.168.2.255;
#   default-lease-time 600;
#   max-lease-time 7200;
# }
# ```

sudo systemctl stop isc-dhcp-server.service
sleep 1s

sudo ifconfig enp0s31f6 192.168.2.1 netmask 255.255.255.0
sleep 1s

sudo systemctl start isc-dhcp-server.service
sleep 1s

ifconfig enp0s31f6

# ``` check status
# sudo systemctl status isc-dhcp-server.service
# ```

# ``` check log
# journalctl -u isc-dhcp-server.service
# ```
