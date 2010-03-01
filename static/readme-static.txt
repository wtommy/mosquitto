This archive contains the mosquitto broker and clients statically compiled
against sqlite3, so they can be used on older distributions that do not have
sufficiently modern versions of sqlite3. Ubuntu Hardy and Dapper are examples
of such distributions that are still supported.

To install, run the following command. I recommend you check the
install-static script before doing so, to convince yourself it is only doing
things you want to happen to your system.

sudo ./install-static

This will install mosquitto to the /usr/local hierarchy, except for the
mosquitto config and init script, which are installed to /etc/mosquitto.conf
and /etc/init.d/mosquitto.

Note that you will still need to install sqlite3-pcre - Ubuntu users can do
this from the mosquitto ppa at https://launchpad.net/~mosquitto-dev/

To uninstall, use

sudo ./uninstall-static
