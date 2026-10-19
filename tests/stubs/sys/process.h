#include <stdint.h>
uint64_t test_peek(uint64_t addr);
#define system_call_1(n, addr) uint64_t test_syscall_result = test_peek(addr)
#define return_to_user_prog(t) return (t)test_syscall_result
