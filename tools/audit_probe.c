#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mach/mach.h>

int main(void) {
    mach_msg_type_number_t count = TASK_AUDIT_TOKEN_COUNT;
    audit_token_t token;
    kern_return_t kr = task_info(mach_task_self(), TASK_AUDIT_TOKEN,
        (task_info_t)&token, &count);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "task_info failed: %#x\n", kr);
        return 1;
    }
    printf("pid=%d euid=%d egid=%d\n", getpid(), geteuid(), getegid());
    printf("token vals: %u %u %u %u %u %u %u %u\n",
        token.val[0], token.val[1], token.val[2], token.val[3],
        token.val[4], token.val[5], token.val[6], token.val[7]);
    return 0;
}