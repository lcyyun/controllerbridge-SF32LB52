#include "sf32lb52_app.h"

/*
 * RT-Thread BSPs commonly call main() after board/runtime initialization.
 * BLOCKER: confirm the exact SiFli application entry contract and replace this
 * wrapper if the selected SDK expects INIT_APP_EXPORT or an rt_thread entry.
 */
int main(void)
{
    return sf32lb52_app_main();
}
