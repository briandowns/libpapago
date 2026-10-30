#include <stdlib.h>
#include <time.h>

#include <crosscheck.h>

#include "../papago.h"

void
cc_setup()
{
}

void
cc_tear_down()
{
}

int
main(void)
{
    srand(time(NULL));

    CC_INIT;

    CC_COMPLETE;
}
