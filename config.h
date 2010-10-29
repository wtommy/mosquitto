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
