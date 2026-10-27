/* Existing behavior oracles model a healthy Services owner. Permanent failure
 * publication and retention are exercised by retirement.c/krm_retirement.c. */
#define KrkrDeviceFaulted(context) 0
static inline int HealthyOracleDeviceFault(const char *operation, int code) { (void)operation; (void)code; return 0; }
#define KrkrDeviceFault(context, operation, code) HealthyOracleDeviceFault(operation, code)
#define KrkrFaultState(word) 0
