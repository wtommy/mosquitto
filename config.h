/* ============================================================
 * Control compile time options.
 *
 * Largely, these are options that are designed to make mosquitto run more
 * easily in restrictive environments by removing features.
 * ============================================================ */

#ifndef CMAKE
/* Only use the compile time options defined here from the standard Makefile. */


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

#endif

/* ============================================================
 * Compatibility defines
 *
 * Generally for Windows native support.
 * ============================================================ */
#ifdef WIN32
#define snprintf sprintf_s
#define strcasecmp strcmpi
#endif
