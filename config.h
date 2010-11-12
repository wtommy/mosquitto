/* ============================================================
 * Control compile time options.
 *
 * Largely, these are options that are designed to make mosquitto run more
 * easily in restrictive environments by removing features.
 * ============================================================ */

/* Uncomment to compile with tcpd/libwrap support. */
//#define WITH_WRAP

/* Compile with database upgrading support? If disabled, mosquitto won't
 * automatically upgrade old database versions. */
//#define WITH_DB_UPGRADE

/* Compile with memory tracking support? If disabled, mosquitto won't track
 * heap memory usage nor export '$SYS/broker/heap/current size', but will use
 * slightly less memory and CPU time. */
#define WITH_MEMORY_TRACKING

/* Compile with the ability to upgrade from old style sqlite persistent
 * databases to the new mosquitto format. This means a dependency on sqlite. It
 * isn't needed for new installations. */
#define WITH_SQLITE_UPGRADE

/* Compile with persistent database support. This allows the broker to store
 * retained messages and durable subscriptions to a file periodically and on
 * shutdown. This is usually desirable (and is suggested by the MQTT spec), but
 * it can be disabled by commenting out this define if required. */
#define WITH_PERSISTENCE

/* Compile with 32-bit integer database IDs instead of 64-bit integers. May be
 * useful in embedded systems or where be64toh()/htobe64() aren't available.
 * There is the potential for bad things to happen after the IDs wrap around.
 * This is especially likely if there are old retained messages. Note that at a
 * sustained rate of 10,000 messages/s, the database ID would overflow every 5
 * days. It is also worth noting that a broker compiled with 64-bit DB IDs will
 * not load a persistent database file saved from a 32-bit DB ID broker and
 * vice versa. */
//#define WITH_32BIT_DBID
