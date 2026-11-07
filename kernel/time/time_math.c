#include "time_math.h"

int time_clamp(unsigned long long us, unsigned long long last,
               unsigned long long *out)
{
    /* 逆行だけを押さえる。等しいときは「クランプした」と数えない —
     * 1 周期のあいだに同じ µs を 2 回読むのは正常で、そこまで数えると
     * ktime_clamp_count が「模擬の粗さ」の指標として使えなくなる。 */
    if (us < last) {
        if (out) *out = last;
        return 1;
    }
    if (out) *out = us;
    return 0;
}
