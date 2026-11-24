// SPDX-License-Identifier: MIT
#include "sf_tb_registry.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cerrno>
struct Node { std::atomic<unsigned> refs{1},destroyed{0};unsigned id=0; };
struct Model {
    std::mutex mutex;sf_tb_registry registry={};std::atomic<Node*> fail{nullptr};
    Node *watch=nullptr,*watchParent=nullptr;
    static void lock(void *ctx){((Model*)ctx)->mutex.lock();}
    static void unlock(void *ctx){((Model*)ctx)->mutex.unlock();}
    static bool retain(void *ctx,void *p){auto &m=*(Model*)ctx;auto *n=(Node*)p;if(m.fail==n)return false;
        unsigned refs=n->refs.load();while(refs && !n->refs.compare_exchange_weak(refs,refs+1)){}return refs!=0;}
    static void release(void *ctx,void *p){auto &m=*(Model*)ctx;auto *n=(Node*)p;auto old=n->refs.fetch_sub(1);assert(old);
        if(old==1){if(n==m.watch)assert(m.watchParent && m.watchParent->refs>0);++n->destroyed;}}
    Model(){sf_tb_registry_ops ops={this,lock,unlock,retain,release};assert(!sf_tb_registry_init(&registry,&ops));assert(sf_tb_registry_init(&registry,&ops)==-EALREADY);}
    int add(Node &n,Node *parent=nullptr,void *bus=(void*)1,void *cls=(void*)2){return sf_tb_registry_add(&registry,&n,parent,bus,cls);}
    void remove(Node &n){assert(!sf_tb_registry_remove(&registry,&n));}
    void ownerPut(Node &n){release(this,&n);}
};
int main(){
    // Delete every currently visited child, dropping its owner and registry
    // refs, after removing the parent. The ticket pins both until put.
    for(unsigned cycle=0;cycle<100;++cycle) {
        Model m;Node parent,children[8];assert(!m.add(parent));
        for(unsigned i=0;i<8;++i){children[i].id=i;assert(!m.add(children[i],&parent));}
        sf_tb_registry_iter it;assert(!sf_tb_registry_begin(&m.registry,&it,SF_TB_MATCH_PARENT,&parent,nullptr,false));
        m.remove(parent);m.ownerPut(parent);
        for(unsigned i=0;i<8;++i) {
            sf_tb_registry_ref ref={};assert(sf_tb_registry_next(&m.registry,&it,&ref)==1 && ref.device==&children[i] && ref.parent==&parent);
            m.remove(children[i]);m.ownerPut(children[i]);m.watch=&children[i];m.watchParent=&parent;
            assert(children[i].refs==1);sf_tb_registry_put(&m.registry,&ref);assert(!ref.device && children[i].destroyed==1);
        }
        sf_tb_registry_ref ref={};assert(!sf_tb_registry_next(&m.registry,&it,&ref) && !ref.device && parent.refs==0 && parent.destroyed==1);
    }
    {
        Model m;Node n[4];for(unsigned i=0;i<4;++i)n[i].id=i;
        for(unsigned i=0;i<3;++i)assert(!m.add(n[i]));
        sf_tb_registry_iter it;assert(!sf_tb_registry_begin(&m.registry,&it,SF_TB_MATCH_BUS,(void*)1,nullptr,false));
        sf_tb_registry_ref ref={};assert(sf_tb_registry_next(&m.registry,&it,&ref)==1 && ref.device==&n[0]);sf_tb_registry_put(&m.registry,&ref);
        m.remove(n[1]);assert(!m.add(n[1]) && !m.add(n[3]));
        assert(sf_tb_registry_next(&m.registry,&it,&ref)==1 && ref.device==&n[2]);sf_tb_registry_put(&m.registry,&ref);
        assert(!sf_tb_registry_next(&m.registry,&it,&ref)); // new/re-added registrations excluded
        assert(!sf_tb_registry_begin(&m.registry,&it,SF_TB_MATCH_CLASS,(void*)2,nullptr,true));
        for(unsigned i:{3u,1u,2u,0u}){assert(sf_tb_registry_next(&m.registry,&it,&ref)==1 && ref.device==&n[i]);sf_tb_registry_put(&m.registry,&ref);}
        assert(!sf_tb_registry_next(&m.registry,&it,&ref));
        for(auto &x:n){m.remove(x);m.ownerPut(x);assert(x.destroyed==1);}
    }
    {
        Model m;Node n,parent;m.fail=&parent;
        assert(m.add(n,&parent)==-ENODEV && n.refs==1 && parent.refs==1 && m.registry.serial==0);
        m.fail=&n;assert(m.add(n)==-ENODEV && n.refs==1);m.fail=nullptr;
        m.registry.serial=UINT64_MAX-1;assert(m.add(n)==-EOVERFLOW && n.refs==1);
        m.ownerPut(n);m.ownerPut(parent);
    }
    {
        Model m;std::vector<Node> n(SF_TB_REGISTRY_CAPACITY+1);
        for(unsigned i=0;i<SF_TB_REGISTRY_CAPACITY;++i)assert(!m.add(n[i]));
        assert(m.add(n.back())==-ENOSPC && n.back().refs==1);
        assert(m.add(n[0])==-EEXIST && n[0].refs==2);
        for(unsigned i=0;i<SF_TB_REGISTRY_CAPACITY;++i)m.remove(n[i]);
        for(auto &x:n){m.ownerPut(x);assert(x.destroyed==1);}
    }
    {
        Model m;Node parent,n[16];for(auto &x:n)assert(!m.add(x,&parent));
        std::vector<std::thread> workers;
        for(unsigned thread=0;thread<8;++thread)workers.emplace_back([&,thread] {
            for(unsigned cycle=0;cycle<200;++cycle) {
                sf_tb_registry_iter it;assert(!sf_tb_registry_begin(&m.registry,&it,SF_TB_MATCH_PARENT,&parent,nullptr,thread&1));
                sf_tb_registry_ref ref={};int ret;
                while((ret=sf_tb_registry_next(&m.registry,&it,&ref))==1) {
                    assert(((Node*)ref.device)->refs>=2 && parent.refs>=2 && ref.parent==&parent);
                    sf_tb_registry_put(&m.registry,&ref);
                }
                assert(!ret);
            }
        });
        for(unsigned cycle=0;cycle<1000;++cycle){auto &x=n[cycle%16];m.remove(x);assert(!m.add(x,&parent));}
        for(auto &worker:workers)worker.join();
        for(auto &x:n){m.remove(x);assert(x.refs==1);m.ownerPut(x);assert(x.destroyed==1);}
        assert(parent.refs==1);m.ownerPut(parent);assert(parent.destroyed==1);
    }
    puts("Router registry: 100 self-removing tree traversals with parent pins, watermark/reverse/class iteration, capacity/overflow/retain failure, 8 concurrent iterators and 1000 remove/re-add cycles pass");
}
