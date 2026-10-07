#include <vector>
#include <cassert>
#include <iostream>
#include "FilaCore.h"
struct Disco : ArmazenamentoFila {
  std::vector<RegistroFlash> slots;
  uint64_t ack=0;
  bool falharAck=false, falharEscrita=false;
  Disco(size_t n) : slots(n) {}
  bool ler(size_t i, RegistroFlash& r) override { r=slots.at(i); return true; }
  bool gravar(size_t i, const RegistroFlash& r) override { if(falharEscrita)return false; slots.at(i)=r; return true; }
  bool lerConfirmacao(uint64_t& s) override {s=ack;return true;}
  bool gravarConfirmacao(uint64_t s) override {if(falharAck)return false;ack=s;return true;}
};
int main(){
 Disco d(2880); FilaPersistente q(d,2880); assert(q.iniciar());
 for(int i=0;i<2880;i++) assert(q.salvar("{\"leitura_id\":\"original\"}",25));
 assert(q.cheia()); assert(!q.salvar("novo",4));
 FilaPersistente reboot(d,2880);assert(reboot.iniciar());assert(reboot.pendentes()==2880);
 RegistroFlash r;assert(reboot.frente(r));assert(r.sequencia==1);
 assert(reboot.confirmar(1));assert(reboot.salvar("novo",4));
 FilaPersistente reboot2(d,2880);assert(reboot2.iniciar());assert(reboot2.pendentes()==2880);assert(reboot2.frente(r));assert(r.sequencia==2);
 d.falharAck=true;assert(!reboot2.confirmar(2));d.falharAck=false;
 FilaPersistente reboot3(d,2880);assert(reboot3.iniciar());assert(reboot3.frente(r));assert(r.sequencia==2);
 d.slots[1].dados[0]='X';FilaPersistente corrupta(d,2880);assert(!corrupta.iniciar());assert(!corrupta.salvar("novo",4));
 Disco failed(2);FilaPersistente f(failed,2);assert(f.iniciar());failed.falharEscrita=true;assert(!f.salvar("x",1));assert(!f.saudavel());
 std::cout<<"PASS: capacidade 2880, reboot, FIFO, fila cheia, reutilização, ACK perdido e corrupção\n";
}
