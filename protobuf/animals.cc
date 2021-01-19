
#include <iostream>
#include <string>

#include "animals.pb.h"

int main() {
  zwierzeta::Gatunek krowa;
  krowa.set_nazwa("krowa");
  krowa.set_liczba_nog(4);
  krowa.add_odglosy("Muuuu");
  zwierzeta::Gatunek kura;
  kura.set_nazwa("Gallus gallus domesticus");
  kura.add_odglosy("ko ko ko");
  kura.add_odglosy("kukuryku");
  kura.set_liczba_nog(2);

  zwierzeta::Zwierze krowa1;
  *krowa1.mutable_gatunek() = krowa;
  krowa1.set_imie("Krasula");
  zwierzeta::Zwierze krowa2;
  *krowa2.mutable_gatunek() = krowa1.gatunek();
  krowa2.set_imie("Mućka");

  std::cout << "Imiona krów: " << krowa1.imie() << ", " << krowa2.imie()
            << "\nDźwięki kur:\n";
  for (const auto& s : kura.odglosy()) { std::cout << s << std::endl; }
  std::cout << "Kura ma " << kura.odglosy_size()
            << " odglosy, ale najczęściej mówi " << kura.odglosy(0)
            << std::endl;

  // AppendToString(), poniżej, wpisuje do "serialized" binarną reprezentację
  // danych z obiektu krowa1. Takiej reprezentacji używamy, żeby zapisać
  // obiekt na dysku, albo wysłać przez sieć. Nie mylić z reprezentacją
  // tekstową, czytelną dla człowieka, produkowaną przez DebugString().

  std::string serialized;
  krowa1.AppendToString(&serialized);
  
  zwierzeta::Zwierze krowa3;
  if (!krowa3.ParseFromString(serialized)) {
    std::cerr << "Coś jest grubo nie tak!\n";
    abort();
  }
  // krowa3 jest kopią krowa1
  std::cout << "krowa3:\n" << krowa3.DebugString();
}
