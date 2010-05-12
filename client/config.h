/* Uncomment to compile with tcpd/libwrap support. */
//#define WITH_WRAP

/* Compile with regex topic matching support?  If disabled, wildcard topic
 * matching won't be possible.
 * Comment out to disable.
 */
//#define WITH_REGEX

/* Compile with database upgrading support? If disabled, mosquitto won't
 * automatically upgrade old database versions. */
//#define WITH_DB_UPGRADE

/* Compile with memory tracking support? If disabled, mosquitto won't track
 * heap memory usage nor export '$SYS/broker/heap/current size', but will use
 * slightly less memory and CPU time. */
//#define WITH_MEMORY_TRACKING
