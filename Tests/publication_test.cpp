#include <cassert>
struct Pool {bool standalone=false,ready=false,tintDirty=true;int exteriorMatchState=-4;};
void publish(Pool& pool){pool.ready = pool.standalone ||
            (pool.exteriorMatchState!=-4 && pool.exteriorMatchState!=-3);}
bool bound(const Pool& pool){return pool.ready && pool.exteriorMatchState!=-4 && pool.exteriorMatchState!=-3;}
int main(){Pool p;publish(p);assert(!bound(p));
p.exteriorMatchState=2;assert(!bound(p));publish(p);assert(bound(p));
p.tintDirty=true;assert(bound(p));publish(p);p.tintDirty=false;assert(bound(p));
p.exteriorMatchState=-3;p.ready=false;assert(!bound(p));
p.exteriorMatchState=1;assert(!bound(p));publish(p);assert(bound(p));
Pool direct;direct.standalone=true;assert(!direct.ready);publish(direct);assert(direct.ready);
}
