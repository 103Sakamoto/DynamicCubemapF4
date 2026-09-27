#include <cassert>
using ULONGLONG=unsigned long long;
struct InvalidReferenceRecovery {
    unsigned invalidSamples=0;
    ULONGLONG firstInvalid=0,nextAttempt=0;
    void Observe(bool valid,ULONGLONG now) {
        if (valid) { *this={}; return; }
        if (!invalidSamples) firstInvalid=now;
        if (invalidSamples<5) ++invalidSamples;
    }
    bool Due(ULONGLONG now) const {
        return invalidSamples>=5 && now-firstInvalid>=10000 && now>=nextAttempt;
    }
    void Attempt(ULONGLONG now) { nextAttempt=now+30000; }
};

int main(){
 InvalidReferenceRecovery r;
 assert(!r.Due(60000));
 for(unsigned i=0;i<5;++i)r.Observe(false,1000+i*1000);
 assert(!r.Due(10999));assert(r.Due(11000));
 r.Attempt(11000);assert(!r.Due(40999));assert(r.Due(41000));
 r.Observe(true,42000);assert(!r.Due(90000));
 r.Observe(false,91000);assert(!r.Due(200000));
 r={};assert(!r.Due(300000));
}
