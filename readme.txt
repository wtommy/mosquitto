This is intended to be an open source implementation of a server for version 3
of the mqtt protocol. IBM have a closed source version of this server, known
as Really Small Message Broker (rsmb). The plan is to make this a more or less
drop in replacement.

See the following links:

http://mqtt.org/
http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10840_.htm

The obvious way to proceed is to write a client that implements the v3 spec
and check it works correctly with rsmb. Then write a server to the spec and
check it with the client. Once all is working, check server operation with
other clients.
