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
 * isn't needed for new installations.
 * Not available on Windows.
 * This will be removed in version 0.11.
 */
#ifndef WIN32
//#define WITH_SQLITE_UPGRADE
#endif

/* Compile with persistent database support. This allows the broker to store
 * retained messages and durable subscriptions to a file periodically and on
 * shutdown. This is usually desirable (and is suggested by the MQTT spec), but
 * it can be disabled by commenting out this define if required.
 * Not available on Windows.
 */
#ifndef WIN32
#define WITH_PERSISTENCE
#endif

/* Compile with bridge support included. This allow the broker to connect to
 * other brokers and subscribe/publish to topics. You probably want to leave
 * this included unless you want to save a very small amount of memory size and
 * CPU time.
 */
#define WITH_BRIDGE

/* Compile with 32-bit integer database IDs instead of 64-bit integers. May be
 * useful in embedded systems or where be64toh()/htobe64() aren't available.
 * There is the potential for bad things to happen after the IDs wrap around.
 * This is especially likely if there are old retained messages. Note that at a
 * sustained rate of 10,000 messages/s, the database ID would overflow every 5
 * days. It is also worth noting that a broker compiled with 64-bit DB IDs will
 * not load a persistent database file saved from a 32-bit DB ID broker and
 * vice versa.
 */
//#define WITH_32BIT_DBID

/* Compile with SSL support */
#define WITH_SSL

/* ============================================================
 * Compatibility defines
 *
 * Generally for Windows native support.
 * ============================================================ */
#ifdef WIN32
#define snprintf sprintf_s
#define strcasecmp strcmpi
#endif
