/* Host counter for the CPU serialization boundary. */
void test_serialize(void);
#define arch_cpu_serialize test_serialize
